/* File:   time.h  RTCC and high level time functions
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * Created: 18 Sep14
 */

#ifndef TIME_H
#define	TIME_H

#ifdef	__cplusplus
extern "C" {
#endif

#define TIMER_INIT  1       // Operational modes
#define TIMER_RUN   2
  
    // Time read modes
#define TIME_UPDATE             0x01
//#define TIME_NEW_MINUTE         0x02
//#define TIME_NEW_SECOND         0x03
#define TIME_LOQUACIOUS         0x04

struct t_struct
{
    byte year;      // 00 - 99    Processes are set by readTime, clrd by user fcn
    byte month;     // 1 - 12
    byte dayMonth;  // 1 - 31
    byte weekday;   // 0 (Sun) - 6  Not presently used.  But in RTCC.  Keep for now.
    byte hour;      // 0 - 23
    byte minute;    // 0 - 59   Only first six saved to SEEPROM
    byte second;    // 0 - 59
    unsigned msec;      // 0 - 1024
} time;

const char monStr[12][4];

byte daysInMonth(byte mo, byte yr);
unsigned delay_ms(byte mode, unsigned timeOut_ms);       // Returns val > 0 until countdown in ms
void delay_us(unsigned T);
void timeRead(byte modeRead);
void timeWrite(void);
bool timeInBounds(void);

#ifdef	__cplusplus
}
#endif

#endif	/* TIME_H */

