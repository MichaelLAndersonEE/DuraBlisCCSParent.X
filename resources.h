/* File:   resources.h
 * Manages the matrix of needed control resoources versus parent / child owners
 * PNet ver A
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Created: 18 Sep14
 */

#ifndef RESOURCES_H
#define	RESOURCES_H

#ifdef	__cplusplus
extern "C" {
#endif

    // Resources are called in order with RSTS time between
#define RESOURCE_STEP_TIME_SECS     3
      
#define RES_LIST_UP     1        // Used in screen editing
#define RES_LIST_DOWN   2

    // Child status bits.  Upper bits are meta, for Parent only.  Lower byte is reported by child[].
#define CHILD_ST_NODE_DEFINED       0x8000
#define CHILD_ST_NODE_ACTIVE        0x4000      // Ie, it's actively communicating
#define CHILD_ST_HAVE_FLOODSEN      0x2000      //
#define CHILD_ST_HAVE_TEMP2         0x1000      // Second, remote temper sensor option, 'boiler temp'
#define CHILD_ST_EXTERNAL_TH        0x0800      // Measures outdoor conditions. We want only 1 to be the external TH measurer.
#define CHILD_ST_OVER_CURRENT       0x0400

    // These must match the byte status code passed in Pnet.
#define CHILD_ST_LIQUID_DETECTED    0x0080
#define CHILD_ST_TEMP1_OKAY         0x0040      // Ie, the sensor is operating within range
#define CHILD_ST_TEMP2_OKAY         0x0020
#define CHILD_ST_RHUMID_OKAY        0x0010
#define CHILD_ST_K1_ON              0x0008
#define CHILD_ST_K2_ON              0x0004
#define CHILD_ST_SWIPWR1_ON         0x0002
#define CHILD_ST_SWIPWR2_ON         0x0001


typedef enum Resource_Types
    { Free, AirConditioner, Heater, DeHumidifier, AirExchangerIn, AirExchangerOut, Humidifier, InternalFan } ResourceType;
    // Note.  resourceQuery() has strings that correspond to this enum.
typedef enum Climate_Types
    { InComfortZone, HotAndDamp, JustDamp, ColdAndDamp, JustCold, ColdAndDry, JustDry, HotAndDry, FreakinHot } ClimateType;
typedef enum RelativeClimate_Types
    { CoolerAndDrier, DrierAndTempCZ, WarmerAndDrier, WarmerAndHumCZ, WarmerAndDamper, DamperAndTempCZ, CoolerAndDamper, CoolerAndHumCZ, NullCase } RelativeClimateType;

struct childResourceStruct
{
    unsigned status;
    byte    miaCtr;
    ResourceType    relay1;         // eg, child[0].relay1 = DeHumidifier;
    ResourceType    relay2;
    ResourceType    swiPwr1;
    ResourceType    swiPwr2;
    float   localTempF;         // TODO use only doubles throughout. Not sure MPLAB has a difference.
    float   localRHumid;
    float   secondaryTempF;
} child[NUM_CHILDREN];

struct parentResourceStruct
{
    ResourceType    relay1;
    ResourceType    relay2;
} parent;

struct resEval_struct
{
    unsigned    dayTotalCounter;    // If resourceManager() called every 3 sec, will reach 28800. Resets at midnight.
    unsigned    tooHotTimer;        // Starts when temp high, stops on control convergence
    unsigned    tooColdTimer;       // What we want is the ratio of tHT / dTC.  TODO log these?  Average?
    unsigned    tooDampTimer;
    unsigned    tooDryTimer;
} resourceEvaluate;

void resourceManager(void);      // Resources
const char *resourceQuery(byte node, char outChan);
ResourceType listResource(ResourceType rety, byte opMode);
ClimateType whatIsInteriorClimateNow(void);
int externalTHavailable(void);

#ifdef	__cplusplus
}
#endif

#endif	/* RESOURCES_H */

