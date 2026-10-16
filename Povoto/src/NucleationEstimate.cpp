#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include "NucleationEstimate.h"
#include "NucleationRecord.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoTasks.h"
#include "CalibrationEvents.h"
#include "datalog.h"

// docs/dissolved-co2.md, section 8. No buffer of its own: the reference is a line
// over co2EvolutionHistory (PressureControl.cpp) and the observation keeps 18 blocks
// (~150 bytes in all). The tasks end in the web task, so the hooks only queue the
// event; everything runs in the main loop, once per CO2 sample (60 s). The only flash
// accesses are the calibration event when an estimate ends and the small "pending"
// marker (written when the observation starts, cleared when it ends) that lets the
// next boot report a reboot during the observation. Nothing is written during it.

static Nucleation::Estimator estimator;
static portMUX_TYPE queueLock = portMUX_INITIALIZER_UNLOCKED;

enum PendingKind : uint8_t { PENDING_NONE, PENDING_START, PENDING_END, PENDING_CANCEL };
struct Pending {
  PendingKind kind;
  byte type;
  double modelDrop;   // running total of nucleationModelDropMols() when the hook ran
};
static constexpr uint8_t QUEUE_SIZE = 6;
static Pending queue[QUEUE_SIZE];
static uint8_t queueCount = 0;

static byte currentTaskType = 0;          // hopping task of the estimate in progress
static double modelDropBase = 0.0;        // running total at its start
static char taskMode[24] = "";            // DissolvedCO2Mode at its end
static bool bootChecked = false;

static const char *taskTypeName(byte type) {
  return type == 4 ? "DryHopping" : type == 5 ? "DynamicHopping" : "Unknown";
}

static void enqueue(PendingKind kind, byte type) {
  portENTER_CRITICAL(&queueLock);
  if (queueCount < QUEUE_SIZE) {
    queue[queueCount++] = {kind, type, nucleationModelDropMols()};
  }
  portEXIT_CRITICAL(&queueLock);
}

static bool dequeue(Pending &out) {
  bool ok = false;
  portENTER_CRITICAL(&queueLock);
  if (queueCount > 0) {
    out = queue[0];
    for (uint8_t i = 1; i < queueCount; ++i) queue[i - 1] = queue[i];
    --queueCount;
    ok = true;
  }
  portEXIT_CRITICAL(&queueLock);
  return ok;
}

static void setPendingMarker(byte type) {
  Preferences store;
  if (!store.begin("pvt_nucl", false)) return;
  store.putUChar("pend", type);
  store.end();
}

static byte readPendingMarker() {
  Preferences store;
  if (!store.begin("pvt_nucl", true)) return 0;
  const byte type = store.getUChar("pend", 0);
  store.end();
  return type;
}

static String molText(float mol, int decimals) {
  return isfinite(mol) ? String(mol, decimals) : String("n/a");
}

static void emit(const Nucleation::Result &r, byte taskType) {
  // The reference rate in g/L/d, the unit of GasCO2Rate.
  const String baseline = (isfinite(r.baselineSlope) && beerVolume > 0.0f)
      ? String(r.baselineSlope * 86400.0f * 44.01f / beerVolume, 2) : String("n/a");
  String details = String("TaskType=") + taskTypeName(taskType) +
    ", TaskDurationMin=" + String(r.taskMinutes, 1) +
    ", DissolvedCO2Mode=" + taskMode +
    ", ConfiguredWindowMin=" + String(r.configuredMinutes, 0) +
    ", EstimatedWindowMin=" + (isfinite(r.estimatedMinutes) ? String(r.estimatedMinutes, 0) : String("n/a")) +
    ", ExcessMolDuringTask=" + molText(r.excessDuringTaskMol, 3) +
    ", ExcessMolInsideWindow=" + molText(r.excessInsideMol, 3) +
    ", ExcessMolAfterWindow=" + molText(r.excessAfterMol, 3) +
    ", ModelDissolvedDropMol=" + String(nucleationModelDropMols() - modelDropBase, 3) +
    ", BaselineRate=" + baseline +
    ", BlockSigma=" + molText(r.blockSigma, 4) +
    ", PeakBlockExcess=" + molText(r.peakBlockExcess, 3) +
    ", Quality=" + Nucleation::qualityLabel(r.quality);
  if (r.reason[0]) details += String(" (") + r.reason + ")";
  details += String(", ObservedMin=") + String(r.observedMinutes, 0) + ", ResolutionMin=10";
  calibrationEventAppend("nucleation-estimate", details.c_str());
  setPendingMarker(0);
}

// What the user (or a rule) sets: a change disturbs the reference and the observation.
struct SetpointSignature {
  byte mode;
  float temperature, slowTemperature, pressure, slowPressure;
};
static SetpointSignature lastSignature = {};
static bool signatureValid = false;

static bool sameFloat(float a, float b) { return a == b || (isnan(a) && isnan(b)); }

static void checkSetpointChange(uint32_t now) {
  const SetpointSignature current = {SetPointData.mode, SetPointData.setPointTemp, SetPointData.setPointSlowTemp,
                                     SetPointData.setPointPressure, SetPointData.setPointSlowPressure};
  if (signatureValid && (current.mode != lastSignature.mode ||
      !sameFloat(current.temperature, lastSignature.temperature) ||
      !sameFloat(current.slowTemperature, lastSignature.slowTemperature) ||
      !sameFloat(current.pressure, lastSignature.pressure) ||
      !sameFloat(current.slowPressure, lastSignature.slowPressure))) {
    estimator.disturb(current.mode != lastSignature.mode ? "mode change" : "setpoint change", now);
  }
  lastSignature = current;
  signatureValid = true;
}

static void emitIfReady() {
  Nucleation::Result result = {};
  if (estimator.takeResult(result)) emit(result, currentTaskType);
}

void nucleationEstimateSample(double gasMols, float gaugePressure, bool externalStep) {
  const uint32_t now = millis();
  if (!isfinite(gasMols)) return;
  Pending event;
  while (dequeue(event)) {
    switch (event.kind) {
      case PENDING_START:
        if (event.type == 4 || event.type == 5) {
          estimator.taskStarted(now, gasMols, nucleationFitReference(), gaugePressure);
          emitIfReady();                  // a previous estimate that was still observed (interrupted)
          currentTaskType = event.type;
          modelDropBase = event.modelDrop;
          taskMode[0] = 0;
        } else {
          estimator.otherTaskStarted(now);
          emitIfReady();
        }
        break;
      case PENDING_END:
        if (event.type == currentTaskType) {
          strlcpy(taskMode, getDissolvedCO2LogData().mode, sizeof(taskMode));
          estimator.taskEnded(now, gasMols, (double)FMTData.nucleationWindow, gaugePressure);
          if (estimator.stage() == Nucleation::OBSERVING) setPendingMarker(currentTaskType);
          emitIfReady();                  // immediate: no baseline, vented or interrupted
        }
        break;
      case PENDING_CANCEL:
        estimator.taskCancelled();
        break;
      default:
        break;
    }
  }
  checkSetpointChange(now);
  emitIfReady();
  estimator.sample(now, gasMols, gaugePressure, externalStep);
  emitIfReady();

  // A reboot during an observation: the estimate is lost; say so once, after the boot.
  if (!bootChecked && now > 180000UL) {
    bootChecked = true;
    const byte pending = readPendingMarker();
    if (pending != 0) {
      calibrationEventAppend("nucleation-estimate",
          (String("TaskType=") + taskTypeName(pending) +
           ", EstimatedWindowMin=n/a, Quality=interrupted (reboot)").c_str());
      setPendingMarker(0);
    }
  }
}

void nucleationTaskStarted(byte taskType) { enqueue(PENDING_START, taskType); }
void nucleationTaskEnded(byte taskType) { if (taskType == 4 || taskType == 5) enqueue(PENDING_END, taskType); }
void nucleationTaskCancelled() { enqueue(PENDING_CANCEL, 0); }

String nucleationEstimateStatusText() {
  const Nucleation::Stage stage = estimator.stage();
  if (stage == Nucleation::TASK)
    return String("Estimate in progress: reference taken, waiting for the end of the ") + taskTypeName(currentTaskType) + " task.";
  if (stage == Nucleation::OBSERVING)
    return String("Estimate in progress: observing after the ") + taskTypeName(currentTaskType) + " task, " +
           String(estimator.observedMinutes(millis()), 0) + " of " +
           String((int)(Nucleation::BLOCKS * Nucleation::BLOCK_MS / 60000UL)) + " min.";
  return String();
}
