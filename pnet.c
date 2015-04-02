/* File:   pnet.c
 * Manages the PNet comm
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * LCD:  NHD NHD-C12864WC-FSW-FBW-3V3, 128 x 64 pixels
 * Created: 18 Sep14
 */

#include <xc.h>
#include "DuraBlisCCSParent.h"
#include "serial.h"

int pnetSend(char *strOut, byte nodeNum)
{
    rs485TransmitEna(1);
    sprintf(ioBfr, "AT%c\r", nodeNum + '0');    // Must end in CR only
    putStr(ioBfr);
    rs485TransmitEna(0);

//    sysFault &= ~(FLT_LOSTCHILD + FLT_COMMUNICATION);
//    retVal = nodeNum;
//    if (getStr(ioBfr, IO_BFR_SIZE, TIMEOUT_PNET) < 1) { sysFault |= FLT_LOSTCHILD; retVal = 0; }    // No one there; end of story.
//    if(ioBfr[0] != 'a')  { sysFault |= FLT_COMMUNICATION; retVal = -1; }
//    if(ioBfr[1] != 'k') { sysFault |= FLT_COMMUNICATION;  retVal = -2; }
//    if(ioBfr[2] != nodeNum + '0'){ sysFault |= FLT_COMMUNICATION; retVal = -3; }          // Protocol fail
//    if(ioBfr[3] != verStr[0]) { sysFault |= FLT_COMMUNICATION; retVal = -4; }  //commProtocolVer) return (-3);    // Version fail
//
//    rs485TransmitEna(1);        // TODO use this below.  ANd functionalize.
//    putChar('\n');          // Advances the traffic display for monitor. Child ignores.
//    rs485TransmitEna(0);
//    if (retVal < 0) return(retVal);     // No point in continuing

}