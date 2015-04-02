/* File:   iic_keypad.h
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform:    PIC32MX350F256H on DB-CCS-Main_v2.x board
 * Created: 17 Sep14
 */

#ifndef KEYPAD_H
#define	KEYPAD_H

#ifdef	__cplusplus
extern "C" {
#endif

#define KEYPAD_SILENT      0x01
#define KEYPAD_LOQUACIOUS  0x02

//#define KEYPAD_SENSIT_MIN   0.006
//#define KEYPAD_SENSIT_MAX   0.020

#define KEY_A           0x01
#define KEY_B           0x02
#define KEY_C           0x04
#define KEY_D           0x08

byte keypadRead(byte opMode);
void keypadInit(void);

#ifdef	__cplusplus
}
#endif

#endif	/* KEYPAD_H */

