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

#define TOO_HOT     0x01
#define TOO_COLD    0x02
#define TOO_DAMP    0x04
#define TOO_DRY     0x08

static int ascii2Hex(char ch);
static bool childHasAirExch(byte nod);
static bool childHasIntFan(byte nod);
static void indexNextResource(void);
static int messageAskRHumid(void);
static int messageAskTemper(void);
static int messageAttention(void);
static int messageRelay(void);
static int messageSwiPwr(void);
static byte nextNodeNum(byte nodeStart);
static int serviceParentResources(void);

typedef enum Action_Types
    { SvcParentResources, MsgAttention, MsgRelay, MsgSwiPwr, MsgAskTemper, MsgAskRHumid } ActionType;
typedef enum Output_Types
    { Relay1, Relay2, SwiPwr1, SwiPwr2  } OutputType;
          
ActionType actionType;      // These guys have file scope. They are prepared by indexNextResource() et al,
ResourceType resourceType;  // then used by messageXX()
OutputType outputType;

static byte climateStatus;     // TOO_...
static byte nodeNum;    // Valid 0 - 8.  The node presently being communicated with.
  
    // Called periodically by taskSchedular to work available child resouces.  Parent is nodeNum 0
    // Sets & clears sysFault FLT_COMMUNICATION bit.
void resourceManager(void)
{
    extern double temperNowF, rhumidNow;
    extern struct operatingParms_struct oP;
    extern unsigned sysFault;
    byte ch;
//    static byte faultCtr;
//    int retVal;
   
    climateStatus = 0;          // TODO we may want a deadband here too, to avoid dithering
    if (temperNowF > ( (1.0 + oP.toleranceComfortZone) * oP.setPointTempF)) climateStatus |= TOO_HOT;
    else if (temperNowF < ( (1.0 - oP.toleranceComfortZone) * oP.setPointTempF)) climateStatus |= TOO_COLD;
    if (rhumidNow > ( (1.0 + oP.toleranceComfortZone) * oP.setPointRelHum)) climateStatus |= TOO_DAMP;
    else if (rhumidNow < ( (1.0 - oP.toleranceComfortZone) * oP.setPointRelHum)) climateStatus |= TOO_DRY;

        // We send one message per cycle. Parent is in same process but does not need messaging.  Every
        // child gets an ATn to start.  Some resources require dialog queries / msgs.
    indexNextResource();

    switch(actionType)
    {
        case SvcParentResources:
            serviceParentResources();
            break;

        case MsgAttention:
            messageAttention();
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

        case MsgAskRHumid:
            messageAskRHumid();
            break;
    }

    sysFault &= ~FLT_COMMUNICATION;
    for (ch = 0; ch < NUM_CHILDREN; ch++)
    {
         if ((child[ch].status & CHILD_ST_NODE_DEFINED) && (!(child[ch].status & CHILD_ST_NODE_ACTIVE))) sysFault |= FLT_COMMUNICATION;
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

    // There can only be one outside temperature. Or we need a way to arbitrate.
    // So here we scan through the active children and return numNode of first
    // that qualifies, or 0 OW.
int externalTHavailable(void)
{
    extern double temperNowExteriorF, rhumidNowExterior;

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

    // Decide next actionType. This is the manager for active nodeNum.  Parent  = node 0.
    // Then scan through the child[] for the next resource item we should handle.
    // The resources may change dynamically, and children may leave network.  There are
    // presently 4 basic child resource outputs: 2 rlys & 2 SwiPwrs.
    // Also set outputType as a funciton of resourceType as appropriate.
void indexNextResource(void)
{
    extern struct operatingParms_struct oP;
    static byte outputCtr;                          // Cycles through 4 possible outputs

    
    if (actionType == SvcParentResources)       // From last time
    {                                           // First up, say hello, get status
        nodeNum = nextNodeNum(nodeNum);
        if (nodeNum) actionType = MsgAttention;      // If node does not respond, it is not ACTIVE
        return;
    }
                        // Hello didn't work, advance to next
    if (!(child[nodeNum - 1].status & CHILD_ST_NODE_ACTIVE))
    {
        nodeNum = nextNodeNum(nodeNum);
        if (nodeNum == 0) actionType = SvcParentResources;
        else actionType = MsgAttention;
        return;
    }

    if ((childHasIntFan(nodeNum)) && (actionType != MsgAskRHumid))
    {
        actionType = MsgAskRHumid;      // Local RH, not outside
        return;
    }

    if ((childHasAirExch(nodeNum)) && (actionType != MsgAskTemper))
    {
        actionType = MsgAskTemper;      // For now just use outside T
        return;
    }

    oP.sysStat &= ~ST_AIR_EXCHGR_ON;

    if (outputCtr == 0)
    {      
        outputCtr++;
        if (child[nodeNum - 1].relay1 != Free)
        {
            actionType = MsgRelay;
            outputType = Relay1;
            resourceType = child[nodeNum - 1].relay1;
            if ((resourceType == AirExchangerIn) || (resourceType == AirExchangerOut))  oP.sysStat |= ST_AIR_EXCHGR_ON;
            return;
        }
    }
    if (outputCtr == 1)
    {
        outputCtr++;
        if (child[nodeNum - 1].relay2 != Free)
        {
            actionType = MsgRelay;
            outputType = Relay2;
            resourceType = child[nodeNum - 1].relay2;
            if ((resourceType == AirExchangerIn) || (resourceType == AirExchangerOut))  oP.sysStat |= ST_AIR_EXCHGR_ON;
            return;
        }
    }
    if (outputCtr == 2)
    {
        outputCtr++;
        if (child[nodeNum - 1].swiPwr1 != Free)
        {
            actionType = MsgSwiPwr;
            outputType = SwiPwr1;
            resourceType = child[nodeNum - 1].swiPwr1;
            if ((resourceType == AirExchangerIn) || (resourceType == AirExchangerOut))  oP.sysStat |= ST_AIR_EXCHGR_ON;
            return;
        }
    }
    if (outputCtr == 3)
    {
        outputCtr++;
        if (child[nodeNum - 1].swiPwr2 != Free)
        {
            actionType = MsgSwiPwr;
            outputType = SwiPwr2;
            resourceType = child[nodeNum - 1].swiPwr2;
            if ((resourceType == AirExchangerIn) || (resourceType == AirExchangerOut))  oP.sysStat |= ST_AIR_EXCHGR_ON;
            return;
        }
    }
    if (outputCtr == 4)
    {
        outputCtr = 0;
        nodeNum = nextNodeNum(nodeNum);
        if (nodeNum == 0) actionType = SvcParentResources;
        else actionType = MsgAttention;
        return;
    }
}

int messageAskRHumid(void)
{
    extern double rhumidNowExterior;
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
    extern double temperNowExteriorF;
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

    // -------------------------------------------
    // Set/clr child[].status CHILD_NODE_ACTIVE
    // Returns nodeNum on success, 0 > code o.w.
    // Any child may set a FLT boit, but no one may clr.
int messageAttention(void)
{
    extern char pnetVer;
    extern struct operatingParms_struct oP;
    extern unsigned sysFault;
    int bfr, bfr2;

    // sysFault |= FLT_LOSTCHILD;
    sprintf(ioBfr, "AT%c\r", nodeNum + '0');    // Must end in CR only
    putStr(ioBfr);
        // if (oP.sysStat & ST_DEBUG_MODE) putChar('\n');     // TODO use passim
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
//    if (! (child[nodeNum - 1].status & CHILD_ST_TEMP1_OKAY) && (child[nodeNum - 1].status & CHILD_ST_RHUMID_OKAY) )
//        sysFault |= FLT_CHILDFAULT;         // This autoclears at midnight, or manually, but still weak. TODO
    child[nodeNum - 1].status |= CHILD_ST_NODE_ACTIVE;
    // sysFault &= ~FLT_LOSTCHILD;
    return(nodeNum);
}

    // Relays presently used only for AirCond, Heater, Dehum, Humid.
    // indexNextResource() has determined: nodeNum, outputType, resourceType
    // resourceManager() has determined: climateStatus
    // But we make the climate call here.  Pass back any neg exception codes.
int messageRelay(void)    
{
    extern struct operatingParms_struct oP;
    char relayChar = 0, polarityChar = 0;

    if (outputType == Relay1) relayChar = 'a';          // Pnet syntax
    else if (outputType == Relay2) relayChar = 'b';
    else return(-10);                                   // System error OW

    switch(resourceType)
    {
        case AirConditioner:
            if (climateStatus & TOO_HOT)
            {
                if (oP.sysStat & ST_AIR_EXCHGR_ON) polarityChar = '-';
                else polarityChar = '+';
            }
            else polarityChar = '-';       // OW just turn that thing off
            break;

        case Heater:
            if (climateStatus & TOO_COLD)
            {
                if (oP.sysStat & ST_AIR_EXCHGR_ON) polarityChar = '-';
                else polarityChar = '+';
            }
            else polarityChar = '-';
            break;

        case Humidifier:
            if (climateStatus & TOO_DRY) polarityChar = '+';
            else polarityChar = '-';
            break;

        case DeHumidifier:
            if (climateStatus & TOO_DAMP) polarityChar = '+';
            else polarityChar = '-';
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
int serviceParentResources(void)
{
    extern struct operatingParms_struct oP;
    extern unsigned sysFault;
    float f;

    resourceEvaluate.dayTotalCounter++;         // These are all reset at midnight
    if (climateStatus & TOO_HOT)
    {
        resourceEvaluate.tooHotTimer++;
        if ( !(oP.sysStat & ST_AIR_EXCHGR_ON))      // Air exchanger trumps AC, heater
        {
            if (parent.relay1 == AirConditioner) RELAY1 = 1;
            if (parent.relay2 == AirConditioner) RELAY2 = 1;
        }
        if (parent.relay1 == Heater) RELAY1 = 0;
        if (parent.relay2 == Heater) RELAY2 = 0;
    }

    if (climateStatus & TOO_COLD)
    {
        resourceEvaluate.tooColdTimer++;
        if ( !(oP.sysStat & ST_AIR_EXCHGR_ON))
        {
            if (parent.relay1 == Heater) RELAY1 = 1;
            if (parent.relay2 == Heater) RELAY2 = 1;
        }
        if (parent.relay1 == AirConditioner) RELAY1 = 0;
        if (parent.relay2 == AirConditioner) RELAY2 = 0;
    }

    if (climateStatus & TOO_DAMP)
    {
        resourceEvaluate.tooDampTimer++;
        if (parent.relay1 == DeHumidifier) RELAY1 = 1;
        if (parent.relay2 == DeHumidifier) RELAY2 = 1;
        if (parent.relay1 == Humidifier) RELAY1 = 0;
        if (parent.relay2 == Humidifier) RELAY2 = 0;
    }

    
    if (climateStatus & TOO_DRY)
    {
        resourceEvaluate.tooDryTimer++;
        if (parent.relay1 == Humidifier) RELAY1 = 1;
        if (parent.relay2 == Humidifier) RELAY2 = 1;
        if (parent.relay1 == DeHumidifier) RELAY1 = 0;
        if (parent.relay2 == DeHumidifier) RELAY2 = 0;
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


