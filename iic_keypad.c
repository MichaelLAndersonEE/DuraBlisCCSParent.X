/* File:   iic_keypad.c
 * IIC interface to capacitive keypad
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * Created: 17 Sep14
 * CAP1296 IIC address is hard-coded in here.  Single purposed code.
 */

#include <xc.h>
#include "DuraBlisCCSParent.h"
#include "iic_keypad.h"
#include "buzzer.h"

#define ADDR_CAP1296    0x50    // Write, read is 0x51
#define ADDR_24AA08     0xA0    // SEEPROM, for test
#define IIC_ACK_n       1
#define IIC_NACK        2

#define CAP1296REGs_SensorInputStatus   0x03

int keypadArm(byte register2Arm);
void iicInit(void);
byte iicStart(void);
void iicStop(void);
int iicSend(byte address, byte datum);
int iicReadByte(byte address, byte *byt);
byte iic_xmit(byte datum);
byte iic_rcv(byte *byt, byte opMode);
void iic_reset(void);

//byte keypadRead(byte opMode)
//{
//    byte retVal;
//    retVal = iicDirectRead(0x03);         // 0x03 is key read
//    if (retVal > 0)         // DEB (opMode == KEYPAD_LOQUACIOUS)
//    {
//        sprintf(ioBfr, "[%02X] ", retVal);
//        putStr(ioBfr, true);
//    }
//    return(retVal);
//}
void keypadInit(void)
{
    extern unsigned sysFault;

  
    iicInit();
    sysFault &= ~FLT_IICFAIL;
    if (iicSend(0x05, 0x6A) < 0)        // Shd be 0x00, 0x00
        sysFault |= FLT_IICFAIL;        // Main control reg @ 00, active state, low gain
//    if (keypadArm(CAP1296REGs_SensorInputStatus) < 0)
//        sysFault |= FLT_IICFAIL;

}

    // Preload CAP1296 register for direct reads ('Send Byte')
int keypadArm(byte register2Arm)
{
    if (!iicStart()) return(-1);
    if (!iic_xmit(ADDR_CAP1296)) return(-2);
    if (!iic_xmit(register2Arm)) return(-3);
    iicStop();
    return(1);
}

    // Must follow after keypadArm  ('Receive Byte').
byte keypadRead(byte opMode)
{
    byte byt = 0, retVal = 0;

   // return(0);      // DEB

    if (opMode == KEYPAD_LOQUACIOUS)
     {
        if (!iicStart()) { putStr("k1 ", true);  return(0); }
        if (!iic_xmit(ADDR_CAP1296 + 1)) { putStr("k2 ", true);  return(0); }       // Read mode
        if(!iic_rcv(&byt, IIC_NACK)){ putStr("k3 ", true);  return(0); }
        iicStop();
        return(byt);
     }



    iicStart();
   // else if (!iic_xmit(ADDR_CAP1296 + 1)) return(0);        // Read mode
    iic_xmit(ADDR_24AA08); // DEB

    iicStop();
    return(0);      // DEB
    //return(byt);
}

void iicStop(void)
{
    I2C1CONbits.PEN = 1;     // Initiate Stop
  //  while(!I2C1STATbits.P) ;
    //delay_us(20);
}

void iicInit(void)
{
    I2C1BRG = 0x50;         // Nominal 200 kHz  0x30
    I2C1CONbits.SMEN = 1;   // CAP1296 is really an SMEM device
    I2C1CONbits.ON = 1;
    //iicStop();
}

byte iicStart(void)
{
    I2C1CONbits.SEN = 1;
    while(!I2C1STATbits.S) ;
    return(1);
//
//    if (I2C1STATbits.P)         // Effectively means we are in Idle
//    {
//        I2C1CONbits.SEN = 1;    // Initiate Start
//        // putChar('P', true); // DEB
//        return(1);              // Signal success
//    }
//    else
//    {
//        iicStop();              // Initate stop
//       // putChar('S', true); // DEB
//        return(0);              // Signal failure
//    }
}

int iicSend(byte address, byte datum)
{
    if (iicStart() == 0) return(-1);
    if (iic_xmit(ADDR_24AA08) == 0) return(-2);    // ADDR_CAP1296
  
//    if (!iic_xmit(address)) return(-3);
//    if (!iic_xmit(datum)) return(-4);
    iicStop();
    return(1);
}

int iicReadByte(byte address, byte *byt)
{  
    if (!iicStart()) return(-1);
    if (!iic_xmit(ADDR_CAP1296)) return(-2);
    if (!iic_xmit(address)) return(-3);
    I2C1CONbits.RSEN = 1;                       // Re-Start
    if (!iic_xmit(ADDR_CAP1296 | 0x01)) return(-5);     // Read now
    if(!iic_rcv(byt, IIC_ACK_n)) return(-6);
    iicStop();

//        sysFault |= FLT_IICFAIL;
    return(1);
}

byte iic_rcv(byte *byt, byte opMode)
{
    unsigned timeOut = 10000;
    if (opMode == IIC_ACK_n) I2C1CONbits.ACKDT = 0;
    else I2C1CONbits.ACKDT = 1;
    I2C1CONbits.RCEN = 1;
    while(timeOut--)
    {
        if (!I2C1STATbits.RBF) break;
    }
    if (timeOut)
    {
        I2C1CONbits.ACKEN;
        *byt = I2C1RCV;
        return(1);
    }
    else return(0);
}

byte iic_xmit(byte datum)
{
 //   unsigned timeOut = 10000;

  
    if (!I2C1STATbits.S)
    {
        if (I2C1STATbits.ACKSTAT) putChar('a', true);
        if (I2C1STATbits.BCL) putChar('b', true);
        if (I2C1STATbits.TBF) putChar('f', true);
        if (I2C1STATbits.IWCOL) putChar('i', true);
        if (I2C1STATbits.P) putChar('p', true);
       // if (I2C1STATbits.S) putChar('s', true);
        if (I2C1STATbits.TRSTAT) putChar('t', true);
        putChar(' ', true);
//        sprintf(ioBfr, "%02X ", (byte) I2C1STAT);        // DEB
//        putStr(ioBfr, true);
    }

    I2C1STATbits.IWCOL = 0;         // In case there had been a collision
  //  if (I2C1STATbits.BCL) { iic_reset(); return(0); }
  //while(I2C1STATbits.TRSTAT) ;       // Wait for any previous to clear
   
    I2C1TRN = datum;
    while(I2C1STATbits.TRSTAT) ;       // Wait for it to clear
   //elay_us(20);

    if (I2C1STATbits.ACKSTAT) return(0);  // ACK high here means failure
    return(1);

//    while(timeOut--)
//    {
 //       if (!I2C1STATbits.ACKSTAT) break;
  //  }
//    if (timeOut)
//    {
//        return(1);
//    }
//   putChar('x', true); // DEB
//    return(0);
}

void iic_reset(void)
{
    I2C1CONbits.ON = 0;
    delay_us(20);
    I2C1CONbits.ON = 1;
}