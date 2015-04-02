/* File:   serial.h  UART consoleIO
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * Created: 16 May 14
 */

#ifndef SERIAL_H
#define	SERIAL_H

#ifdef	__cplusplus
extern "C" {
#endif

#define LINE_FEED           10
#define CARRIAGE_RETURN     13

#define GETSTR_LEN          21          // For general comm
#define PARM_LEN            11           // For communicating flop parms

 // Timeouts  
#define TIMEOUT_HUMAN           1       // 2 sec nominal
#define TIMEOUT_PNET            2       // 10 msec
#define TIMEOUT_RN171           3       // 1 msec

//void rs485TransmitEna2(void);
//void rs485TransmitDisa(void);

void rs485TransmitEna(bool mode);
int parseHex3Byte(char nibHi, char nibMid, char nibLo, unsigned *hexaFlexagon);
void putUns2Hex(unsigned uns);
void putByte2Hex(byte me);
void putPrompt(void);
void putChar(char c);  // , bool txCtrl);
void putChar2(char c);      // USART #2
char getChar(void);
int getStr(char *buffer, byte timeOutMode);
char getChar2(void);
int getStr2(char *buffer, byte numChars);      // USART #2
void putStr2(char *);                          // USART #2
void putStr(char *); // , bool txCtrl);
char *itoa(char *buf, int val, int base);
char *utoa(char *buf, unsigned val, int base);

#ifdef	__cplusplus
}
#endif

#endif	/* SERIAL_H */

