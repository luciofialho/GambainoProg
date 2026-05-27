
extern byte ChillHeatMode;
extern bool interruptedCooling;
extern bool interruptedHeating;

void temperatureControl();
void resetChillHeatCycle();
char *getTemperatureModeLabel();
char *getTemperatureControlStatus(char *st);
unsigned long int holdPressureDueToTemperatureRelays();
