/* File:   adc.c  Analog functions low and high level
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board.  
 * Created: 10 Jul 14
 */

#include <xc.h>
#include "DuraBlisCCSParent.h"
#include "adc.h"
#include "serial.h"

#define NUM_SA   16
#define ADC_LOW_TH_RANGE   0.2     // Acceptable V range for T&H sensors
#define ADC_HIGH_TH_RANGE  3.0
#define T_SEN_STDDEV_MAX     2      // Std dev in ADC cts
#define RH_SEN_STDDEV_MAX   15      // Honeywell sensor is skittish

static unsigned adcUrConvert(byte chan);
static double adcMeanConvert(byte chan, byte opMode);
static double sqrtBabylon(double S);

static double sigmaLastADC;     // Standard deviation from last conversion cycle

    // There are 8 ports.  Each measured in turn.
void adcCurrents(byte chan, byte opMode)
{
    const byte chArray[NUM_CHILDREN] = { ANCH_ISEN1, ANCH_ISEN2, ANCH_ISEN3, ANCH_ISEN4,
        ANCH_ISEN5, ANCH_ISEN6, ANCH_ISEN7, ANCH_ISEN8 };
    extern double iSen[NUM_CHILDREN], iSenCalFactor [NUM_CHILDREN];
    double adcV, mA;

    adcV = adcMeanConvert(chArray[chan], ADC_SILENT);
    mA = iSenCalFactor[chan] * adcV * 1000;   // Nominal 1 ohm sense resistor, conv to mA

    if (chan > 3) mA = 0;        // DEB.  Demo has only ch 0 - 3

    iSen[chan] += 0.017 * (mA - iSen[chan]);     // IIR filter for noisy loads
    if (opMode == ADC_LOQUACIOUS)
    {
        sprintf(ioBfr, "\tCh %d: %2.01f mA [%4.03f]\n\r", chan + 1, iSen[chan], iSenCalFactor[chan]);
        putStr(ioBfr);
    }
   
}

    // TODO set/clr error bits
void adcTH(byte opMode)
{
    extern double temperNowF, rhumidNow;
    extern struct operatingParms_struct oP;
    extern unsigned sysFault;
    double adcV;

        // Microchip MCP9700A
    adcV = adcMeanConvert(ANCH_TEMPER, opMode);
    if ((adcV < ADC_LOW_TH_RANGE) || (adcV > ADC_HIGH_TH_RANGE)) sysFault |= FLT_PARENT_TSENSOR;   // User must clear
//    sysFault &= ~FLT_PARENT_TSEN_NOISY;  DEB
//    if (sigmaLastADC > T_SEN_STDDEV_MAX) sysFault |= FLT_PARENT_TSEN_NOISY;
    temperNowF = 180 * adcV - 58;
    temperNowF *= oP.temperCalFactor;

    if (opMode == ADC_LOQUACIOUS)
    {
        putStr("\tTemperature: ");  //, true);
        sprintf(ioBfr, "%3.02f oF (%3.02fV) ", temperNowF, adcV);
        putStr(ioBfr);
        putStr("\n\r");  //, true);
    }

        // Honeywell HIH5030, normalized to unity
        // TODO Sensor has temperature dependence and slight hysteresis
    adcV = adcMeanConvert(ANCH_RELHUM, opMode);
    if ((adcV < ADC_LOW_TH_RANGE) || (adcV > ADC_HIGH_TH_RANGE)) sysFault |= FLT_PARENT_HSENSOR;   // User must clear
//    sysFault &= ~FLT_PARENT_RHSEN_NOISY;  DEB
//    if (sigmaLastADC > RH_SEN_STDDEV_MAX) sysFault |= FLT_PARENT_RHSEN_NOISY;
    rhumidNow = 50 * adcV - 25;
    rhumidNow *= oP.rhumidCalFactor;

    if (opMode == ADC_LOQUACIOUS)
    {
        putStr("\r\n\tRel humidity: ");  //, true);
        sprintf(ioBfr, "%3.02f %% (%3.02fV)", rhumidNow, adcV);
        putStr(ioBfr);  // , true);
        putStr("\n\r");  //, true);
    }
}

double adcMeanConvert(byte chan, byte opMode)
{
   // extern double adcInternalCal;
    extern double vRef;
    byte sa;
    double retVal = 0.0, vMean = 0.0,  errSqr = 0.0;
    unsigned samp;  //, minSa = 0xFFFF, maxSa = 0;
    unsigned uBfr[NUM_SA];


    adcUrConvert(chan);     // First is often worst
    delay_us(10);

    for (sa = 0; sa < NUM_SA; sa++)
    {
        delay_us(10);
        samp = adcUrConvert(chan);
        uBfr[sa] = samp;
            //        if (samp < minSa) minSa = samp;
            //        if (samp > maxSa) maxSa = samp;
    }

    for (sa = 0; sa < NUM_SA; sa++)
    {
        if (opMode == ADC_LOQUACIOUS)
        {
            sprintf(ioBfr, "\n\r  %d", (unsigned) uBfr[sa]); // DEB
            putStr(ioBfr);   //, true);
        }
        vMean += uBfr[sa];
    }

            //    vMean -= minSa;
            //    vMean -= maxSa;
            // vMean = vMean / (NUM_SA - 2);
     vMean = vMean / NUM_SA;
    if (opMode == ADC_LOQUACIOUS)
    {
        sprintf(ioBfr, "\n\r\tADC mean: %3.02f\n\r", vMean);
        putStr(ioBfr);  //, true);
    }

    for (sa = 0; sa < NUM_SA; sa++)     // This is a little off since I am leaving
    {                                   // outliers in.
        errSqr += (vMean - uBfr[sa]) * (vMean - uBfr[sa]);
    }
    errSqr /= (NUM_SA - 1);            // Bessel corrected
    sigmaLastADC = sqrtBabylon(errSqr);

    if (opMode == ADC_LOQUACIOUS)
    {
        sprintf(ioBfr, "\tsigma %5.04f, Sa = %d\n\r", sigmaLastADC, NUM_SA - 2);
        putStr(ioBfr);  //, true);
    }
    //if (adcInternalCal < 0.5 || adcInternalCal > 1.5) { sprintf(ioBfr, "\t* aIC range err: %4.03f *\n\r", adcInternalCal); putStr(ioBfr); }
    //if (vRef < 2.8 || vRef > 3.3) { sprintf(ioBfr, "\t* vRef range err: %4.03f *\n\r", vRef); putStr(ioBfr); }
    retVal = vRef * vMean / 1023;

    return(retVal);
}

double sqrtBabylon(double S)
{
    const double epsilon = 0.00001;
    byte i;
    double xn0, xn1;
    if (S <= epsilon) return (0.0);
    xn0 = S;
    for(i = 0; i < 10; i++)
    {
        xn1 = 0.5 * (xn0 + (S / xn0));
        xn0 = xn1;
    }
    return(xn1);
}
   
unsigned adcUrConvert(byte chan)
{
    AD1CHSbits.CH0SA = chan;    // A mux <- chan
    delay_us(10);
    AD1CON1bits.SAMP = 1;       // Start sampling
    delay_us(10);
    AD1CON1bits.SAMP = 0;
    while(!(AD1CON1bits.DONE)) ;    // Wait
    return(ADC1BUF0);
}

void adcInit(void)
{
    AD1CON1bits.FORM = 0b000;   // Integer 16-bit, 10 lsb's, DEFLT
    AD1CON1bits.SSRC = 0b000;   // Clearing SAMP bit ends sampling and starts conversion, DFLT
    AD1CON1bits.ASAM = 0;       // Sampling begins when SAMP bit is set, DFLT

    AD1CON2bits.VCFG = 0b001;   // External Vref+, AVss on analog ground
    AD1CON2bits.OFFCAL = 0;     // Disa offset cal mode
    AD1CON2bits.CSCNA = 0;      // Disa scan the inputs
    AD1CON2bits.BUFM = 0;       // Buffer is one 16-bit word
    AD1CON2bits.ALTS = 0;       // Always use sample A input mux settings

    AD1CON3bits.ADRC = 1;       // ADC clk from FRC
    AD1CON3bits.SAMC = 0b11111; // 4 TAD autosample time, WAS 4
    AD1CON3bits.ADCS = 0b11111111;  // = 0xFF : 512 * T(PB)

   //AD1CON3 = 1;    // ADCS = 1 : TPB ? 2 ? (ADCS<7:0> + 1) = 4 ? TPB = TAD

        // These don't matter if we scan.
    AD1CHSbits.CH0NB = 0;       // Sample B Ch 0 neg input is Vrefl
    AD1CHSbits.CH0SB = 0b00000; // Select B: AN0
    AD1CHSbits.CH0NA = 0;       // Sample A Ch 0 neg input is Vrefl
    AD1CHSbits.CH0SA = 0b00000; // Select B: AN0

    AD1CSSL = 0x0;        // No channels scanned

    AD1CON1bits.ON = 1;         // This must be last step.
}