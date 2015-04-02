/* File:   keypad.c
 * IIC interface to capacitive keypad
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * Created: 17 Sep14
 */

#include <xc.h>
#include "DuraBlisCCSParent.h"
#include "keypad.h"
#include "buzzer.h"
#include "time.h"

#define NUM_CAPKEYS  4
const byte keypadADCchan[NUM_CAPKEYS] = { 20, 21, 22, 23 };
double iirSlow[NUM_CAPKEYS] = { 1350.0, 1350.0, 1350.0, 1350.0 };   // Set overigh to force init
double iirFast[NUM_CAPKEYS] = { 350.0, 350.0, 350.0, 350.0 };  // Peaks set to ca 1.0 V (311 cts)

unsigned ctmuSample(byte chan);
void _delayCIT(void);

void keypadInit(void)
{
    // extern struct operatingParms_struct oP;
    extern struct operatingParms_struct
    {
        unsigned sysStat;           // [2 bytes]
        unsigned dlRecordsSaved;    // [2]
        double setPointTempF;       // [4]
        double setPointRelHum;      // [4] All hums normalized to 100
        double temperCalFactor;     // [4] For Fahrenheit
        double rhumidCalFactor;     // [4]
        double kpadSensit[4];       // [16]
        char uniqueID[11];          // [12] Ostensibly, mmddyy and 4-alpha serial num
    } oP;

    CTMUCONbits.ON = 0;
    CTMUCONbits.CTMUSIDL = 0;   // CTMU runs in Idle
    CTMUCONbits.EDG2STAT = 0;   // Leave this 0 to avoid double trig
    CTMUCONbits.IRNG = 0b10;    // Current range 10 x base ( = 5.5 uA nominal)  ***
    //CTMUCONbits.IRNG = 0b11;        // 100 x base = 55 uA
    //CTMUCONbits.IRNG = 0b01;        // = base = 550 nA
        // Next 2 are not actual, but preferable to defaulting to 0b0000 Timer1 event as source
    CTMUCONbits.EDG1SEL = 0b0001;   // OC1 is Edge 1 source
    CTMUCONbits.EDG2SEL = 0b0001;   // OC1 is Edge 1 source
    CTMUCONbits.ON = 1;   
}

    // Return bit image xxxx DCBA from capacitive keypad.
    // A has preemptive priority over B, et seq.
    // Each key can only be set after it has reset with a release.  For debounce.
    // Called key sequentially with period 16 ms.
    // *** Timing is critical with CTMU ***
byte keypad(byte opMode)
{
    static byte keyState = 0;
    static byte key = 0;
    static byte keypadStartup = 32;
    unsigned adcMeas;                       // Meas voltages, 1023 full scale
    float iirSlowCoeff = 0.483;
    float iirFastCoeff = 0.895;
    float keyPressFactor = 0.007;       // TODO may go to 4 indiv key sensit factors
    float keyReleaseFactor = 0.006;

    AD1CHSbits.CH0NA = 0;       // Sample A Ch 0 neg input is Vrefl
    
    switch(key)
    {
        case 0:
            if (iirSlow[0] > 1023)
            {
                iirFast[0] = iirSlow[0] = ctmuSample(0);
//                sprintf(ioBfr, "A %2.1f\r\n", iirSlow[0]);  // DEB
//                putStr(ioBfr);
                break;
            }
            adcMeas = ctmuSample(0);
            iirSlow[0] += iirSlowCoeff * (adcMeas - iirSlow[0]);
            iirFast[0] += iirFastCoeff * (adcMeas - iirFast[0]);
//            sprintf(ioBfr, "%2.1f %2.1f\r\n", iirSlow[0], iirFast[0]);  // DEB
//            putStr(ioBfr);
            if (keypadStartup) { keypadStartup--; break; }
            if ((iirSlow[0] - iirFast[0]) > (keyPressFactor * iirSlow[0]))
            {
                if (!(keyState & 0x01))
                {
                    if (opMode == KEYPAD_LOQUACIOUS) putChar('A');
                    buzzerControl(BUZ_KEYA, 1);
                    keyState |= 0x01;      // Set
                    return(0x01);
                }
            }
            if ((iirSlow[0] - iirFast[0]) < (keyReleaseFactor * iirSlow[0])) keyState &= 0xFE;   // Reset
            break;
    
        case 1:
            if (iirSlow[1] > 1023)
            {
                iirFast[1] = iirSlow[1] = ctmuSample(1);
//                sprintf(ioBfr, "B %2.1f\r\n", iirSlow[1]);  // DEB
//                putStr(ioBfr);
                break;
            }
            adcMeas = ctmuSample(1);
            iirSlow[1] += iirSlowCoeff * (adcMeas - iirSlow[1]);
            iirFast[1] += iirFastCoeff * (adcMeas - iirFast[1]);
            if (keypadStartup) { keypadStartup--; break; }
            if ((iirSlow[1] - iirFast[1]) > keyPressFactor * iirSlow[1])
            {
//                sprintf(ioBfr, "B %2.1f \r\n", iirSlow[1] - iirFast[1]);  // DEB
//                putStr(ioBfr);

                if (!(keyState & 0x02))
                {
                    if (opMode == KEYPAD_LOQUACIOUS) putChar('B');
                    buzzerControl(BUZ_KEYB, 1);
                    keyState |= 0x02;      // Set
                    return(0x02);
                }
            }
            if ((iirSlow[1] - iirFast[1]) < keyReleaseFactor * iirSlow[1]) keyState &= 0xFD;   // Reset
            break;

        case 2:
            if (iirSlow[2] > 1023)
            {
                iirFast[2] = iirSlow[2] = ctmuSample(2);
//                sprintf(ioBfr, "C %2.1f\r\n", iirSlow[2]);  // DEB
//                putStr(ioBfr);
                break;
            }
            adcMeas = ctmuSample(2);
            iirSlow[2] += iirSlowCoeff * (adcMeas - iirSlow[2]);
            iirFast[2] += iirFastCoeff * (adcMeas - iirFast[2]);
            if (keypadStartup) { keypadStartup--; break; }
            if ((iirSlow[2] - iirFast[2]) > keyPressFactor * iirSlow[2])
            {
//                sprintf(ioBfr, "C %2.1f \r\n", iirSlow[2] - iirFast[2]);  // DEB
//                putStr(ioBfr);

                if (!(keyState & 0x04))
                {
                    if (opMode == KEYPAD_LOQUACIOUS) putChar('C');
                    buzzerControl(BUZ_KEYC, 1);
                    keyState |= 0x04;      // Set
                    return(0x04);
                }
            }
            if ((iirSlow[2] - iirFast[2]) < keyReleaseFactor * iirSlow[2]) keyState &= 0xFB;   // Reset
            break;

        case 3:
             if (iirSlow[3] > 1023)
            {
                iirFast[3] = iirSlow[3] = ctmuSample(3);
//                sprintf(ioBfr, "D %2.1f\r\n", iirSlow[3]);  // DEB
//                putStr(ioBfr);
                break;
            }
            adcMeas = ctmuSample(3);
            iirSlow[3] += iirSlowCoeff * (adcMeas - iirSlow[3]);
            iirFast[3] += iirFastCoeff * (adcMeas - iirFast[3]);
            if (keypadStartup) { keypadStartup--; break; }
            if ((iirSlow[3] - iirFast[3]) > keyPressFactor * iirSlow[3])
            {
//                sprintf(ioBfr, "D %2.1f \r\n", iirSlow[3] - iirFast[3]);  // DEB
//                putStr(ioBfr);

                if (!(keyState & 0x08))
                {
                    if (opMode == KEYPAD_LOQUACIOUS) putChar('D');
                    buzzerControl(BUZ_KEYD, 1);
                    keyState |= 0x08;      // Set
                    return(0x08);
                }
            }
            if ((iirSlow[3] - iirFast[3]) < keyReleaseFactor * iirSlow[3]) keyState &= 0xF7;   // Reset
            break;
    }

    if (++key > 3) key = 0;
    return(0);
}

    // Todo put in time marks for duration
void keypadTest(void)
{
    unsigned adcMeas;                       // Meas voltages, 1023 full scale
    double iirSlow[NUM_CAPKEYS] = { 311.0, 311.0, 311.0, 311.0 };
    double iirSlowCoeff = 0.0143;
    double iirFast[NUM_CAPKEYS] = { 311.0, 311.0, 311.0, 311.0 };
    double iirFastCoeff = 0.435;            // TODO do adaptive in begin
    byte keyState = 0;                      // Bit image, set-reset
    byte keyPressThreshold = 5;
    byte keyReleaseThreshold = 2;
    byte key = 0;
    unsigned d;
    char charCh;
        //byte counter = 0;     // Use set-reset instead

    putStr("\tKeypad test. Any serial input ends.\r\n");

    AD1CHSbits.CH0NA = 0;       // Sample A Ch 0 neg input is Vrefl
    while(1)
    {            
        adcMeas = ctmuSample(key);
       
        iirSlow[key] += iirSlowCoeff * (adcMeas - iirSlow[key]);
        iirFast[key] += iirFastCoeff * (adcMeas - iirFast[key]);

        if ((iirSlow[key] - iirFast[key]) > keyPressThreshold)
        {
            if (!(keyState & (1 << key)))
            {
                 sprintf(ioBfr, "%d %2.1f \r\n", key, iirSlow[key] - iirFast[key]);  // DEB
                 putStr(ioBfr);

             //   putChar('A' + key);
            }
            keyState |= (1 << key);      // Set
        }
        if ((iirSlow[key] - iirFast[key]) < keyReleaseThreshold) keyState &= ~(1 << key);   // Reset

        if (key < NUM_CAPKEYS - 1) key++;
        else key = 0;

        for (d=0; d<500; d++)
        {
            charCh = getChar();
            if (charCh > 31)
            {
                 putChar(charCh);
                 return;
            }
            delay_us(100);
        }

//    sprintf(ioBfr, "%2.1f  %2.1f\r\n", iirSlow[key], iirFast[key]);  // DEB
//    putStr(ioBfr);  // DEB
    }
}

unsigned ctmuSample(byte chan)
{
    //AD1CON1bits.SAMP = 0;       // End sampling & start conver
            // AD1CHSbits.CH0NA = 0;       // Sample A Ch 0 neg input is Vrefl
            //  iChan = keypadADCchan[iButton];
    AD1CHSbits.CH0SA = keypadADCchan[chan];  // iChan;
    AD1CON1bits.SAMP = 1;       // Start sampling
            //  ADC_Sum = 0;
    //AD1CSSL = 0;                // Write to scan buff 0
            //AD1CON1bits.SAMP = 1; // Manual sampling start
    CTMUCONbits.IDISSEN = 1; // Ground charge pump
    delay_us(100);           // Wait for grounding  TODO
    CTMUCONbits.IDISSEN = 0; // End drain of circuit
    interruptsEnable(false);
    CTMUCONbits.EDG1STAT = 1; // Begin charging the circuit
    _delayCIT();        // Charge integration time, typ 10 usec
    
    AD1CON1bits.SAMP = 0;       // Start ADC
    //delay_us(200);   // ***     // Charge time
    // AD1CON1bits.SAMP = 0; // Begin analog-to-digital conversion
    CTMUCONbits.EDG1STAT = 0; // Stop charging circuit
  //  asm("NOP"); asm("NOP"); asm("NOP");
    while (!AD1CON1bits.DONE); // Wait for ADC conversion

    AD1CON1bits.DONE = 0; // ADC conversion done, clear flag
    interruptsEnable(true);
    return(ADC1BUF0);
}


    // --------------- 128 NOPs gives ca 1.04 V pk, with press ca 0.98 V
void _delayCIT(void)
{
    unsigned ciTime = 12;

    while(ciTime--) petTheDog();
    
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
//    asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP"); asm("NOP");
}


    // -------------------------------
//unsigned ctmuSample(byte chan)  As is 07 Oct 14
//{
//    AD1CON1bits.SAMP = 0;       // End sampling & start conver
//            // AD1CHSbits.CH0NA = 0;       // Sample A Ch 0 neg input is Vrefl
//            //  iChan = keypadADCchan[iButton];
//    AD1CHSbits.CH0SA = keypadADCchan[chan];  // iChan;
//            //  ADC_Sum = 0;
//    AD1CSSL = 0;                // Write to scan buff 0
//            //AD1CON1bits.SAMP = 1; // Manual sampling start
//    CTMUCONbits.IDISSEN = 1; // Ground charge pump
//    delay_us(10);           // Wait for grounding
//    CTMUCONbits.IDISSEN = 0; // End drain of circuit
//
//    CTMUCONbits.EDG1STAT = 1; // Begin charging the circuit
//    AD1CON1bits.SAMP = 1;       // Start sampling
//    delay_us(200);   // ***     // Charge time
//     AD1CON1bits.SAMP = 0; // Begin analog-to-digital conversion
//    CTMUCONbits.EDG1STAT = 0; // Stop charging circuit
//
//
//    while (!AD1CON1bits.DONE); // Wait for ADC conversion
//
//    AD1CON1bits.DONE = 0; // ADC conversion done, clear flag
//    return(ADC1BUF0);
//}

    // MEchanical keypad
//byte keypad(byte opMode)
//{
//    static byte keyState = 0;
//
//    if (opMode == KEYPAD_LOQUACIOUS) putStr("\tKeypad nonCTMU test.\r\n");
//
//    if (PORTEbits.RE6 == 0)
//    {
//        if (!(keyState & 1))
//        {
//            if (opMode == KEYPAD_LOQUACIOUS) putChar('A');
//            buzzerControl(BUZ_KEYA, 1);
//            keyState |= 1;      // Set
//            return(0x01);
//        }
//     }
//     else keyState &= 0xFE;      // Reset
//
//     if (PORTEbits.RE5 == 0)
//     {
//        if (!(keyState & 2))
//        {
//            if (opMode == KEYPAD_LOQUACIOUS) putChar('B');
//             buzzerControl(BUZ_KEYB, 1);
//            keyState |= 2;      // Set
//            return(0x02);
//        }
//    }
//    else keyState &= 0xFD;   // Reset
//
//    if (PORTEbits.RE4 == 0)
//    {
//        if (!(keyState & 4))
//        {
//            if (opMode == KEYPAD_LOQUACIOUS) putChar('C');
//             buzzerControl(BUZ_KEYC, 1);
//            keyState |= 4;      // Set
//            return(0x04);
//        }
//    }
//    else keyState &= 0xFB;   // Reset
//
//    if (PORTEbits.RE2 == 0)
//    {
//        if (!(keyState & 8))
//        {
//
//            if (opMode == KEYPAD_LOQUACIOUS)   putChar('D');
//             buzzerControl(BUZ_KEYD, 1);
//            keyState |= 8;      // Set
//            return(0x08);
//        }
//    }
//    else keyState &= 0xF7;   // Reset
//
//    return(0);
//}
