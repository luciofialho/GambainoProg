#ifndef ProcVar_h

#define ProcVar_h

#include "Arduino.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#ifdef __AVR__
  #include <EEPROM.h>
#endif
#include <Time.h>
#include <Wire.h>
#include <ESPAsyncWebServer.h>

#define MAXLINE             81           // Maximum length of a displayed line or command 
#define MAXTOKENS           3            // Maximum number of tokens per command

#define MODEOUTPUT          0
#define MODEOUTPUTTIMECTRL  1
#define MODEFLOAT           2
#define MODEDALLAS          3

#define NOPIN               0,0,0
#define DALLAS1             0,0,0
#define DALLAS2             1,0,0
#define DALLAS3             2,0,0
#define DALLAS4             3,0,0

#define NOTPERSISTENT       (uint8_t)0
#define PROCESSPERSISTENCE  (uint8_t)1
#define CONFIGPERSISTENCE   (uint8_t)2
#define OVERRIDEPERSISTENCE (uint8_t)255  // not a persistence group, but a option to use in function call
#define CONFIGEEPROMSTARTADDR   100
#define CONFIGEEPROMENDADDR     499
#define PROCESSEEPROMSTARTADDR  500
#define PROCESSEEPROMENDADDR    899
#define OVERRIDEEEPROMSTARTADDR 900
#define OVERRIDEEEPROMENDADDR  1300

#define NOJSON          0
#define JSONPROCESS     1
#define JSONRECIPE      2
#define JSONCOLDSIDE    3
#define JSONCONFIG      4

#define DATALOGOFF      0
#define DATALOGPROCESS  1
#define DATALOGCOLD1    2
#define DATALOGCOLD2    3
#define DATALOGCOLD3    4

#define NOTINVERTED     false
#define INVERTED        true

#define NORMALOVERRIDE  false
#define AUTOOVERRIDE    true

#define NOTOVERRIDED -327
#define NOLATENCY -99
#define NOTaTEMP        DEVICE_DISCONNECTED_C
#define DALLASREQUESTINTERVAL 1000

#define PINIDLENGTH    30
#define PINALIASLENGTH  5

extern const int BIGBUFFERSIZE;

#define DECL(GR,TAG,ALIAS,MODE,PERS,DEF,ADDR,DL,JSON,INV,OVER,DALLAS) const char _ ## TAG ##_N[] = #TAG; const char _ ## TAG ##_A[] = #ALIAS; ProcVar TAG(GR,_ ## TAG ##_N,_ ## TAG ##_A,DEF,ADDR,MODE,PERS,DL,JSON,INV,OVER,DALLAS);

extern uint8_t NODALLAS                 [];

typedef struct {
  uint8_t              delayValueToSet;
  unsigned long     delayWhenToSet;
  uint8_t              latencyTime;
  unsigned long     lastLatencyTimeMillis;
} timeControlStruct;

/*
struct dataLogTaskParameters {
    uint8_t logGroup;
    const char *folderName;
    const char *spreadsheetName;
    const char *sheetName;
    volatile int32_t interval; // Volatile para evitar otimizações indevidas
};
*/

#define DALLASAVERAGESIZE 5
#define DALLASFLUCTUATIONNUM 5
#define DALLASFLUCTUATIONTHRESHOLD 0.1 
#define DALLASFLUCTUATIONTIMELIMIT 5*60*1000L
typedef struct {
  uint8_t *addr;
  unsigned long lastFluctuationMillis;
  uint8_t fluctuationPos;
  float fluctuationVect[DALLASFLUCTUATIONNUM]; 
  int8_t avgPos;
  float avgReads[DALLASAVERAGESIZE];
  unsigned int lossesCount;
  unsigned int totalLosses;
  unsigned int fluctuation;
  unsigned int totalReadsForFluctuation;
  bool foundInScan;

} DallasStruct;

typedef union {
  struct {uint8_t b0,b1,b2,b3;} bytes;
  float f;
  uint32_t li;
} eeStruct; // for eeprom

class ProcVar {
    public: 
        ProcVar(    char          _group_,
                    const char    *_pinID_,
                    const char    *_pinAlias_,
                    float         _defaultValue_,
                    uint8_t          _pin_,
                    uint8_t          _I2CCluster_,
                    uint8_t          _I2CMask_,
                    uint8_t          _mode_,
                    uint8_t          _persistenceGroup_,
                    uint8_t          _dataLogGroup_,
                    uint8_t          _JSONGroup_,
                    bool          _isInverted_,
                    bool          _autoOverride_,
                    uint8_t      *_DallasAddr_
                );
        static void    initProcVarLib();        
        void statisticsMessage();
        
        char    group();        
        char    *tag();
        char    *alias();
        float   defaultValue();
        uint8_t    pin();
        uint8_t    I2CCluster();
        uint8_t    I2CMask();
        uint8_t    mode();
        uint8_t    persistenceGroup();
        uint8_t    dataLogGroup();
        uint8_t    JSONGroup();
        bool    isInverted();
        bool    autoOverride();
        bool    timeControl();
        uint8_t *DallasAddr();        
        
        char * getDallasAddrAsString(char *buf);
        float  operator = (const float d);
        void   operator = (ProcVar p);
        //ProcVar  operator = (const ProcVar p);
      
        void   setWithDelay(const float d, unsigned long int delay);
        void   checkDelayedAction();
        operator float()  {if (overrided != NOTOVERRIDED) return (float)overrided; else return (float)progValue;};
        operator int()    {if (overrided != NOTOVERRIDED) return (int)overrided; else return (int)progValue;};        
        operator long int()    {if (overrided != NOTOVERRIDED) return (long int)overrided; else return (long int)progValue;};        
        operator long unsigned int()    {if (overrided != NOTOVERRIDED) return (long unsigned int)overrided; else return (long unsigned int)progValue;};        
        operator uint8_t()   {if (overrided != NOTOVERRIDED) return (uint8_t)(int)overrided; else return (uint8_t)(int)progValue;};        
        bool   asBoolean();

        void  setBooleanFromOffset(float TargetValue, float ActualValue, float Offset, char Signal = '+');
        void  override(float v=NOTOVERRIDED);
        float getProgValue();
        bool  getProgValueAsBoolean();
        float getOverridedValue();
        void  assumeOverrideAsProg();
        uint8_t  isOverrided();

        void  write();
        char  *displayString(char *buf,bool noAddress = false);
        char  *JSONString(char *buf);
        ProcVar* setLatency(unsigned long time = NOLATENCY);
        void  flagI2C();
        bool  modeIsOutput();
        bool  modeIsDallas();
        bool  isValidTemp();
        

        static uint8_t numVars();
        static ProcVar* ProcVarByIndex(uint8_t idx);
        static uint8_t numI2CClusters();
        static uint8_t I2CClusterAddress(uint8_t idx);
        static uint8_t I2CGetClusterValue(uint8_t idx);
        static void I2CSetClusterValue(uint8_t idx,uint8_t data,uint8_t valid);
        static void I2CSetInvertedCluster(uint8_t idx);
        static void I2CSetDualBitCluster(uint8_t idx);

        static ProcVar *ProcVarByID(const char *ID);
        static void    releaseAllOverrides();
        static void    writeProcVars();
        static void    WriteI2Cs();
        static void    setDallasLib(DallasTemperature *_dallas,OneWire *_bus);
        static void    acquireDallas();        
        static void    DallasScan(char * buf=NULL, int bufSize=0);
        static void    forceResetToFactory();
        static void    writeToEEPROM(uint8_t persistenceGroup);
        static void    readFromEEPROM(uint8_t persistenceGroup);
        static bool    checkI2CCluster(uint8_t j, char * device, char * msg);
        static char *  getClustersStatus(char *st);
        static char *  getDallasStatus(char *st, uint8_t mode, int bufSize);
        static String  getDallasStatus(uint8_t mode, int bufSize);

        static void         dataLogHeader(char *buf, int bufSize);
        static void         setDatalogInterval(uint32_t interval) {datalogInterval_ = interval;};
        static uint32_t     getDatalogInterval() {return datalogInterval_;};
        static void         setDatalogData(int batchNum, const char *folderName, const char * sheetName, const char *statusName, const char *subStatusName, const char *secondaryStatusName);
        static void         dataLogTask(void *pvParameters);

        static void setSayFunction(void (*func)(const char *format,...));
        static void setAlertFunction(void (*func)(const char *format,...));      

    private:
        char          group_;
        char          pinID_[PINIDLENGTH+1];
        char          pinAlias_[PINALIASLENGTH+1];
        float         defaultValue_;
        uint8_t          pin_;
        uint8_t          I2CCluster_;
        uint8_t          I2CMask_;
        uint8_t          mode_;
        uint8_t          persistenceGroup_;
        uint8_t          dataLogGroup_;
        uint8_t          JSONGroup_;
        bool          isInverted_;
        bool          autoOverride_;
        void          *ext_;    
 
        float         progValue;
        float         overrided;
        //uint8_t          count;

        static uint32_t datalogInterval_;
        static void (*say)(const char *format,...);
        static void (*alert)(const char *format,...);

        void  setProgValue(float v);
        void  writeI2C(int cluster);
        
        float value() {if (overrided != NOTOVERRIDED) return (float)overrided; else return (float)progValue;};

        static char*   ConvertDallasAddrToString(char *buf, uint8_t* addr);
        
        static unsigned long    persistenceChecksum(uint8_t PersistenceGroup);
        static void             write_eeStructToEEPROM(int addr, eeStruct v);
        static eeStruct         read_eeStructFromEEPROM(int addr);


};

inline bool operator ==  (ProcVar p, float d)    {return (float)p == d;};
inline bool operator ==  (ProcVar p, double d)   {return (float)p == (float)d;};
inline bool operator ==  (ProcVar p, int    i)   {return (float)p == i;};
inline bool operator ==  (ProcVar p, unsigned long int i) {return (float)p == i;};
inline bool operator ==  (float   d, ProcVar p)  {return (float)p == d;};
inline bool operator ==  (double  d, ProcVar p)  {return (float)p == (float)d;};
inline bool operator ==  (int     i, ProcVar p)  {return (float)p == i;};
inline bool operator ==  (unsigned long int i, ProcVar p) {return i == (float)p;};
inline bool operator !=  (ProcVar p, float d)    {return (float)p != d;};
inline bool operator !=  (ProcVar p, double d)   {return (float)p != (float)d;};
inline bool operator !=  (ProcVar p, int    i)   {return (float)p != i;};
inline bool operator !=  (ProcVar p, unsigned long int i) {return (float)p != i;};
inline bool operator !=  (float   d, ProcVar p)  {return (float)p != d;};
inline bool operator !=  (double  d, ProcVar p)  {return (float)p != (float)d;};
inline bool operator !=  (int     i, ProcVar p)  {return (float)p != i;};
inline bool operator !=  (unsigned long int i, ProcVar p) {return i != (float)p;};
inline bool operator >   (ProcVar p, float d)    {return (float)p > d;};
inline bool operator >   (ProcVar p, double d)   {return (float)p > d;};
inline bool operator >   (ProcVar p, int    i)   {return (float)p > i;};
inline bool operator >   (ProcVar p, long int i) {return (float)p > i;};
inline bool operator >   (ProcVar p, unsigned long int i) {return (float)p > i;};
inline bool operator >   (ProcVar p, ProcVar q)  {return (float)p > float(q);};
inline bool operator >   (unsigned long int i, ProcVar p) {return i > (float)p;};
inline bool operator >=  (ProcVar p, float d)    {return (float)p >= d;};
inline bool operator >=  (ProcVar p, double d)   {return (float)p >= d;};
inline bool operator >=  (ProcVar p, int    i)   {return (float)p >= i;};
inline bool operator >=  (ProcVar p, long int i) {return (float)p >= i;};
inline bool operator >=  (ProcVar p, unsigned long int i) {return (float)p >= i;};
inline bool operator >=  (ProcVar p, ProcVar q)  {return (float)p >= float(q);};
inline bool operator >=  (unsigned long int i, ProcVar p) {return i >= (float)p;};
inline bool operator <   (ProcVar p, float d)    {return (float)p < d;};
inline bool operator <   (ProcVar p, double d)   {return (float)p < d;};
inline bool operator <   (ProcVar p, int    i)   {return (float)p < i;};
inline bool operator <   (ProcVar p, long int i) {return (float)p < i;};
inline bool operator <   (ProcVar p, unsigned long int i) {return (float)p < i;};
inline bool operator <   (ProcVar p, ProcVar q)  {return (float)p < float(q);};
inline bool operator <   (unsigned long int i, ProcVar p) {return i < (float)p;};
inline bool operator <=  (ProcVar p, float d)    {return (float)p <= d;};
inline bool operator <=  (ProcVar p, double d)   {return (float)p <= d;};
inline bool operator <=  (ProcVar p, int    i)   {return (float)p <= i;};
inline bool operator <=  (ProcVar p, long int i) {return (float)p <= i;};
inline bool operator <=  (ProcVar p, unsigned long int i) {return (float)p <= i;};
inline bool operator <=  (ProcVar p, ProcVar q)  {return (float)p <= float(q);};
inline bool operator <=  (unsigned long int i, ProcVar p) {return i <= (float)p;};

inline float operator + (ProcVar p, float d)   {return (float)p+d;};
inline float operator + (ProcVar p, double d)  {return (float)p+d;};
inline float operator + (ProcVar p, int    d)  {return (float)p+d;};  
inline float operator + (ProcVar p, long int    d)  {return (float)p+d;};  
inline float operator + (ProcVar p, long unsigned int    d)  {return (float)p+d;};  
inline float operator + (float d, ProcVar p)   {return d + (float)p;};
inline float operator + (double d, ProcVar p)  {return d + (float)p;};
inline float operator + (int d, ProcVar p)  {return d + (float)p;};  
inline float operator + (long int d, ProcVar p)  {return d + (float)p;};  
inline float operator + (long unsigned int d, ProcVar p)  {return d + (float)p;};  

inline float operator + (ProcVar p, ProcVar q) {return (float)p+(float)q;};
inline float operator - (ProcVar p, float d)   {return (float)p-d;};
inline float operator - (ProcVar p, double d)  {return (float)p-d;};
inline float operator - (ProcVar p, int    d)  {return (float)p-d;};
inline float operator - (ProcVar p, long int d){return (float)p-d;};
inline float operator - (ProcVar p, long unsigned  int d)  {return (float)p-d;};
inline float operator - (float d , ProcVar p)  {return d-(float)p;};
inline float operator - (double d , ProcVar p) {return d-(float)p;};
inline float operator - (int d    , ProcVar p) {return d-(float)p;};
inline float operator - (ProcVar p, ProcVar q) {return (float)p-(float)q;};
inline float operator * (float  d, ProcVar p)  {return (float)p*d;};
inline float operator * (double d, ProcVar p)  {return (float)p*d;};
inline float operator * (int    d, ProcVar p)  {return (float)p*d;};
inline float operator * (long   d, ProcVar p)  {return (float)p*d;};
inline float operator * (ProcVar q, ProcVar p) {return (float)p*(float)q;};
inline float operator * (ProcVar p, float d)   {return (float)p*d;};
inline float operator * (ProcVar p, double d)  {return (float)p*d;};
inline float operator * (ProcVar p, int    d)  {return (float)p*d;};
inline float operator * (ProcVar p, unsigned long int    d)  {return (float)p*d;};
inline float operator * (ProcVar p, long   d)  {return (float)p*d;};
inline float operator / (ProcVar p, float d)   {return (float)p/d;};
inline float operator / (ProcVar p, double d)  {return (float)p/d;};
inline float operator / (ProcVar p, int    d)  {return (float)p/d;};
inline float operator / (ProcVar p, ProcVar q) {return (float)p/(float)q;};
inline float operator / (float  d, ProcVar p)  {return d/(float)p;};
inline float operator / (double d, ProcVar p)  {return d/(float)p;};
inline float operator / (int    d, ProcVar p)  {return d/(float)p;};



#endif
