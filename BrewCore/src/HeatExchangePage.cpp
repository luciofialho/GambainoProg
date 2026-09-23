#include "HeatExchangePage.h"
#include "Variables.h"
#include "WaterHeatControl.h"

static void formatTemperature(char *out, size_t outSize, float temperature) {
  if (temperature == NOTaTEMP) {
    snprintf(out, outSize, "--.-&deg;C");
  } else {
    snprintf(out, outSize, "%.1f&deg;C", temperature);
  }
}

static void formatTemperatureAndTarget(char *out, size_t outSize,
                                       float temperature, float target) {
  char temperatureText[20];
  char targetText[20];
  formatTemperature(temperatureText, sizeof(temperatureText), temperature);
  if (!(target > 0.0f)) {
    snprintf(out, outSize, "%s", temperatureText);
    return;
  }
  formatTemperature(targetText, sizeof(targetText), target);
  snprintf(out, outSize, "%s &rarr; %s", temperatureText, targetText);
}

// Diagnostic target for the coil outlet.  It deliberately excludes coil
// efficiency: the latter converts this desired outlet temperature into the
// HLT target used by the controller.
static float hltOutTargetForDisplay() {
  const float target = MLTTargetTemp;
  if (!(target > 0.0f)) return NAN;

  float bottom = MLTOutTemp;
  if (bottom == NOTaTEMP) bottom = MLTTemp;
  if (bottom == NOTaTEMP) bottom = MLTTopTemp;
  const float weightedMlt = weightedMLTTemperatureForHeating();
  if (bottom == NOTaTEMP || weightedMlt == NOTaTEMP) return NAN;

  return bottom + (target - bottom +
                   HeatDampeningFactor * (target - weightedMlt) +
                   HeatAdditiveCorrection);
}

static void heatExchangeValues(char *hlt, char *hltOut, char *mltTop,
                               char *mlt, char *mltOut, char *weighted,
                               char *efficiency) {
  formatTemperatureAndTarget(hlt, 64, HLTTemp, HLTTargetTemp);
  formatTemperatureAndTarget(hltOut, 64, HLTOutTemp, hltOutTargetForDisplay());
  formatTemperature(mltTop, 20, MLTTopTemp);
  formatTemperatureAndTarget(mlt, 64, weightedMLTTemperatureForHeating(), MLTTargetTemp);
  formatTemperature(mltOut, 20, MLTOutTemp);
  formatTemperature(weighted, 20, MLTTemp);
  snprintf(efficiency, 20, "%.1f%%", 100.0f * float(HeatCoilEfficiency));
}

void heatExchangeWebPage(AsyncWebServerRequest *request) {
  char hltTemperature[64], hltOutTemperature[64], mltTopTemperature[20];
  char mltTemperature[64], mltOutTemperature[20], weightedMltTemperature[20];
  char additive[16], dampening[16], coilEfficiency[20];
  heatExchangeValues(hltTemperature, hltOutTemperature, mltTopTemperature,
                     mltTemperature, mltOutTemperature, weightedMltTemperature,
                     coilEfficiency);
  snprintf(additive, sizeof(additive), "%.2f", float(HeatAdditiveCorrection));
  snprintf(dampening, sizeof(dampening), "%.2f", float(HeatDampeningFactor));

  static const char page[] = R"html(<!doctype html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
*{box-sizing:border-box}body{margin:0;background:#f4f0e9;color:#312b28;font-family:Arial,sans-serif}
.page{min-width:1300px;padding:22px}.title{text-align:center;font-size:24px;font-weight:bold;margin-bottom:12px}
.layout{display:grid;grid-template-columns:205px 1045px;gap:22px;justify-content:center;align-items:center}.controls{display:grid;gap:16px}
.control{background:#fff;border:1px solid #d8d0c8;border-radius:12px;padding:14px;box-shadow:0 2px 5px #0002}
.control label{display:block;font-size:14px;font-weight:bold;margin-bottom:8px}.control input{width:82px;padding:7px;border:1px solid #aaa;border-radius:6px;font-size:16px}
.control button{padding:7px 10px;border:0;border-radius:6px;background:#7f483e;color:#fff;font-weight:bold;cursor:pointer}
.refresh{padding:10px;border:0;border-radius:8px;background:#36515c;color:#fff;font-weight:bold;cursor:pointer}.refresh.off{background:#8a817b}
.diagram{position:relative;width:1045px;height:470px;border:2px solid #6c6058;border-radius:18px;background:linear-gradient(180deg,#fffdf8,#eee5d9);overflow:hidden}
.tank{position:absolute;top:82px;width:230px;height:300px;border:7px solid #655852;border-radius:28px 28px 48px 48px;background:#dce9ed;box-shadow:inset 0 -60px #c7dde3}.tank h2{position:absolute;top:-60px;width:100%%;text-align:center;font-size:22px}.hlt{left:13%%}.mlt{right:13%%}
.flow{position:absolute;left:25%%;right:25%%;height:0;border-top:3px solid #526c77}.flow::after{content:"";position:absolute;top:-8px;width:13px;height:13px;border-top:3px solid #526c77;border-right:3px solid #526c77}.upper{top:24px}.upper::after{right:0;transform:rotate(45deg)}.lower{bottom:22px}.lower::after{left:0;transform:rotate(-135deg)}
.efficiency{position:absolute;top:155px;left:0;width:100%%;text-align:center;font-size:22px;font-weight:bold;color:#36515c}.efficiency small{display:block;font-size:13px;font-weight:normal;margin-bottom:6px}
.bubble{position:absolute;min-width:108px;padding:9px 12px;border-radius:999px;background:#ef9eb9;border:2px solid #b65c7b;text-align:center;font-size:16px;font-weight:bold;white-space:nowrap;box-shadow:0 2px 4px #0002}.bubble small{display:block;font-size:11px;font-weight:normal;margin-bottom:2px}.hlt-main{left:24%%;top:168px;transform:translateX(-50%%)}.hlt-out{left:28%%;top:48px}.mlt-top{right:28%%;top:48px}.mlt-main{left:76%%;top:168px;transform:translateX(-50%%)}.mlt-out{right:28%%;bottom:48px}.weighted{left:76%%;top:250px;transform:translateX(-50%%);background:#f7bed0;font-size:14px}
</style></head><body><main class="page"><div class="title">Heat Exchange Monitor — Mashing / Mashout</div><div class="layout"><aside class="controls">
<form class="control" onsubmit="this.action='/fo_HeatAdditiveCorrection_'+encodeURIComponent(this.value.value)"><label>Heat Additive Correction (&deg;C)</label><input name="value" type="number" step="0.01" value="%s"><button>Apply</button></form>
<form class="control" onsubmit="this.action='/fo_HeatDampeningFactor_'+encodeURIComponent(this.value.value)"><label>Heat Dampening Factor</label><input name="value" type="number" step="0.01" value="%s"><button>Apply</button></form>
<button class="refresh" id="autoRefresh" type="button"></button>
</aside><section class="diagram"><div class="tank hlt"><h2>HLT</h2><div class="efficiency"><small>Coil efficiency</small><span id="efficiency">%s</span></div></div><div class="tank mlt"><h2>MLT</h2></div><div class="flow upper" role="img" aria-label="HLT para MLT"></div><div class="flow lower" role="img" aria-label="MLT para HLT"></div>
<div class="bubble hlt-main"><small>HLT</small><span id="hlt">%s</span></div><div class="bubble hlt-out"><small>HLT Out / Target</small><span id="hltOut">%s</span></div><div class="bubble mlt-top"><small>MLT Top</small><span id="mltTop">%s</span></div><div class="bubble mlt-main"><small>MLT</small><span id="mlt">%s</span></div><div class="bubble weighted"><small>MLT middle</small><span id="weighted">%s</span></div><div class="bubble mlt-out"><small>MLT Out</small><span id="mltOut">%s</span></div></section></div></main><script>
let automatic=localStorage.getItem('heatAutomatic')!=='off';const button=document.getElementById('autoRefresh');
function setMode(){button.textContent=automatic?'Auto update: ON':'Auto update: OFF';button.classList.toggle('off',!automatic);localStorage.setItem('heatAutomatic',automatic?'on':'off')}
async function updateHeat(){if(!automatic)return;try{const v=await fetch('/heat/data',{cache:'no-store'}).then(r=>r.json());for(const k of ['hlt','hltOut','mltTop','mlt','weighted','mltOut','efficiency'])document.getElementById(k).innerHTML=v[k]}catch(e){console.log('Heat update failed',e)}}
button.onclick=()=>{automatic=!automatic;setMode();updateHeat()};setMode();setInterval(updateHeat,5000);
</script></body></html>)html";

  AsyncResponseStream *response = request->beginResponseStream("text/html; charset=utf-8");
  response->printf(page, additive, dampening, coilEfficiency, hltTemperature, hltOutTemperature,
                   mltTopTemperature, mltTemperature, weightedMltTemperature,
                   mltOutTemperature);
  request->send(response);
}

void heatExchangeData(AsyncWebServerRequest *request) {
  char hlt[64], hltOut[64], mltTop[20], mlt[64], mltOut[20], weighted[20], efficiency[20];
  heatExchangeValues(hlt, hltOut, mltTop, mlt, mltOut, weighted, efficiency);
  AsyncResponseStream *response = request->beginResponseStream("application/json; charset=utf-8");
  response->printf("{\"hlt\":\"%s\",\"hltOut\":\"%s\",\"mltTop\":\"%s\",\"mlt\":\"%s\",\"mltOut\":\"%s\",\"weighted\":\"%s\",\"efficiency\":\"%s\"}",
                   hlt, hltOut, mltTop, mlt, mltOut, weighted, efficiency);
  request->send(response);
}
