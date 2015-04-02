/* File:        DuraBlisCCSParent.c  Main source file
 * Author:      Michael L Anderson
 * Contact:     MichaelLAndersonEE@gmail.com
 * Platform:    PIC32MX350F256H on DB-CCS-Main board.
 * Created:     16 May 14
 * Timers:      T2 runs at Fclk = 25 MHz. 1 ct = 40 ns.
 * Interrupts:  T1 is accurate 1 ms timer
 */

#define _SUPPRESS_PLIB_WARNING
    // TODO May migrate to Harmony
#include <xc.h>
#include <plib.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "DuraBlisCCSParent.h"
#include "adc.h"
#include "buzzer.h"
#include "configs.h"
#include "edits.h"
// #include "iic_keypad.h"
#include "resources.h"
#include "serial.h"
#include "smem.h"
#include "time.h"
#include "windows.h"

#define KEYPAD_LOQUACIOUS   1     // opModes
#define KEYPAD_SILENT       2
#define SEC_INIT            1
#define SEC_LOOP            2

void currentMonitor(void);          // Exec loop process.  Check for overcurrents on Pnet lines.
double dewpointCalc(double temperC, double relHum);
void factoryHardInit(void);         // Force factory default vals to parms
void hardwareInit(void);            // Low level setup
void interruptsEnable(bool onOff);
byte keypadRead(byte);
void keypadTest(void);
void petTheDog(void);               // WDT
void presetConfig(void);            // Macros for test & debug.  Clear for release.
void rconMonitor(void);             // Debug only.
void reportInfo(void);              // Serial dump
void securityCheck(byte opMode);
void serialInParse(void);           // Passes Pnet traffic to resourceManager() & manages user IO
double temperF2C(double temperF);
void taskScheduler(void);           // Realtime processes
void __ISR(_TIMER_1_VECTOR, IPL7AUTO) T1_IntHandler(void);

extern struct t_struct time;        // Saved partially to SEEPROM Pg 0.
char verStr[11] = "1.040215";       // Major.Build date
char pnetVer = 'A';                 // Often appended to verStr
char uniqueID[UID_SIZE];        // Unique Identifier, 000000 - 999999, set in production
char siteID[SID_SIZE];          // Zip + 4
struct operatingParms_struct oP;        // Saved to SEEPROM Pg 1.
double iSenCalFactor [NUM_CHILDREN];    // SEEPROM Pg 2.  [64]
double vRef = 2.994;                    // Measured on prototype. Should not vary more than +/-2%
  
    // Public data, but not saved to SEEPROM
double temperNowF, rhumidNow;       // Note, these are logged to SEEPROM Pgs 4:204
double temperNowExteriorF, rhumidNowExterior;
double iSen[NUM_CHILDREN];
unsigned sysFault, sysFaultOld;
char ioBfr[IO_BFR_SIZE + 1];    // For LCD lines and serial IO

int main(void)
{
    extern byte splashScreen[];

    hardwareInit();
    factoryHardInit();  // Usable default software parm inits.  Can be overwritten by sRP.
       
    if (SWI_DEBUG == 0) oP.sysStat |= ST_DEBUG_MODE;
    if (oP.sysStat & ST_DEBUG_MODE) rconMonitor();      // Startup bits.

    // wifiInit();
    lcdInit();
    lcdOnOff(1);
    lcdScreen(splashScreen);
    buzzerControl(BUZ_KEY1, 8);
    if (oP.sysStat & ST_DEBUG_MODE) smemReadParms(SMEM_LOQUACIOUS);
    else smemReadParms(SMEM_SILENT);

    if (oP.sysStat & ST_DEBUG_MODE)
    {
        putStr("\n\n\r DB CCS Parent - Monitor\r\n Ver ");
        putStr(verStr);  
        putStr(", MLA\n\n\r"); 
        menuDisplay();      
    }
    presetConfig();
    buzzerControl(BUZ_KEY2, 6);
    securityCheck(SEC_INIT);
    interruptsEnable(true);
    rs485TransmitEna(false);        // Enable reception unless actively transmitting
    
    while(1)
    {
        timeRead(TIME_UPDATE);
        adcTH(ADC_SILENT);
        taskScheduler();            // Realtime processes
        currentMonitor();
        if (sysFault != sysFaultOld) 
        {
//            sprintf(ioBfr, "[%04X]", sysFault);    // DEB
//            putStr(ioBfr);
            buzzerControl(BUZ_STATUSBAD, 5);
        }
        sysFaultOld = sysFault;
        serialInParse();            // Check UART1 for user input super commands
        petTheDog();                // WDT

        if (SWI_DEBUG == 0) oP.sysStat |= ST_DEBUG_MODE;
        else  oP.sysStat &= ~ST_DEBUG_MODE;
        securityCheck(SEC_LOOP);
    };      // EO while (1)
}

void currentMonitor(void)
{
    static byte ch;
   
    adcCurrents(ch, ADC_SILENT);
    child[ch].status &= ~CHILD_ST_OVER_CURRENT;
    if (iSen[ch] > CHILD_FAULT_CURRENT) child[ch].status |= CHILD_ST_OVER_CURRENT;
    if (child[ch].status & CHILD_ST_OVER_CURRENT) sysFault |= FLT_OVER_CURRENT;
    if (++ch >= NUM_CHILDREN) ch = 0;
}
 
    // Returns dewpoint temper in oC, using Magnus Formula
    // Returns 0.0 on error, which is slightly misleading
    // relHum assumed normalized to 100.0
double dewpointCalc(double temperC, double relHum)
{
    const double b = 16.67;     // Dimensionless
    const double c = 243.5;     // oC
    const double epsilon = 0.1; // Dimensionless
    double gamma;

    if (temperC < (-1 * c) + epsilon) return(0.0);
    if (relHum < 0.0 || relHum > 100.0) return(0.0);
    gamma = log(relHum) + ((b * temperC) / (c + temperC));
    if (gamma > b + epsilon) return(0.0);
    return((c * gamma) / (b - gamma));
}

    // These are usable if not necessarily accurate values.  They
    // should be overwriiten by a successful read of smem.
void factoryHardInit(void)
{
    byte ch;
   
    // sysFault = FLT_SENSORSNOTCALIB;
    oP.initializer = 0x5A;      // Signifies that smem has been written to
    oP.sysStat = ST_SECURITY_PASS;      // For now, override security mechanism
    oP.dlRecordsSaved = 0;
    oP.rhumidCalFactor = 1.00;
    oP.temperCalFactor = 1.00;
    oP.setPointTempF = 70.0;
    oP.setPointRelHum = 45.0;
    oP.toleranceCZtemperF = 5.0;    // This defines the CZ window height & width
    oP.toleranceCZrhumid = 8.0;
    for (ch = 0; ch < NUM_CHILDREN; ch++)
    {
        child[ch].secondaryTempF = 68;  // These have an IIR. Flags a fall below 50 oF if enabled
        iSenCalFactor [ch] = 1.0;       // TODO back this to smem
    }

    strcpy(uniqueID, "012345");
    strcpy(siteID, "11779-7664");
}

void hardwareInit(void)
{
        // Configure the device for maximum performance, but do not change the PBDIV clock divisor.
        // Given the options, this function will change the program Flash wait states,
        // RAM wait state and enable prefetch cache, but will not change the PBDIV.
        // The PBDIV value is already set via the pragma FPBDIV option above.
    SYSTEMConfig(SYS_FREQ, SYS_CFG_WAIT_STATES | SYS_CFG_PCACHE);

    PORTSetPinsAnalogIn(IOPORT_B, BIT_9 | BIT_8 | BIT_7 | BIT_6 | BIT_5 | BIT_4 | BIT_1);
        // Could also use mPORTDSetPinsDigitalOut(BIT_6 | BIT_7);
    PORTSetPinsDigitalOut(IOPORT_B, BIT_15 | BIT_14 | BIT_13 | BIT_12 | BIT_10 | BIT_3 | BIT_2);
    PORTSetPinsDigitalIn(IOPORT_B, BIT_11);
   
    PORTSetPinsDigitalOut(IOPORT_D, BIT_11 | BIT_10 | BIT_9 | BIT_8 | BIT_7 | BIT_6 | BIT_5 | BIT_4 |
        BIT_3 | BIT_2 | BIT_1 | BIT_0);

    PORTSetPinsAnalogIn(IOPORT_E, BIT_7 | BIT_6 | BIT_5);
    PORTSetPinsDigitalOut(IOPORT_E, BIT_3 | BIT_1 | BIT_0);
    PORTSetPinsDigitalIn(IOPORT_E, BIT_4 | BIT_2 );

    PORTSetPinsDigitalOut(IOPORT_F, BIT_6 | BIT_3 | BIT_0);
    PORTSetPinsDigitalIn(IOPORT_F, BIT_5 | BIT_4 | BIT_2 | BIT_1);

    PORTSetPinsDigitalOut(IOPORT_G, BIT_9 | BIT_8 | BIT_6);
    PORTSetPinsDigitalIn(IOPORT_G, BIT_7 | BIT_3 | BIT_2);
    RELAY1 = RELAY2 = 0;
  
        // This is the waggle to set reprogrammable peripheral
        // Assume ints & DMA disa
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
    U1RXR = 0b1111;         // Selects RPF2 as RxD1 input
    RPF3R = 0b0011;         // Selects U1TX as TxD1 output
    U2RXR = 0b0100;         // Selects RPF1 as RxD2 input
    RPF0R = 0b0001;         // Selects RPF0 as TxD2 output
    RPF6R = 0b1100;         // Selects RPF6 as OC1 output, buzzer PWM
    SDI2R = 0b0001;         // Selects pin 5 as SDI2, RPG7 (PICO)
    RPG8R = 0b0110;         // Selects pin 6 as SDO2, RPG8 (POCI)
    RTCCONSET = 0b1000;     // Turn on RTCC.RTCWREN.
    SYSKEY = 0x33333333;    // Junk relocks it

    OSCCONbits.SOSCEN = 1;
    OSCCONbits.SOSCRDY = 1;
        // Serial port on UART1
        // Note, Mode & Sta have atomic bit clr, set, & inv registers
    U1MODEbits.ON = 1;      // Ena UART1, per UEN, with UTXEN
    U1MODEbits.SIDL = 0;    // Continue oper in idle mode
    U1MODEbits.IREN = 0;    // Disa IrDA
    U1MODEbits.RTSMD = 1;   // U1RTS_n pin in simplex mode
    U1MODEbits.UEN = 0b00;  // U1CTS_n & U1RTS_n unused
    U1MODEbits.WAKE = 0;    // Disa wakeup
    U1MODEbits.LPBACK = 0;  // Disa loopback
    U1MODEbits.ABAUD = 0;   // Disa autobaud
    U1MODEbits.RXINV = 0;    // U1Rx idle is 1.  
    U1MODEbits.BRGH = 0;    // Std speed 16x, 1: high speed is 4x baud clk
    U1MODEbits.PDSEL = 0b00;    // 8 bit data, no parity
    U1MODEbits.STSEL = 0;   // 1 stop bit

    U1STAbits.ADM_EN = 0;   // Disa automatic address mode detect
    U1STAbits.UTXISEL = 0b01;   // Interrupt gen when all chars transmitted
    U1STAbits.UTXINV = 0;   // U1Tx idle is 1.
    U1STAbits.URXEN = 1;    // Ena U1Rx
    U1STAbits.UTXBRK = 0;   // Disa Break (Start bit, then 12 0's, Stop)
    U1STAbits.UTXEN = 1;    // Ena U1Tx
    U1STAbits.URXISEL = 0b01;   // Interrupt flag asserted while buffer is 1/2 or more full
    U1STAbits.ADDEN = 0;    // Disa address mode

    U1BRG = 162;            // 80 for 19200 baud = PBCLK / (16 x (BRG + 1)), PBCLK = 25 MHz @ div / 1, Jitter 0.47%
                            // 162 for 9600 baud, 325 for 4800
    RS485_DE = 0;
    RS485_RE_n = 0;

         // UART2 Serial port
    U2MODEbits.ON = 1;      // Ena UART2, per UEN, with UTXEN
    U2MODEbits.SIDL = 0;    // Continue oper in idle mode
    U2MODEbits.IREN = 0;    // Disa IrDA
    U2MODEbits.RTSMD = 1;   // U1RTS_n pin in simplex mode
    U2MODEbits.UEN = 0b00;  // U1CTS_n & U1RTS_n unused
    U2MODEbits.WAKE = 0;    // Disa wakeup
    U2MODEbits.LPBACK = 0;  // Disa loopback
    U2MODEbits.ABAUD = 0;   // Disa autobaud
    U2MODEbits.RXINV = 0;   // U1Rx idle is 1
    U2MODEbits.BRGH = 0;    // Std speed 16x, 1: high speed is 4x baud clk
    U2MODEbits.PDSEL = 0b00;    // 8 bit data, no parity
    U2MODEbits.STSEL = 0;   // 1 stop bit

    U2STAbits.ADM_EN = 0;   // Disa automatic address mode detect
    U2STAbits.UTXISEL = 0b01;   // Interrupt gen when all chars transmitted
    U2STAbits.UTXINV = 0;   // U1Tx idle is 1
    U2STAbits.URXEN = 1;    // Ena U1Rx
    U2STAbits.UTXBRK = 0;   // Disa Break (Start bit, then 12 0's, Stop)
    U2STAbits.UTXEN = 1;    // Ena U1Tx
    U2STAbits.URXISEL = 0b01;   // Interrupt flag asserted while buffer is 1/2 or more full
    U2STAbits.ADDEN = 0;    // Disa address mode

    U2BRG = 162;             // 80 for 19200 baud = PBCLK / (16 x (BRG + 1)), PBCLK = 25 MHz @ div / 1, Jitter 0.47%
                             // 162 for 9600 baud, 325 for 4800

        //  Real time clock 
    RTCCONbits.ON = 1;
    RTCCONbits.RTCOE = 0;   // Disa RTCC output

    INTEnableSystemMultiVectoredInt();  // Enable system wide interrupt to multivectored mode.

        // T1 timer & interrupt are used as the fast realtime clock

    TMR1 = 0x0;
    PR1 = 0xBF6;                // This combo yields a 1 ms intrpt
    T1CONbits.TCKPS = 0b01;     // 1:8 prescale
    T1CONbits.TCS = 0;          // Internal
    IPC1SET = 0x0000000C; // Set priority level = 3
    IPC1SET = 0x00000001; // Set subpriority level = 1
        // Can be done in a single operation by assigning PC2SET = 0x0000000D
    IFS0CLR = 0x00000100; // Clear the timer interrupt status flag
    IFS0bits.T1IF = 0;
    IEC0SET = 0x00000100; // Enable timer interrupts
    IEC0bits.T1IE = 1;
    T1CONbits.ON = 1;           // Go

        // Buzzer PWM & ancillaries preliminary setup
        // buzzerControl() handles tones & durations
    T2CONSET = 0x8000;                  // Enable Timer2
    TMR2 = 1;                           // Clear register
    IFS0CLR = 0x00000100;               // Clear T2 int flag
  //  IEC0SET = 0x00000100;               // Ena T2 int
    IPC2SET = 0x0000001C;               // Set T2 int priority to 7
    
        // SPI2 used by SMEM
    SPI2CON = 0x8120;
    SPI2BRG = 20;         // ca 600 kHz clock frequency
    SPI2STATCLR = 0x40;     // Clear the Overflow
    SPI2CONbits.MODE16 = 0;
    SPI2CONbits.MODE32 = 0;     // 8 bit transfers
    SPI2CONbits.ON = 1;

    adcInit();        
}

void interruptsEnable(bool onOff)
{
    if (onOff) IEC0bits.T1IE = 1;
    else IEC0bits.T1IE = 0;
}

byte keypadRead(byte opMode)
{
    static byte keyImageOld;
    byte keyImage = 0, retVal = 0;

    if (STD_KEY4 == 0) keyImage |= 0x01;
    if (STD_KEY3 == 0) keyImage |= 0x02;
    if (STD_KEY2 == 0) keyImage |= 0x04;
    if (STD_KEY1 == 0) keyImage |= 0x08;

    if ((keyImageOld == 0) && (keyImage != keyImageOld))
    {
        if (opMode == KEYPAD_LOQUACIOUS) { sprintf(ioBfr, "[%02X]", keyImage); putStr(ioBfr); } 
        retVal = keyImage;
        if (keyImage & 0x01) buzzerControl(BUZ_KEY1, 1);
        if (keyImage & 0x02) buzzerControl(BUZ_KEY2, 1);
        if (keyImage & 0x04) buzzerControl(BUZ_KEY3, 1);
        if (keyImage & 0x08) buzzerControl(BUZ_KEY4, 1);
    }
    keyImageOld = keyImage;
    return(retVal);
}

    // Crude.  Times out in a few seconds.
void keypadTest(void)
{
    unsigned T;

    putStr("\tKeypad test: "); 
    for (T = 0; T < 5000; T++)
    {
        keypadRead(KEYPAD_LOQUACIOUS);
        delay_us(1000);
    }
}

    // ------------------
void petTheDog(void)
{
    WDTCONbits.WDTCLR = 1;      // Pet the dog
}

    // ----- Macro for debug ------
void presetConfig(void)
{  
    parent.relay1 = AirConditioner;
    parent.relay2 = Heater;
                                                                // Node needs to start ACTIVE
    child[0].status = CHILD_ST_NODE_DEFINED + CHILD_ST_NODE_ACTIVE;
    child[0].swiPwr1 = InternalFan;
 
    child[1].status = CHILD_ST_NODE_DEFINED + CHILD_ST_NODE_ACTIVE;
    child[1].swiPwr1 = InternalFan;

    child[2].status = CHILD_ST_NODE_DEFINED + CHILD_ST_NODE_ACTIVE + CHILD_ST_HAVE_FLOODSEN;

    child[3].status = CHILD_ST_NODE_DEFINED + CHILD_ST_NODE_ACTIVE;
    child[3].relay1 = DeHumidifier;

    child[4].status = CHILD_ST_NODE_DEFINED + CHILD_ST_NODE_ACTIVE + CHILD_ST_EXTERNAL_TH;
    child[4].relay1 = AirExchangerOut;
    child[4].relay2 = AirExchangerOut;

    child[5].status = CHILD_ST_NODE_DEFINED + CHILD_ST_NODE_ACTIVE;
    child[5].swiPwr1 = InternalFan;

    child[6].status = CHILD_ST_NODE_DEFINED + CHILD_ST_NODE_ACTIVE + CHILD_ST_HAVE_TEMP2;
   
}

    // These present multiply and not as expected from FRM.
void rconMonitor(void)
{
    putStr("\r\n\n");
    if (RCONbits.BOR) { RCONbits.BOR = 0; putStr("rcon BOR\n\r"); }     // Brown out
    if (RCONbits.CMR) putStr("rcon CMR\n\r");       // Configuration mismatch
    if (RCONbits.EXTR) putStr("rcon EXTR\n\r");     // ~MCLR
    if (RCONbits.VREGS) putStr("rcon VREGS\n\r");     // High V detect
    if (RCONbits.IDLE) putStr("rcon IDLE\n\r");     // Wake from Idle
    if (RCONbits.POR) { RCONbits.POR = 0; putStr("rcon POR\n\r"); }   // Power On Reset
    if (RCONbits.SLEEP) putStr("rcon SLEEP\n\r");     // Wake from Sleep
    if (RCONbits.SWR) putStr("rcon SWR\n\r");     // Software
    if (RCONbits.WDTO) putStr("rcon WDTO\r");     // Watchdog barked
    putStr("\r\n\n");
}

    // -----------------------------------
void reportInfo(void)
{
    byte ch;
            
    putStr("\n\r DuraBlis CCS Monitor");
    putStr("\n\r Firmware ver: "); 
    putStr(verStr);  
    putChar(pnetVer);  
    putStr("\n\r UID: "); 
    putStr(uniqueID);
    putStr("\t SID: ");
    putStr(siteID);

    sprintf(ioBfr, "\n\r Status: %02Xh, Fault: %04Xh", oP.sysStat, sysFault);
    putStr(ioBfr);  

    putStr("\n\r K: "); 
    if (RELAY1) putChar('+'); else putChar('-');
    if (RELAY2) putChar('+'); else putChar('-');

    timeRead(TIME_UPDATE);
    sprintf(ioBfr, "\n\r Time: %02d%s%02d-%02d:%02d:%02d.%03d\r\n",
        time.dayMonth, monStr[time.month - 1], time.year,
        time.hour, time.minute, time.second, time.msec);
    putStr(ioBfr); 

    sprintf(ioBfr, " DL records: %d\r\n", oP.dlRecordsSaved);
    putStr(ioBfr);  

    sprintf(ioBfr, " vRef: %4.03f V\r\n", vRef);
    putStr(ioBfr);

    sprintf(ioBfr, " tolCZhu: %2.01f\r\n", oP.toleranceCZrhumid);
    putStr(ioBfr);
    sprintf(ioBfr, " tolCZte: %2.01f\r\n", oP.toleranceCZtemperF);
    putStr(ioBfr);


    adcTH(ADC_SILENT);
    putStr(" Temp now: "); 
    if (oP.sysStat & ST_USE_CELSIUS) sprintf(ioBfr, "%2.01foC [%4.03f]", temperF2C(temperNowF), oP.temperCalFactor);
    else sprintf(ioBfr, "%2.01foF [%4.03f]", temperNowF, oP.temperCalFactor);
    putStr(ioBfr); 

    putStr("\n\r Humid now: ");  
//    if (oP.sysStat & ST_USE_DEWPOINT) sprintf(ioBfr, "%2.01foC [%4.03f]", dewpointCalc(temperF2C(temperNowF), rhumidNow), oP.rhumidCalFactor);
//    else
    sprintf(ioBfr, "%2.01f [%4.03f]",rhumidNow, oP.rhumidCalFactor);
    putStr(ioBfr);  

    putStr("\n\r Setpoint: "); 
    if (oP.sysStat & ST_USE_CELSIUS) sprintf(ioBfr, "%2.01foC, %2.1f %%RH", temperF2C(oP.setPointTempF), oP.setPointRelHum);
    else sprintf(ioBfr, "%2.1f oF, %2.1f %%RH", oP.setPointTempF, oP.setPointRelHum);
    putStr(ioBfr); 

    putStr("\n\r Exterior: ");  
    if (oP.sysStat & ST_USE_CELSIUS) sprintf(ioBfr, "%2.01foC, %2.1f %%RH", temperF2C(temperNowExteriorF), rhumidNowExterior);
    else sprintf(ioBfr, "%2.1f oF, %2.1f %%RH", temperNowExteriorF, rhumidNowExterior);
    putStr(ioBfr);  

    putStr("\n\n\r  Resources...\r\n");  
    putStr(" Parent ");  
    sprintf(ioBfr, "K1:%s, ", resourceQuery(0, 'A'));
    putStr(ioBfr); 
    sprintf(ioBfr, "K2:%s\n\r", resourceQuery(0, 'B'));
    putStr(ioBfr); 

    for (ch = 1; ch < NUM_CHILDREN; ch++)
    {
        if (child[ch - 1].status & CHILD_ST_NODE_DEFINED)
        {
            sprintf(ioBfr, " Ch%d K1:%s, ", ch, resourceQuery(ch, 'A'));
            putStr(ioBfr);
            sprintf(ioBfr, "K2:%s, ", resourceQuery(ch, 'B'));
            putStr(ioBfr);
            sprintf(ioBfr, "P1:%s, ", resourceQuery(ch, 'C'));
            putStr(ioBfr);
            sprintf(ioBfr, "P2:%s ", resourceQuery(ch, 'D'));
            putStr(ioBfr);
            if (child[ch - 1].status & CHILD_ST_HAVE_TEMP2)
            {
                sprintf(ioBfr, "\n\r T2 = %4.03f", child[ch - 1].secondaryTempF);
                putStr(ioBfr);
            }
            if (child[ch - 1].status & CHILD_ST_HAVE_FLOODSEN) putStr(" Flood");
            putStr("\n\r");
            petTheDog();
        }
    }

    putStr("\n\r iSens, mA:\r\n ");
    for (ch = 0; ch < NUM_CHILDREN; ch++)
    {
        adcCurrents(ch, ADC_SILENT);
        sprintf(ioBfr, " %2.1f", iSen[ch]);
        putStr(ioBfr);
        petTheDog();
    }
    putStr("\n\r");

    if (resourceEvaluate.dayTotalCounter > 0)
    {
        putStr("\n\r  Daily Discomfort Ratios:");
        sprintf(ioBfr, "\n\r Too hot: %3.02f",  resourceEvaluate.tooHotTimer / resourceEvaluate.dayTotalCounter); putStr(ioBfr);
        sprintf(ioBfr, "\n\r Too cold: %3.02f",  resourceEvaluate.tooColdTimer / resourceEvaluate.dayTotalCounter); putStr(ioBfr);
        petTheDog();
        sprintf(ioBfr, "\n\r Too damp: %3.02f",  resourceEvaluate.tooDampTimer / resourceEvaluate.dayTotalCounter); putStr(ioBfr);
        sprintf(ioBfr, "\n\r Too dry: %3.02f",  resourceEvaluate.tooDryTimer / resourceEvaluate.dayTotalCounter); putStr(ioBfr);
    }
}

    // -----------------------------
void securityCheck(byte opMode)
{
    static byte dayCtr, dayOld;

    if (oP.sysStat & ST_SECURITY_PASS) return;
    if (opMode == SEC_INIT)
    {
        if (oP.initCtr < 10) oP.initCtr++;
        else oP.sysStat |= ST_SECURITY_LAPSED;
        smemWriteParms(SMEM_SILENT);
    }
    else if (opMode == SEC_LOOP)
    {
       // if (time.dayMonth != dayOld)  DEB
        if (time.minute!= dayOld)
        {
           // dayOld = time.dayMonth;  DEB
            dayOld = time.minute;
            if (dayCtr < 31) dayCtr++;        
            else oP.sysStat |= ST_SECURITY_LAPSED;    
        }
    }
}

    // ---- Input from user console 
void serialInParse(void)
{
    char charCh;
    int retCode;
    byte b;

    charCh = getChar();
    if (!charCh) return;
        // if (oP.sysStat & ST_ECHO_SERIALIN) putChar(charCh, true);
    if (charCh == '/')      // Super commands for Monitor
    {
        buzzerControl(BUZ_SERIAL_SUPER, 1);
        charCh = 0;
        while (!charCh)
        {
            charCh = getChar();
            if (!charCh) continue;           
            putChar(charCh);  
            switch(charCh)
            {
                case 'D':
                    retCode = smemDatalogReport();
                    if (retCode < 0)
                    {
                        sprintf(ioBfr, "\r\n DL report fail [%d]\n\r", retCode);
                        putStr(ioBfr); 
                    }
                    break;

                case 'E':
                    smemEraseAll(SMEM_LOQUACIOUS);
                    break;

                case 'F':
                    factoryHardInit();
                    putStr("\r\n Factory defaults restored.\n\r");
                    break;

                case 'i':                    
                    putStr("\tChild currents: \n\r");//
                    for (b = 0; b < NUM_CHILDREN; b++)
                        adcCurrents(b, ADC_LOQUACIOUS);
                    break;

                case 'k':
                    keypadTest();
                    break;

                case 'm':
                    smemTest();
                    break;

                case 'p':
                    retCode = smemWriteParms(SMEM_LOQUACIOUS);
                    if (retCode > 0) { sprintf(ioBfr, "\n\r %d pages of parms written to SMEM\n\r", retCode); putStr(ioBfr); }
                    else { sprintf(ioBfr, "\r\n sWP fail [%d]\n\r", retCode); putStr(ioBfr); }
                    break;

                case 'P':
                    retCode = smemReadParms(SMEM_LOQUACIOUS);
                    if (retCode < 0) { sprintf(ioBfr, "\r\n sRP fail [%d]\n\r", retCode); putStr(ioBfr); }
                    break;

                case 'r':
                    reportInfo();
                    break;

                case 's':
                    adcTH(ADC_LOQUACIOUS);
                    break;

                case 'S':
                    editSetPoint();
                    break;

                case 't':
                    timeRead(TIME_LOQUACIOUS);
                    break;

                case 'T':
                    editTime();
                    timeWrite();
                    break;

                case 'w':
                    wifiInit();
                    break;

                case 'Z':
                    smemDatalog(SMEM_DL_RESET);
                    putStr("\r\n Datalog counter reset.\r\n");
                    break;

                case '1':
                    RELAY1 ^= 1;
                    if (RELAY1) putStr("\r\n Relay 1 on");
                    else putStr("\r\n Relay 1 off");
                    break;

                case '2':
                    RELAY2 ^= 1;
                    if (RELAY2) putStr("\r\n Relay 2 on");
                    else putStr("\r\n Relay 2 off");
                    break;

                case '4':
                    if (smemReadTime() == 0) sysFault |= FLT_SMEM;
                    break;

                case '5':
                    smemWriteTest();
//                    retCode = smemWriteTest();
//                    if (retCode < 0) { sprintf(ioBfr, "\tsWT fail [%d]\n\r", retCode); putStr(ioBfr); }
                    break;

                case '6':
                     smemReadTest();
//                    retCode = smemReadTest();
//                    if (retCode < 0) { sprintf(ioBfr, "\tsRT fail [%d]\n\r", retCode); putStr(ioBfr); }
                    break;

                case '7':
                    putChar2('*');
                    for (b = 0; b < 0xFF; b++)
                    {
                        if (getChar2() == '*') { putStr("\r\n UART2 loopback okay\r\n"); return; }
                    }
                    putStr("\r\n UART2 loopback fail\r\n");
                    break;
           
                case '?':
                    menuDisplay();                    
                    break;
            }           // EO switch
                // putPrompt();  Obsolete
            rs485TransmitEna(0);       // Return to receive state

        }       // EO while
    }           // EO if char
}

    // Simple periodic process scheduler
void taskScheduler(void)
{
    static bool processDone0 = false, processDone1 = false,
        processDone2 = false, processDone3 = false;
    byte ch;

    if ((time.minute % DATALOG_PERIOD_MINS) == (DATALOG_PERIOD_MINS - 1))      
    {
        if ((processDone0 == false) && (oP.sysStat & ST_DATALOG_ON))
        {           
            sysFault &= ~FLT_SMEM;      // Preclear          
            if (smemDatalog(SMEM_DL_RECORD) <= 0) sysFault |= FLT_SMEM;
            if (smemWriteParms(SMEM_SILENT) <= 0) sysFault |= FLT_SMEM;
            processDone0 = true;
        }       
    }
    else processDone0 = false;

    if (time.second % RESOURCE_STEP_TIME_SECS == RESOURCE_STEP_TIME_SECS - 1)
    {
        if (processDone1 == false)
        {         
            resourceManager();          // Function cycles over available nodes
            processDone1 = true;
        }
    }
    else processDone1 = false;
    
    if (time.second % 2 == 1)
    {
        if (processDone3 == false)
        {
            if (sysFault) 
            {
                LED_GREEN = 0;
                if (LED_RED) LED_RED = 0;
                else LED_RED = 1;
            }
            else
            {
                LED_RED = 0;
                if (LED_GREEN) LED_GREEN = 0;
                else LED_GREEN = 1;
            }
            processDone3 = true;
        }
    }
    else processDone3 = false;

    if (time.msec % 16 == 15) 
    {
        if (processDone2 == false)
        {
            timeRead(TIME_UPDATE);
            windowManager(keypadRead(KEYPAD_SILENT));
            processDone2 = true;
        }
    }
    else processDone2 = false;
}

double temperF2C(double temperF)
{
    return (0.55556 * (temperF - 32));
}

   // This clocks many processes such as keypad.  It is an accurate 1.0 ms, but
    // is allowed to run to 1.024 sec for timing symmetry.
void __ISR(_TIMER_1_VECTOR, IPL7AUTO) T1_IntHandler(void)
{
    if (++time.msec > 1023) { time.msec = 0; }
    IFS0bits.T1IF = 0;
}