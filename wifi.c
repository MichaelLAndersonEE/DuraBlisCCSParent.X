/* File:   wifi.c
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * RN-171 module
 * Created: 02 Oct 14
 */

#include <xc.h>
#include <string.h>
#include "durablisccsparent.h"
#include "wifi.h"
#include "serial.h"
#include "time.h"
#define TIMEOUT_RN171_ms  1000

byte wifiIP[4] = { 0x01, 0x01, 0x10, 0x20 };
byte wifiMask[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
bool wifiDHPC = false;
char wifiBfr[11];

extern struct t_struct time;        // Saved partially to SEEPROM Pg 0.

void wifiInit(void)
{
    char inCh;
    byte bufIdx = 0;
    
   // putStr2("$$$");     // RN-171 Command mode.  No CR or LF.
    delay_ms(TIMER_INIT, TIMEOUT_RN171_ms);
    while(delay_ms(TIMER_RUN, 0))
    {
        inCh = getChar2();
        if (inCh > 0)
        {
            delay_ms(TIMER_INIT, TIMEOUT_RN171_ms);
            putChar(inCh);      // DEB
            ioBfr[bufIdx] = inCh;
            if (ioBfr[bufIdx] == '\r')
            {
                ioBfr[bufIdx] = 0;
                bufIdx = 0;
               // putStr(ioBfr);
            }
            if (bufIdx < IO_BFR_SIZE) bufIdx++; else bufIdx = 0;
            if (strcmp (ioBfr, "DCHP Server Init") == 0) break;
        }
    }
    
    putStr2("$$$");             // RN-171 Command mode.  No CR or LF.
    delay_ms(TIMER_INIT, TIMEOUT_RN171_ms);
    while(delay_ms(TIMER_RUN, 0))
    {
        inCh = getChar2();
        if (inCh > 0)
        {
            delay_ms(TIMER_INIT, TIMEOUT_RN171_ms);
            putChar(inCh);      // DEB
           // ioBfr[bufIdx++] = inCh;
        }
    }

    putStr("\tRN171 timeout\r\n");
    return;

}

//    if (wifiBfr[0] == 'C' && wifiBfr[1] == 'M' & wifiBfr[2] == 'D')
//    {
//        putStr("\tRN-171 in Command mode\r\n");
//    }
//    else { sprintf(ioBfr, "\tUnknown RN-171 output: %s", wifiBfr); putStr(ioBfr); }

//    putStr(" WiFi now in console mode. 'XX' exits.\r\n");

//    while(1)
//    {
//        putStr("\n\r\t> ");
//        getStr(ioBfr, TIMEOUT_HUMAN);
//        if (strncmp(ioBfr, "XX", 2) == 0) return;
//        putStr2(ioBfr);
//        getStr2(ioBfr, IO_BFR_SIZE, TIMEOUT_RN171);
//        rs485TransmitEna(1);
//        putStr(ioBfr);
//        rs485TransmitEna(0);
//    }
//    sec = time.second;
//    while(time.second == sec) { timeRead(TIME_UPDATE); petTheDog(); }
//    putStr2("get$everything");
//

