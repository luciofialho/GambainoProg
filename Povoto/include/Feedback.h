#ifndef FEEDBACK_H
#define FEEDBACK_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Calibration Feedback tab (docs/feedback.md): values measured during the batch, read only,
// with "Use" buttons for the polytropic exponent and the nucleation window. Nothing is applied
// automatically. Persisted in NVS (pvt_feedback) once per hour, at an OTA start and on a new batch.

// One vent, from processPressure() (Fermenting and the other modes except Conditioning).
struct FeedbackVent {
  uint32_t reliefNumber;   // CountersData.totalReliefCount after the vent
  float pOnGauge;          // bar: P_on extrapolated (measured when there is no rise reference)
  float pAfterGauge;       // bar: settled pressure after the vent (NAN = unknown)
  float gasKelvin;
  bool counted;            // Fermenting, outside task windows, with effect
  bool ratioEvaluated;     // the vent check applied: ratio is meaningful
  float ratio;             // fermenter side / flow
  float apparentHeadspace; // L
};
void feedbackVent(const FeedbackVent &vent);
// Polytropic exponent of relief sourceRelief, estimated when the next one opens.
void feedbackExponent(float exponent, float rmseBar, uint32_t sourceRelief);
void processFeedback(); // pressureControl(), main loop
void feedbackResetForNewBatch();
bool readFeedbackFromNIV();
bool writeFeedbackToNIV();
void feedbackFactoryReset(); // RAM only: the namespace is cleared by the caller
String feedbackTabHtml();
void handleFeedbackUseExponent(AsyncWebServerRequest *request);   // POST /calibration/feedback/exponent
void handleFeedbackUseNucleation(AsyncWebServerRequest *request); // POST /calibration/feedback/nucleation

#endif
