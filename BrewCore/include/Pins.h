#ifndef Pins_h
#define Pins_h

#include <arduino.h>

#define N_D 0,0,0,0,0


#define RELAYSA_ADDR           0x38
#define RELAYSB_ADDR           0x39
#define RELAYSC_ADDR           0x26
#define RELAYSD_ADDR           0x3B
#define RELAYSE_ADDR           0x25

#define VALVESJ_ADDR           0x20
#define VALVESK_ADDR           0x21
#define VALVESL_ADDR           0x22
#define VALVESM_ADDR           0x23
#define VALVESN_ADDR           0x24

#define RELAYSAI2C             0, RELAYSA_ADDR
#define RELAYSBI2C             0, RELAYSB_ADDR
#define RELAYSCI2C             0, RELAYSC_ADDR
#define RELAYSDI2C             0, RELAYSD_ADDR
#define RELAYSEI2C             0, RELAYSE_ADDR

#define VALVESJI2C             0, VALVESJ_ADDR
#define VALVESKI2C             0, VALVESK_ADDR
#define VALVESLI2C             0, VALVESL_ADDR
#define VALVESMI2C             0, VALVESM_ADDR
#define VALVESNI2C             0, VALVESN_ADDR

#define ADDRINA219             0x40



// Power Control 
#define ADDRPOWER12VON          RELAYSBI2C,          (1<<0)
#define ADDRPOWER12VBLOCK       RELAYSBI2C,          (1<<1)



// Sensors
#define PINBKLEVEL              35//35  
#define PINBKTRANSDUCER         33//33
#define PINTRANSFERFLOWMETER    14//32 
#define PINKEGLIQUIDSENSORS     36//36
#define PINWATERINMETER         26//23
#define PINMLTTOPLEVEL          34//34  
#define PINHLTTOMLTMETER        04//4   
#define PINHLTTRANSDUCER        39//39
#define PINONEWIRE1             13//13 // HLT/MLT
#define PINONEWIRE2             16 //17 // BK
#define PINONEWIRE3             17 //16 // Cold


// Water Control
#define ADDRHOTWATERIN          RELAYSEI2C,           (1<<4)
#define ADDRCOLDWATERIN         VALVESNI2C,           (1<<6)
#define ADDRDRAINBLOCK          VALVESNI2C,           (1<<4)

// Heater control
#define ADDRHLTHEATER           RELAYSCI2C,           (1<<0)
#define ADDRBKHEATERL           RELAYSCI2C,           (1<<1)
#define ADDRBKHEATERH           RELAYSCI2C,           (1<<2)
#define ADDRTOPUPHEATER         RELAYSCI2C,           (1<<3)
#define ADDRDEHUMIDIFIER        RELAYSDI2C,           (1<<6)



// Hot side valves
#define ADDRHLTVALVE            VALVESMI2C,           (1<<6)
#define ADDRHLTWATERIN          RELAYSEI2C,           (1<<6)
#define ADDRHLTDRAIN            RELAYSEI2C,           (1<<5)

#define ADDRMLTVALVEA           VALVESMI2C,           (1<<4)
#define ADDRMLTVALVEB           VALVESMI2C,           (1<<2)
#define ADDRMLTDRAINVALVE       VALVESMI2C,           (1<<0)

#define ADDRBKVALVEA            VALVESLI2C,           (1<<0)
#define ADDRBKVALVEB            VALVESLI2C,           (1<<2)
#define ADDRWHIRLPOOLVALVE      VALVESLI2C,           (1<<4)
#define ADDRBKWATERINVALVE      VALVESLI2C,           (1<<6)
#define ADDRBKDRAINVALVE        VALVESKI2C,           (1<<4)

// Hot side pumps
#define ADDRHLTPUMP             RELAYSCI2C,           (1<<5)
#define ADDRMLTPUMP             RELAYSCI2C,           (1<<6)
#define ADDRBKPUMP              RELAYSCI2C,           (1<<7)

// Chillers
#define ADDRCHILLER1VALVE       RELAYSDI2C,           (1<<2)
#define ADDRCOLDBANKPUMP        RELAYSAI2C,           (1<<3)
#define ADDRCOLDBANKWATERIN     RELAYSAI2C,           (1<<4)
#define ADDRCOLDBANKCONTROL     RELAYSAI2C,           (1<<0)
#define ADDRCOLDBANKLVALVE      VALVESJI2C,           (1<<0)

// KEG CIP
#define ADDRKEGPUMP             RELAYSCI2C,           (1<<4)
#define ADDRKEGWATERIN          RELAYSEI2C,           (1<<0)
#define ADDRKEGDETERGVALVE      RELAYSEI2C,           (1<<1)
#define ADDRKEGDETERGPUMP       RELAYSEI2C,           (1<<2)
#define ADDRKEGDRAINVALVE       RELAYSEI2C,           (1<<3)
#define ADDRKEGCYCLE            VALVESNI2C,           (1<<0)

// FMT CIP
#define ADDRFMTPUMP             RELAYSAI2C,           (1<<1)
#define ADDRFMTWATERINVALVE     RELAYSAI2C,           (1<<2)
#define ADDRFMTCYCLEVALVE       VALVESJI2C,           (1<<4)
#define ADDRFMTDRAINVALVE       VALVESJI2C,           (1<<2)

// Pot cleaner
#define ADDRPOTCLEANERDRAIN     VALVESNI2C,           (1<<2)
#define ADDRPOTCLEANERWATERIN   RELAYSEI2C,           (1<<7)

// interface
#define ADDRLED1                RELAYSBI2C,           (1<<2)
#define ADDRLED2                RELAYSBI2C,           (1<<3)
#define ADDRBUZZER              RELAYSDI2C,           (1<<1)




// miscellaneous
#define PINBUZZER               27

#define PINI2CSDA               21
#define PINI2CSCL               22
#define I2CFrequency            100000

// ================== SERIAL2 (GPIO 18/19) ========
#define SERIAL2_RX 18
#define SERIAL2_TX 19
#define SERIAL2_SPEED 1000000


// const DeviceAddress MLTTopThermometer        = {0x28,0x3b,0x4b,0x48,0x05,0x00,0x00,0x5b};
 //const DeviceAddress MLTThermometer           = {0x28,0x84,0x09,0x08,0x06,0x00,0x00,0x16};


extern uint8_t MLTTopThermometer        [];
extern uint8_t MLTThermometer           [];  
extern uint8_t MLTBottomThermometer     [];
extern uint8_t HLTThermometer           [];
extern uint8_t HLTOutThermometer        [];
extern uint8_t BKThermometer            [];
extern uint8_t TopUpWaterThermometer    [];
extern uint8_t Chiller1Thermometer      [];
extern uint8_t Chiller2Thermometer      [];
extern uint8_t Chiller2WasteThermometer [];
extern uint8_t ColdBankThermometer      [];
extern uint8_t EnvironmentThermometer   [];




#endif




