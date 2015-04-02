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

void keypadInit(void)
{}

    // Return 0 or key image when that has changed
byte keypad(byte opMode)
{
    static byte keyImageOld;
    byte keyImage = 0, retVal = 0;

    if (STD_KEY1 == 0) keyImage |= 0x01;
    if (STD_KEY2 == 0) keyImage |= 0x02;
    if (STD_KEY3 == 0) keyImage |= 0x04;
    if (STD_KEY4 == 0) keyImage |= 0x08;

    if ((keyImageOld == 0) && (keyImage != keyImageOld))
    {
        if (opMode == KEYPAD_LOQUACIOUS) { sprintf(ioBfr, "[%02X]", keyImage); putStr(ioBfr); }
        retVal = keyImage;
        if (keyImage & 0x01) buzzerControl(BUZ_KEYA, 1);
        if (keyImage & 0x02) buzzerControl(BUZ_KEYB, 1);
        if (keyImage & 0x04) buzzerControl(BUZ_KEYC, 1);
        if (keyImage & 0x08) buzzerControl(BUZ_KEYD, 1);
    }
    keyImageOld = keyImage;
    return(retVal);
}
  
    // Todo put in time marks for duration
void keypadTest(void)
{
    unsigned T;

    putStr("\tKeypad test: ");
    for (T = 0; T < 0xFFF0; T++)
    {
        keypad(KEYPAD_LOQUACIOUS);
        delay_us(1000);
    }


}
