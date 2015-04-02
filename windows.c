/* File:   windows.c user IO objects
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * LCD:  NHD NHD-C12864WC-FSW-FBW-3V3, 128 x 64 pixels
 * Created: 18 Sep14
 * This is a state machine in windowState.  There is some spaghetti, esp
 * with fault reporting and smooth timing, but it is stable.
 */

#include <xc.h>
#include <string.h>
#include "DuraBlisCCSParent.h"
#include "lcd.h"
#include "time.h"
#include "iic_keypad.h"
#include "resources.h"
#include "adc.h"            // For Sys Info, pg 3
#include "smem.h"           // For last save defaults
#include "wifi.h"
#include "buzzer.h"         // For security

    // Window states 
    // TODO needed:  calibration consts, sysinfo
#define WIN_MAIN                 0
#define WIN_FAULT                1
#define WIN_SETUP1               2
#define WIN_SETUP2               3
#define WIN_SETUP3               4
#define WIN_COMFORT_BASE         5
#define WIN_CZ_SET_TEMP          6
#define WIN_CZ_SET_RH            7
//#define WIN_QUALITATIVE_HUM      8
#define WIN_SET_TIME             9
#define WIN_RESOURCES_BASE      10
#define WIN_RES_SET_HVAC        11
#define WIN_RES_MODULES         12
#define WIN_RES_SET_MODULE      13
#define WIN_MEAS_UNITS          14
#define WIN_SYSINFO             15
#define WIN_SET_WIFI            16
#define WIN_SET_CALIBRATIONS    17
#define WIN_DATALOG             18
#define WIN_DEFAULTS_BASE       19
#define WIN_EXECUTE_DEFAULTS    20
#define WIN_SECURITY_STOP       21

#define TIMEOUT_T             2812        // 45 sec typ
#define TIMEOUT_FAULT_WINDOW     4        // 4 sec tied to time.second

#define QUAL_HUM_DRY_MIN        20          // These are climate CZ center humidities
#define QUAL_HUM_EPA_MIN        40          // RH low recommended by US EPA
#define QUAL_HUM_EPA_MAX        50
#define QUAL_HUM_DAMP_MAX       60

static void windowMain(char keyIn);
static void windowFault(char keyIn);
static void windowSetParent(char keyIn);
static void windowSetTime(char keyIn);
static void windowComfortZone(char keyIn);
static void windowCZTemper(char keyIn);
static void windowCZRHumid(char keyIn);
static void windowReviewModules(char keyIn);
static void windowModifyModules(char keyIn);
static void windowMeasUnits(char keyIn);
static void windowSysInfo(char keyIn);
static void windowDefaults(char keyIn);
static void windowSetWiFi(char keyIn);
static void windowCalibrate(char keyIn);
static void windowDatalog(char keyIn);
static void windowSecurityStop(char keyIn);

static char *adjectiveRH[5] = { "Very Dry", "Somewhat Dry", "EPA Recommend", "Somewhat Damp", "Very Damp" };
static byte fuzzyRHidx;     // Index for RH fuzzy categories, inited by windowComfortZone(), changed by windowCZRHumid()
static unsigned windowTimeout;
static byte windowState;    // State machine index
static byte field;          // Index into fields in a given window state
static byte childFocus = 1;
struct t_struct timeBuffer;
char bfr2[11];

extern struct operatingParms_struct oP;
extern struct t_struct time;
extern byte mainScreen[];
extern unsigned sysFault;

    // State machine manager.  Presently called every 16 ms by taskScheduler().
void windowManager(char keyIn)
{
    static byte windowStateOld = 0xFF;       // State machine index buffer

    if (oP.sysStat & ST_SECURITY_LAPSED)            
        windowState = WIN_SECURITY_STOP;    
    else
    {
        if (windowState != windowStateOld)  // This erases previous content
        {
            lcdScreen(mainScreen);
            windowStateOld = windowState;
            windowTimeout = TIMEOUT_T;
        }

        if (windowTimeout > 0) windowTimeout--;
        else
        {
            field = 0;
            windowState = WIN_MAIN;
        }
    }

    switch(windowState)
    {
        case WIN_MAIN:
                // if (sysFault) { windowState = WIN_FAULT; break; }
            windowMain(keyIn);      // Will periodically change state to Fault as necessary
            break;

        case WIN_FAULT:
            windowFault(keyIn);           
            break;

        case WIN_SETUP1:
            lcdCenter(7, "Setup 1");
            lcdSoftkeys1("Com", "Set", "Con", "More");
            lcdSoftkeys0(" fort", " Time", " fig", "");
            if (keyIn & KEY_A) windowState = WIN_COMFORT_BASE;
            else if (keyIn & KEY_B)
            {
                windowState = WIN_SET_TIME;
                timeBuffer = time;
                if (timeBuffer.month == 0 || timeBuffer.month > 12)      // Need valid values for formatted string
                {
                    timeBuffer.dayMonth = 10;
                    timeBuffer.month = 11;
                    timeBuffer.year = 15;
                }
            }
            else if (keyIn & KEY_C) windowState = WIN_RESOURCES_BASE;
            else if (keyIn & KEY_D) windowState = WIN_SETUP2;
            break;

        case WIN_SETUP2:
            lcdCenter(7, "Setup 2");
            lcdSoftkeys1("[F/[C", "Sys", "Set", "More");
            lcdSoftkeys0("", "Info", " WiFi", "");
            if (keyIn & KEY_A) windowState = WIN_MEAS_UNITS;
            else if (keyIn & KEY_B) windowState = WIN_SYSINFO;
            else if (keyIn & KEY_C) windowState = WIN_SET_WIFI;
            else if (keyIn & KEY_D) windowState = WIN_SETUP3;
            break;

        case WIN_SETUP3:
            lcdCenter(7, "Setup 3");
            lcdSoftkeys1("Calib", "Data", "Defau", "Esc");
            lcdSoftkeys0(" rate", " log", " lts", "");
            if (keyIn & KEY_A) windowState = WIN_SET_CALIBRATIONS;
            else if (keyIn & KEY_B) windowState = WIN_DATALOG;
            else if (keyIn & KEY_C) windowState = WIN_DEFAULTS_BASE;
            else if (keyIn & KEY_D) windowState = WIN_MAIN;
            break;

        case WIN_COMFORT_BASE:
            windowComfortZone(keyIn);
            break;

        case WIN_CZ_SET_TEMP:
            windowCZTemper(keyIn);
            break;

        case WIN_CZ_SET_RH:
            windowCZRHumid(keyIn);
            break;

        case WIN_RESOURCES_BASE:
            lcdCenter(7, "Configure Resources");
            lcdUpDnEnEs();
            strcpy(ioBfr, "Setup modules");
            if (field == 0) lcdStrAt(4, 116, "<");
            else strcat(ioBfr, "    ");
            lcdStrAt(4, 4, ioBfr);

            strcpy(ioBfr, "Setup HVAC");
            if (field == 1) lcdStrAt(3, 116, "<");
            else strcat(ioBfr, "    ");
            lcdStrAt(3, 4, ioBfr);

            if (keyIn & (KEY_A | KEY_B))
            {
                if (field == 0)
                {
                    field = 1;
                    lcdStrAt(4, 116, " ");
                }
                else
                {
                    field = 0;
                    lcdStrAt(3, 116, " ");
                }
            }
            else if (keyIn & KEY_C)
            {
                if (field == 0)
                {
                    windowState = WIN_RES_MODULES;
                }
                else
                {
                    windowState = WIN_RES_SET_HVAC;
                    field = 0;
                }
            }
            else if (keyIn & KEY_D) windowState = WIN_SETUP1;
            break;

        case WIN_RES_SET_HVAC:
            windowSetParent(keyIn);
            break;

        case WIN_SET_TIME:
            windowSetTime(keyIn);
            break;

        case WIN_RES_MODULES:
            windowReviewModules(keyIn);
            break;

        case WIN_RES_SET_MODULE:
            windowModifyModules(keyIn);
            break;

        case WIN_MEAS_UNITS:
            windowMeasUnits(keyIn);
            break;

        case WIN_SYSINFO:   
            windowSysInfo(keyIn);
            break;

        case WIN_DEFAULTS_BASE:
            windowDefaults(keyIn);
            break;

        case WIN_EXECUTE_DEFAULTS:      // Uses field as index
            strcpy(ioBfr, field == 1 ? "Recover last save." : "Factory defaults");
            lcdCenter(5, ioBfr);
            lcdCenter(4, "Are you sure?");
            lcdSoftkeys1("", "", "Yes", "No");
            if (keyIn & KEY_C)
            {
                if (field) smemReadParms(SMEM_SILENT);
                else
                {
                    smemEraseAll(SMEM_SILENT);
                    factoryHardInit();
                    buzzerControl(BUZ_CLEARSTATUS, 8);
                }                 
                field = 0;
                windowState = WIN_MAIN;
            }
            else if (keyIn & KEY_D)
            {
                field = 0;
                windowState = WIN_SETUP3;
            }
            break;

        case WIN_SET_WIFI:      // TODO just a stub
            windowSetWiFi(keyIn);
            break;

         case WIN_SET_CALIBRATIONS:   // TODO just a stub
            windowCalibrate(keyIn);
            break;

        case WIN_DATALOG:
            windowDatalog(keyIn);
            break;

        case WIN_SECURITY_STOP:
            windowSecurityStop(keyIn);
            break;
    }
    if (keyIn) windowTimeout = TIMEOUT_T;
}

    // ---------------------------------------
void windowMain(char keyIn)
{   
    extern double temperNowF;  
    byte hourDisplayed, timeSequencer;

    lcdCenter(7, "DuraBlis CCS");
    timeRead(TIME_UPDATE);
    if ((oP.sysStat & ST_SHOW_TIME) && timeInBounds())
    {
        if (time.hour == 0) hourDisplayed = 12;
        else if (time.hour > 12) hourDisplayed = time.hour - 12;
        else hourDisplayed = time.hour;

        sprintf(ioBfr, " %s %d - %d:%02d%s ",
            monStr[time.month-1], time.dayMonth, hourDisplayed, time.minute, time.hour > 11 ? "pm" : "am" );
        lcdCenter(6, ioBfr);
    }

    timeSequencer = time.second % 15;
    switch(timeSequencer)
    {
        case 0:         // Show interior temp now
            lcdCenter(5, " Inside Climate: ");
            strcpy(ioBfr, "* ");
            switch((ClimateType) whatIsInteriorClimateNow())
            {
                case InComfortZone:
                    strcat(ioBfr, "Comfortable");
                    break;

                case HotAndDamp:
                    strcat(ioBfr, "Hot & damp");
                    break;

                case JustDamp:
                    strcat(ioBfr, "Too damp");
                    break;

                case ColdAndDamp:
                    strcat(ioBfr, "Cold & damp");
                    break;

                case JustCold:
                    strcat(ioBfr, "Too cold");
                    break;

                case ColdAndDry:
                    strcat(ioBfr, "Cold & dry");
                    break;

                case JustDry:
                    strcat(ioBfr, "Too dry");
                    break;

                case HotAndDry:
                    strcat(ioBfr, "Hot & dry");
                    break;

                case FreakinHot:
                    strcat(ioBfr, "Too hot");
                    break;
            }

            strcat(ioBfr, " *");
            lcdClearPage(4);
            lcdCenter(4, ioBfr);

            strcpy(ioBfr, "Temperature: ");
            if (oP.sysStat & ST_USE_CELSIUS) sprintf(bfr2, "%2.01f[C", 0.55556 * (temperNowF - 32));
            else sprintf(bfr2, "%1.0f[F ", temperNowF);
            strcat(ioBfr, bfr2);
            lcdCenter(3,ioBfr);
            
//            strcpy(ioBfr, "Humidity: ");
//            if (oP.sysStat & ST_USE_DEWPOINT) sprintf(bfr2, "%2.01foC", dewpointCalc(temperF2C(temperNowF), rhumidNow));
//            else sprintf(bfr2, "%2.01f%%",rhumidNow);
//            strcat(ioBfr, bfr2);
//            lcdStrAt(3, 4, ioBfr);
            break;

        case 4:
            if (externalTHavailable())
            {
                lcdCenter(5, "Outside Climate:");
                strcpy(ioBfr, "* ");
                switch((RelativeClimateType) whatIsExteriorClimateNow())
                {                   
//                    case InCZ:
//                        strcat(ioBfr, "Comfortable");
//                        break;

                    case CoolerAndDrier:
                        strcat(ioBfr, "Cooler & drier");
                        break;

                    case DrierAndTempCZ:
                        strcat(ioBfr, "Drier & Temp OK");
                        break;

                    case WarmerAndDrier:
                        strcat(ioBfr, "Warmer & drier");
                        break;

                    case WarmerAndHumCZ:
                        strcat(ioBfr, "Warmer & RH OK");
                        break;

                    case WarmerAndDamper:
                        strcat(ioBfr, "Warmer & moister");
                        break;

                    case DamperAndTempCZ:
                        strcat(ioBfr, "Moister & temp OK");
                        break;

                    case CoolerAndDamper:
                        strcat(ioBfr, "Cooler & moister");
                        break;

                    case CoolerAndHumCZ:
                        strcat(ioBfr, "Cooler & RH OK");
                        break;

                    case NullCase:
                    default:
                        strcat(ioBfr, "Uncertain");
                        break;
            }

            strcat(ioBfr, " *");
            lcdClearPage(4);
            lcdCenter(4, ioBfr);

            strcpy(ioBfr, "Temperature: ");
            if (oP.sysStat & ST_USE_CELSIUS) sprintf(bfr2, "%2.01f[C", 0.55556 * (temperNowF - 32));
            else sprintf(bfr2, "%1.0f[F ", temperNowF);
            strcat(ioBfr, bfr2);
            lcdCenter(3,ioBfr);
            }
            break;

        case 8:
            lcdClearPage(3);
            strcpy(ioBfr, "Comfort temp: ");
            if (oP.sysStat & ST_USE_CELSIUS) sprintf(bfr2, "%2.01f[C", 0.55556 * (oP.setPointTempF - 32));
            else sprintf(bfr2, "%1.0f[F",  oP.setPointTempF);
            strcat(ioBfr, bfr2);            
            lcdCenter(3, ioBfr);
//            sprintf(ioBfr, "  RH: %2.01f%%", oP.setPointRelHum);
//            lcdClearPage(3);
//            lcdStrAt(3, 4, ioBfr);
            break;
            
        case 12:
            if (sysFault) windowState = WIN_FAULT;
            break;
    }  
 
    lcdSoftkeys1("", "  ^", "  _", "Setup");
    lcdSoftkeys0("", "Temp", "Temp", "");
    if (keyIn & KEY_B) 
    {
        if (oP.setPointTempF < REASONABLE_TEMP_MAX) oP.setPointTempF += 1;
        else oP.setPointTempF = REASONABLE_TEMP_MIN;
       // upDownTmr = 1000;        
    }
    if (keyIn & KEY_C) 
    {
        if (oP.setPointTempF > REASONABLE_TEMP_MIN) oP.setPointTempF -= 1;
        else oP.setPointTempF = REASONABLE_TEMP_MAX;
    }
   // if (upDownTmr) upDownTmr--;
    
    if (keyIn & KEY_D) windowState = WIN_SETUP1;


}

    // ---- Displays for timeOut, then reverts to Main ------
void windowFault(char keyIn)
{
    static unsigned timeOut = TIMEOUT_FAULT_WINDOW;
    static byte oldSec;
    unsigned fltIdx;
    byte hourDisplayed, ch;

    if (sysFault == 0) { windowState = WIN_MAIN; return; }

    lcdCenter(7, "DuraBlis CCS");
    timeRead(TIME_UPDATE);
    if ((oP.sysStat & ST_SHOW_TIME) && timeInBounds())
    {
        if (time.hour == 0) hourDisplayed = 12;
        else if (time.hour > 12) hourDisplayed = time.hour - 12;
        else hourDisplayed = time.hour;

        sprintf(ioBfr, " %s %d - %d:%02d%s ",
            monStr[time.month-1], time.dayMonth, hourDisplayed, time.minute, time.hour > 11 ? "pm" : "am" );
        lcdCenter(6, ioBfr);
    }
   
    for (fltIdx = 0x8000; fltIdx > 0; fltIdx >>= 1)
    {
        if (sysFault & fltIdx)
        {
            switch(fltIdx)
            {
                case FLT_LOSTCHILD:
                    strcpy(ioBfr, "MODULE LOST"); lcdCenter(4, ioBfr);      // TODO mechanism to say which one
                    strcpy(ioBfr, "FROM NETWORK"); lcdCenter(3, ioBfr);
                    break;

                case FLT_CHILDFAULT:
                    strcpy(ioBfr, "MODULE REPORTS"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "MAJOR FAULT"); lcdCenter(3, ioBfr);
                    break;

                 case FLT_FLOODDETECTED:
                    strcpy(ioBfr, "FLOOD"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "DETECTED"); lcdCenter(3, ioBfr);
                    break;

                case FLT_OVER_CURRENT:
                    strcpy(ioBfr, "CHANNEL"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "OVERCURRENT"); lcdCenter(3, ioBfr);
                    break;

                case FLT_SENSORSNOTCALIB:
                    strcpy(ioBfr, "SENSORS NOT"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "CALIBRATED"); lcdCenter(3, ioBfr);
                    break;

                case FLT_SEC_TEMP_LOW:
                    strcpy(ioBfr, "BOILER TEMPERATURE"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "HAS FALLEN"); lcdCenter(3, ioBfr);
                    break;

                case FLT_PARENT_TSENSOR:
                    strcpy(ioBfr, "MAIN TEMPERATURE"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "SENSOR FAULT"); lcdCenter(3, ioBfr);
                    break;

                case FLT_PARENT_HSENSOR:
                    strcpy(ioBfr, "MAIN HUMIDITY"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "SENSOR FAULT"); lcdCenter(3, ioBfr);
                    break;

                case FLT_CANNOTCONTROLTEMP:
                    strcpy(ioBfr, "CANNOT REACH"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "TEMPERATURE SETPOINT"); lcdCenter(3, ioBfr);
                    break;

                case FLT_CANNOTCONTROLHUM:
                    strcpy(ioBfr, "CANNOT REACH"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "HUMIDITY SETPOINT"); lcdCenter(3, ioBfr);
                    break;
          
                case FLT_SMEM:
                    strcpy(ioBfr, "SMEM"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "FAULT"); lcdCenter(3, ioBfr);
                    break;

                case FLT_PARENT_TSEN_NOISY:
                    strcpy(ioBfr, "Temperature"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "Sensor Noisy"); lcdCenter(3, ioBfr);
                    break;

                case FLT_PARENT_RHSEN_NOISY:
                    strcpy(ioBfr, "Humidity"); lcdCenter(4, ioBfr);
                    strcpy(ioBfr, "Sensor Noisy"); lcdCenter(3, ioBfr);
                    break;

//                case FLT_COMMUNICATION:
//                    strcpy(ioBfr, "Network Commun-"); lcdCenter(4, ioBfr);
//                    strcpy(ioBfr, "ication Fault"); lcdCenter(3, ioBfr);
//                    break;

                case FLT_IICFAIL:
                    strcpy(ioBfr, "IIC Fault"); lcdCenter(4, ioBfr);
                    break;
            }
            break;              // Break for loop to preserve fltIdx
        }
    }

    lcdSoftkeys1("Clear", "", "", "");
    windowTimeout = TIMEOUT_T;      // There is no timeout from fault window
    
    if (keyIn & KEY_A)
    {
        if (sysFault & FLT_LOSTCHILD)
        {
            for (ch = 0; ch < NUM_CHILDREN; ch++) child[ch].miaCtr = 0;
        }
        sysFault &= ~fltIdx;     // Clear each bit in succession from MSB on down     
        lcdScreen(mainScreen);       
    }

    if (time.second != oldSec)
    {
        timeOut--;
        oldSec = time.second;
    }
    if (timeOut == 0)
    {
        timeOut = TIMEOUT_FAULT_WINDOW;     // Reinit for next time
        windowState = WIN_MAIN;             // But go back to Main
    }
}

    // ---------------------------------------
    // This is somewhat restrictive, but done so at Midi request.
void windowSetParent(char keyIn)
{
    lcdCenter(7, "Main module HVAC");
    lcdUpDnEnEs();
    strcpy(ioBfr, "Air Cond.: ");
    if (parent.relay1 == AirConditioner) strcat(ioBfr, "Yes");
    else strcat(ioBfr, "No ");
    if (field == 0)
    {
        lcdStrAt(3, 116, " ");
        lcdStrAt(4, 116, "<");
    }
    lcdStrAt(4, 2, ioBfr);

    strcpy(ioBfr, "Heater: ");
    if (parent.relay2 == Heater) strcat(ioBfr, "Yes");
    else strcat(ioBfr, "No ");
    if (field == 1)
    {
        lcdStrAt(4, 116, " ");
        lcdStrAt(3, 116, "<");
    }
    lcdStrAt(3, 2, ioBfr);

    if (keyIn & (KEY_A | KEY_B))
    {
        if (field == 0) field = 1; else field = 0;
    }
    else if (keyIn & KEY_C)
    {
        if (field == 0) 
        {
            if (parent.relay1 == Free) parent.relay1 = AirConditioner;
            else parent.relay1 = Free;
        }
        else
        {
            if (parent.relay2 == Free) parent.relay2 = Heater;
            else parent.relay2 = Free;
        }
        if (smemWriteParms(SMEM_SILENT) <= 0) sysFault |= FLT_SMEM;
    }
    else if (keyIn & KEY_D)     // TODO buffer?
    {
        field = 0;
        windowState =  WIN_RESOURCES_BASE;
    }
}

    // 0---0---0-----1--1-1-
    // 0---4---8-----4--7-9-    x 6
    // Nov 10, 2014  05:55am
void windowSetTime(char keyIn)
{
    byte hourDisplayed;
    bool amPm;

    lcdCenter(7, "Set Date & Time");
    lcdUpDnEnEs();
    if (timeBuffer.hour >= 13) { hourDisplayed = timeBuffer.hour - 12; amPm = false; }
    else if (timeBuffer.hour == 12) { hourDisplayed = 12; amPm = false; }
    else if (timeBuffer.hour == 0) { hourDisplayed = 12; amPm = true; }
    else { hourDisplayed = timeBuffer.hour; amPm = true; }
    sprintf(ioBfr, "%s %02d, 20%02d  %02d:%02d%s",
        monStr[timeBuffer.month-1], timeBuffer.dayMonth, timeBuffer.year, hourDisplayed, timeBuffer.minute, amPm ? "am" : "pm" );
    lcdStrAt(4, 2, ioBfr);
    if (field == 0) { lcdStrAt(3, 116, "  "); lcdStrAt(3, 2, "^^^"); }
    else if (field == 1) { lcdStrAt(3, 2, "   "); lcdStrAt(3, 26, "^^"); }
    else if (field == 2) { lcdStrAt(3, 26, "  "); lcdStrAt(3, 50, "^^^^"); }
    else if (field == 3) { lcdStrAt(3, 50, "    "); lcdStrAt(3, 86, "^^"); }
    else if (field == 4) { lcdStrAt(3, 86, "  "); lcdStrAt(3, 104, "^^"); }
    else { field = 5; lcdStrAt(3, 104, "  "); lcdStrAt(3, 116, "^^"); }

    if (keyIn & KEY_A)
    {
        switch(field)
        {
            case 0:
                if (timeBuffer.month < 12) timeBuffer.month++;
                else timeBuffer.month = 1;
                break;
            case 1:
                if (timeBuffer.dayMonth < daysInMonth(timeBuffer.month, timeBuffer.year)) timeBuffer.dayMonth++;       // TODO Do reality check final date
                else timeBuffer.dayMonth = 1;
                break;
            case 2:
                if (timeBuffer.year < 99) timeBuffer.year++;
                else timeBuffer.year = 0;
                break;
            case 3:
                if (timeBuffer.hour < 23) timeBuffer.hour++;
                else timeBuffer.hour = 0;
                break;
            case 4:
                if (timeBuffer.minute < 59) timeBuffer.minute++;
                else timeBuffer.minute = 0;
                break;
            case 5:
                if (amPm) timeBuffer.hour += 12;
                else timeBuffer.hour -= 12;
                break;
        }
    }
    else if (keyIn & KEY_B)
    {
        switch(field)
        {
            case 0:
                if (timeBuffer.month > 1) timeBuffer.month--;
                else timeBuffer.month = 12;
                break;
            case 1:
                 if (timeBuffer.dayMonth > 1) timeBuffer.dayMonth--;
                else timeBuffer.dayMonth = daysInMonth(timeBuffer.month, timeBuffer.year);
                break;
            case 2:
                if (timeBuffer.year > 0) timeBuffer.year--;
                else timeBuffer.year = 99;
                break;
            case 3:
                if (timeBuffer.hour > 0) timeBuffer.hour--;
                else timeBuffer.hour = 23;
                break;
            case 4:
                if (timeBuffer.minute > 0) timeBuffer.minute--;
                else timeBuffer.minute = 59;
                break;
            case 5:
                if (amPm) timeBuffer.hour += 12;
                else timeBuffer.hour -= 12;
                break;
        }
    }
    else if (keyIn & KEY_C)
    {
        if (amPm && timeBuffer.hour > 11) timeBuffer.hour -= 12;
        else if (!amPm && timeBuffer.hour < 12) timeBuffer.hour += 12;
        time = timeBuffer;       
        timeWrite();
        oP.sysStat |= ST_SHOW_TIME;
        //if (smemWriteTime() == 0) sysFault |= FLT_SMEM;
        if (field < 5) field++;
        else field = 0;
    }
    else if (keyIn & KEY_D)     
    {
        windowState = WIN_SETUP1;
        field = 0;
    }
}

    // ------------------
void windowComfortZone(char keyIn)
{   
    lcdCenter(7, "Comfort Zone");
    lcdSoftkeys1("Temp", "Hum", "Enter", "Esc");
    strcpy(ioBfr, "Temperature: ");
    if (oP.sysStat & ST_USE_CELSIUS) sprintf(bfr2, "%2.0f[C", temperF2C(oP.setPointTempF));
    else sprintf(bfr2, "%2.0f[F", oP.setPointTempF);
    strcat(ioBfr, bfr2);
    if (field == 0) strcat(ioBfr, " <");
    else strcat(ioBfr, "    ");
    lcdStrAt(4, 4, ioBfr);

//    strcpy(ioBfr, "Rel Humidity: ");
//    sprintf(bfr2, "%2.0f%%", oP.setPointRelHum);
//    strcat(ioBfr, bfr2);
//    if (field == 1) strcat(ioBfr, " <");
//    else strcat(ioBfr, "    ");
//    lcdStrAt(3, 4, ioBfr);

    if (oP.setPointRelHum < QUAL_HUM_DRY_MIN) fuzzyRHidx = 0;         // Very dry
    else if (oP.setPointRelHum < QUAL_HUM_EPA_MIN) fuzzyRHidx = 1;    // Somewhat dry
    else if (oP.setPointRelHum < QUAL_HUM_EPA_MAX) fuzzyRHidx = 2;    // EPA recommended
    else if (oP.setPointRelHum < QUAL_HUM_DAMP_MAX) fuzzyRHidx = 3;   // Somewhat damp
    else fuzzyRHidx = 4;                                              // Very damp

    strcpy(ioBfr, "Hum: ");
    strcat(ioBfr, adjectiveRH[fuzzyRHidx]);
    if (field == 1) strcat(ioBfr, " <");
    else strcat(ioBfr, "    ");
    lcdStrAt(3, 4, ioBfr);

    if (keyIn & KEY_A) field = 0;
    else if (keyIn & KEY_B) field = 1;

    else if (keyIn & KEY_C)
    {
        if (field == 0) windowState = WIN_CZ_SET_TEMP;
        else windowState = WIN_CZ_SET_RH;
        field = 0;
    }

    else if (keyIn & KEY_D)
    {
        field = 0;
        windowState = WIN_SETUP1;
    }
}

 // ------------------
void windowCZTemper(char keyIn)
{
    lcdCenter(7, "Comfort Zone");
    lcdUpDnEnEs();
    strcpy(ioBfr, "Temperature: ");
    if (oP.sysStat & ST_USE_CELSIUS) sprintf(bfr2, "%2.01f[C", temperF2C(oP.setPointTempF));
    else sprintf(bfr2, "%2.0f[F", oP.setPointTempF);
    strcat(ioBfr, bfr2);
    if (field == 0) strcat(ioBfr, " <");
    else strcat(ioBfr, "    ");
    lcdStrAt(4, 4, ioBfr);

    if (keyIn & KEY_A)
    {
        if (oP.setPointTempF < SET_TEMP_MAX) oP.setPointTempF += 1;
        else oP.setPointTempF = SET_TEMP_MIN;
    }
    else if (keyIn & KEY_B)
    {
        if (oP.setPointTempF > SET_TEMP_MIN) oP.setPointTempF -= 1;
        else oP.setPointTempF = SET_TEMP_MAX;
    }
    else if (keyIn & KEY_C)
    {
       if (smemWriteParms(SMEM_SILENT) <= 0) sysFault |= FLT_SMEM;
       windowState =  WIN_CZ_SET_RH;
    }
    else if (keyIn & KEY_D)
    {
        field = 0;
        windowState = WIN_COMFORT_BASE;
    }
}

// // ------------------
//void windowCZRHumid(char keyIn)
//{
//    lcdCenter(7, "Comfort Zone");
//    lcdUpDnEnEs();
//    strcpy(ioBfr, "Rel Humid: ");
//    sprintf(bfr2, "%2.0f%%", oP.setPointRelHum);
//    strcat(ioBfr, bfr2);
//    if (field == 0) strcat(ioBfr, " <");
//    else strcat(ioBfr, "    ");
//    lcdStrAt(4, 4, ioBfr);
//
//    if (keyIn & KEY_A)
//    {
//        if (oP.setPointRelHum < SET_RH_MAX) oP.setPointRelHum += 1;
//        else oP.setPointRelHum = SET_RH_MIN;
//    }
//    else if (keyIn & KEY_B)
//    {
//        if (oP.setPointRelHum > SET_RH_MIN) oP.setPointRelHum -= 1;
//        else oP.setPointRelHum = SET_RH_MAX;
//    }
//    else if (keyIn & (KEY_C | KEY_D))
//    {
//        if (smemWriteParms(SMEM_SILENT) <= 0) sysFault |= FLT_SMEM;
//        field = 0;
//        windowState = WIN_COMFORT_BASE;
//    }
//}

    // ------------------
void windowCZRHumid(char keyIn)
{  
    lcdCenter(7, "Comfort Zone");
    lcdCenter(5, "I want my relative");
    lcdCenter(4, "humidity to be:");
    lcdUpDnEnEs();

    sprintf(ioBfr, "> %s <", adjectiveRH[fuzzyRHidx]);        // fuzzyRHidx has already been inited by windowComfortZone()
    lcdCenter(3, ioBfr);
  
    if (keyIn & KEY_A)
    {
        lcdClearPage(3);
        if (fuzzyRHidx < 4) fuzzyRHidx++;
        else fuzzyRHidx = 0;
    }
    else if (keyIn & KEY_B)
    {
        lcdClearPage(3);
        if (fuzzyRHidx > 0) fuzzyRHidx--;
        else fuzzyRHidx = 4;
    }
    else if (keyIn & KEY_C)
    {
        if (fuzzyRHidx == 0) oP.setPointRelHum = QUAL_HUM_DRY_MIN - 5;
        else if (fuzzyRHidx == 1) oP.setPointRelHum = QUAL_HUM_EPA_MIN - 5;
        else if (fuzzyRHidx == 2) oP.setPointRelHum = QUAL_HUM_EPA_MIN + 5;
        else if (fuzzyRHidx == 3) oP.setPointRelHum = QUAL_HUM_EPA_MAX + 5;
        else oP.setPointRelHum = QUAL_HUM_DAMP_MAX + 5;
        if (smemWriteParms(SMEM_SILENT) <= 0) sysFault |= FLT_SMEM;
        field = 0;
        windowState = WIN_COMFORT_BASE;
    }
    else if (keyIn & KEY_D)
    {
        field = 0;
        windowState = WIN_COMFORT_BASE;
    }
}

    // --------------------------
    // child is 1-idx'd, 0 used for parent
void windowReviewModules(char keyIn)
{
    byte extraSensors = 0;
    sprintf(ioBfr, "Module %d", childFocus);
    if (child[childFocus - 1].status & CHILD_ST_NODE_ACTIVE) strcat(ioBfr, "     ACTIVE");
    else if (child[childFocus - 1].status & CHILD_ST_NODE_DEFINED) strcat(ioBfr, "   INACTIVE");
    lcdCenter(7, ioBfr);
    lcdUpDnEnEs();

    sprintf(ioBfr, "Relay 1: %s", resourceQuery(childFocus, 'A'));
    lcdStrAt(6, 4, ioBfr);

    sprintf(ioBfr, "Relay 2: %s", resourceQuery(childFocus, 'B'));
    lcdStrAt(5, 4, ioBfr);

    strcpy(ioBfr, "InternalFan: ");
    if (strcmp(resourceQuery(childFocus, 'C'), "IntFan") == 0) { extraSensors++; strcat(ioBfr, "#1 "); }
    if (strcmp(resourceQuery(childFocus, 'D'), "IntFan") == 0) { extraSensors++; strcat(ioBfr, "#2 "); }
    if (extraSensors == 0) strcat(ioBfr, "None");
    lcdStrAt(4, 4, ioBfr);

    extraSensors = 0;
    strcpy(ioBfr, "+ ");
    if (child[childFocus - 1].status & CHILD_ST_HAVE_FLOODSEN) { extraSensors++; strcat(ioBfr, "H2OSpy "); }
    if (child[childFocus - 1].status & CHILD_ST_EXTERNAL_TH)
    {
       // if (extraSensors) strcat(ioBfr, " + ");
        extraSensors++;
        strcat(ioBfr, "ExtTH ");
    }
    if (child[childFocus - 1].status & CHILD_ST_HAVE_TEMP2) { extraSensors++; strcat(ioBfr, "BSpy"); }
    if (extraSensors > 0) lcdStrAt(3, 4, ioBfr);

    if (keyIn & KEY_A)
    {
        if (childFocus < NUM_CHILDREN) childFocus++; else childFocus = 1;
        lcdScreen(mainScreen);
    }
    else if (keyIn & KEY_B)
    {
        if (childFocus > 1) childFocus--; else childFocus = NUM_CHILDREN;
        lcdScreen(mainScreen);     
    }
    else if (keyIn & KEY_C)
    {
        windowState = WIN_RES_SET_MODULE;       // Will use childFocus
        field = 0;
    }
    else if (keyIn & KEY_D)     
    {
        windowState = WIN_RESOURCES_BASE;
        field = 0;
    }
}

void windowModifyModules(char keyIn)
{
    sprintf(ioBfr, "Modify Module %d", childFocus);
    lcdCenter(7, ioBfr);
    lcdUpDnEnEs();
    
    if (field == 0) sprintf(ioBfr, "Enabled: %s", child[childFocus - 1].status & CHILD_ST_NODE_DEFINED ? "Yes" : "No");
    else if (field == 1) sprintf(ioBfr, "Relay 1: %s", resourceQuery(childFocus, 'A'));
    else if (field == 2) sprintf(ioBfr, "Relay 2: %s", resourceQuery(childFocus, 'B'));
    else if (field == 3) sprintf(ioBfr, "IntFan1: %s", resourceQuery(childFocus, 'C'));
    else if (field == 4) sprintf(ioBfr, "IntFan2: %s", resourceQuery(childFocus, 'D'));
    else if (field == 5) sprintf(ioBfr, "H2OSpy: %s", child[childFocus - 1].status & CHILD_ST_HAVE_FLOODSEN ? "Yes" : "No");
    else if (field == 6) sprintf(ioBfr, "Exterior T&H: %s", child[childFocus - 1].status & CHILD_ST_EXTERNAL_TH ? "Yes" : "No");
    else if (field == 7) sprintf(ioBfr, "Secondary T: %s", child[childFocus - 1].status & CHILD_ST_HAVE_TEMP2 ? "Yes" : "No");
    lcdStrAt(5, 4, ioBfr);

    if (keyIn & KEY_A)
    {
        lcdClearPage(5);
        if (field == 0)
        {
            child[childFocus - 1].status ^= CHILD_ST_NODE_DEFINED;
            child[childFocus - 1].miaCtr = 0;
        }
        else if (field == 1) child[childFocus - 1].relay1 = listResource(child[childFocus - 1].relay1, RES_LIST_UP);       // Choose next from list
        else if (field == 2) child[childFocus - 1].relay2 = listResource(child[childFocus - 1].relay2, RES_LIST_UP);
        else if (field == 3) 
        {
            if (child[childFocus - 1].swiPwr1 == InternalFan) child[childFocus - 1].swiPwr1 = Free;
            else child[childFocus - 1].swiPwr1 = InternalFan;
        }
        else if (field == 4)
        {
            if (child[childFocus - 1].swiPwr2 == InternalFan) child[childFocus - 1].swiPwr2 = Free;
            else child[childFocus - 1].swiPwr2 = InternalFan;
        }
        else if (field == 5) child[childFocus - 1].status ^= CHILD_ST_HAVE_FLOODSEN;
        else if (field == 6) child[childFocus - 1].status ^= CHILD_ST_EXTERNAL_TH;
        else if (field == 7) child[childFocus - 1].status ^= CHILD_ST_HAVE_TEMP2;
    }

    else if (keyIn & KEY_B)
    {
        lcdClearPage(5);
        if (field == 0) child[childFocus - 1].status ^= CHILD_ST_NODE_DEFINED;
        else if (field == 1) child[childFocus - 1].relay1 = listResource(child[childFocus - 1].relay1, RES_LIST_DOWN);     // Choose previous from list
        else if (field == 2) child[childFocus - 1].relay2 = listResource(child[childFocus - 1].relay2, RES_LIST_DOWN);
        else if (field == 3)
        {
            if (child[childFocus - 1].swiPwr1 == InternalFan) child[childFocus - 1].swiPwr1 = Free;
            else child[childFocus - 1].swiPwr1 = InternalFan;
        }
        else if (field == 4)
        {
            if (child[childFocus - 1].swiPwr2 == InternalFan) child[childFocus - 1].swiPwr2 = Free;
            else child[childFocus - 1].swiPwr2 = InternalFan;
        }
        else if (field == 5) child[childFocus - 1].status ^= CHILD_ST_HAVE_FLOODSEN;
        else if (field == 6) child[childFocus - 1].status ^= CHILD_ST_EXTERNAL_TH;
        else if (field == 7) child[childFocus - 1].status ^= CHILD_ST_HAVE_TEMP2;
    }

    else if (keyIn & KEY_C)
    {
        if (smemWriteParms(SMEM_SILENT) <= 0) sysFault |= FLT_SMEM;
        lcdClearPage(5);
        if (field < 7) field++;        // Ie, we did relay 1, now do 2, then the extras
        else field = 0;
    }
    else if (keyIn & KEY_D)     
    {
        field = 0;
        windowState = WIN_RES_MODULES;
    }
}

 // ------------------
void windowMeasUnits(char keyIn)
{   
    lcdCenter(7, "Temperature Unit");
    lcdSoftkeys1(" [F", " [C", "Enter", "");

    lcdClearPage(4);
    strcpy(ioBfr, "Use ");
    if (oP.sysStat & ST_USE_CELSIUS) strcat(ioBfr, "Celsius ");
    else strcat (ioBfr, "Fahrenheit");
    lcdStrAt(4, 4, ioBfr);

//    strcpy(ioBfr, "Humid: ");
////    if (oP.sysStat & ST_USE_DEWPOINT) strcat(ioBfr, "Dew point");
////    else
//    strcat (ioBfr, "Rel Humid");
//    if (field == 1) strcat(ioBfr, " <");
//    else strcat(ioBfr, "    ");
//    lcdStrAt(3, 4, ioBfr);

    if (keyIn & KEY_A)
    {
       oP.sysStat &= ~ST_USE_CELSIUS;
    }
    else if (keyIn & KEY_B)
    {
        oP.sysStat |= ST_USE_CELSIUS;
    }
    else if (keyIn & KEY_C)
    {
        field = 0;
        if (smemWriteParms(SMEM_SILENT) <= 0) sysFault |= FLT_SMEM;
        windowState = WIN_SETUP1;
    }
}

    // --------------------------
void windowSysInfo(char keyIn)
{
    extern char verStr[11];
    extern char uniqueID[UID_SIZE];
    extern char pnetVer;
    extern char siteID[SID_SIZE];       
    extern double vRef, rhumidNow;
    extern double iSen[NUM_CHILDREN];
    byte ch;
  
    sprintf(ioBfr, "System Info #%d", field + 1);
    lcdCenter(7, ioBfr);
    lcdUpDnEnEs();

    if (field == 0)
    {
        sprintf(ioBfr, "Ver: %s%c", verStr, pnetVer);
        lcdStrAt(6, 4, ioBfr);
        sprintf(ioBfr, "UID: %s", uniqueID);
        lcdStrAt(5, 4, ioBfr);
        sprintf(ioBfr, "SID: %s", siteID);
        lcdStrAt(4, 4, ioBfr);
        sprintf(ioBfr, "SysStat: %02X", oP.sysStat);
        lcdStrAt(3, 4, ioBfr);
    }
    else if (field == 1)
    {
        sprintf(ioBfr, "Sys Fault: %04X", sysFault);
        lcdStrAt(6, 4, ioBfr);
        sprintf(ioBfr, "Int RH: %3.01f%% :%3.01f", rhumidNow, oP.setPointRelHum);
        lcdStrAt(5, 4, ioBfr);
        sprintf(ioBfr, "Tcal factor: %5.04f", oP.temperCalFactor);
        lcdStrAt(4, 4, ioBfr);
        sprintf(ioBfr, "Hcal factor: %5.04f", oP.rhumidCalFactor);
        lcdStrAt(3, 4, ioBfr);
    }
    else if (field == 2)
    {
        sprintf(ioBfr, "DL records: %d", oP.dlRecordsSaved);
        lcdStrAt(6, 4, ioBfr);
        sprintf(ioBfr, "vRef: %4.03fV", vRef);
        lcdStrAt(5, 4, ioBfr);
        sprintf(ioBfr, "tolCZte: %2.01f", oP.toleranceCZtemperF);
        lcdStrAt(4, 4, ioBfr);
        sprintf(ioBfr, "tolCZhu: %2.01f", oP.toleranceCZrhumid);
        lcdStrAt(3, 4, ioBfr);
    }
    else if (field == 3)
    {
        for (ch = 0; ch < NUM_CHILDREN; ch++) adcCurrents(ch, ADC_SILENT);
        sprintf(ioBfr, "Ch1:%3.01f 2:%3.01f mA ", iSen[0], iSen[1]);
        lcdStrAt(6, 4, ioBfr);
        sprintf(ioBfr, "Ch3:%3.01f 4:%3.01f mA ", iSen[2], iSen[3]);
        lcdStrAt(5, 4, ioBfr);
        sprintf(ioBfr, "Ch5:%3.01f 6:%3.01f mA ", iSen[4], iSen[5]);
        lcdStrAt(4, 4, ioBfr);
        sprintf(ioBfr, "Ch7:%3.01f 8:%3.01f mA ", iSen[6], iSen[7]);
        lcdStrAt(3, 4, ioBfr);
    }
    else if (field == 4)
    {
        sprintf(ioBfr, "K1: ");           
        sprintf(bfr2, "%s", resourceQuery(0, 'A'));
        strcat(ioBfr, bfr2);
        if (RELAY1) strcat(ioBfr, " +"); else strcat(ioBfr, " -");
        lcdStrAt(6, 4, ioBfr);
        sprintf(ioBfr, "K2: ");        
        sprintf(bfr2, "%s", resourceQuery(0, 'B'));
        strcat(ioBfr, bfr2);
        if (RELAY2) strcat(ioBfr, " +"); else strcat(ioBfr, " -");
        lcdStrAt(5, 4, ioBfr);
    }
    else if (field == 5)
    {
        lcdCenter(6, "Discomfort Ratios");
        sprintf(ioBfr, "Data: %d", resourceEvaluate.dayTotalCounter);
        lcdStrAt(3, 4, ioBfr);
        if (resourceEvaluate.dayTotalCounter > 0)
        {            
            sprintf(ioBfr, "Hot: %3.02f Cold: %3.02f",
                (float) resourceEvaluate.tooHotTimer / resourceEvaluate.dayTotalCounter, (float) resourceEvaluate.tooColdTimer / resourceEvaluate.dayTotalCounter);
            lcdStrAt(5, 4, ioBfr);
            sprintf(ioBfr, "Dry: %3.02f Damp: %3.02f",
                (float) resourceEvaluate.tooDryTimer / resourceEvaluate.dayTotalCounter, (float) resourceEvaluate.tooDampTimer / resourceEvaluate.dayTotalCounter);
            lcdStrAt(4, 4, ioBfr);
        }
        else lcdCenter(4, "Need more data");
    }
    else if (field == 6)
    {        
        if (child[0].status & CHILD_ST_NODE_ACTIVE)
        {
            sprintf(ioBfr, "Ch1: %2.1foF %2.1f%%RH", child[0].localTempF, child[0].localRHumid);
            lcdStrAt(6, 4, ioBfr);
        }
        if (child[1].status & CHILD_ST_NODE_ACTIVE)
        {
            sprintf(ioBfr, "Ch2: %2.1foF %2.1f%%RH", child[1].localTempF, child[1].localRHumid);
            lcdStrAt(5, 4, ioBfr);
        }
        if (child[2].status & CHILD_ST_NODE_ACTIVE)
        {
            sprintf(ioBfr, "Ch3: %2.1foF %2.1f%%RH", child[2].localTempF, child[2].localRHumid);
            lcdStrAt(4, 4, ioBfr);
        }
        if (child[3].status & CHILD_ST_NODE_ACTIVE)
        {
            sprintf(ioBfr, "Ch4: %2.1foF %2.1f%%RH", child[3].localTempF, child[3].localRHumid);
            lcdStrAt(3, 4, ioBfr);
        }
    }
    else if (field == 7)
    {
        if (child[4].status & CHILD_ST_NODE_ACTIVE)
        {
            sprintf(ioBfr, "Ch5: %2.1foF %2.1f%%RH", child[4].localTempF, child[4].localRHumid);
            lcdStrAt(5, 4, ioBfr);
        }
        if (child[5].status & CHILD_ST_NODE_ACTIVE)
        {
            sprintf(ioBfr, "Ch6: %2.1foF %2.1f%%RH", child[5].localTempF, child[5].localRHumid);
            lcdStrAt(4, 4, ioBfr);
        }
        if (child[6].status & CHILD_ST_NODE_ACTIVE)
        {
            sprintf(ioBfr, "Ch7: %2.1foF %2.1f%%RH", child[6].localTempF, child[6].localRHumid);
            lcdStrAt(3, 4, ioBfr);
        }
        if (child[7].status & CHILD_ST_NODE_ACTIVE)
        {
            sprintf(ioBfr, "Ch8: %2.1foF %2.1f%%RH", child[7].localTempF, child[7].localRHumid);
            lcdStrAt(2, 4, ioBfr);
        }
    }
    else if (field == 8)
    {
        lcdCenter(6, "Sensor secondary T");          // Crude.  Assumes only one.
        for (ch = 0; ch < NUM_CHILDREN; ch++)
        {
            if (child[ch].status & CHILD_ST_HAVE_TEMP2)
            {
                sprintf(ioBfr, "#%d: %2.1foF", ch + 1, child[ch].secondaryTempF);
                lcdStrAt(5, 4, ioBfr);
                break;
            }
        }        
    }

    if (keyIn & KEY_A) { if (field < 8) field++; else field = 0; lcdScreen(mainScreen); }
    else if (keyIn & KEY_B) {if (field > 0) field--; else field = 8; lcdScreen(mainScreen); }
    else if (keyIn & KEY_D) { field = 0; windowState = WIN_SETUP2; }
}

    // ---------------
 void windowDefaults(char keyIn)
 {
    lcdCenter(7, "Restore Defaults");
    lcdUpDnEnEs();
    strcpy(ioBfr, "Factory?");
    if (field == 0) strcat(ioBfr, " <");
    else strcat(ioBfr, "    ");
    lcdStrAt(4, 4, ioBfr);

    strcpy(ioBfr, "Last save?");
    if (field == 1) strcat(ioBfr, " <");
    else strcat(ioBfr, "    ");
    lcdStrAt(3, 4, ioBfr);

    if (keyIn & (KEY_A | KEY_B))
    {
        if (field == 0) field = 1; else field = 0;
    }

    else if (keyIn & KEY_C)
    {
        windowState = WIN_EXECUTE_DEFAULTS;     // Keep field as a reference
    }

    else if (keyIn & KEY_D)
    {
        field = 0;
        windowState = WIN_SETUP3;
    }
}


    // ------------------
    // This is an experiment in reverse fielding
 void windowSetWiFi(char keyIn)
{
    static char keyInOld = 0xFF;

    //windowTimeout = TIMEOUT_T;    // DEB
    if (keyIn == keyInOld) return;
    keyInOld = keyIn;
    lcdCenter(7, "Setup WiFi");
    lcdUpDnEnEs();
    sprintf(ioBfr, "IP:   %02X.%02X.%02X.%02X", wifiIP[3], wifiIP[2], wifiIP[1], wifiIP[0]);
    lcdStrAt(6, 4, ioBfr);
    sprintf(ioBfr, "Mask: %02X.%02X.%02X.%02X", wifiMask[3], wifiMask[2], wifiMask[1], wifiMask[0]);
    lcdStrAt(5, 4, ioBfr);
    sprintf(ioBfr, "DHCP: %s", wifiDHPC ? "Yes" : "No ");
    lcdStrAt(4, 4, ioBfr);

    if (keyIn & KEY_D)
    {
        windowState = WIN_SETUP2;
        field = 0;
        return;
    }
    else if (keyIn & KEY_C)
    {
        if (field < 8) field++;  else field = 0;
    }

    switch(field)
    {
        case 0:
            if (keyIn == KEY_A) wifiIP[3]++;
            else if (keyIn == KEY_B) wifiIP[3]--;                
            sprintf(ioBfr, "%02X", wifiIP[3]);
            lcdReverseStrAt(6, 40, ioBfr);
            break;

        case 1:
            if (keyIn == KEY_A) wifiIP[2]++;
            else if (keyIn == KEY_B) wifiIP[2]--;
            sprintf(ioBfr, "%02X", wifiIP[2]);
            lcdReverseStrAt(6, 58, ioBfr);
            break;

        case 2:
           if (keyIn == KEY_A) wifiIP[1]++;
            else if (keyIn == KEY_B) wifiIP[1]--;
            sprintf(ioBfr, "%02X", wifiIP[1]);
            lcdReverseStrAt(6, 76, ioBfr);
            break;

        case 3:
           if (keyIn == KEY_A) wifiIP[0]++;
            else if (keyIn == KEY_B) wifiIP[0]--;
            sprintf(ioBfr, "%02X", wifiIP[0]);
            lcdReverseStrAt(6, 94, ioBfr);
            break;

        case 4:
            if (keyIn == KEY_A) wifiMask[3]++;
            else if (keyIn == KEY_B) wifiMask[3]--;
            sprintf(ioBfr, "%02X", wifiMask[3]);
            lcdReverseStrAt(5, 40, ioBfr);
            break;

        case 5:
            if (keyIn == KEY_A) wifiMask[2]++;
            else if (keyIn == KEY_B) wifiMask[2]--;
            sprintf(ioBfr, "%02X", wifiMask[2]);
            lcdReverseStrAt(5, 58, ioBfr);
            break;

        case 6:
            if (keyIn == KEY_A) wifiMask[1]++;
            else if (keyIn == KEY_B) wifiMask[1]--;
            sprintf(ioBfr, "%02X", wifiMask[1]);
            lcdReverseStrAt(5, 76, ioBfr);
            break;

        case 7:
            if (keyIn == KEY_A) wifiMask[0]++;
            else if (keyIn == KEY_B) wifiMask[0]--;
            sprintf(ioBfr, "%02X", wifiMask[0]);
            lcdReverseStrAt(5, 94, ioBfr);
            break;

        case 8:
            if (keyIn == KEY_A) wifiDHPC ^= true;
            else if (keyIn == KEY_B) wifiDHPC ^= true;
            sprintf(ioBfr, "%s", wifiDHPC ? "Yes" : "No ");
            lcdReverseStrAt(4, 40, ioBfr);
            break;
    }  
}

void windowCalibrate(char keyIn)
{
    lcdCenter(7, "Calibra. Constants");
    lcdUpDnEnEs();
    strcpy(ioBfr, "Temperature: ");
    sprintf(bfr2, "%3.02f", oP.temperCalFactor);
    strcat(ioBfr, bfr2);
    if (field == 0) strcat(ioBfr, " <");
    else strcat(ioBfr, "    ");
    lcdStrAt(5, 4, ioBfr);

    strcpy(ioBfr, "Humidity: ");
    sprintf(bfr2, "%3.02f", oP.rhumidCalFactor);
    strcat(ioBfr, bfr2);
    if (field == 1) strcat(ioBfr, " <");
    else strcat(ioBfr, "    ");
    lcdStrAt(4, 4, ioBfr);

    if (keyIn & KEY_A)
    {
        if (field == 0)
        {
            if (oP.temperCalFactor < CAL_FACTOR_MAX) oP.temperCalFactor += 0.01;
            else oP.temperCalFactor = CAL_FACTOR_MIN;
        }
        else
        {
            if (oP.rhumidCalFactor < CAL_FACTOR_MAX) oP.rhumidCalFactor += 0.01;
            else oP.rhumidCalFactor = CAL_FACTOR_MIN;
        }
    }
    else if (keyIn & KEY_B)
    {
        if (field == 0)
        {
            if (oP.temperCalFactor > CAL_FACTOR_MIN) oP.temperCalFactor -= 0.01;
            else oP.temperCalFactor = CAL_FACTOR_MAX;
        }
        else
        {
            if (oP.rhumidCalFactor > CAL_FACTOR_MIN) oP.rhumidCalFactor -= 0.01;
            else oP.rhumidCalFactor = CAL_FACTOR_MAX;
        }
    }
    else if (keyIn & KEY_C)
    {
        if (field < 1) field++; 
        else
        {
            sysFault &= ~FLT_SENSORSNOTCALIB;
            if (smemWriteParms(SMEM_SILENT) <= 0) sysFault |= FLT_SMEM;
            field = 0;
        }

    }
    else if (keyIn & KEY_D)
    {
        field = 0;
        windowState = WIN_SETUP3;
    }
}

    // ------------------------------
void windowDatalog(char keyIn)
{
    lcdCenter(7, "Datalog Control");

    sprintf(ioBfr, "State: %s", (oP.sysStat & ST_DATALOG_ON) ? "ON" : "OFF");
    lcdStrAt(5, 4, ioBfr);

    sprintf(ioBfr, "Records: %d", oP.dlRecordsSaved);
    lcdStrAt(4, 4, ioBfr);

    sprintf(ioBfr, "Period: %d min", DATALOG_PERIOD_MINS);
    lcdStrAt(3, 4, ioBfr);

    lcdSoftkeys1("Start", "Stop", "Reset", "Esc");
    if (keyIn == KEY_A)
    {
        lcdClearPage(5);
        smemDatalog(SMEM_DL_RESET);     
        oP.sysStat |= ST_DATALOG_ON;
    }
    else if (keyIn == KEY_B)
    {
        lcdClearPage(5);
        oP.sysStat &= ~ST_DATALOG_ON;
    }
    else if (keyIn == KEY_C)
    {
        lcdClearPage(4);
        smemDatalog(SMEM_DL_RESET);
    }
    else if (keyIn == KEY_D) windowState = WIN_SETUP3;
}

    // ----------------------------
 void windowSecurityStop(char keyIn)
 {
    static char keyBfr[8] = "-------";
    static bool firstTime = true;
    const char keySecurity[8] = "2134421";

    if (firstTime) { lcdScreen(mainScreen); firstTime = false; }

    lcdCenter(6, "Enter code or call");
    lcdCenter(5, "(631) 467-8686");
    lcdCenter(3, keyBfr);
    lcdSoftkeys1("  1", "  2", "  3", "  4");
    
    if (keyIn == KEY_A) keyBfr[field++] = '1';
    else if (keyIn == KEY_B) keyBfr[field++] = '2';
    else if (keyIn == KEY_C) keyBfr[field++] = '3';
    else if (keyIn == KEY_D) keyBfr[field++] = '4';

    if (strncmp(keySecurity, keyBfr, 7) == 0)
    {
        oP.sysStat &= ~ST_SECURITY_LAPSED;
        oP.sysStat |= ST_SECURITY_PASS;
        windowState = WIN_MAIN;
        field = 0;
        lcdScreen(mainScreen);
        return;
    }

    if (field >= strlen(keyBfr))
    {
        field = 0;
        strcpy(keyBfr, "-------");
        buzzerControl(BUZ_STATUSBAD, 8);
        return;
    }

  
 }