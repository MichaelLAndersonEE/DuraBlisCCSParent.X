/* File:   time.c  RTCC and high level time functions
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * Created: 18 Sep14
 */

#include <xc.h>
#include "DuraBlisCCSParent.h"
#include "time.h"
#include "resources.h"

byte bcd2byte(byte bcd);
byte byte2bcd(byte by);

const char monStr[12][4] =
    { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    // Tracks time in secs for 9 chans.  Must be called at least minutely.
//unsigned secsLapsed(byte mode, byte chan)
//{
//    static unsigned secsCtr[9], oldSecs[9];
//    if (mode == TIMER_INIT) secsCtr[chan] = 0;
//    else if (mode == TIMER_RUN)
//    {
//        if (time.second > oldSecs[chan])
//            secsCtr[chan] = time.second - oldSecs[chan];
//        else secsCtr[chan] = 60 - oldSecs[chan] + time.second;
//        oldSecs[chan] = time.second;
//    }
//
//    return(secsCtr[chan]);
//}

bool timeInBounds(void)
{
    if (time.year > 99) return (false);
    if (time.month == 0 || time.month > 12) return (false);
    if (time.dayMonth == 0 || time.dayMonth > 31) return (false);
    if (time.weekday > 6) return (false);
    if (time.hour > 23) return (false);
    if (time.minute > 59) return (false);
    return(true);
}

    // Mon is 1 - 12, year is 00 - 99
byte daysInMonth(byte mo, byte yr)
{
    byte retVal;
    const byte dIM[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mo < 1 || mo > 12 || yr > 99) return(0);
    retVal = dIM[mo - 1];
    if ((yr % 4 == 0) && (mo == 2) ) retVal++;
    return(retVal);
}

    // Returns val > 0 until countdown in ms
unsigned delay_ms(byte mode, unsigned timeOut_ms)
{
    static unsigned ms_ctr, last_ms;

    petTheDog();
    if (mode == TIMER_INIT)
    {
        ms_ctr = timeOut_ms;
        last_ms = time.msec;
        return(ms_ctr);
    }
    else if (mode == TIMER_RUN)
    {
        if (time.msec != last_ms) ms_ctr--;     // Will lose accuracy if time btwn calls > 1 ms, but this is acceptable here.
        last_ms = time.msec;      
        return(ms_ctr);                          
    }
    else return(0xFFFF);
}

   // --- Very roughly 1 us and not linear
void delay_us(unsigned T)   // TODO base this on a clock, or provide such a version
{
    unsigned t;
    for (t=0; t<T; t++ ) petTheDog();
}

//// This is simply based on the present exec loop timing.
//    // In Ver 0.51, period = 5 us.
//void fastTimer(void)
//{
//    static unsigned fastCtr;
//
//    if (fastCtr++ > 200)
//    {
//        fastCtr = 0;
//        JTAG_TDO = 1;          // DEB For timing analysis
//        if (time.msec++ > 999) time.msec = 0;
//        JTAG_TDO = 0;          // DEB For timing analysis
//        timeRead(TIME_UPDATE);  // Ca 1 ms resolution
//    }
//}

    // Reads RTCC and updates time struct.  Called every 16 ms.
    // On midnight clears resourceEvaluate & sysFault
void timeRead(byte modeRead)
{
    extern unsigned sysFault;
    byte tBfr;
    long unsigned rtccImage;

    rtccImage = RTCTIME;
    if (modeRead == TIME_LOQUACIOUS)
    {
        sprintf(ioBfr, "\n\r\tRTCC T: %08lX\n\r", rtccImage);
        putStr(ioBfr);
    }

    rtccImage >>= 8;        // Lower 8 empty
    tBfr = rtccImage;
    tBfr = bcd2byte(tBfr);
    if (tBfr < 60) time.second = tBfr;
    if (modeRead == TIME_LOQUACIOUS)
    {
        sprintf(ioBfr, "\ts %02d, ", tBfr);
        putStr(ioBfr);
    }

    rtccImage >>= 8;
    tBfr = rtccImage;
    tBfr = bcd2byte(tBfr);
    if (tBfr < 60) time.minute = tBfr;
    if (modeRead == TIME_LOQUACIOUS)
    {
        sprintf(ioBfr, "m %02d, ", tBfr);
        putStr(ioBfr);
    }

    rtccImage >>= 8;
    tBfr = rtccImage;
    tBfr = bcd2byte(tBfr);
    if (tBfr < 24) time.hour = tBfr;
    if (modeRead == TIME_LOQUACIOUS)
    {
        sprintf(ioBfr, "h %02d, ", tBfr);
        putStr(ioBfr);
    }

    rtccImage = RTCDATE;
    if (modeRead == TIME_LOQUACIOUS)
    {
        sprintf(ioBfr, "\n\r\tD: %08lX\n\r", rtccImage);
        putStr(ioBfr);
    }
    tBfr = rtccImage;
    tBfr = bcd2byte(tBfr);
    if (tBfr < 7) time.weekday = tBfr;
    if (modeRead == TIME_LOQUACIOUS)
    {
        sprintf(ioBfr, "\twd %02d, ", tBfr);
        putStr(ioBfr);
    }

    rtccImage >>= 8;
    tBfr = rtccImage;
    tBfr = bcd2byte(tBfr);
    if (tBfr < 32) time.dayMonth = tBfr;
    if (modeRead == TIME_LOQUACIOUS)
    {
        sprintf(ioBfr, "dm %02d, ", tBfr);
        putStr(ioBfr);
    }

    rtccImage >>= 8;
    tBfr = rtccImage;
    tBfr = bcd2byte(tBfr);
    if (tBfr < 13) time.month = tBfr;
    if (modeRead == TIME_LOQUACIOUS)
    {
        sprintf(ioBfr, "m %02d, ", tBfr);
        putStr(ioBfr);
    }

    rtccImage >>= 8;
    tBfr = rtccImage;
    tBfr = bcd2byte(tBfr);
    if (tBfr < 100) time.year = tBfr;
    if (modeRead == TIME_LOQUACIOUS)
    {
        sprintf(ioBfr, "y %02d\r\n", tBfr);
        putStr(ioBfr);
    }

    if (time.hour == 0 && time.minute == 0 && time.second == 0)     // Midnight resets
    {                                           // Not perfect, but till something better...
        resourceEvaluate.dayTotalCounter = 0;
        resourceEvaluate.tooColdTimer = 0;
        resourceEvaluate.tooDampTimer = 0;
        resourceEvaluate.tooDryTimer = 0;
        resourceEvaluate.tooHotTimer = 0;
       // sysFault &= 0xF000;     TODO why?                // See FLT bits in DuraBlisCCSParent.h
    }
}

    ////////////////////
void timeWrite(void)
{
    unsigned long timeNow;  // = 0x12345678 Set time to 12 hr, 34 min, 56 sec
    unsigned long dateNow;  // = 0x14111001 Set date to Mon, 10 Nov 2014

    timeNow = byte2bcd(time.hour);
    timeNow <<= 8;
    timeNow += byte2bcd(time.minute);
    timeNow <<= 16;         // Time write a fortiori sets sec = 0;
                            // PIC32MX350 RTCC does not have subsec resolution
    dateNow =  byte2bcd(time.year);
    dateNow <<= 8;
    dateNow += byte2bcd(time.month);
    dateNow <<= 8;
    dateNow += byte2bcd(time.dayMonth);
    dateNow <<= 8;
    dateNow += byte2bcd(time.weekday);

    RTCCONCLR = 0x8000;     // Turn off the RTCC
    while(RTCCON & 0x40);   // Wait for clock to be turned off
    RTCTIME = timeNow;      // Safe to update the time
    RTCDATE = dateNow;      // Update the date
    RTCCONSET = 0x8000;     // Turn on the RTCC
    while(!(RTCCON & 0x40));// Wait for clock to be turned on  TODO timeout
}

    /////////////////////
byte bcd2byte(byte bcd)
{
    byte retVal;

    if ((bcd & 0x0F) > 9) return(0xFE);
    else retVal = bcd & 0x0F;
    bcd >>= 4;
    if ((bcd & 0x0F) > 9) return(0xFF);
    else retVal += 10 * bcd;
    return(retVal);
}

    /////////////////////
byte byte2bcd(byte by)
{
    byte nibH, nibL, retVal;

    if (by > 99) return(0xFF);
    nibH = by / 10;
    nibL = by % 10;
    retVal = (nibH << 4) + nibL;
    return(retVal);
}
