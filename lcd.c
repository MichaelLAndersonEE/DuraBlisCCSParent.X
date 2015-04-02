/* File:   lcd.c  low and high level display functions
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * LCD:  NHD NHD-C12864WC-FSW-FBW-3V3, 128 x 64 pixels
 * Strapped in parallel mode, 8080 series timing
 * Created: 08 Sep14
 */

#define _SUPPRESS_PLIB_WARNING
#include <xc.h>
#include <plib.h>
#include "DuraBlisCCSParent.h"
#include "lcd.h"
#include "screens.h"
#include "fonts.h"

#define NUM_COLS    128
#define NUM_ROWS    64

static void _lcdWriteCommand(byte datum);
static void _lcdWriteDatum(byte datum);
static byte _lcdReadDatum(void);
static byte _lcdReadStatus(void);
static void _lcdDelay(unsigned);

void lcdUpDnEnEs(void)
{
  // lcdSoftkeys1("Up", "Down", "Enter", "Esc");
    lcdSoftkeys1("  ^", "  _", "Enter", "Esc");
}

void lcdCenter(byte page, char *str)
{
    lcdStrAt(page, 64 - 3 * strlen(str), str);
}

void lcdSoftkeys1(char *strA, char *strB, char *strC, char *strD)
{
    lcdStrAt(1, 1, strA);
    lcdStrAt(1, 33, strB);
    lcdStrAt(1, 65, strC);
    lcdStrAt(1, 97, strD);
}

void lcdSoftkeys0(char *strA, char *strB, char *strC, char *strD)
{
    lcdStrAt(0, 1, strA);
    lcdStrAt(0, 33, strB);
    lcdStrAt(0, 65, strC);
    lcdStrAt(0, 97, strD);
}

void lcdStrAt(byte page, byte col, char *str)
{
    byte c = 0;
    while(str[c] != 0)
    {        
        lcdCharAt(page, col + 6 * c, str[c]);   // TODO check line overrun?
        if (c++ > 30) return;
    }
}

void lcdClearPage(byte page)        // A page is a text row
{
    byte c;
    for (c = 0; c < 30; c++)
    {
        lcdCharAt(page, 6 * c, ' ');
    }
}

void lcdReverseStrAt(byte page, byte col, char *str)
{
    byte c = 0;
    while(str[c] != 0)
    {
        lcdReverseCharAt(page, col + 6 * c, str[c]);   // TODO check line overrun?
        if (c++ > 30) return;
    }
}

void lcdReverseCharAt(byte page, byte col, char ch)
{
    byte colX;
    lcdPageAddress(page);
    lcdColumnAddressUpper(col>>4);
    lcdColumnAddressLower(col);
    for (colX = 0; colX < 5; colX++)
        _lcdWriteDatum(~((byte) *(fontSet5x7 + 5 * (ch - 32) + colX)));
    _lcdWriteDatum(0xFF);       // Relief space

//    lcdPageAddress(page - 1);   // Relief space below.
//    lcdColumnAddressUpper(col>>4);
//    lcdColumnAddressLower(col);
//    for (colX = 0; colX < 5; colX++)
//        _lcdWriteDatum(0x80);
//    _lcdWriteDatum(0x80);
}

void lcdCharAt(byte page, byte col, char ch)
{
    byte colX;
    col += 4;   // DEB
    lcdPageAddress(page);
    lcdColumnAddressUpper(col>>4);
    lcdColumnAddressLower(col);  
    for (colX = 0; colX < 5; colX++)
        _lcdWriteDatum((byte) *(fontSet5x7 + 5 * (ch - 32) + colX));
    _lcdWriteDatum(0x00);       // Relief space
}

void lcdScreen(byte pic[])
{
    unsigned col, colOffset = 0;
    byte page = 0;
 
    for(page=0; page<8; page++)
    {
            // _lcdWriteCommand(page);
        lcdPageAddress(page);
            //_lcdWriteCommand(0x10);     //
        lcdColumnAddressUpper(0);
            //_lcdWriteCommand(0x00);     //
        lcdColumnAddressLower(4);       // PROD inverted display requires offset 4

	for (col=0; col<128; col++)
	{
                _lcdWriteDatum(pic[col + colOffset]);
	}
        colOffset += NUM_COLS;
            // page++;
    }
}

void lcdInit(void)
{
    LCD_RES_n = 0;
    _lcdDelay(0xFFF0);
    LCD_RES_n = 1;
    LCD_RD_n = 1;
    LCD_WR_n = 1;
    LCD_CS1_n = 1;

    _lcdWriteCommand(0xA2);     // 1/9 bias
    _lcdWriteCommand(0xA1);     // ADC segment driver direction: normal A0 | reverse A1 [PROD]
    _lcdWriteCommand(0xC8);     // Common output mode select: normal C0 | reverse C8 [PROD]
        // In production we want display inverted, so do A1 & C8
    _lcdWriteCommand(0x40);     // Operating Mode
    _lcdWriteCommand(0x25);     // Resistor ratio
    _lcdWriteCommand(0x81);     // Electronic volume mode set
    _lcdWriteCommand(0x2F);     // Electronic volume register set, 19
    _lcdWriteCommand(0x2F);     // Power control set
    _lcdWriteCommand(0xAF);     // Display ON/OFF - set to ON

//    _lcdWriteCommand(0xA0);     // ADC select. A1 for reverse
    _lcdWriteCommand(0xA7);     // A6: Display normal. A7 for reverse

    lcdStartLine(0);
}

    // ------------------------
void lcdOnOff(byte onOff)                 // 1/0
{
    if (onOff) _lcdWriteCommand(0xAF);  // On
    else _lcdWriteCommand(0xAE);        // Off
}

    // -------------------------
void lcdStartLine(byte startAddress)   // 6 bit right-just display RAM start line address
{
    byte arg = 0x40;
    arg |= (startAddress & 0x3F);
    _lcdWriteCommand(arg);
}

    // ------------------------
void lcdPageAddress(byte startAddress) // 4 bit right-just page address
{
    byte arg = 0xB0;
    arg |= (startAddress & 0x0F);
    _lcdWriteCommand(arg);
}

    // -----------------------
void lcdColumnAddressUpper(byte mostSigColAddress)    // Most sig 4 bits right-just of display RAM col address
{
    byte arg = 0x10;
    arg |= (mostSigColAddress & 0x0F);
    _lcdWriteCommand(arg);
}

void lcdColumnAddressLower(byte leastSigColAddress)   // Least sig 4 bits right-just of display RAM col address
{
    byte arg = 0x00;
    //leastSigColAddress += 4;        // DEB
    arg |= (leastSigColAddress & 0x0F);
    _lcdWriteCommand(arg);
}

//    // -----------------------
//byte lcdStatusRead(void)              // Upper nibble is Status
//{
//    return(_lcdReadStatus());
//}

//    // -------------------------
//void lcdDataWrite(byte datum)
//{
//    _lcdWriteDatum(datum);
//}
//
//    // ----------------------
//byte lcdDataRead(void)
//{
//    return(_lcdReadDatum());
//}

    // ----------------------
void lcdAllPointsOnOff(byte onOff)
{
    if (onOff) _lcdWriteCommand(0xA5);  // All on
    else _lcdWriteCommand(0xA4);        // Normal display
}

void lcdReadModifyWrite(byte onOff)
{
    if (onOff) _lcdWriteCommand(0xE0);  // Increments column address every call
    else _lcdWriteCommand(0xEE);        // Clears process
}

void lcdReset(void)
{
    _lcdWriteCommand(0xE2);
}

    // --------------- Low Level Funcitons ------------------
void _lcdDelay(unsigned T)
{
    while(T) T--;
}

    // Note, we write to LATD4..11; read from PORTD4..11
void _lcdWriteCommand(byte cmd)
{
    PORTSetPinsDigitalOut(IOPORT_D, BIT_11 | BIT_10 | BIT_9 | BIT_8 | BIT_7 | BIT_6 | BIT_5 | BIT_4 );
    LCD_CS1_n = 0;
    LCD_A0 = 0;
   
    if (cmd & 0x01) LCD_WD0 = 1; else LCD_WD0 = 0;
    if (cmd & 0x02) LCD_WD1 = 1; else LCD_WD1 = 0;
    if (cmd & 0x04) LCD_WD2 = 1; else LCD_WD2 = 0;
    if (cmd & 0x08) LCD_WD3 = 1; else LCD_WD3 = 0;
    if (cmd & 0x10) LCD_WD4 = 1; else LCD_WD4 = 0;
    if (cmd & 0x20) LCD_WD5 = 1; else LCD_WD5 = 0;
    if (cmd & 0x40) LCD_WD6 = 1; else LCD_WD6 = 0;
    if (cmd & 0x80) LCD_WD7 = 1; else LCD_WD7 = 0;
    //lcdDelay(10);    // 40 ns wait  Tds6 = write data setup time
    LCD_WR_n = 0;
    _lcdDelay(10);    // 40 ns wait  Tds6 = write data setup time
    LCD_WR_n = 1;
    LCD_CS1_n = 1;
    _lcdDelay(10);
}

void _lcdWriteDatum(byte datum)
{
    PORTSetPinsDigitalOut(IOPORT_D, BIT_11 | BIT_10 | BIT_9 | BIT_8 | BIT_7 | BIT_6 | BIT_5 | BIT_4 );
//    LCD_RW = 0;
    LCD_CS1_n = 0;
    LCD_A0 = 1;
//    LCD_E = 1;
    if (datum & 0x01) LCD_WD0 = 1; else LCD_WD0 = 0;
    if (datum & 0x02) LCD_WD1 = 1; else LCD_WD1 = 0;
    if (datum & 0x04) LCD_WD2 = 1; else LCD_WD2 = 0;
    if (datum & 0x08) LCD_WD3 = 1; else LCD_WD3 = 0;
    if (datum & 0x10) LCD_WD4 = 1; else LCD_WD4 = 0;
    if (datum & 0x20) LCD_WD5 = 1; else LCD_WD5 = 0;
    if (datum & 0x40) LCD_WD6 = 1; else LCD_WD6 = 0;
    if (datum & 0x80) LCD_WD7 = 1; else LCD_WD7 = 0;
    LCD_WR_n = 0;
    _lcdDelay(10);    // 40 ns wait  Tds6 = write data setup time
    LCD_WR_n = 1;
    LCD_CS1_n = 1;
    _lcdDelay(10);
}

byte _lcdReadDatum(void)
{
    byte retVal = 0;
    PORTSetPinsDigitalIn(IOPORT_D, BIT_11 | BIT_10 | BIT_9 | BIT_8 | BIT_7 | BIT_6 | BIT_5 | BIT_4 );
    LCD_CS1_n = 0;
    LCD_A0 = 1;
    LCD_RD_n = 0;
    _lcdDelay(10);
    if (LCD_RD0 & 0x01) retVal |= 0x01;
    if (LCD_RD1 & 0x02) retVal |= 0x02;
    if (LCD_RD2 & 0x04) retVal |= 0x04;
    if (LCD_RD3 & 0x08) retVal |= 0x08;
    if (LCD_RD4 & 0x10) retVal |= 0x10;
    if (LCD_RD5 & 0x20) retVal |= 0x20;
    if (LCD_RD6 & 0x40) retVal |= 0x40;
    if (LCD_RD7 & 0x80) retVal |= 0x80;
    LCD_RD_n = 1;
    LCD_CS1_n = 1;
    _lcdDelay(10);
    return(retVal);
}

byte _lcdReadStatus(void)
{
    byte retVal = 0;
    PORTSetPinsDigitalIn(IOPORT_D, BIT_11 | BIT_10 | BIT_9 | BIT_8 | BIT_7 | BIT_6 | BIT_5 | BIT_4 );   
    LCD_CS1_n = 0;
    LCD_A0 = 0;
    LCD_RD_n = 0;
    _lcdDelay(10);
    if (LCD_RD0 & 0x01) retVal |= 0x01;
    if (LCD_RD1 & 0x02) retVal |= 0x02;
    if (LCD_RD2 & 0x04) retVal |= 0x04;
    if (LCD_RD3 & 0x08) retVal |= 0x08;
    if (LCD_RD4 & 0x10) retVal |= 0x10;
    if (LCD_RD5 & 0x20) retVal |= 0x20;
    if (LCD_RD6 & 0x40) retVal |= 0x40;
    if (LCD_RD7 & 0x80) retVal |= 0x80;
    LCD_RD_n = 1;
    LCD_CS1_n = 1;
    _lcdDelay(10);
    return(retVal);
}

