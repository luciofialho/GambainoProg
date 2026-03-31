
extern byte ChillHeatMode;
extern bool interruptedCooling;

void temperatureControl();
void resetChillHeatCycle();
char *getTemperatureControlStatus(char *st);
unsigned long int holdPressureDueToTemperatureRelays();
