/* File:   buzzer.c
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * Created: 24 Sep14
 */

#include <xc.h>
#include "DuraBlisCCSParent.h"
#include "buzzer.h"

// TODO limit buzzes on numerous errors
    // T2 runs at Fclk = 25 MHz. 1 ct = 40 ns.
    // C5 = 523.25 Hz, Eb5 = 622.25 Hz, F5 = 698.46 Hz, G5 = 783.99 Hz, Bb5 = 932.33 Hz
    // C minor pentatonic
void buzzerControl(byte buzz, byte t_100ms)
{
    unsigned t;

    OC1CON = 0x0000;    // Turn off the OC1 when performing the setup
    switch(buzz)
    {
        case BUZ_CLEARSTATUS:   // C5
        case BUZ_KEY1:
            OC1R = 23889;       // Initialize primary Compare register
            OC1RS = 23889;      // Initialize secondary Compare register
            OC1CON = 0x0006;    // Configure for PWM mode without Fault pin enabled
            PR2 = 47778;        // Set period
            break;

        case BUZ_STATUSBAD:     // G5
            case BUZ_KEY2:
            OC1R = 15900;       // Initialize primary Compare register
            OC1RS = 15988;      // Initialize secondary Compare register
            OC1CON = 0x0006;    // Configure for PWM mode without Fault pin enabled
            PR2 = 31888;        // Set period
            break;
         
        case BUZ_KEY3:      // Bb5
            OC1R = 13407;       // Initialize primary Compare register
            OC1RS = 13407;      // Initialize secondary Compare register
            OC1CON = 0x0006;    // Configure for PWM mode without Fault pin enabled
            PR2 = 26815;        // Set period
            break;

        case BUZ_NEWCHILD:      // Eb5
            case BUZ_KEY4:
            OC1R = 20088;      // Initialize primary Compare register
            OC1RS = 20088;     // Initialize secondary Compare register
            OC1CON = 0x0006;    // Configure for PWM mode without Fault pin enabled
            PR2 = 40177;       // Set period
            break;

        case BUZ_SERIAL_SUPER:  // Eb6
            OC1R = 10044;       // Initialize primary Compare register
            OC1RS = 10044;      // Initialize secondary Compare register
            OC1CON = 0x0006;    // Configure for PWM mode without Fault pin enabled
            PR2 = 20088;        // Set period
            break;
    }

    OC1CONSET = 0x8000; // Enable OC1
 //   t2Flag = false;
 //   t2Ctr = ((unsigned long) t_100ms) * 60000;
//    while (!t2Flag);
    for (t=0; t < t_100ms * 1000; t++) delay_us(100);
    OC1CON = 0x0000;
}