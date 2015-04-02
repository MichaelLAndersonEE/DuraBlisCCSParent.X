/* File:   iic_keypad.c
 * IIC interface to capacitive keypad
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * Created: 17 Sep14
 * CAP1296 is hard-coded in here.
 */

#include <xc.h>
#include "DuraBlisCCSParent.h"
#include "iic_keypad.h"
#include "buzzer.h"

#define ADDR_CAP1296       0x50     // Write, read is 0x51

void iicInit(void);
byte iicRead(byte address);
void iicStart(void);
int iic_xmit(byte datum);
byte iic_rcv(void);

byte keypadRead(byte opMode)
{
    byte retVal;
    retVal = iicRead(0x03);         // 0x03 is key read
    if (retVal > 0)         // DEB (opMode == KEYPAD_LOQUACIOUS)
    {
        sprintf(ioBfr, "[%02X] ", retVal);
        putStr(ioBfr, true);
    }
    return(retVal);    
}
void keypadInit(void)
{
    iicInit();
    iicSend(0x00, 0x00);        // Main control reg @ 00, active state, low gain
}

void iicStop(void)
{
    IIC_SDA = 0;
    IIC_SCL = 1;
    delay_us(8);
    IIC_SDA = 1;
}

void iicInit(void)
{
    iicStop();
  //  delay_us(10);
}

void iicStart(void)
{
    if (IIC_SCL == 0 || IIC_SDA == 0)
    {       
        IIC_SDA = 1;
        delay_us(2);
        IIC_SCL = 1;
        delay_us(8);
    }
    IIC_SDA = 0;
    delay_us(8);
}

int iicSend(byte address, byte datum)
{
    iicStart();
    if (iic_xmit(ADDR_CAP1296) < 0) return(-1);
    if (iic_xmit(address) < 0) return(-2);
    if (iic_xmit(datum) < 0) return(-3);
    iicStop();
    return(1);
}

byte iicRead(byte address)
{
    extern unsigned sysFault;
    static byte faultCtr;
    byte retVal;

    iicStart();
    if (iic_xmit(ADDR_CAP1296) < 0) { faultCtr++; retVal = 0; }
    if (iic_xmit(address) < 0) { faultCtr++; retVal = 0; }
    iicStart();                                 // Re-Start
    if (iic_xmit(ADDR_CAP1296 | 0x01) < 0) { faultCtr++; retVal = 0; }   // Read now
    retVal = iic_rcv();

  //  delay_us(50);       // DEB
    iicStop();
    
    if (faultCtr > 6)
    {
        faultCtr = 6;
        sysFault |= FLT_IICFAIL;
    }
    else if (retVal)
    {
        sysFault &= ~FLT_IICFAIL;
        faultCtr = 0;
    }
    //if (retVal) putChar('@' + retVal);  // DEB
    return(retVal);
}

byte iic_rcv(void)
{
    byte mask = 0x80;
    byte retVal = 0;

    IIC_SCL = 0;
    delay_us(2);
    IIC_SDA_TRIS = 1;
    delay_us(2);
    while(mask)
    {
        IIC_SCL = 1;
        delay_us(2);
        if (IIC_SDA == 1) retVal |= mask;
        delay_us(2);
        IIC_SCL = 0;
        mask >>= 1;
        if (mask) delay_us(2);
    }
    IIC_SDA == 1;               // NACK
    IIC_SDA_TRIS = 0;
    delay_us(2);
    IIC_SCL = 1;
    delay_us(2);
    IIC_SCL = 0;
//    if (retVal > 0)         // DEB (opMode == KEYPAD_LOQUACIOUS)
//    {
//        sprintf(ioBfr, "[%02X] ", retVal);
//        putStr(ioBfr, true);
//    }
    return(retVal);
}

int iic_xmit(byte datum)
{
    byte mask = 0x80;
    int retVal = -1;

    IIC_SCL = 0;
    delay_us(4);
    while(mask)
    {       
        if (datum & mask) IIC_SDA = 1;
        else IIC_SDA = 0;       
        delay_us(3);
        IIC_SCL = 1;
        delay_us(6);
        IIC_SCL = 0;
        mask >>= 1;
        if (mask) delay_us(1);
    } 
    IIC_SDA = 0;
    IIC_SDA_TRIS = 1;
    delay_us(2);
    IIC_SCL = 1;
    delay_us(2);
    if (IIC_SDA == 0) retVal = 1;   // ACK
    delay_us(2);
    IIC_SCL = 0;
    delay_us(2);
    IIC_SDA = 0;
    IIC_SDA_TRIS = 0;
    delay_us(2);
  //  IIC_SDA_TRIS = 0;
//    delay_us(4);
//    IIC_SDA = 0;
    return(retVal);
}