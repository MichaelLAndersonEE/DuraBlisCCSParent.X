/* File:   lcd.h  low and high level display functions
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * LCD:  NHD NHD?C12864WC?FSW?FBW?3V3, 128 x 64 pixels
 * Created: 08 Sep14
 */

#ifndef LCD_H
#define	LCD_H

#ifdef	__cplusplus
extern "C" {
#endif

void lcdUpDnEnEs(void);
void lcdCenter(byte page, char *);
void lcdSoftkeys1(char *, char *, char *, char *);
void lcdSoftkeys0(char *, char *, char *, char *);
void lcdClearPage(byte page);

void lcdStrAt(byte page, byte col, char *str);
void lcdReverseStrAt(byte page, byte col, char *str);   // Highlit
void lcdReverseCharAt(byte page, byte col, char ch);
void lcdCharAt(byte page, byte col, char ch);
void lcdScreen(byte pic[]);
void lcdOnOff(byte onOff);              // 1/0
void lcdStartLine(byte startAddress);   // 6 bit right-just display RAM start line address
void lcdPageAddress(byte startAddress); // 4 bit right-just page address
void lcdColumnAddressUpper(byte mostSigColAddress);    // Most sig 4 bits right-just of display RAM col address
void lcdColumnAddressLower(byte leastSigColAddress);   // Least sig 4 bits right-just of display RAM col address
byte lcdStatusRead(void);       // Upper nibble is Status
void lcdDataWrite(byte datum);
void lcdAllPointsOnOff(byte onOff);
void lcdReadModifyWrite(byte onOff);
void lcdReset(void);




#ifdef	__cplusplus
}
#endif

#endif	/* ADC_H */

