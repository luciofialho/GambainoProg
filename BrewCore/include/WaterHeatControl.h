#ifndef WaterHeatControl_h
#define WaterHeatControl_h

#define CIRCNONE            0
#define CIRCHEAT            1
#define CIRCRUNOFF          2
#define CIRCHLTTOMLT        3
#define CIRCCIP             4
#define NUMCIRCSTATUS       5



#define WCSTATUSNAMESIZE 21

const char CircStatusName_0[WCSTATUSNAMESIZE] = "Circ: None";
const char CircStatusName_1[WCSTATUSNAMESIZE] = "Circ: Heat";
const char CircStatusName_2[WCSTATUSNAMESIZE] = "Circ: SPARGE";
const char CircStatusName_3[WCSTATUSNAMESIZE] = "Circ: HLT to MLT";
const char CircStatusName_4[WCSTATUSNAMESIZE] = "Circ: CIP";

const char * const CircStatusNames[NUMCIRCSTATUS] = {
CircStatusName_0,
CircStatusName_1,
CircStatusName_2,
CircStatusName_3,
CircStatusName_4
};

void tempVolControl(); 
void CircControl();

#endif