/* File:   adc.h  Analog functions low and high level
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board.
 * Created: 11 Jul 14
 */

#ifndef ADC_H
#define	ADC_H

#ifdef	__cplusplus
extern "C" {
#endif

#define ADC_SILENT      0x01
#define ADC_LOQUACIOUS  0x02

void adcTH(byte);
void adcCurrents(byte chan, byte opMode);
void adcInit(void);

#ifdef	__cplusplus
}
#endif

#endif	/* ADC_H */

