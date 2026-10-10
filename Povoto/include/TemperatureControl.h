
extern byte ChillHeatMode;
extern bool interruptedCooling;
extern bool interruptedHeating;

void temperatureControl();
void setEnvironmentTemperatureFromPacket(const char *payload);
const char *environmentTempSource();   // "Dallas" or "SideKick"
const char *expansionTankTempSource(); // "Dallas" or "environment"
bool fermenterThermometerAlarmActive(); // target set, no fermenter thermometer (docs/thermometers.md)
String describeFermenterThermometer();  // several lines, for the alarm e-mail
void resetChillHeatCycle();
char *getTemperatureModeLabel();
char *getTemperatureControlStatus(char *st);
unsigned long int holdPressureDueToTemperatureRelays();
void markTemperatureSetpointChanged(bool slow);
const char *getTempStateLabel();
