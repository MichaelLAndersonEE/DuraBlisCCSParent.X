/* File:   buzzer.h  Buzzer for keypad user feedback and alarm signaling
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board.
 * Created: 04 Sep 14
 */


#ifndef BUZZER_H
#define	BUZZER_H

#ifdef	__cplusplus
extern "C" {
#endif

#define BUZ_CLEARSTATUS         0x01
#define BUZ_STATUSBAD           0x02
#define BUZ_SERIAL_SUPER        0x04
#define BUZ_NEWCHILD            0x08
#define BUZ_KEY1                0x10
#define BUZ_KEY2                0x20
#define BUZ_KEY3                0x40
#define BUZ_KEY4                0x80

void buzzerControl(byte buzz, byte t_100ms);


#ifdef	__cplusplus
}
#endif

#endif	/* BUZZER_H */

