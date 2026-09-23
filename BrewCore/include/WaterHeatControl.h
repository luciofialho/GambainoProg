#ifndef WaterHeatControl_h
#define WaterHeatControl_h

#define CIRCNONE            0
#define CIRCHEAT            1
#define CIRCRUNOFF          2
#define CIRCHLTTOMLT        3
#define CIRCCIP             4
#define NUMCIRCSTATUS       5



extern const char * const CircStatusNames[NUMCIRCSTATUS];

void tempVolControl(); 
void CircControl();

#endif
