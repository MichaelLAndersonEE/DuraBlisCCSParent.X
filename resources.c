/* File:   resources.c
 * Serves as middleware between the available dynamic resources and communication
 * with nodes, with all of the intricacies thereof.  The Pnet messages originate here.
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main board
 * Created: 18 Sep14
 * Passim: Node 0 = parent, child nodes are 1 - 8. For child[idx], idx = node - 1.
 * Some resources require additional communication.  IntFan requires that we first ask the
 * local RH.  Air Exchangers require that we ask the outside T & H.
 * Exception handling:  message fcns return neg error codes. This is caught by task scheduler, which
 * sets / clrs FLT_COMMUNICATION.  Additionally, the messageAttention() fcn sets / clrs node Active.
 */

#include <xc.h>
#include <string.h>
#include <stdlib.h>
#include "DuraBlisCCSParent.h"
#include "resources.h"
#include "pnet.h"
#include "serial.h"
#include "time.h"

typedef enum Action_Types
    { SvcParentResources, MsgAttention, MsgRelay, MsgSwiPwr, MsgAskTemper, MsgAskRHumid, MsgAskTemper2 } ActionType;
typedef enum Output_Types
    { Relay1, Relay2, SwiPwr1, SwiPwr2  } OutputType;

static int ascii2Hex(char ch);
static bool childHasDeHumidifier(byte nod);
static bool childHasAirExch(byte nod);
static bool childHasIntFan(byte nod);
static void indexNextResource(void);
static int messageAskRHumid(void);
static int messageAskTemper(void);
static int messageAskSecTemper(void);
static int messageAttention(void);
static int messageRelay(void);
static int messageSwiPwr(void);
static byte nextNodeNum(byte nodeStart);
static int serviceParentResources(void);

bool temperInComfortZone(double temper);
bool rhumidInComfortZone(double rhumid);

    // These guys have file scope. They are prepared by indexNextResource(), et al, then used by messageXX()
ActionType actionType;      
ResourceType resourceType;  
OutputType outputType;

extern double temperNowF, rhumidNow;
extern double temperNowExteriorF, rhumidNowExterior;
extern struct operatingParms_struct oP;
extern unsigned sysFault;

static byte nodeNum;                // Valid 0 - 8.  The node presently being communicated with.

    // Returns exterior climate position with respect to interior & CZ.  Assumes both adcTH() updated and exterior T&H avail.
RelativeClimateType whatIsExteriorClimateNow(void)
{
    if ( (temperNowExteriorF < temperNowF) && (rhumidNowExterior < rhumidNow) ) return(CoolerAndDrier);
    if ( (rhumidNowExterior < rhumidNow) && (temperNowExteriorF > (oP.setPointTempF - oP.toleranceCZtemperF))
        && (temperNowExteriorF < (oP.setPointTempF + oP.toleranceCZtemperF)) ) return(DrierAndTempCZ);
    if ( (temperNowExteriorF > temperNowF) && (rhumidNowExterior < rhumidNow) ) return(WarmerAndDrier);
    if ( (temperNowExteriorF > temperNowF) && (rhumidNowExterior > (oP.setPointRelHum - oP.toleranceCZrhumid))
        && (rhumidNowExterior < (oP.setPointRelHum + oP.toleranceCZrhumid)) ) return(WarmerAndHumCZ);
    if ( (temperNowExteriorF > temperNowF) && (rhumidNowExterior < rhumidNow) ) return(WarmerAndDrier);
    if ( (temperNowExteriorF > temperNowF) && (rhumidNowExterior > rhumidNow) ) return(WarmerAndDamper);
    if ( (rhumidNowExterior > rhumidNow) && (temperNowExteriorF > (oP.setPointTempF - oP.toleranceCZtemperF))
        && (temperNowExteriorF < (oP.setPointTempF + oP.toleranceCZtemperF)) ) return(DamperAndTempCZ);
    if ( (temperNowExteriorF < temperNowF) && (rhumidNowExterior > rhumidNow) ) return(CoolerAndDamper);
    if ( (temperNowExteriorF < temperNowF) && (rhumidNowExterior > (oP.setPointRelHum - oP.toleranceCZrhumid))
        && (rhumidNowExterior < (oP.setPointRelHum + oP.toleranceCZrhumid)) ) return(CoolerAndHumCZ);
    return(NullCase);       // Indeterminate
}

    // Returns interior climate position with respect to CZ.  Assumes adcTH() has been recently called.
ClimateType whatIsInteriorClimateNow(void)
{   
    if (temperNowF < (oP.setPointTempF - oP.toleranceCZtemperF))
    {
        if (rhumidNow < (oP.setPointRelHum - oP.toleranceCZrhumid)) return(ColdAndDry);
        else if (rhumidNow > (oP.setPointRelHum + oP.toleranceCZrhumid)) return(ColdAndDamp);
        else return(JustCold);
    }
    else if (temperNowF > (oP.setPointTempF + oP.toleranceCZtemperF))
    {
        if (rhumidNow < (oP.setPointRelHum - oP.toleranceCZrhumid)) return(HotAndDry);
        else if (rhumidNow > (oP.setPointRelHum + oP.toleranceCZrhumid)) return(HotAndDamp);
        else return(FreakinHot);
    }
    else 
    {
        if (rhumidNow < (oP.setPointRelHum - oP.toleranceCZrhumid)) return(JustDry);
        else if (rhumidNow > (oP.setPointRelHum + oP.toleranceCZrhumid)) return(JustDamp);
        else return (InComfortZone);
    }
}

bool temperInComfortZone(double temper)
{
    if ( (temper > (oP.setPointTempF - oP.toleranceCZtemperF)) &&
         (temper < (oP.setPointTempF + oP.toleranceCZtemperF)) ) return(true);
    else return(false);
}

bool rhumidInComfortZone(double rhumid)
{
    if ( (rhumid > (oP.setPointRelHum - oP.toleranceCZrhumid)) &&
         (rhumid < (oP.setPointRelHum + oP.toleranceCZrhumid)) ) return(true);
    else return(false);
}

    // Called periodically by taskSchedular to work available child resouces.  Parent is nodeNum 0
    // Sets & clears sysFault FLT_COMMUNICATION bit.
void resourceManager(void)
{    
    byte ch;

    if (oP.sysStat & ST_SECURITY_LAPSED) return;        
   
        // We send one message per cycle. Parent is in same process but does not need messaging per se.  Every
        // child gets an ATn to start.  Some resources require dialog queries / msgs thereafter.
    indexNextResource();

    switch(actionType)
    {
        case SvcParentResources:
            serviceParentResources();
            break;

        case MsgAttention:
            if (messageAttention() < 0)
                if (child[nodeNum - 1].miaCtr < CHILD_MIA_MAX) child[nodeNum - 1].miaCtr++;
                else child[nodeNum - 1].miaCtr = 0;
            break;

        case MsgRelay:                      // messageRelay() and others will only go out to ACTIVE nodes, as determined by iNR()
            messageRelay();
            break;

        case MsgSwiPwr:
            messageSwiPwr();
            break;

        case MsgAskTemper:
            messageAskTemper();
            break;

        case MsgAskTemper2:
            messageAskSecTemper();
            break;

        case MsgAskRHumid:
            messageAskRHumid();
            break;
    }

    sysFault &= ~FLT_LOSTCHILD;
    for (ch = 0; ch < NUM_CHILDREN; ch++)
    {
       // if ((child[ch].status & CHILD_ST_NODE_DEFINED) && (!(child[ch].status & CHILD_ST_NODE_ACTIVE))) sysFault |= FLT_COMMUNICATION;
            if (child[ch].miaCtr >= CHILD_MIA_MAX) sysFault |= FLT_LOSTCHILD;
    }
}

    // ----------------------------------------
    // This 'list-procesing' may be a little excessive, but probably safer and more adjustable than simple int arithmetic.
ResourceType listResource(ResourceType rety, byte opMode)
{
    switch(rety)
    {
        case Free:              if (opMode == RES_LIST_DOWN) return(AirConditioner); else return(InternalFan);
        case AirConditioner:    if (opMode == RES_LIST_DOWN) return(Heater); else return(Free);
        case Heater:            if (opMode == RES_LIST_DOWN) return(DeHumidifier); else return(AirConditioner);
        case DeHumidifier:      if (opMode == RES_LIST_DOWN) return(AirExchangerIn); else return(Heater);
        case AirExchangerIn:    if (opMode == RES_LIST_DOWN) return(AirExchangerOut); else return(DeHumidifier);
        case AirExchangerOut:   if (opMode == RES_LIST_DOWN) return(Humidifier); else return(AirExchangerIn);
        case Humidifier:        if (opMode == RES_LIST_DOWN) return(InternalFan); else return(AirExchangerOut);
        case InternalFan:       if (opMode == RES_LIST_DOWN) return(Free); else return(Humidifier);
    }
}

    // Write formatted str with resources associated with nodeNumber. 
    // outChan A = relay1, B = relay2, C = swipwr1, D = swipwr2
    // Limit to 10 chars.
const char *resourceQuery(byte nodeNumber, char outChan)
{
    const char *rsrcStr[8] = {       // This has to match the enum.
        "Free", "AirCond", "Heater", "Dehumid", "AirExchIn",
        "AirExchOut", "Humidif", "IntFan" };

    if (nodeNumber == 0)
    {
        if (outChan == 'A') return(rsrcStr[(int)parent.relay1]);
        else return(rsrcStr[(int)parent.relay2]);
    }
    else
    {
        if (outChan == 'A') return(rsrcStr[(int)child[nodeNumber - 1].relay1]);
        else if (outChan == 'B') return(rsrcStr[(int)child[nodeNumber - 1].relay2]);
        else if (outChan == 'C') return(rsrcStr[(int)child[nodeNumber - 1].swiPwr1]);
        else if (outChan == 'D') return(rsrcStr[(int)child[nodeNumber - 1].swiPwr2]);
    }

    return("Error");
}

    // There can only be one outside temperature. Or we would need a way to arbitrate.
    // So here we scan through the active children and return numNode of first
    // that qualifies, or 0 OW.
int externalTHavailable(void)
{ 
    unsigned ch;
    for (ch = 0; ch < NUM_CHILDREN; ch++)
    {
        if ((child[ch].status & CHILD_ST_EXTERNAL_TH) && (child[ch].status & CHILD_ST_NODE_ACTIVE))
        {
            temperNowExteriorF = child[ch].localTempF;
            rhumidNowExterior = child[ch].localRHumid;  // TODO best place?
            return(ch + 1);
        }
    }
    return(0);
}

    /* ----------- Private functions -------------- */

int ascii2Hex(char ch)
{
    if (ch >= '0' && ch <'9') return(ch - '0');
    if (ch >= 'a' && ch <'f') return(ch - 'a' + 10);
    if (ch >= 'A' && ch <'F') return(ch - 'A' + 10);
    return(-1);
}

bool childHasDeHumidifier(byte nod)
{
    if (child[nod - 1].relay1 == DeHumidifier) return(true);
    if (child[nod - 1].relay2 == DeHumidifier) return(true);
    return(false);
}

    // For now AirExch only on relays.
bool childHasAirExch(byte nod)
{
    if (child[nod - 1].relay1 == AirExchangerIn) return(true);
    if (child[nod - 1].relay1 == AirExchangerOut) return(true);
    if (child[nod - 1].relay2 == AirExchangerIn) return(true);
    if (child[nod - 1].relay2 == AirExchangerOut) return(true);
    return(false);
}

    // For now IntFans are only on SwiPwr
bool childHasIntFan(byte nod)
{
    if (child[nod - 1].swiPwr1 == InternalFan) return(true);
    if (child[nod - 1].swiPwr2 == InternalFan) return(true);
    return(false);
}

    // TODO I had to quickly specialize this on Demo setup
void indexNextResource(void)
{
    static byte seq;
    const byte numCases = 19;    // Ordinal  16
    
    switch(seq)
    {
        case 0:
            actionType = SvcParentResources;
            nodeNum = 0;
            break; 
            
        case 1:
            if (!(child[0].status & CHILD_ST_NODE_DEFINED)) { seq = 4; return; }       // Skip over undefined 1.
            else 
            {
                actionType = MsgAttention;
                nodeNum = 1;
            }
            break;
            
        case 2:
            if (!(child[0].status & CHILD_ST_NODE_ACTIVE)) { seq = 4; return; }
            else actionType = MsgAskRHumid;
            break;
            
        case 3:
            if (!(child[0].status & CHILD_ST_NODE_ACTIVE)) { seq = 4; return; }
            actionType = MsgSwiPwr;
            outputType = SwiPwr1;
            resourceType = InternalFan;            
            break;

        case 4:
            if (!(child[1].status & CHILD_ST_NODE_DEFINED)) { seq = 7; return; }        // Skip over undefined 2
            else
            {
                actionType = MsgAttention;
                nodeNum = 2;
            }
            break;

        case 5:
            if (!(child[1].status & CHILD_ST_NODE_ACTIVE)) { seq = 7; return; }
            else actionType = MsgAskRHumid;
            break;

        case 6:
            if (!(child[1].status & CHILD_ST_NODE_ACTIVE)) { seq = 7; return; }
            actionType = MsgSwiPwr;
            outputType = SwiPwr1;
            resourceType = InternalFan;
            break;

        case 7:
            if (!(child[2].status & CHILD_ST_NODE_DEFINED)) { seq = 8; return; }        // Skip over undefined 2
            else
            {
                actionType = MsgAttention;
                nodeNum = 3;
            }
            break;

        case 8:
            if (!(child[3].status & CHILD_ST_NODE_DEFINED)) { seq = 10; return; }        // Skip over undefined 2
            else
            {
                actionType = MsgAttention;
                nodeNum = 4;
            }
            break;

        case 9:
            if (!(child[3].status & CHILD_ST_NODE_ACTIVE)) { seq = 10; return; }
            actionType = MsgRelay;
            outputType = Relay1;
            resourceType = DeHumidifier;
            break;

        case 10:
            if (!(child[4].status & CHILD_ST_NODE_DEFINED)) { seq = 14; return; }        // Skip over undefined 2
            else
            {
                actionType = MsgAttention;
                nodeNum = 5;
            }
            break;

        case 11:
            if (!(child[4].status & CHILD_ST_NODE_ACTIVE)) { seq = 15; return; }
            actionType = MsgAskTemper;
            break;

        case 12:
           if (!(child[4].status & CHILD_ST_NODE_ACTIVE)) { seq = 15; return; }
            actionType = MsgAskRHumid;
            break;

        case 13:
            if (!(child[4].status & CHILD_ST_NODE_ACTIVE)) { seq = 15; return; }
            actionType = MsgRelay;
            outputType = Relay1;
            resourceType = AirExchangerOut;
            break;

        case 14:
            if (!(child[4].status & CHILD_ST_NODE_ACTIVE)) { seq = 15; return; }
            actionType = MsgRelay;
            outputType = Relay2;
            resourceType = AirExchangerOut;
            break;

        case 15:
            if (!(child[5].status & CHILD_ST_NODE_DEFINED)) { seq = 18; return; }        // Skip over undefined 2
            else
            {
                actionType = MsgAttention;
                nodeNum = 6;
            }
            break;

        case 16:
            if (!(child[5].status & CHILD_ST_NODE_ACTIVE)) { seq = 18; return; }
            else actionType = MsgAskRHumid;
            break;

        case 17:
            if (!(child[5].status & CHILD_ST_NODE_ACTIVE)) { seq = 18; return; }
            actionType = MsgSwiPwr;
            outputType = SwiPwr1;
            resourceType = InternalFan;
            break;

        case 18:
            if (!(child[6].status & CHILD_ST_NODE_DEFINED)) { seq = 0; return; }        // Skip over undefined 2
            else
            {
                actionType = MsgAttention;
                nodeNum = 7;
            }
            break;

        case 19:
            if (!(child[6].status & CHILD_ST_NODE_ACTIVE)) { seq = 0; return; }
        //    else actionType = MsgAskRHumid;
            else actionType = MsgAskTemper2;
            break;

//        case 20:
//            if (!(child[6].status & CHILD_ST_NODE_ACTIVE)) { seq = 0; return; }
//            actionType = MsgSwiPwr;
//            outputType = SwiPwr1;
//            resourceType = InternalFan;
//            break;
   
        default:
            seq = 0;
            break;
    }
    
    if (seq < numCases) seq++;
    else seq = 0;
    
   }

    // Decide next actionType. This is the manager for active nodeNum.  Parent  = node 0.
    // Then scan through the child[] for the next resource item we should handle.
    // The resources may change dynamically, and children may leave network.  There are
    // presently 4 basic child resource outputs: 2 rlys & 2 SwiPwrs.
    // Also set outputType as a funciton of resourceType as appropriate.
//void indexNextResource(void)
//{
//    static byte outputCtr;                          // Cycles through 4 possible outputs
//
//
//    if (actionType == SvcParentResources)       // From last time
//    {                                           // First up, say hello, get status
//        nodeNum = nextNodeNum(nodeNum);
//        if (nodeNum) actionType = MsgAttention;      // If node does not respond, it is not ACTIVE
//        return;
//    }
//                        // Hello didn't work, advance to next
//    if (!(child[nodeNum - 1].status & CHILD_ST_NODE_ACTIVE))
//    {
//        nodeNum = nextNodeNum(nodeNum);
//        if (nodeNum == 0) actionType = SvcParentResources;
//        else actionType = MsgAttention;
//        return;
//    }
//
//    if ((childHasIntFan(nodeNum)) && (actionType != MsgAskRHumid))
//    {
//        actionType = MsgAskRHumid;      // Local RH, not outside
//        return;
//    }
//
//    if ((childHasAirExch(nodeNum)) && (actionType != MsgAskTemper))
//    {
//        actionType = MsgAskTemper;
//        return;
//    }
//
//    if ((childHasAirExch(nodeNum)) && (actionType != MsgAskRHumid))
//    {
//        actionType = MsgAskRHumid;
//        return;
//    }
//
////    if ((childHasDeHumidifier(nodeNum)) && (actionType != MsgAskRHumid))
////    {
////        actionType = MsgAskRHumid;
////        return;
////    }
//
//    if ((child[nodeNum - 1].status & CHILD_ST_HAVE_TEMP2) && (actionType != MsgAskTemper2))
//    {
//        actionType = MsgAskTemper2;
//        return;
//    }
//
//
//
//    oP.sysStat &= ~ST_AIR_EXCHGR_ON;
//
//    if (outputCtr == 0)
//    {
//        outputCtr++;
//        if (child[nodeNum - 1].relay1 != Free)
//        {
//            actionType = MsgRelay;
//            outputType = Relay1;
//            resourceType = child[nodeNum - 1].relay1;
//            if ((resourceType == AirExchangerIn) || (resourceType == AirExchangerOut))  oP.sysStat |= ST_AIR_EXCHGR_ON;     // TODO ??
//            return;
//        }
//    }
//    if (outputCtr == 1)
//    {
//        outputCtr++;
//        if (child[nodeNum - 1].relay2 != Free)
//        {
//            actionType = MsgRelay;
//            outputType = Relay2;
//            resourceType = child[nodeNum - 1].relay2;
//            if ((resourceType == AirExchangerIn) || (resourceType == AirExchangerOut))  oP.sysStat |= ST_AIR_EXCHGR_ON;
//            return;
//        }
//    }
//    if (outputCtr == 2)
//    {
//        outputCtr++;
//        if (child[nodeNum - 1].swiPwr1 != Free)
//        {
//            actionType = MsgSwiPwr;
//            outputType = SwiPwr1;
//            resourceType = child[nodeNum - 1].swiPwr1;
//            if ((resourceType == AirExchangerIn) || (resourceType == AirExchangerOut))  oP.sysStat |= ST_AIR_EXCHGR_ON;
//            return;
//        }
//    }
//    if (outputCtr == 3)
//    {
//        outputCtr++;
//        if (child[nodeNum - 1].swiPwr2 != Free)
//        {
//            actionType = MsgSwiPwr;
//            outputType = SwiPwr2;
//            resourceType = child[nodeNum - 1].swiPwr2;
//            if ((resourceType == AirExchangerIn) || (resourceType == AirExchangerOut))  oP.sysStat |= ST_AIR_EXCHGR_ON;
//            return;
//        }
//    }
//    if (outputCtr == 4)
//    {
//        outputCtr = 0;
//        nodeNum = nextNodeNum(nodeNum);
//        if (nodeNum == 0) actionType = SvcParentResources;
//        else actionType = MsgAttention;
//        return;
//    }
//}

int messageAskRHumid(void)
{
    char bfr[8];
    float fVal;

    sprintf(ioBfr, "H?%c\r", nodeNum + '0');    // Must end in CR only
    putStr(ioBfr);
    rs485TransmitEna(0);
    if (getStr(ioBfr, TIMEOUT_PNET) < 1) return(-1);    // No one there; end of story.
    if(ioBfr[0] != 'h') return(-2);
    if(ioBfr[1] != 'k') return(-3);
    if(ioBfr[2] != nodeNum + '0') return(-4);
    strncpy(bfr, &ioBfr[3], 7);
    bfr[7] = 0;
    fVal = atof(bfr);
    if ((fVal >= REASONABLE_RH_MIN) && (fVal <= REASONABLE_RH_MAX))
    {
        child[nodeNum - 1].localRHumid = fVal;
        if (child[nodeNum - 1].status & CHILD_ST_EXTERNAL_TH) rhumidNowExterior = fVal;  // This assumes there is only one.
    }
    else return(-5);
    return(nodeNum);
}

int messageAskTemper(void)
{
    char bfr[8];
    float fVal;

    sprintf(ioBfr, "T?%c\r", nodeNum + '0');    // Must end in CR only
    putStr(ioBfr);
    rs485TransmitEna(0);
    if (getStr(ioBfr, TIMEOUT_PNET) < 1) return(-1);    // No one there; end of story.
    if(ioBfr[0] != 't') return(-2);
    if(ioBfr[1] != 'k') return(-3);
    if(ioBfr[2] != nodeNum + '0') return(-4);
    strncpy(bfr, &ioBfr[3], 7);
    bfr[7] = 0;
    fVal = atof(bfr);
    if ((fVal >= REASONABLE_TEMP_MIN) && (fVal <= REASONABLE_TEMP_MAX))
    {
        child[nodeNum - 1].localTempF = fVal;
        if (child[nodeNum - 1].status & CHILD_ST_EXTERNAL_TH) temperNowExteriorF = fVal;  // This assumes there is only one.
    }
    else return(-5);
    return(nodeNum);
}

    // ---------------------------
int messageAskSecTemper(void)
{
    char bfr[8];
    float fVal;
 
    sprintf(ioBfr, "S?%c\r", nodeNum + '0');    // Must end in CR only
    putStr(ioBfr);
    rs485TransmitEna(0);
    if (getStr(ioBfr, TIMEOUT_PNET) < 1) return(-1);    // No one there; end of story.
    if(ioBfr[0] != 's') return(-2);
    if(ioBfr[1] != 'k') return(-3);
    if(ioBfr[2] != nodeNum + '0') return(-4);
    strncpy(bfr, &ioBfr[3], 7);
    bfr[7] = 0;
    fVal = atof(bfr);
    if ((fVal >= REASONABLE_TEMP_MIN) && (fVal <= REASONABLE_TEMP_MAX))
    {
        child[nodeNum - 1].secondaryTempF += 0.0327 * (fVal - child[nodeNum - 1].secondaryTempF);
        sysFault &= ~FLT_SEC_TEMP_LOW;
        if (child[nodeNum - 1].secondaryTempF < CRITICAL_LOW_SEC_TEMP) sysFault |= FLT_SEC_TEMP_LOW;
    }
    else return(-5);
    return(nodeNum);
}

    // -------------------------------------------
    // Set/clr child[].status CHILD_NODE_ACTIVE
    // Returns nodeNum on success, 0 > code o.w.
    // Any child may set a FLT boit, but no one may clr.
int messageAttention(void)
{
    extern char pnetVer;   
    int bfr, bfr2;

    // sysFault |= FLT_LOSTCHILD;
    sprintf(ioBfr, "AT%c\r", nodeNum + '0');    // Must end in CR only
    putStr(ioBfr);
        // if (oP.sysStat & ST_DEBUG_MODE) putChar('\n');     // TODO use passim?

        // Child input parser will eat this whitespace.
    child[nodeNum - 1].status &= ~CHILD_ST_NODE_ACTIVE;
    rs485TransmitEna(0);
    if (getStr(ioBfr, TIMEOUT_PNET) < 1) return(-1);    // No one there; end of story.
    if(ioBfr[0] != 'a') return(-2);
    if(ioBfr[1] != 'k') return(-3);
    if(ioBfr[2] != nodeNum + '0') return(-4);
    if(ioBfr[3] != pnetVer) return(-5);
    if ((bfr = ascii2Hex(ioBfr[4])) >= 0) bfr2 = bfr << 4;
    if ((bfr = ascii2Hex(ioBfr[5])) >= 0) bfr2 += bfr;
    child[nodeNum - 1].status &= 0xFF00;
    child[nodeNum - 1].status += bfr2;
    if (child[nodeNum - 1].status & CHILD_ST_LIQUID_DETECTED) sysFault |= FLT_FLOODDETECTED;        // User must clear
//    if (! (child[nodeNum - 1].status & CHILD_ST_TEMP1_OKAY) && (child[nodeNum - 1].status & CHILD_ST_RHUMID_OKAY) )   TODO
//        sysFault |= FLT_CHILDFAULT;         // This autoclears at midnight, or manually, but still weak. TODO
    child[nodeNum - 1].status |= CHILD_ST_NODE_ACTIVE;

    return(nodeNum);
}

    // Relays presently used only for AirCond, Heater, Dehum, Humid.
    // indexNextResource() has determined: nodeNum, outputType, resourceType
    // resourceManager() has determined: climeNow
    // But we make the climate call here.  Pass back any neg exception codes.
int messageRelay(void)    
{
    RelativeClimateType relClime;                // whatIsExteriorClimateNow(void)
    ClimateType climeNow;
    char relayChar = 0, polarityChar = 0;
    bool exchangeNeeded;

    if (outputType == Relay1) relayChar = 'a';          // Pnet syntax
    else if (outputType == Relay2) relayChar = 'b';
    else return(-10);                                   // System error OW

    climeNow = whatIsInteriorClimateNow();   
    relClime = whatIsExteriorClimateNow();
    // InComfortZone, HotAndDamp, JustDamp, ColdAndDamp, JustCold, ColdAndDry, JustDry, HotAndDry, FreakinHot } ClimateType;
    //   { NullCase, CoolerAndDrier, DrierAndTempCZ, WarmerAndDrier, WarmerAndHumCZ, WarmerAndDamper, DamperAndTempCZ, CoolerAndDamper, CoolerAndHumCZ
    switch(resourceType)
    {
        case AirExchangerIn:
        case AirExchangerOut:               // There is no real software distinction at this point
            exchangeNeeded = false;         // Initialize
            switch(climeNow)
            {
                case InComfortZone:         // Do nothing
                    break;
                    
                case ColdAndDamp:
                    if (relClime == WarmerAndDrier) exchangeNeeded = true;  
                    break;
                            
                case JustCold:
                    if (relClime == WarmerAndHumCZ) exchangeNeeded = true;
                    break;
                    
                case ColdAndDry:
                    if (relClime == WarmerAndDamper) exchangeNeeded = true;  
                    break;
                    
                case JustDry:
                    if (relClime == DamperAndTempCZ) exchangeNeeded = true;  
                    break;
                    
                case HotAndDry:
                    if (relClime == CoolerAndDamper) exchangeNeeded = true;                      
                    break;
                    
                case FreakinHot:
                    if (relClime == CoolerAndHumCZ) exchangeNeeded = true;                      
                    break;
                    
                case HotAndDamp:
                    if (relClime == CoolerAndDrier) exchangeNeeded = true;
                    break;
                    
                case JustDamp:
                    if (relClime == DrierAndTempCZ) exchangeNeeded = true;
                    break;
            }
            if (exchangeNeeded)
            {
                oP.sysStat |= ST_AIR_EXCHGR_ON;
                polarityChar = '+';
            }
            else
            {
                oP.sysStat &= ~ST_AIR_EXCHGR_ON;
                polarityChar = '-';
            }

            break;

        case AirConditioner:
            if ( (climeNow == HotAndDamp) || (climeNow == JustDamp) || (climeNow == FreakinHot))
                polarityChar = '+';
            else polarityChar = '-';
            if (oP.sysStat & ST_AIR_EXCHGR_ON) polarityChar = '-';      // Override
            break;

        case DeHumidifier:
            if (climeNow == JustDamp)
                polarityChar = '+';
            else polarityChar = '-';
            if (oP.sysStat & ST_AIR_EXCHGR_ON) polarityChar = '-';      // Override
            break;

        case Heater:
            if ( (climeNow == ColdAndDamp) || (climeNow == JustCold) || (climeNow == ColdAndDry) )
                polarityChar = '+';
            else polarityChar = '-';
            if (oP.sysStat & ST_AIR_EXCHGR_ON) polarityChar = '-';      // Override
            break;

        case Humidifier:
            if ( (climeNow == ColdAndDry) || (climeNow == JustDry) || (climeNow == HotAndDry) )
                polarityChar = '+';
            else polarityChar = '-';
            if (oP.sysStat & ST_AIR_EXCHGR_ON) polarityChar = '-';      // Override
            break;

        default:
            return(-11);        // OW system error
    }

    sprintf(ioBfr, "K%c%c%c\r", nodeNum + '0', relayChar, polarityChar);
 
    putStr(ioBfr);                           
    rs485TransmitEna(0); 
    if (getStr(ioBfr, TIMEOUT_PNET) < 1) return(-1);        // No one there; end of story.
    if (ioBfr[0] != 'k') return(-2);                       // Protocol fail
    if(ioBfr[1] != 'k') return(-2);
    if(ioBfr[2] != nodeNum + '0') return(-2);
    if(ioBfr[3] != relayChar) return(-2); 
    if(ioBfr[4] != polarityChar) return(-2);
    return(nodeNum);
}

    // SwiPwrs presently used only for Internal fans
    // indexNextResource() has determined: nodeNum, outputType, resourceType
    // We must have already determined the associated local Rhumid.
int messageSwiPwr(void)
{
    char swipwrChar = 0, polarityChar = 0;

    if (outputType == SwiPwr1) swipwrChar = 'a';          // Pnet syntax
    else if (outputType == SwiPwr2) swipwrChar = 'b';
    else return(-10);                                   // System error OW

    if (child[nodeNum - 1].localRHumid > LOCAL_RH_MAX) polarityChar = '+';
    else polarityChar = '-';

    sprintf(ioBfr, "P%c%c%c\r", nodeNum + '0', swipwrChar, polarityChar);

    putStr(ioBfr);
    rs485TransmitEna(0); 
    if (getStr(ioBfr, TIMEOUT_PNET) < 1) return(-1);                        // No one there; end of story.
    if (ioBfr[0] != 'p') return(-2);                                        // Protocol fail
    if(ioBfr[1] != 'k') return(-2); 
    if(ioBfr[2] != nodeNum + '0') return(-2);
    if(ioBfr[3] != swipwrChar) return(-2);
    if(ioBfr[4] != polarityChar) return(-2);
    return(nodeNum);
}

    // Nodes range form 0 (parent) - 8.  What is the next valid node?
byte nextNodeNum(byte nodeStart)
{
    byte nod;

    if (nodeStart == NUM_CHILDREN) return(0);   // Return to parent.
    //if (nodeStart == 0) nodeStart = 1;
    for (nod = nodeStart + 1; nod <= NUM_CHILDREN; nod++)
    {
        if (child[nod - 1].status & CHILD_ST_NODE_DEFINED) return(nod);
    }
    return(0);      // Ie, we didn't find a nonzero node
}

    // -----------------------------------------
    // Also updates our discomfort timers. Doesn't care about nodeNum, outputType.
    // Right now, AC (relay 1) and Heater (2) are fixed mapped, per MIDI.
    // Switched are dry contact, low power relays, like a wall thermostat.
int serviceParentResources(void)
{ 
    float f;
    ClimateType climeNow;

    climeNow = whatIsInteriorClimateNow();
    resourceEvaluate.dayTotalCounter++;         // These are all reset at midnight

    switch(climeNow)
    {
        case HotAndDamp:
            resourceEvaluate.tooHotTimer++;
            resourceEvaluate.tooDampTimer++;
            if (oP.sysStat & ST_AIR_EXCHGR_ON) PARENT_K_AirCon = 0;      // Air exchanger trumps AC, heater
            else PARENT_K_AirCon = 1;
            PARENT_K_Heater = 0;
            break;

        case JustDamp:
            resourceEvaluate.tooDampTimer++;            // DH handles this one
//            if (oP.sysStat & ST_AIR_EXCHGR_ON) PARENT_K_AirCon = 0;      // Air exchanger trumps AC, heater
//            else PARENT_K_AirCon = 1;
            PARENT_K_AirCon = 0;
            PARENT_K_Heater = 0;
            break;

        case ColdAndDamp:
            resourceEvaluate.tooDampTimer++;
            resourceEvaluate.tooColdTimer++;
            if (oP.sysStat & ST_AIR_EXCHGR_ON) PARENT_K_Heater = 0;      // Air exchanger trumps AC, heater
            else PARENT_K_Heater = 1;
            PARENT_K_AirCon = 0;
            break;

        case JustCold:
            resourceEvaluate.tooColdTimer++;
            if (oP.sysStat & ST_AIR_EXCHGR_ON) PARENT_K_Heater = 0;      // Air exchanger trumps AC, heater
            else PARENT_K_Heater = 1;
            PARENT_K_AirCon = 0;
            break;

        case ColdAndDry:
            resourceEvaluate.tooDryTimer++;
            resourceEvaluate.tooColdTimer++;
            if (oP.sysStat & ST_AIR_EXCHGR_ON) PARENT_K_Heater = 0;      // Air exchanger trumps AC, heater
            else PARENT_K_Heater = 1;
            PARENT_K_AirCon = 0;
            break;

        case JustDry:
            resourceEvaluate.tooDryTimer++;   
            PARENT_K_Heater = 0;        // Assumes only Heater / AC here.  Neither should be on.
            PARENT_K_AirCon = 0;
            break;

         case HotAndDry:
            resourceEvaluate.tooDryTimer++;
            resourceEvaluate.tooHotTimer++;
            if (oP.sysStat & ST_AIR_EXCHGR_ON) PARENT_K_AirCon = 0;      // Air exchanger trumps AC, heater
            else PARENT_K_AirCon = 1;
            PARENT_K_Heater = 0;
            break;

        case FreakinHot:
            resourceEvaluate.tooHotTimer++;
            if (oP.sysStat & ST_AIR_EXCHGR_ON) PARENT_K_AirCon = 0;      // Air exchanger trumps AC, heater
            else PARENT_K_AirCon = 1;
            PARENT_K_Heater = 0;
            break;
    }

        // Nominal 1 hour if rM() called at 3 sec. Recall this ctr clears at midnight.
    if (resourceEvaluate.dayTotalCounter > 1200)    
    {
        sysFault &= ~FLT_CANNOTCONTROLTEMP;
        f = (double) resourceEvaluate.tooColdTimer / resourceEvaluate.dayTotalCounter;
        if (f > RESOURCE_EFFECTIVENESS_CRITERION)  sysFault |= FLT_CANNOTCONTROLTEMP;

        f = (double) resourceEvaluate.tooHotTimer / resourceEvaluate.dayTotalCounter;
        if (f > RESOURCE_EFFECTIVENESS_CRITERION)  sysFault |= FLT_CANNOTCONTROLTEMP;
        
        sysFault &= ~FLT_CANNOTCONTROLHUM;
        f = (double) resourceEvaluate.tooDampTimer / resourceEvaluate.dayTotalCounter;
        if (f > RESOURCE_EFFECTIVENESS_CRITERION)  sysFault |= FLT_CANNOTCONTROLHUM;

        f = (double) resourceEvaluate.tooDryTimer / resourceEvaluate.dayTotalCounter;
        if (f > RESOURCE_EFFECTIVENESS_CRITERION)  sysFault |= FLT_CANNOTCONTROLHUM;
    }
    else sysFault &= ~(FLT_CANNOTCONTROLTEMP + FLT_CANNOTCONTROLHUM);

    return(1);      // All good
}


