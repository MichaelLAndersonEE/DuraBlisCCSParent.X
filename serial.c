/* File:   serial.c  UART consoleIO
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board with RS-485
 * Created: 16 May 14
 */

#include <xc.h>
#include "DuraBlisCCSParent.h"
#include "serial.h"
#include "time.h"

void rs485TransmitEna(bool mode)   //       byte mode)
{
    char chIn;

    if (mode == true)
    {
        RS485_RE_n = 1;
        RS485_DE = 1;       // Enable Tx
        delay_us(30);
    }
    else
    {
        while(U1STAbits.TRMT == 0) ;    // 0 means transmission in progress
        RS485_DE = 0;                   // Disable Tx
        RS485_RE_n = 0;
        chIn =  U1RXREG;                // Flush URXDA
    }
}

//void putPrompt(void)
//{
//    putStr("\r\n > ", false);
//}

    // This is a noninterrupt UART read. It is transparent;
    // returning 0 if there is nothing in.
char getChar(void)
{
    char ch;

   // RS485_RE_n = 0;
    if (U1STAbits.URXDA)
    {
        ch =  U1RXREG;      // Clears URXDA
        return(ch);
    }
    else return(0);
}

    // This is a noninterrupt UART#2 read. It is transparent;
    // returning 0 if there is nothing in.
char getChar2(void)
{
    char ch;

    if (U2STAbits.URXDA)
    {
        ch =  U2RXREG;      // Clears URXDA
        return(ch);
    }
    else return(0);
}
    // Case invariant
int parseHex3Byte(char nibHi, char nibMid, char nibLo, unsigned *hexaFlexagon)
{
    unsigned buffer = 0;
    if (nibHi >= '0' && nibHi <= '9') buffer = (nibHi - '0') << 8;
    else if (nibHi >= 'a' && nibHi <= 'f') buffer = (nibHi - 'a' + 10) << 8;
    else if (nibHi >= 'A' && nibHi <= 'F') buffer = (nibHi - 'A' + 10) << 8;
    else return(-1);

    if (nibMid >= '0' && nibMid <= '9') buffer += (nibMid - '0') << 4;
    else if (nibMid >= 'a' && nibMid <= 'f') buffer = (nibMid - 'a' + 10) << 4;
    else if (nibMid >= 'A' && nibMid <= 'F') buffer = (nibMid - 'A' + 10) << 4;
    else return(-1);

    if (nibLo >= '0' && nibLo <= '9') buffer += (nibLo - '0');
    else if (nibLo >= 'a' && nibLo <= 'f') buffer += (nibLo - 'a' + 10);
    else if (nibLo >= 'A' && nibLo <= 'F') buffer += (nibLo - 'A' + 10);
    else return(-1);
    *hexaFlexagon = buffer;
    return(1);
}

    // ------------------------
    // Max length GETSTR_LEN
    // TODO Need retval for null CR ?
int getStr(char *buffer, byte timeOutType)
{
    unsigned timeOut_ms;  // = 0xFFFF;
    byte bufIdx = 0;
    char inCh;
    
    if (timeOutType == TIMEOUT_HUMAN) timeOut_ms = 10000;
    else if (timeOutType == TIMEOUT_PNET) timeOut_ms = 35;
    delay_ms(TIMER_INIT, timeOut_ms);
  
    while(delay_ms(TIMER_RUN, 0))
    {
        inCh = getChar();
       
        if (inCh > 0)
        {            
            delay_ms(TIMER_INIT, timeOut_ms);
            //timeOut_ms = 0xFFFF;
          //  putChar(inCh, true);      // DEB
            buffer[bufIdx++] = inCh;          
        }
          
        if (bufIdx > GETSTR_LEN) return(-2);
        if ((bufIdx > 3) && (inCh == CARRIAGE_RETURN))
        {
            buffer[bufIdx] = 0;
            return(bufIdx);
        }
    }
  //  RS232_ENA_n = 0;
    return(-3);         // Timeout
}

    // USART#2
int getStr2(char *buffer, byte numChars)
{
    unsigned timeOut_ms;
    byte bufIdx = 0;
    char inCh;

    timeOut_ms = 300;   // TODO expose this
    delay_ms(TIMER_INIT, timeOut_ms);

    while(delay_ms(TIMER_RUN, 0))
    {
        inCh = getChar2();

        if (inCh > 0)
        {
            delay_ms(TIMER_INIT, timeOut_ms);
            putChar(inCh);      // DEB
            buffer[bufIdx++] = inCh;
        }

        if (bufIdx > numChars) return(10);  // DEB
        if (inCh == CARRIAGE_RETURN)
        {
            buffer[bufIdx] = 0;
            return(bufIdx);
        }
    }
    return(-3);         // Timeout
}

    // ------------------------
    // TODO needs work.  Need retval for null CR
//int getStr2(char *buffer, byte numChars, unsigned timeOut)
//{
//    byte bufIdx = 0;
//    char inCh;
//
//    while(timeOut)
//    {
//        inCh = getChar2();
//        if (inCh > 0)
//        {
//            timeOut = TIMEOUT_RN171;
//            buffer[bufIdx++] = inCh;
//          //  putChar(inCh);          // Echo
//        }
//        else timeOut--;
//        if (bufIdx > numChars) return(-2);
////        if (inCh == CARRIAGE_RETURN)
////        {
////            buffer[bufIdx] = 0;
////
////            putChar('\n');      // For terminal
////            return(bufIdx);
////        }
//        delay_us(10);     // TODO improve
//    }
//    if (timeOut == 0) return(-1);
//    else return(bufIdx);
//}

    // USART#2. Print string up to IO_BFR_SIZE chars
void putStr2(char *str)
{
    byte me;
    for (me = 0; me < IO_BFR_SIZE; me++)
    {
        if (str[me] == 0) return;
        putChar2(str[me]);
        U2TXREG = str[me];
        while (U2STAbits.UTXBF) ;
    }
}

    // Print string up to IO_BFR_SIZE chars
void putStr(char *str)   //, bool txCtrl)
{
    byte me;

    //if(txCtrl) rs485TransmitEna(1);     // In chains of putStr() toggling this confuses the half-duplex RS-485 driver
    rs485TransmitEna(true);
    for (me = 0; me < IO_BFR_SIZE; me++)
    {
        if (str[me] == 0) return;
        U1TXREG = str[me];
        while (U1STAbits.UTXBF) ;
    }
   // if(txCtrl) rs485TransmitEna(0);
    rs485TransmitEna(false);
}

void putChar(char c)  // , bool txCtrl)
{
    // if(txCtrl) rs485TransmitEna(1);
    rs485TransmitEna(true);
    U1TXREG = c;
    while (U1STAbits.UTXBF) ;
   // if(txCtrl) rs485TransmitEna(0);
    rs485TransmitEna(false);
}

    // USART#2
void putChar2(char c)
{
    U2TXREG = c;
    while (U2STAbits.UTXBF) ;
}

char *itoa(char *buf, int val, int base)
{
    char *cp = buf;

    if(val < 0)
    {
	*buf++ = '-';
	val = -val;
    }
    utoa(buf, val, base);
    return cp;
}

char *utoa(char *buf, unsigned val, int base)
{
    unsigned	v;
    char	c;

    v = val;
    do
    {
	v /= base;
	buf++;
    } while(v != 0);
    *buf-- = 0;
    do
    {
	c = val % base;
	val /= base;
	if(c >= 10)
	c += 'A' - '0' - 10;
	c += '0';
	*buf-- = c;
    } while(val != 0);
    return buf;
}
