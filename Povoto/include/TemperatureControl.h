
extern byte ChillHeatMode;
extern bool interruptedCooling;
extern bool interruptedHeating;

void temperatureControl();
// fromSideKick: sent by the SideKick (preferred over the BrewCore's).
void setEnvironmentTemperatureFromPacket(const char *payload, bool fromSideKick);
const char *environmentTempSource();   // "Dallas", "SideKick", "BrewCore" or "none received"
// Gas temperature (headspace thermometer when assigned and valid, else the
// fermenter's) and its origin, "headspace" or "liquid"; docs/thermometers.md.
float gasTemperature();
const char *gasTemperatureSource();
float headspaceTemperature(); // NAN without a valid headspace thermometer
bool fermenterThermometerAlarmActive(); // target set, no fermenter thermometer (docs/thermometers.md)
String describeFermenterThermometer();  // several lines, for the alarm e-mail
void resetChillHeatCycle();
void getAdaptiveTemperatureStatus(char *out, size_t size);
struct TemperatureControlLogData {
  const char *state;
  float drop, rise, center, trend;
  float chillOn, chillOff, legacyOn, legacyOff;
  int heatBlocked, dropSamples, riseSamples;
};
TemperatureControlLogData getTemperatureControlLogData();
char *getTemperatureModeLabel();
char *getTemperatureControlStatus(char *st);
unsigned long int holdPressureDueToTemperatureRelays();
void markTemperatureSetpointChanged(bool slow);
const char *getTempStateLabel();
