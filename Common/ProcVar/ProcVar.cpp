#include "Arduino.h"
#include "Arduino.h"
#include "ProcVar.h"
//#include "__Timer.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "TimeLib.h"
#include "EEPROM.h"
#include "SPIFFS.h"
#include "GambainoCommon.h"
//#include <BrewCoreCommon.h>
#include "IOTK.h"
#include "IOTK_NTP.h"
#include "IOTK_ESPAsyncServer.h"
#include "IOTK_Dallas.h"
#include "IOTK_GLog.h"
#include <EEPROM.h>
#include <esp_err.h>
#include "esp_task_wdt.h"
//#include "ui.h"
//#include "status.h"
//#include "ipc.h"

//vinculos com gambaino que tornam a biblioteca dependente
//#include <Status.h>
//#include <IPC.h>
//#include <Variables.h>
#include "GambainoCommon.h"
//#include <BrewCoreCommon.h>

#define MAXI2CCTRL 12

#define TERMOMETERWARNINGINTERVAL 15*MINUTESms
#define MAXDALLASBUSES 4

SemaphoreHandle_t mutexLog;

uint8_t NODALLAS                 [] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

#define MAXPROCVARS 120
byte    numProcVars = 0;
byte    numI2CClusters_;
byte    I2CClusters [MAXI2CCTRL];
boolean I2CInitialized[MAXI2CCTRL];
boolean I2CClusterChanged[MAXI2CCTRL];
boolean I2CClusterInverted[MAXI2CCTRL];
boolean I2CClusterDualBit[MAXI2CCTRL];
byte    I2CModes[MAXI2CCTRL];
ProcVar *ProcVars[MAXPROCVARS];

uint32_t ProcVar::datalogInterval_ = 0;
void (*ProcVar::say)(const char *format,...) = NULL;
void (*ProcVar::alert)(const char *format,...) = NULL;

    
DallasTemperature *DallasDevs [MAXDALLASBUSES];
OneWire           *DallasBuses[MAXDALLASBUSES];
unsigned long int lastDallasRequest = 0;
byte numDallasBuses = 0;

const int BIGBUFFERSIZE = 4000;
char bigBuffer[BIGBUFFERSIZE+1];

int datalogBatchNum = 0;
const char * datalogFolderName = NULL;
const char * datalogSheetName = NULL;
const char * datalogStatusName = NULL;
const char * datalogSubStatusName = NULL;
const char * datalogSecondaryStatusName = NULL;

ProcVar::ProcVar(
  char          _group_,
  const char    *_pinID_,
  const char    *_pinAlias_,
  float         _defaultValue_,
  byte          _pin_,
  byte          _I2CCluster_,
  byte          _I2CMask_,
  byte          _mode_,
  byte          _persistenceGroup_,
  byte          _dataLogGroup_,
  byte          _JSONGroup_,
  bool          _isInverted_,
  bool          _autoOverride_,
  uint8_t       *_DallasAddr_
) {
  group_ = _group_;
  strncpy(pinID_,_pinID_,PINIDLENGTH);
  strncpy(pinAlias_,_pinAlias_,PINALIASLENGTH);
  defaultValue_ = _defaultValue_;
  pin_ = _pin_;
  I2CCluster_ = _I2CCluster_;
  I2CMask_ = _I2CMask_;
  mode_= _mode_;
  persistenceGroup_ = _persistenceGroup_;
  dataLogGroup_ = _dataLogGroup_;
  JSONGroup_ = _JSONGroup_;
  isInverted_ = _isInverted_;
  autoOverride_ = _autoOverride_;
  //memcpy(DallasAddr_,_DallasAddr_,8);
  ext_ = (void*)_DallasAddr_;

  numProcVars++;
  if (numProcVars <= MAXPROCVARS)
    ProcVars[numProcVars - 1] = this;
}

void ProcVar::initProcVarLib() {
    mutexLog = xSemaphoreCreateMutex();

    if (numProcVars > MAXPROCVARS) 
        alert(" Error: exceeded maximum number of ProcVars");

    for (byte i=0; i<MAXI2CCTRL; i++) {
      I2CInitialized[i] = false;
      I2CClusterChanged[i] = false;
      I2CClusterInverted[i] = false;
      I2CClusterDualBit[i] = false;
    }

    byte numDallasVar = 0;
    byte numTimeCtrlVar = 0;

    for (int i = 0; i<numProcVars; i++) { // for each ProcVar
        ProcVar *x = ProcVars[i];
        x->overrided = NOTOVERRIDED;
//        x->count     = 0;


        if (x->pin() != 0) {
            if (x->modeIsOutput()) {
                byte  def = x->isInverted() ? !x->defaultValue() : x->defaultValue();
                digitalWrite(x->pin(),def);
                pinMode(x->pin(),OUTPUT);
                digitalWrite(x->pin(),def);
            }
        }

        if (x->modeIsDallas() && x->defaultValue() == 0)
            x->progValue = NOTaTEMP;
        else
            x->progValue = x->defaultValue();

        if (x->I2CCluster()) {
            int i;
            for (i = 0; (i < numI2CClusters_) && (I2CClusters[i] != x->I2CCluster()); i++);

            if (i==numI2CClusters_) { // it is a new bit cluster
                if (i < MAXI2CCTRL) {
                    I2CClusters [i] = x->I2CCluster();
                    I2CClusterChanged[i] = true;
                    I2CModes[i] = 0;
                    numI2CClusters_ ++;
                }
                else {
                    alert("Procvar: Exceeded maximum number of bit clusters in I2C control",x,i);
                    return;
                }
            }

        }

        if (x->timeControl()) {
            x->ext_ = (void*) calloc(1,sizeof(timeControlStruct));
            numTimeCtrlVar++;
        }

        if (!x->modeIsDallas() && x->DallasAddr())
            alert("Procvar: Attempt to setup Dallas Addr in a non-Dallas ProcVar",x,i);

        if (x->modeIsDallas()) {
          if (!x->ext_) 
            alert("Procvar: Attempt to setup a Dallas ProcVar with no Dallas address",x,i);
          else {
            numDallasVar++;
            uint8_t *temp = (uint8_t *) x->ext_;
            x->ext_ = (DallasStruct*) calloc(1,sizeof(DallasStruct));
            ((DallasStruct*)x->ext_)->addr = temp;
            ((DallasStruct*)x->ext_)->lastFluctuationMillis = 0;
            ((DallasStruct*)x->ext_)->fluctuationPos = 0;
            for (byte i=0;i<DALLASFLUCTUATIONNUM;i++)
              ((DallasStruct*)x->ext_)->fluctuationVect[i] = i;
            ((DallasStruct*)x->ext_)->avgPos = -DALLASAVERAGESIZE;
            ((DallasStruct*)x->ext_)->lossesCount = 0;
            ((DallasStruct*)x->ext_)->totalLosses = 0;
            ((DallasStruct*)x->ext_)->fluctuation = 0;
            ((DallasStruct*)x->ext_)->totalReadsForFluctuation = 0;
          }
        }

    } // for each ProcVar


    say("%d bytes used for control info of %d ProcVars",
        numProcVars * (sizeof(ProcVar*) + sizeof(ProcVar))+ 
                       numTimeCtrlVar*sizeof(timeControlStruct)+
                       numDallasVar*sizeof(DallasStruct)
        ,numProcVars);   

}

char ProcVar::group() {
    return group_;
}

char * ProcVar::tag() {
   return pinID_;
}

char * ProcVar::alias() {
    if (pinAlias_)
        return pinAlias_;
    else
        return tag();
}

float ProcVar::defaultValue() {
    return defaultValue_;
}

byte ProcVar::pin() {
    return pin_; 
}

byte ProcVar::I2CCluster() {
    return I2CCluster_; 
}

byte ProcVar::I2CMask() {
    return I2CMask_; 
}

byte ProcVar::mode() {
    return mode_; 
}

byte ProcVar::persistenceGroup() {
    return persistenceGroup_; 
}

byte ProcVar::dataLogGroup() {
    return dataLogGroup_; 
}

byte ProcVar::JSONGroup() {
    return JSONGroup_; 
}

bool ProcVar::isInverted() {
    return isInverted_; 
}

bool ProcVar::autoOverride() {
    return autoOverride_; 
}

bool ProcVar::timeControl() {
    return mode_==MODEOUTPUTTIMECTRL;
}

uint8_t *ProcVar::DallasAddr() {
    if (ext_ && ((DallasStruct*)ext_)->addr && ((DallasStruct*)ext_)->addr[0]) 
      return ((DallasStruct*)ext_)->addr;
    else   
      return NULL;

}


char *ProcVar::getDallasAddrAsString(char *buf) {
    ConvertDallasAddrToString(buf,DallasAddr());
    return buf;
}

void ProcVar::setProgValue(float v) {
    char buf[80];
    if (modeIsOutput() && (v<0 || v>1)) 
        alert("Procvar: setProgValue parameter out of range for digital output value (%s)",this->tag());
    else {
        timeControlStruct *tctrl;
        float actualValue;

        if (timeControl())
          tctrl = (timeControlStruct*)ext_;

        if (timeControl() && tctrl->delayWhenToSet > 0) 
            actualValue = tctrl->delayValueToSet; 
        else 
            actualValue = progValue;
        
        if (actualValue != v) { 
            if (modeIsOutput() && I2CCluster()) 
                flagI2C();
     
            if (modeIsOutput() && timeControl()   // consider latency
                    && (millis() < tctrl->lastLatencyTimeMillis + (long)tctrl->latencyTime*1000L)
                    && (defaultValue() != v)) {

                if (tctrl->delayWhenToSet == 0) {
                    if (tctrl->lastLatencyTimeMillis != 0)
                        tctrl->delayWhenToSet = tctrl->lastLatencyTimeMillis + (long)tctrl->latencyTime*1000L;
                    else // first time - no latency delay
                        tctrl->delayWhenToSet = millis();
                    tctrl->delayValueToSet = (v != 0);
                    tctrl->lastLatencyTimeMillis  = millis();
                    say("[latency delay] %s in %ld ms will change to %d",
                          displayString(buf,true),
                          tctrl->delayWhenToSet-millis(),
                          int(tctrl->delayValueToSet));

                }
            }
            else { 
                //xSemaphoreTake(mutexLog, pdMS_TO_TICKS(20));              
                progValue = v;
                //xSemaphoreGive(mutexLog);
                if ((modeIsOutput() || group()=='C'))
                    say("[set] %s",displayString(buf,true));
                if (timeControl()) {
                    if (tctrl->delayWhenToSet != 0) {
                        say("[previous delayed set cancelled] %s",displayString(buf,true)); 
                        tctrl->delayWhenToSet = 0;
                    }
                    tctrl->lastLatencyTimeMillis  = millis();
                }
            }
        }
    }

}


float ProcVar::operator = (const float d) {
  setProgValue(d);
  return d;
}

void ProcVar::operator = (ProcVar p) {
  setProgValue(p.value());
}



void ProcVar::setWithDelay(const float d, unsigned long int delay)
{
  if (modeIsOutput()) {
    if (timeControl()) {
      timeControlStruct *tctrl = (timeControlStruct*)ext_;
      if (tctrl->delayValueToSet != (d != 0) || (tctrl->delayWhenToSet  == 0 && d!=getProgValue())) { 
        tctrl->delayWhenToSet  = millis() + delay*1000L;
        tctrl->delayValueToSet = (d != 0);   
        char buf[40];            
        say("[delay %ld s] %s <-- %d", delay,displayString(buf,true), int(tctrl->delayValueToSet));
      } // otherwise ignore, since there is already an equal setWithDelay 
    }
    else
      alert("FIRMWARE ERROR: setWithDelay only applies to time controlled variables (%s)", this->tag());
  }
  else
    alert("FIRMWARE ERROR: setWithDelay only applies to output variables (%s)", this->tag());
}

void ProcVar::checkDelayedAction()
{
  if (modeIsOutput() && timeControl()) {
    timeControlStruct *tctrl = (timeControlStruct*)ext_;    
    if (tctrl->delayWhenToSet>0 && millis() > tctrl->delayWhenToSet) { /* Todo3: incluir tratamento da corrente com waitForValve */
      tctrl->delayWhenToSet = 0;
      progValue = tctrl->delayValueToSet;
      char buf[40];
      say("[delayed set] %s", displayString(buf,true));
    }
  }

}

void  ProcVar::setBooleanFromOffset(float TargetValue, float ActualValue, float Offset, char Signal)
{
  if (Signal == '+') {
    if (ActualValue >= TargetValue + Offset) 
      setProgValue(0);

    if (ActualValue <= TargetValue - Offset) 
      setProgValue(1);
  }
  else {
    if (ActualValue <= TargetValue - Offset)
      setProgValue(0);

    if (ActualValue >= TargetValue + Offset) 
      setProgValue(1);
  }
}

void ProcVar::override(float v) 
{
  char buf[80];
  if (!autoOverride()) {
      if (overrided != v) {
          overrided = v;
          if (modeIsOutput() && I2CCluster()) 
              flagI2C();
          if (v==NOTOVERRIDED)
              say("[release] %s",displayString(buf,true)); 
          else
              say("[override] %s",displayString(buf,true));
      }
  }
  else
      if (v != NOTOVERRIDED)
          setProgValue(v);
}

float ProcVar::getProgValue() 
{
  float x;
  x = progValue;
  return x;
}

bool ProcVar::getProgValueAsBoolean()
{
  bool b;
  b = progValue != 0;
  return b;

}


float ProcVar::getOverridedValue() 
{
  float x;
  x = overrided;
  return x;
}

void ProcVar::assumeOverrideAsProg()
{
  if (overrided != NOTOVERRIDED) {
    //xSemaphoreTake(mutexLog, pdMS_TO_TICKS(20));
    progValue = overrided;
    overrided = NOTOVERRIDED;
    //xSemaphoreGive(mutexLog);
  }
}

byte ProcVar::isOverrided()
{
  return (overrided != NOTOVERRIDED);
}



bool ProcVar::asBoolean()
{
  bool b;
  b = (value()>0.001) || (value()<-0.001);
  return b;
}


void ProcVar::write() {
  if (!modeIsOutput())
    alert("FIRMWARE ERROR:write is for output only variables (%s)",this->tag());
  else if (pin()) {
    if (isInverted())
      if (asBoolean())
        digitalWrite(pin(),LOW);
      else
        digitalWrite(pin(),HIGH);
    else
      if (asBoolean())
        digitalWrite(pin(),HIGH);
      else
        digitalWrite(pin(),LOW);
  }
}

void ProcVar::flagI2C()
{
  if (modeIsOutput() && I2CCluster()) {
    for (int i=0;i<numI2CClusters_;i++)
      if (I2CClusters[i] == I2CCluster()) {
        I2CClusterChanged[i] = true;
      }
  }
  else
    alert("FIRMWARE ERROR: flagI2C() can only be called in output variables with bit cluster (%s)",this->tag());
}


char *ProcVar::displayString(char *buf,bool noAddress)
{
    char fbuf[40];
    char pbuf[40];
    char pinbuf[30];
    
    if (modeIsOutput()) {
      sprintf(fbuf,"%i",(int)value());
      sprintf(pbuf,"%i",(int)progValue);
    }
    else {
      dtostrf(value()  ,3,1,fbuf);
      dtostrf(progValue,3,1,pbuf);
    }
   
    if (noAddress)
        pinbuf[0] = 0;
    else if (pin())
        sprintf(pinbuf,"[%i] ", pin());
    else if (modeIsOutput() && I2CCluster()) {
        sprintf(pinbuf,"[$%02x / 12345678] ", I2CCluster());
                                 
        binToStr(pinbuf+7,I2CMask());
        pinbuf[15] = ']';
        //      [$xx / 12345678]
        //      0123456789012345678901
    }
    else if (modeIsOutput())
        sprintf(pinbuf,"[N/D] ");
    else
        pinbuf[0] = 0;    
        
    if (overrided == NOTOVERRIDED) 
        sprintf(buf,"%s %s= %s",tag(),pinbuf,fbuf);
    else
        sprintf(buf,"%s %s= %s [overrided / original =  %s]",tag(),pinbuf,fbuf, pbuf);
    return buf;
}

char *ProcVar::JSONString(char *buf)
{
    char fbuf[20];
    
    if (modeIsOutput())
        sprintf(buf,"\"%s\":\"%i\",",tag(),(int)value());
    else {
        if (modeIsDallas() && (value()==NOTaTEMP || value()==85 || !DallasAddr()))
            fbuf[0] = 0;
        else
            dtostrf(value(),3,1,fbuf);
        sprintf(buf,"\"%s\":\"%s\",",tag(),fbuf);
    }
    if (isOverrided())
      sprintf(buf,"%s\"%s_O\":\"%i\",",buf,tag(),(int)isOverrided());
    return buf;
}

ProcVar* ProcVar::setLatency(unsigned long time)
{
    if (!timeControl()) {
        alert("FIRMWARE ERROR: Attempt to set latency in a ProcVar initialized without time control (%s)",this->tag());
        return this;
    }

    if (time>255) {
        alert("FIRMWARE ERROR: Attempt to set latency bigger than 255 seconds (%s)",this->tag());
        time = 255;
    }
    ((timeControlStruct*)ext_)->latencyTime = time;
    ((timeControlStruct*)ext_)->lastLatencyTimeMillis = 0;
        
    return this;
}

byte ProcVar::numVars()
{
    return numProcVars;
}

ProcVar* ProcVar::ProcVarByIndex(byte idx)
{
    return ProcVars[idx];
}

byte ProcVar::numI2CClusters()
{
    return numI2CClusters_;
}

byte ProcVar::I2CClusterAddress(byte idx)
{
    return I2CClusters[idx];
}

byte ProcVar::I2CGetClusterValue(byte idx) {
  byte data;

  data = 0x00;
  for (int i=0;i<numProcVars;i++)
    if (ProcVars[i]->modeIsOutput() && ProcVars[i]->I2CCluster() == I2CClusters[idx]) {
      if (     (ProcVars[i]->asBoolean() && !ProcVars[i]->isInverted())
          ||  (!(ProcVars[i]->asBoolean()) && ProcVars[i]->isInverted()))
          data = data | (ProcVars[i]->I2CMask());
      else 
        if (I2CClusterDualBit[idx]) 
          data = data | (ProcVars[i]->I2CMask()) << 1;
    }
  if (I2CClusterInverted[idx]) {
    Serial.print("data antes: ");Serial.println(data, BIN);
    data = ~data; 
    Serial.print("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% processed cluster inverted: ");Serial.println(idx);
    Serial.print("data depois: ");Serial.println(data, BIN);
  }
  return data;
}

void ProcVar::I2CSetInvertedCluster(byte idx)
{
  byte i;
  for (i = 0; (i < numI2CClusters_) && (I2CClusters[i] != idx); i++);
  if (I2CClusters[i] == idx) {
    I2CClusterInverted[i] = true;
  }
}

void ProcVar::I2CSetDualBitCluster(byte idx)
{
  byte i;
  for (i = 0; (i < numI2CClusters_) && (I2CClusters[i] != idx); i++);
  if (I2CClusters[i] == idx) 
    I2CClusterDualBit[i] = true;
}

ProcVar * ProcVar::ProcVarByID(const char *ID) {
    int i;
    for (i = 0;i<numProcVars;i++)
        if (!strcasecmp(ID,ProcVars[i]->tag()) || !strcasecmp(ID,ProcVars[i]->alias()))
            break;
    if (i<numProcVars)
        return (ProcVars[i]);
    else
        return NULL;
} 

void ProcVar::releaseAllOverrides() {
    for (int i=0;i<numProcVars;i++) 
        ProcVars[i]->override();
}

void ProcVar::writeProcVars()
{
    for (int i=0;i<numProcVars;i++) {
        ProcVars[i]->checkDelayedAction();
        if (ProcVars[i]->modeIsOutput())
            ProcVars[i]->write();
    }
    WriteI2Cs();
}

bool ProcVar::checkI2CCluster(byte j, char * device, char * msg) {
  if (j>=numI2CClusters_) {
    device[0] = 0;
    return false;
  }

  int cluster = I2CClusters[j];
  sprintf(device,"Device: 0x%02X",byte(cluster));
  
  yield();
  Wire.beginTransmission(cluster); 
  if (Wire.endTransmission()!=ESP_OK) {
    strcpy(msg,"Connection FAILED");
    return false;
  }
  else {
    yield();
    if (Wire.requestFrom(cluster,1)<=0) {
      strcpy(msg,"Read error");
      return false;
    }  
    else {
      int b = Wire.read();
      if (b==ProcVar::I2CGetClusterValue(j)) {
        sprintf(msg,"Read: 0x%02X (correct)",b);
        return true;
      }
      else {
        sprintf(msg,"Read: 0x%02X  -- expected value: 0x%02X",b,ProcVar::I2CGetClusterValue(j));
        return false;
      }
    }  
    yield();
  }
  return true;
}

char * ProcVar::getClustersStatus(char *st) {
  char buf1[40], buf2[40];
  char buf3[100];
  strcpy(st, "<br><br>I2C Status");

  for (int j=0; j<numI2CClusters_; j++) {
    checkI2CCluster(j,buf1,buf2);
    snprintf(buf3,100,"<BR>%s  --  %s",buf1,buf2);

    strnncat(st,buf3,1024);
    yield();
    taskYIELD();
  }
  return st;
}

void ProcVar::WriteI2Cs() {
    if(debugging) return;

    byte cluster;
    byte data;
    static unsigned long lastUpdate=0;
    const int UPDATEINTERVAL = 200;
    bool haveAGo = false; 

    if (MILLISDIFF(lastUpdate, UPDATEINTERVAL)) {
      haveAGo = true;
    }
    else {
      for (int j = 0; j<numI2CClusters_; j++) 
        if (I2CClusterChanged[j]) {
          I2CClusterChanged[j] = false;
          haveAGo=true;
          break;
        }
    }

    if (haveAGo) {
      for (int j = 0; j<numI2CClusters_; j++) {
          if (1 || I2CModes[j] & 0b01) {   
            cluster = I2CClusters[j];
            data = ProcVar::I2CGetClusterValue(j);

            int err;
            if (Wire.requestFrom(int(cluster),1,true) == 1) {
              byte b = Wire.read();
              if (b != data) { // then try again to confirm if it was not a read error
                delay(5); 

                if (Wire.requestFrom(int(cluster),1,true) == 1) {
                  b = Wire.read();
                  if (b != data) {
                    Wire.beginTransmission(int(cluster));
                    Wire.write(data);
                    Serial.print("I2C sent: addr 0X"); Serial.print(cluster,HEX); 
                    Serial.print("  ---> "); Serial.println(data,HEX);
                    if ((err=Wire.endTransmission(true)) != 0) {
                      Serial.print("  error: "); Serial.println(err);
                      b = data; // error reading, better do nothing
                    }
                    else {
                      //Serial.println("  OK");
                    }
                    delay(5);
                  }
                }
                else {
                  b = data; // error reading, better do nothing
                }
              }
              if (b != data) {
                int attempt = 1;
                Wire.beginTransmission(int(cluster));
                Wire.write(data);               
                err = Wire.endTransmission(true);
                while (err != 0 &&  attempt < 5) {
                  Wire.beginTransmission(int(cluster));
                  Wire.write(data);
                  int err2 = Wire.endTransmission(true);
                  delay(5);
                    
                  Serial.print("I2C error: addr 0X"); Serial.print(cluster,HEX); 
                  Serial.print("  error: "); Serial.print(err); 
                  Serial.print(" Last value: "); Serial.print(b);
                  Serial.print(" New value:  "); Serial.print(data);
                  Serial.print(" attempt "); Serial.println(attempt);

                  attempt ++;
                  err = err2;
                }
                if (attempt<=2) 
                  I2CClusterChanged[j] = 0;
              }
            }
          }
      }
      lastUpdate = millis();
    }
}



void  ProcVar::setDallasLib(DallasTemperature *_dallas,OneWire *_bus)
{
    if (numDallasBuses >= MAXDALLASBUSES)
      alert("Procvar: Too many Dallas Buses");
        
    DallasDevs[numDallasBuses] = _dallas;
    DallasBuses[numDallasBuses] = _bus;
    DallasDevs[numDallasBuses] -> setWaitForConversion(false);
    numDallasBuses++;    
}

bool  ProcVar::modeIsOutput()
{
    int i = mode();
    return i == MODEOUTPUT
        || i == MODEOUTPUTTIMECTRL;
}

bool  ProcVar::modeIsDallas()
{
    int i = mode();
    return i == MODEDALLAS;
}

bool ProcVar::isValidTemp() {
  return value()!=85 && value()!=-127;
}

void  ProcVar::acquireDallas()
{
    static unsigned long lastWarning = -TERMOMETERWARNINGINTERVAL;
    bool warning;
    
    if (!DallasDevs[0]) {
        alert("Procvar: Dallas not initialized");
        return;
    }

    if (lastDallasRequest==0) {
        for (byte bus=0; bus<numDallasBuses; bus++) DallasDevs[bus]->requestTemperatures();
        lastDallasRequest = millis();
    }
    
    if (MILLISDIFF(lastDallasRequest, DALLASREQUESTINTERVAL)) {
        warning = false;

        for (int i = 0; i<numProcVars; i++) {
            if (ProcVars[i]->modeIsDallas()) {
                DallasStruct *ds = (DallasStruct*) ProcVars[i]->ext_;
                byte bus = ProcVars[i]->pin();
                if (ProcVars[i]->DallasAddr()) {
                    float x = DallasDevs[bus]->getTempC(ProcVars[i]->DallasAddr());
                    if (debugging && (x==85 || x==-127)) x = 20; //else {;Serial.print("x dallas =  ");Serial.println(x);}
                    if (x == DEVICE_DISCONNECTED_C || x == DEVICE_DISCONNECTED_RAW || x == 85 || x > 110) {
                        if (ProcVars[i]->getProgValue() != NOTaTEMP) {
                            ds->lossesCount ++;
                            if (ds->lossesCount==1)
                              ds->totalLosses ++;
                            if (ds->lossesCount >= (x==85 ? 20 : 10)) { /**** Constantes *****/
                                ProcVars[i]->setProgValue(NOTaTEMP);
                                char addrstr[60];
                                ConvertDallasAddrToString(addrstr,ProcVars[i]->DallasAddr());
                                say("Warning: lost thermometer %s %s",ProcVars[i]->tag(), addrstr);
                            }
                        }
                        else if (MILLISDIFF(lastWarning, TERMOMETERWARNINGINTERVAL)) {
                            char addrstr[60];
                            ConvertDallasAddrToString(addrstr,ProcVars[i]->DallasAddr());
                            alert("ERROR: Thermometer %s is disconnected/misconfigured (%.1f) %s", 
                                  ProcVars[i]->tag(), x, addrstr);
                            warning = true;
                        }
                    }
                    else {
                        float f;
                        if (ds->avgPos<0) {
                          f = x;
                          ds->avgReads[ds->avgPos+DALLASAVERAGESIZE] = x;
                          ds->avgPos++;
                        }
                        else {
                          ds->avgReads[ds->avgPos] = x;
                          f = 0;
                          for (byte p=0;p<DALLASAVERAGESIZE;p++)
                            f += ds->avgReads[p];
                          f /= DALLASAVERAGESIZE;
                          ds->avgPos = (ds->avgPos+1) % DALLASAVERAGESIZE;
                        }

                        ProcVars[i]->setProgValue(f);

                        ds->lossesCount = 0;

                        // fluctuation monitoring
                        ds->totalReadsForFluctuation++;
                        if (ds->fluctuationPos==0) {
                          ds->fluctuationVect[0] = x;
                          ds->lastFluctuationMillis = millis();
                          ds->fluctuationPos++;
                        }
                        else {
                          if (!(MILLISDIFF(ds->lastFluctuationMillis,DALLASFLUCTUATIONTIMELIMIT)) 
                              && (x != ds->fluctuationVect[ds->fluctuationPos-1])
                              && abs(x - ds->fluctuationVect[ds->fluctuationPos-1]) <= DALLASFLUCTUATIONTHRESHOLD) {
                            ds->fluctuationVect[ds->fluctuationPos] = x;
                            ds->fluctuationPos++;
                            ds->lastFluctuationMillis = millis();
                            if (ds->fluctuationPos == DALLASFLUCTUATIONNUM) {
                              float t = 0;
                              for (byte p=1; p<DALLASFLUCTUATIONNUM; p++)
                                t += abs(ds->fluctuationVect[p] - ds->fluctuationVect[p-1]);
                              if (t > DALLASFLUCTUATIONTHRESHOLD*DALLASFLUCTUATIONNUM)
                                ds->fluctuation++;
                              ds->fluctuationPos = 0;
                            }
                          }
                          else
                            ds->fluctuationPos = 0;
                        }
                    }
                }
                else
                    ProcVars[i]->setProgValue(NOTaTEMP);
            }
        }
        if (warning)
            lastWarning = millis();
            
        for (byte bus=0; bus<numDallasBuses; bus++) DallasDevs[bus]->requestTemperatures();
        lastDallasRequest = millis();
    }
}

char*   ProcVar::ConvertDallasAddrToString(char *buf, uint8_t* addr)
{
    sprintf(buf,"{0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x}",
            addr[0] & 0xff, addr[1] & 0xff, addr[2] & 0xff, addr[3] & 0xff,
            addr[4] & 0xff, addr[5] & 0xff, addr[6] & 0xff, addr[7] & 0xff);

    return buf;
}            

void ProcVar::DallasScan(char * buf, int bufSize) {
  int n;
  int i, j, k;
  float t=0;
  bool match;
  
  uint8_t addr[8];
  char addrstr[60];
  char *pvID;
  char line[300];
  char notAssigned[] = "NOT ASSIGNED";

  if (buf) buf[0] = 0;

  for (i=0; i<numProcVars; i++)
    if (ProcVars[i]->modeIsDallas()) 
      ((DallasStruct*) ProcVars[i]->ext_) -> foundInScan = false;

  say("Scanning for Dallas thermometers");
  for (byte bus = 0; bus<numDallasBuses; bus++)
    DallasDevs[bus]->requestTemperatures();
  delay(DALLASREQUESTINTERVAL*2);

  for (byte bus = 0; bus<numDallasBuses; bus++) {
    say("");
    if (buf)
      strnncat(buf,"<BR>",bufSize);

    sprintf(line,"Scanning for Dallas thermometers on bus %d - %s",(int)bus+1,
                 DallasDevs[bus]->isParasitePowerMode() ? "parasitic mode" : "three-wire mode"
           );
    say(line);

    if (buf) {
      strnncat(buf,line,bufSize);
      strnncat(buf,"<BR><BR>",bufSize);
    }

    //n = DallasDevs[bus]->getDeviceCount();    
    //say("%d devices found",(int)n); -- does not work as in 2023 - try in newer lib versions later
    int count = 0;
      
    DallasBuses[bus]->reset_search();
    i = 0;
    while(DallasBuses[bus]->search(addr)) {
      i++;
      if (true) {
        delay(1);
        count++;
        ConvertDallasAddrToString(addrstr,addr);
        t = 0;
        pvID = notAssigned;
        for (j = 0; j < numProcVars; j++) {
          if (ProcVars[j]->modeIsDallas() && ProcVars[j]->DallasAddr()) {
            match = true;
            for (k = 0; k < 8; k++) {
              if (addr[k] != ProcVars[j]->DallasAddr()[k]) {
                match = false;
                break;
              }
            }
            if (match) {
              pvID = ProcVars[j]->tag();
              ((DallasStruct*) ProcVars[j]->ext_) -> foundInScan = true;
              t = ProcVars[j]->getProgValue();
              break;
            }
            else
              t = DallasDevs[bus]->getTempC(addr);                
          }
        }
        //t = DallasDevs[bus]->getTempC(addr);
        sprintf(line, "Found Dallas thermometer %d addr %s --> %s = %.2fC %s -- diag: %s",i,
          addrstr,pvID,float(t),
          (j<numProcVars && bus != ProcVars[j]->pin() && match)?" --> CONNECTED TO WRONG BUS":"",
          "Desabilitado" /*checkupDallas(DallasBuses[bus],addr).c_str()*/); //***** Pendencia
        say(line);
        if (buf) {
          strnncat(buf,line,bufSize);
          strnncat(buf,"<BR>",bufSize);
        }
      }
    }
    sprintf(line,"%i thermometers found",count);
    say(line);
    say("");
    if (buf) {
      strnncat(buf,line,bufSize);
      strnncat(buf,"<BR><BR>",bufSize);
    }
  }

  for (i=0; i<numProcVars; i++)
    if (ProcVars[i]->modeIsDallas() && !(((DallasStruct*) ProcVars[i]->ext_) -> foundInScan) ) {
      pvID = ProcVars[i]->tag();
      ConvertDallasAddrToString(addrstr,ProcVars[i]->DallasAddr());                
      say("           Missing Dallas thermometer addr %s --> %s (bus %d)",addrstr,pvID,ProcVars[i]->pin()+1);
      if (buf) {
        sprintf(line,"Missing Dallas thermometer addr %s --> %s (bus %d)",addrstr,pvID,ProcVars[i]->pin()+1);
        strnncat(buf,line,bufSize);
        strnncat(buf,"<BR>",bufSize);
      }
    }
}

char * ProcVar::getDallasStatus(char *buf, byte mode, int bufSize) {
  char line[200];
  
  switch (mode) {
    case 1:
      strcpy(buf,"<BR>Dallas thermometers status<BR>");
    
      for (int i=0; i<numProcVars; i++)
        if (ProcVars[i]->modeIsDallas()) {
          DallasStruct* ds = (DallasStruct*) ProcVars[i]->ext_;
          if (ProcVars[i]->getProgValue() != NOTaTEMP) 
            if (ds -> lossesCount> 0)
              snprintf(line,200,"WARNING: therm  %s had %d consecutive errors. Considering last reading = %.1fºC<BR>",ProcVars[i]->tag(),ds->lossesCount,ProcVars[i]->getProgValue());
            else
              snprintf(line,200,"OK: therm  %s = %.1fºC<BR>",ProcVars[i]->tag(),ProcVars[i]->getProgValue());
          else
            snprintf(line,200,"ERROR: therm %s not found<BR>",ProcVars[i]->tag());
    
          strnncat(buf,line,bufSize);
        }
      break;
          
    case 2:
      strcpy(buf,"<BR><BR>Dallas thermometer monitor<BR>");
      for (int i=0; i<numProcVars; i++)
        if (ProcVars[i]->modeIsDallas()) {
          DallasStruct* ds = (DallasStruct*) ProcVars[i]->ext_;
          if (ds && ds->totalReadsForFluctuation>0) 
            snprintf(line,200,"Therm: %s: %d losses -- %d fluctuations/kReads (%d / %d)<BR>",
              ProcVars[i]->tag(),
              int(ds->totalLosses),
              int(ds->fluctuation*1000*DALLASFLUCTUATIONNUM/ds->totalReadsForFluctuation),
              int(ds->fluctuation),
              int(ds->totalReadsForFluctuation));
          else
            snprintf(line,200,"Therm: %s: no thermometer structure<BR>",ProcVars[i]->tag());
          strnncat(buf,line,bufSize);
        }
      break;
                  
    default:
      buf[0]=0;
  }

  return buf;
}


String ProcVar::getDallasStatus(byte mode, int bufSize) {
  char buf[2048];
  getDallasStatus(buf, mode, bufSize);
  return String(buf);
}



unsigned long    ProcVar::persistenceChecksum(byte PersistenceGroup)
{
  unsigned long chk = 0;
  int  padding=0;
  
  for (int i = 0; i<numProcVars; i++) {
    if (ProcVars[i]->persistenceGroup() == PersistenceGroup) {
      const char * pos = ProcVars[i]->tag();
      while (byte b = (byte)(*pos)) {
        unsigned long x = ((unsigned long) b) << padding;
        chk = chk xor x;
        padding = (padding + 1) % 15;
        pos++;
      }
    }
  }
  return chk;
}

void ProcVar::write_eeStructToEEPROM(int addr, eeStruct v)
{
  byte *b;
  int addrTemp = addr;
  
  eeStruct t = read_eeStructFromEEPROM(addrTemp);
  if (v.f != t.f) {
    b = (byte *)&v.li;

    for (int i=0; i<4; i++)  {
      EEPROM.write(addr+i,*b);
      b++;
    }
  }
}

eeStruct ProcVar::read_eeStructFromEEPROM(int addr)
{
  eeStruct v;
  byte *b;
  
   b = (byte *) &v.li;

  for (int i=0; i<4; i++) {
    (*b) = EEPROM.read(addr+i);
    b++;
  }
  return v;
}

void ProcVar::forceResetToFactory()
{
  eeStruct s;
  s.f=0;
  int pos;
  
  pos=CONFIGEEPROMSTARTADDR; write_eeStructToEEPROM(pos,s);
  pos=PROCESSEEPROMSTARTADDR; write_eeStructToEEPROM(pos,s);
  pos=OVERRIDEEEPROMSTARTADDR; write_eeStructToEEPROM(pos,s);
  ESPRestart(NULL);
}
  

void ProcVar::writeToEEPROM(byte persistenceGroup)
{
    int pos,maxPos=0;
    eeStruct s;

    pos = 0;
    switch (persistenceGroup) {
        case CONFIGPERSISTENCE :  pos = CONFIGEEPROMSTARTADDR;   maxPos = CONFIGEEPROMENDADDR-3;   break;
        case PROCESSPERSISTENCE:  pos = PROCESSEEPROMSTARTADDR;  maxPos = PROCESSEEPROMENDADDR-3;  break;
        case OVERRIDEPERSISTENCE: pos = OVERRIDEEEPROMSTARTADDR; maxPos = OVERRIDEEEPROMENDADDR-3; break;
        default: alert("Procvar: Persistence mode %d not supported",int(persistenceGroup));
    }
    if (pos) {
        s.li = persistenceChecksum(persistenceGroup);
        write_eeStructToEEPROM(pos,s); 
        pos += 4;
        for (int i=0; i<numProcVars; i++) {
            if (ProcVars[i]->persistenceGroup() == persistenceGroup) {
                s.f = ProcVars[i]->getProgValue();
                write_eeStructToEEPROM(pos,s); 
                pos += 4;
            }
            if ((ProcVars[i]->persistenceGroup() == persistenceGroup) || 
                (ProcVars[i]->persistenceGroup() == NOTPERSISTENT && persistenceGroup == OVERRIDEPERSISTENCE)) {
                  
                s.f = ProcVars[i]->getOverridedValue();
                write_eeStructToEEPROM(pos,s); 
                pos += 4;
            }
            if (pos>maxPos)
                alert("FIRMWARE ERROR: Persistence group %d exceeded EEPROM address range",persistenceGroup);
        }
    }
    EEPROM.commit();
}

void ProcVar::readFromEEPROM(byte persistenceGroup) {
    int pos;
    eeStruct s;
    const char * name = NULL;
    pos = 0;
    
    switch (persistenceGroup) {
        case CONFIGPERSISTENCE :  pos = CONFIGEEPROMSTARTADDR;   name = "configuration"; break;
        case PROCESSPERSISTENCE:  pos = PROCESSEEPROMSTARTADDR;  name = "process";       break;
        case OVERRIDEPERSISTENCE: pos = OVERRIDEEEPROMSTARTADDR; name = "override";      break;
        default: alert("Procvar: Persistence mode not supported");
    }
    
    s = read_eeStructFromEEPROM(pos); 
    pos += 4;
    if (s.li != persistenceChecksum(persistenceGroup)) { 
        say("Data signature for persistence group %s is obsolete. Using firmware default values instead of saved ones.",
            name);
    }
    else {
        for (int i=0; i<numProcVars; i++) {

            if (ProcVars[i]->persistenceGroup() == persistenceGroup) {
                s = read_eeStructFromEEPROM(pos);
                pos += 4;
                ProcVars[i]->setProgValue(s.f);
            }
            if ((ProcVars[i]->persistenceGroup() == persistenceGroup) || 
                (ProcVars[i]->persistenceGroup() == NOTPERSISTENT && persistenceGroup == OVERRIDEPERSISTENCE)) {
                s = read_eeStructFromEEPROM(pos);
                pos += 4;
                ProcVars[i]->override(s.f);
            }
        }
        say("Successfully loaded persistent data from group %s",name);
    }
}

void ProcVar::dataLogHeader(char *buf, int bufSize) {
  if (!buf || bufSize <= 0) {
    return;
  }

  strcpy(buf, "DateTime;Status;SubStatus;IPCStatus");
  int pos = strlen(buf);

  for (int i = 0; i < numProcVars; i++) {
    ProcVar *x = ProcVars[i];
    if (!x || x->dataLogGroup() == DATALOGOFF) {
      continue;
    }

    const char *tag = x->tag();
    if (!tag || !tag[0]) {
      continue;
    }

    int tagLen = strlen(tag);
    int required = tagLen + 1;
    if (pos + required >= bufSize-15) { // reserve space for ;Millis;Cycles and null terminator
      break;
    }
    
    buf[pos++] = ';';

    strncpy(buf + pos, tag, bufSize - pos - 1);
    pos += tagLen;
    buf[pos] = '\0';
  }
  strcat(buf, ";Millis;Cycles");
}

void ProcVar::setDatalogData(int batchNum, const char *folderName, const char * sheetName, const char *statusName, const char *subStatusName, const char *secondaryStatusName) {
  datalogBatchNum = batchNum;
  datalogFolderName = folderName;
  datalogSheetName = sheetName;
  datalogStatusName = statusName;
  datalogSubStatusName = subStatusName;
  datalogSecondaryStatusName = secondaryStatusName;
}

void ProcVar::dataLogTask(void *pvParameters) {
  
  uint32_t intervalLog; 

  int lastDatalogBatchNum   = 0;

  unsigned long int lastHotDatalog = 0;
 
  char spreadsheetName[10];
  char sheetName[5];
  const unsigned long int coldSideLogInterval = 60000;
  bool hasLog = false;

  for (;;) {
    // Reset watchdog
    esp_task_wdt_reset();
    
    if (!safeMode) {
      if (debugging && millis()>20*60L*60L*1000L) { // Data log task is being killed after 5 hours
      //  if ( millis()>5*60L*60L) { // Data log task is being killed after 5 hours
          say("Data log task is being killed after some time in debugging mode");
        vTaskDelete(NULL);
        return;
      }

      //if (xSemaphoreTake(mutexLog, pdMS_TO_TICKS(20))) {
        intervalLog = ProcVar::getDatalogInterval();
        //xSemaphoreGive(mutexLog);
      //}

      hasLog = false;

      if (datalogFolderName && datalogFolderName[0]) {
        if (1/*xSemaphoreTake(mutexLog, pdMS_TO_TICKS(20))*/) {
          // HOT
          if (intervalLog > 0) {
            if (datalogBatchNum) {
              snprintf(spreadsheetName,4,"%03d",datalogBatchNum);
              strcpy(sheetName, datalogSheetName);
              if (lastDatalogBatchNum != datalogBatchNum) {
                hasLog = true;
                lastDatalogBatchNum = datalogBatchNum;
                GLogBegin(datalogFolderName, spreadsheetName, sheetName);
                GLogAddTimeStamp();
                GLogAddData("Status");
                GLogAddData("SubStatus");
                GLogAddData("IPCStatus");
                for (int i = 0; i<numProcVars; i++)
                  if (ProcVars[i]->dataLogGroup() == DATALOGPROCESS) {
                    GLogAddData(ProcVars[i]->tag());
                  }
                GLogAddData("Millis");
                GLogAddData("Cycles");
              }
              else if (MILLISDIFF(lastHotDatalog, intervalLog)) {
                hasLog = true;
                lastHotDatalog = millis();
                GLogBegin(datalogFolderName, spreadsheetName, sheetName);
                GLogAddTimeStamp();
                GLogAddData(datalogStatusName ? datalogStatusName : "");
                GLogAddData(datalogSubStatusName ? datalogSubStatusName : "");
                GLogAddData(datalogSecondaryStatusName ? datalogSecondaryStatusName : "");
                for (int i = 0; i<numProcVars; i++)
                  if (ProcVars[i]->dataLogGroup() == DATALOGPROCESS) 
                    if (ProcVars[i]->modeIsDallas() && !ProcVars[i]->isValidTemp() || ProcVars[i]->value()==NOTaTEMP)
                      GLogAddData("");
                    else
                      GLogAddData(ProcVars[i]->value());
                GLogAddData(millis());
                GLogAddData(numCycles);
              }
            }
          }
          //xSemaphoreGive(mutexLog);
          yield(); taskYIELD();
        }
            
        if (hasLog) { 
          // Tentar enviar log com timeout
          try {
            GLogSend();
          } catch (...) {
            // Log de erro em caso de falha
            alert("Error sending GLog data");
          }
        }
      }
    } // if (!safeMode)

    vTaskDelay(500 / portTICK_PERIOD_MS);  
  } // infinite for loop}
   
}

void ProcVar::setSayFunction(void (*func)(const char *format,...)) {
  say = func;
}

void ProcVar::setAlertFunction(void (*func)(const char *format,...)) {
  alert = func;
}


