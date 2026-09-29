
extern byte ChillHeatMode;
extern bool interruptedCooling;
extern bool interruptedHeating;

void temperatureControl();
void setEnvironmentTemperatureFromPacket(const char *payload);
void resetChillHeatCycle();
char *getTemperatureModeLabel();
char *getTemperatureControlStatus(char *st);
unsigned long int holdPressureDueToTemperatureRelays();
void markTemperatureSetpointChanged(bool slow);
const char *getTempStateLabel();
