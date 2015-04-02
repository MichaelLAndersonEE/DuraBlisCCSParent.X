/* File:   edits.c  Utility user editing routines
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board with RS-485
 * Created: 15 Jan 15
 */

#include <xc.h>
#include "DuraBlisCCSParent.h"
#include "edits.h"
#include "serial.h"
#include "time.h"

 // TODO needs updating
void editSetPoint(void)
{
    extern struct operatingParms_struct oP; 
    double inDo;

    putStr("\tSetpoint temperature (oF): "); //, true);
    if (getStr(ioBfr, TIMEOUT_HUMAN) < 1) return;
    inDo = atof(ioBfr);
    if (inDo < 50.0)
    {
        putStr("\tToo low");  //, true);
        return;
    }
    if (inDo > 95.0)
    {
        putStr("\tToo high");  //, true);
        return;
    }
    oP.setPointTempF = inDo;
    putStr("\r\n\tRelative humidity (%): ");  //, true);
    if (getStr(ioBfr, TIMEOUT_HUMAN) < 1) return;
    inDo = atof(ioBfr);
    if (inDo < 10.0)
    {
        putStr("\tToo low");  //, true);
        return;
    }
    if (inDo > 80.0)
    {
        putStr("\tToo high");  //, true);
        return;
    }
    oP.setPointRelHum = inDo;
    putStr("\r\n\tOkay");  //, true);
}

    ////////////////////
void editTime(void)
{
    int inVal;

    putStr("\tMinute (0 - 59): ");
    if (getStr(ioBfr, TIMEOUT_HUMAN) < 1) return;
    inVal = atoi(ioBfr);
    if (inVal < 0 || inVal > 59)
    {
        putStr("\tRange error");  //, true);
        return;
    }
    time.minute = inVal;
    putStr("\n\r\tHour (0 - 23): ");  //, true);
    if (getStr(ioBfr, TIMEOUT_HUMAN) < 1) return;
    inVal = atoi(ioBfr);

    if (inVal < 0 || inVal > 23)
    {
        putStr("\tRange error"); //, true);
        return;
    }
    time.hour = inVal;
    putStr("\n\r\tDay of month (1 - 31): ");  //, true);
    if (getStr(ioBfr, TIMEOUT_HUMAN) < 1) return;
    inVal = atoi(ioBfr);

    if (inVal < 1 || inVal > 31)
    {
        putStr("\tRange error");  //, true);
        return;
    }
    time.dayMonth = inVal;
    putStr("\n\r\tMonth (1 - 12): "); //, true);
    if (getStr(ioBfr, TIMEOUT_HUMAN) < 1) return;
    inVal = atoi(ioBfr);

    if (inVal < 1 || inVal > 12)
    {
        putStr("\tRange error");  //, true);
        return;
    }
    time.month = inVal;
    putStr("\n\r\tYear (00 - 99): ");  //, true);
    if (getStr(ioBfr, TIMEOUT_HUMAN) < 1) return;
    inVal = atoi(ioBfr);

    if (inVal < 0 || inVal > 99)
    {
        putStr("\tRange error");  //, true);
        return;
    }
    time.year = inVal;

//    putStr("\n\r\tDay of week (0 Sun - 6 Sat): ");
//    if (getStr(ioBfr, 11, TIMEOUT_HUMAN) < 1) return;
//    inVal = atoi(ioBfr);
//
//    if (inVal < 0 || inVal > 6)
//    {
//        putStr("\tRange error");
//        return;
//    }
//    time.weekday = inVal;
//
  //  sysStat &= ~ST_CLOCKNOTSET;
    putStr("\r\n\tOkay");
}

    // ----------------------------------
void menuDisplay(void)
{
   // rs485TransmitEna(1);
    putStr("\n\r Use / for these commands\n\r");
    putStr(" MichaelLAndersonEE@gmail.com\n\r");
   // putStr(" C - Calibrate constants * \n\r");  TODO
    putStr(" D - Datalog CSV dump\n\r");
    putStr(" E - smem Erase all\n\r");
    putStr(" F - Factory hard defaults\n\r");
    putStr(" i - read channel currents\n\r");
    putStr(" k - keypad test\n\r");     // TODO Obsolete
    putStr(" m - smem simple test\n\r");
    putStr(" p - sMem write parms\n\r");
    putStr(" P - sMem read parms\n\r");
    putStr(" r - report basic info\n\r");
    putStr(" s - read T & RH sensors\n\r");
    putStr(" S - Set T & RH control point\n\r");
    putStr(" t - read rtcc\n\r");
    putStr(" T - set rtcc Time\n\r");
    putStr(" w - WiFi init & test\n\r");
   // putStr(" W - WiFi read\n\r");
    putStr(" Z - reset datalog counter\n\r");
    putStr(" 1 - toggle relay 1\n\r");
    putStr(" 2 - toggle relay 2\n\r");
    putStr(" 4 - smem read clock regs\n\r");
    putStr(" 5 - destructive smem write test\n\r");
    putStr(" 6 - smem read test\n\r");
    putStr(" 7 - UART2 loopback test\n\r");    
    putStr(" ? - show this menu\n\r");
  //  rs485TransmitEna(0);
}

