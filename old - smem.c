/* File:   smem.c  SEEPROM SPI2-based functions, incl datalog
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DuraBlis-CCS-Main_v4 board.
 * SEEPROM: 25AA128, 16 Kbyte, 256 64-byte pages
 * Created: 02 Oct 14
 * A flote is a signed int = float * 100.  (Obsolete)
 */

#include <xc.h>
#include <string.h>
#include "DuraBlisCCSParent.h"
#include "smem.h"
#include "serial.h"
#include "time.h"
#include "keypad.h"

    // SMEM semantics
#define SMEM_DUMMY  0x00
#define SMEM_READ   0x03
#define SMEM_WRITE  0x02
#define SMEM_WRDI   0x04
#define SMEM_WREN   0x06
#define SMEM_RDSR   0x05
#define SMEM_WRSR   0x01

#define SMEM_NUM_PAGES      256
#define SMEM_PAGE_SIZE       64
#define SMEM_ARRAY_SIZE      64     // Max bytes in buffer for any one rd/wr block
#define SMEM_ADDR_TIME        0     // Page 0 for time save
#define SMEM_ADDR_PARMS      64     // Page 1 for parms (cal consts, etc)
#define SMEM_ADDR_ISEN_CAL  128     // Page 2 for iSen cal factors
#define SMEM_ADDR_RESMTX    192     // Page 3 for resourceMatrix
#define SMEM_ADDR_DATALOG   256     // Pages 4..204 for DL

#define DL_MAX_RECORDS     1600     // 8 doubles per page x 200 pgs
#define DL_RECORD_SIZE        4     // Bytes, = 2 flotes now

    // Stuffing means putting bytes in ioBfr for transfer, indexed by dAx  OBSOLETE
//static void smem_StuffString(char *strPtr, byte strLen);
//static int smem_UnstuffString(char *strPtr, byte strLen);
//static void smem_StuffFlote(float f);
//static float smem_UnstuffFlote(void);
//static void smem_StuffUnsigned(unsigned u);
//static unsigned smem_UnstuffUnsigned(void);

static int smem_ReadArray(unsigned addr, byte *arrayPtr, unsigned arraySize);
static int smem_WriteArray(unsigned addr, byte *arrayPtr, unsigned arraySize);
static bool smem_Busy(void);
static byte spi2_WRBuffer(byte datum);

extern struct t_struct time;                    // Page 0
extern struct operatingParms_struct oP;         // Page 1
extern double iSenCalFactor [NUM_CHILDREN];     // Page 2
extern struct res_struct resourceMatrix;        // Page 3.  Datalog uses Pages 4..204

    // Volatile
extern float temperNowF, rhumidNow;
static unsigned dlPage, dlPageIdx;

    // dlPtr and dlRecordsSaved form a wrap around buffer.  dlPtr increments to DL_MAX_RECORDS * DL_RECORD_SIZE, then
    // resets to 0 again.  dlRecordsSaved increases by DL_RECORD_SIZE until it hits DL_MAX_RECORDS * DL_RECORD_SIZE.
//byte dAx = 0;       // dataArray index  TODO??
char smemBfr[31];   // For debugging
//byte *dlogPtr;

    // Destructive erasure of entire SEEPROM.
void smemEraseAll(void)
{
    unsigned page, d;

    for (d = 0; d < SMEM_ARRAY_SIZE; d++) ioBfr[d] = 0xFF;

    putStr("\tSmem destructive erase all...\r\n");
    for (page = 0; page < SMEM_NUM_PAGES; page++)
    {
         if (smem_WriteArray(page * SMEM_PAGE_SIZE, (byte *) ioBfr, SMEM_ARRAY_SIZE) != SMEM_ARRAY_SIZE)
         {
             putStr("\tsmemEraseAll failed.\r\n");
             return;
         }        
         petTheDog();
    }

    sprintf(ioBfr, "\tErased %d pages of smem.\r\n", SMEM_NUM_PAGES);
    putStr(ioBfr);
}

        // Destructive write to entire SEEPROM.  TODO Test cross-page writes.
void smemWriteTest(void)
{
    unsigned page, d;
    char testStr[SMEM_ARRAY_SIZE + 1];

    // "Page 000 : 012345678901234567890123456789012345678901234567890123456789

    for (d = 0; d < 40; d++) testStr[d] = '0' + (d % 10);
    testStr[d] = 0;

    putStr("\tsmem destructive write test...\r\n");
    for (page = 0; page < SMEM_NUM_PAGES; page++)
    {
         sprintf(ioBfr, "Page %03d : ", page);
         strcat(ioBfr, testStr);
         if (smem_WriteArray(page * SMEM_PAGE_SIZE, (byte *) ioBfr, SMEM_ARRAY_SIZE) != SMEM_ARRAY_SIZE)
         {
             putStr("\tsmemWriteTest failed.\r\n");
             return;
         }
         else
         {
             putStr(ioBfr);
             putStr("\r\n");
         }
         petTheDog();
    }
   
    sprintf(ioBfr, "\tWrote %d pages to smem.", SMEM_NUM_PAGES);
    putStr(ioBfr);    
}

void smemReadTest(void)
{
    unsigned page;
    for (page = 0; page < SMEM_NUM_PAGES; page++)
    {
         if (smem_ReadArray(page * SMEM_PAGE_SIZE, (byte *) ioBfr, SMEM_ARRAY_SIZE) != SMEM_ARRAY_SIZE)
         {
             putStr("\tsmemReadTest failed.\r\n");
             return;
         }
         else
         {
             putStr(ioBfr);
             putStr("\r\n");
         }
         petTheDog();
    }


//    smem_ReadArray(SMEM_ADDR_PARMS, (byte *) ioBfr, SMEM_ARRAY_SIZE);
//    ioBfr[SMEM_ARRAY_SIZE] = 0;
//    putStr(ioBfr);
}

    // Called nominally every 5 mins to keep a rough nonvol clock
int smemWriteTime(void)
{
    ioBfr[0] = time.year;
    ioBfr[1] = time.month;
    ioBfr[2] = time.dayMonth;
    ioBfr[3] = time.hour;
    ioBfr[4] = time.minute;

    return (smem_WriteArray(SMEM_ADDR_TIME, (byte *) ioBfr, 5));
}

    // Called at reset.  Has non-recursive retry.
int smemReadTime(void)
{
    bool success = true;
    
    if (smem_ReadArray(SMEM_ADDR_TIME, (byte *) ioBfr, 5) < 5) success = false;

    if (ioBfr[0] > 0 && ioBfr[0] < 100) time.year = ioBfr[0];   // Years from (20)14 - (20)99
    else success = false;
    if (ioBfr[1] > 0 && ioBfr[1] < 13) time.month = ioBfr[1];
    else success = false;
    if (ioBfr[2] > 0 && ioBfr[2] < 32) time.dayMonth = ioBfr[2];
    else success = false;
    if (ioBfr[3] < 60) time.hour = ioBfr[3];
    else success = false;
    if (ioBfr[4] < 60) time.minute = ioBfr[4];
    else success = false;

    if (success == false)
    {
        if (smem_ReadArray(SMEM_ADDR_TIME, (byte *) ioBfr, 5) == 5) return(-1);

        if (ioBfr[0] > 0 && ioBfr[0] < 100) time.year = ioBfr[0];
        else return(-2);
        if (ioBfr[1] > 0 && ioBfr[1] < 13) time.month = ioBfr[1];
        else return(-3);
        if (ioBfr[2] > 0 && ioBfr[2] < 32) time.dayMonth = ioBfr[2];
        else return(-4);
        if (ioBfr[3] < 60) time.hour = ioBfr[3];
        else return(-5);
        if (ioBfr[4] < 60) time.minute = ioBfr[4];
        else return(-6);
    }

//    sprintf(ioBfr, "\ty%d m%d d%d h%d m%d\n\r", time.year, time.month, time.dayMonth, time.hour, time.minute );    // DEB
//    putStr(ioBfr);  // DEB
    oP.sysStat &= ~ST_CLOCKNOTSET;
    return (5);
}

    // TODO regenerate actual times
int smemDatalogReport(void)
{   
    unsigned r;
    float T, RH;

    if (oP.dlRecordsSaved == 0)
    {
        putStr("\tNo datalog records saved.\n\r");
        return(0);
    }                       // We will count down from here, backward in time

    sprintf(ioBfr, "\n\r// #,\toF,\t%%RH\t[%d records]\n\r", oP.dlRecordsSaved / DL_RECORD_SIZE);
    putStr(ioBfr);
    smem_ReadArray(SMEM_ADDR_DATALOG, (byte *) ioBfr, SMEM_ARRAY_SIZE);
    dAx = 0;
    for (r = 0; r < oP.dlRecordsSaved / DL_RECORD_SIZE; r++)
    {
        T = smem_UnstuffFlote();
        RH = smem_UnstuffFlote();
        sprintf(smemBfr, "%3d, \t%2.1f, \t%2.1f,\n\r", r + 1, T, RH);
        putStr(smemBfr);
        dAx += DL_RECORD_SIZE;
    }
    return(r);         // DEB

//    addr = SMEM_ADDR_DATALOG + dlPtr - 4;        // TODO do wraparound
//
//    sprintf(ioBfr, "\n\r// #,\toF,\t%%RH\t[%d records]\n\r", dlRecordsSaved / 4);
//    putStr(ioBfr);
//
//    //addr = SMEM_ADDR_DATALOG + 4 * dlRecordsSaved;
//
//    for (r = 0; r < dlRecordsSaved; r += 4)
//    {
//        addr -= 4;
//        if (smem_ReadArray(addr, ioBfr, 4) != 4) return(-1);
//        T = smem_UnstuffFlote();
//        RH = smem_UnstuffFlote();
//        sprintf(ioBfr, "%3d, \t%2.1f, \t%2.1f,\n\r", ri++, T, RH);
//        putStr(ioBfr);
//    }
//    return(r);
}

    // Called every time t (nom 5 min).  Smem writes latest T & RH as flotes.
void smemDatalog(void)
{
    dAx = 0;
    smem_StuffFlote(temperNowF);
    smem_StuffFlote(rhumidNow);
    smem_WriteArray(SMEM_ADDR_DATALOG + dlPtr, ioBfr, DL_RECORD_SIZE);
    if (dlPtr < DL_MAX_RECORDS - DL_RECORD_SIZE)
    {
        dlPtr += DL_RECORD_SIZE;     // Bytes
        oP.dlRecordsSaved = dlPtr;
    }
    else dlPtr = 0;
}

int smemReadParmsTest(void)
{
    unsigned dummyUns;
    unsigned dummyFlo;
    char dummyStr[31];
    byte i;

    putStr("\r\n\t* sRP Test *\r\n");
    smem_ReadArray(SMEM_ADDR_PARMS, (byte *) ioBfr, SMEM_ARRAY_SIZE);

    for (i = 0; i <SMEM_ARRAY_SIZE; i++)
    {
        sprintf(smemBfr, "%02d: %02X\r\n", i, ioBfr[i]);
        putStr(smemBfr);
    }
    putStr("\r\n");
    dAx = 0;
    dummyUns = smem_UnstuffUnsigned();
    sprintf(smemBfr, " sysStat: %04X\r\n", dummyUns);
    putStr(smemBfr);

    dummyUns = smem_UnstuffUnsigned();
    sprintf(smemBfr, " dlRecordsSaved: %04X\r\n", dummyUns);
    putStr(smemBfr);

    smem_UnstuffString(dummyStr, 11);
    sprintf(smemBfr, " uniqueID: %s\r\n", dummyStr);
    putStr(smemBfr);
 
    dummyFlo = smem_UnstuffFlote();
    sprintf(smemBfr, " setPointTempF: %3.02f\r\n", dummyFlo);
    putStr(smemBfr);

    dummyFlo = smem_UnstuffFlote();
    sprintf(smemBfr, " setPointRelHum: %3.02f\r\n", dummyFlo);
    putStr(smemBfr);

    dummyFlo = smem_UnstuffFlote();
    sprintf(smemBfr, " temperCalFactor: %3.02f\r\n", dummyFlo);
    putStr(smemBfr);

    dummyFlo = smem_UnstuffFlote();
    sprintf(smemBfr, " rhumidCalFactor: %3.02f\r\n\n", dummyFlo);
    putStr(smemBfr);
}

    // Called on startup reset and as factory setting restore of operating parms
    // One non-recursive retry.  TODO non-self-referential checksum.
    // Returns num bytes read on success or neg failure code.
int smemReadParms(void)
{
    struct operatingParms_struct oPBfr;
    int success = 1;
    byte d, *dPtr;
    byte ch;

    if (smem_ReadArray(SMEM_ADDR_PARMS, (byte *) ioBfr, SMEM_ARRAY_SIZE) < SMEM_ARRAY_SIZE) success = -1;

    dPtr = (byte *) &oPBfr;
    for (d = 0; d < SMEM_ARRAY_SIZE; d++)
        *(dPtr + d) = ioBfr[d];

    if (oPBfr.sysStat & ST_ALWAYS_TRUE) oP.sysStat = oPBfr.sysStat;
    else success = -2;

    if (oPBfr.dlRecordsSaved < DL_MAX_RECORDS) oP.dlRecordsSaved = oPBfr.dlRecordsSaved;
    else success = -3;

    if (oPBfr.setPointTempF > SET_TEMP_MIN && oPBfr.setPointTempF <  SET_TEMP_MAX) oP.setPointTempF = oPBfr.setPointTempF;
    else success = -4;

    if (oPBfr.setPointRelHum > SET_RH_MIN && oPBfr.setPointRelHum <  SET_RH_MAX) oP.setPointRelHum = oPBfr.setPointRelHum;
    else success = -5;

    if (oPBfr.temperCalFactor > CAL_FACTOR_MIN && oPBfr.temperCalFactor < CAL_FACTOR_MAX) oP.temperCalFactor = oPBfr.temperCalFactor;
    else success = -6;

    if (oPBfr.rhumidCalFactor > CAL_FACTOR_MIN && oPBfr.rhumidCalFactor < CAL_FACTOR_MAX) oP.rhumidCalFactor = oPBfr.rhumidCalFactor;
    else success = -7;

    if (oPBfr.rhumidCalFactor > CAL_FACTOR_MIN && oPBfr.rhumidCalFactor < CAL_FACTOR_MAX) oP.rhumidCalFactor = oPBfr.rhumidCalFactor;
    else success = -8;

    if (oPBfr.kpadSensit[0] > KEYPAD_SENSIT_MIN && oPBfr.kpadSensit[0] < KEYPAD_SENSIT_MAX) oP.kpadSensit[0] = oPBfr.kpadSensit[0];
    else success = -9;

    if (oPBfr.kpadSensit[1] > KEYPAD_SENSIT_MIN && oPBfr.kpadSensit[1] < KEYPAD_SENSIT_MAX) oP.kpadSensit[1] = oPBfr.kpadSensit[1];
    else success = -10;

    if (oPBfr.kpadSensit[2] > KEYPAD_SENSIT_MIN && oPBfr.kpadSensit[2] < KEYPAD_SENSIT_MAX) oP.kpadSensit[2] = oPBfr.kpadSensit[2];
    else success = -11;

    if (oPBfr.kpadSensit[3] > KEYPAD_SENSIT_MIN && oPBfr.kpadSensit[3] < KEYPAD_SENSIT_MAX) oP.kpadSensit[3] = oPBfr.kpadSensit[3];
    else success = -12;

    oPBfr.uniqueID[UID_SIZE] = 0;
    for (ch = 0; ch < UID_SIZE; ch++)
        if (oPBfr.uniqueID[ch] > 31 && oPBfr.uniqueID[ch] < 128) oP.uniqueID[ch] = oPBfr.uniqueID[ch];

        // TODO now add iSens & resMtx

////    for (i = 0; i < NUM_CHILDREN; i++)
////    {
////        dummyFlo = smem_UnstuffFlote();
////        if (dummyFlo > CAL_FACTOR_MIN && dummyFlo < CAL_FACTOR_MAX) iSenCalFactor[i] = dummyFlo;
////        else success = -9;
////    }
//
//    if (success < 0)
//    {
//        sprintf(smemBfr, "(sRP %d)\n\r", success);       // DEB
//        putStr(smemBfr); // DEB
//
//        if (smem_ReadArray(SMEM_ADDR_PARMS, (byte *) ioBfr, SMEM_ARRAY_SIZE) < SMEM_ARRAY_SIZE) return(-1);
//
//        dAx = 0;
//        dummyUns = smem_UnstuffUnsigned();
//        if (dummyUns & ST_ALWAYS_TRUE) sysStat = dummyUns;
//        else return(-2);
//
//        dummyUns = smem_UnstuffUnsigned();      // Assure at least one record written
//        if (dummyUns > 0 && dummyUns < DL_MAX_RECORDS * DL_RECORD_SIZE) dlRecordsSaved = dummyUns;
//        else return(-3);
//
//        if (smem_UnstuffString(dummyStr, 11) == 11) strcpy(uniqueID, dummyStr);
//        else return(-4);
//
//        dummyFlo = smem_UnstuffFlote();
//        if (dummyFlo > SET_TEMP_MIN && dummyFlo < SET_TEMP_MAX) setPointTempF = dummyFlo;
//        else return(-5);
//
//        dummyFlo = smem_UnstuffFlote();
//        if (dummyFlo > SET_RH_MIN && dummyFlo < SET_RH_MAX) setPointRelHum = dummyFlo;
//        else return(-6);
//
//        dummyFlo = smem_UnstuffFlote();
//        if (dummyFlo > CAL_FACTOR_MIN && dummyFlo < CAL_FACTOR_MAX) temperCalFactor = dummyFlo;
//        else return(-7);
//
//        dummyFlo = smem_UnstuffFlote();
//        if (dummyFlo > CAL_FACTOR_MIN && dummyFlo < CAL_FACTOR_MAX) rhumidCalFactor = dummyFlo;
//        else return(-8);
////
////        for (i = 0; i < NUM_CHILDREN; i++)  TODO
////        {
////            dummyFlo = smem_UnstuffFlote();
////            if (dummyFlo > CAL_FACTOR_MIN && dummyFlo < CAL_FACTOR_MAX) iSenCalFactor[i] = dummyFlo;
////            else return(-9);
////        }
//    }
//    return(dAx);
}

    // Called periodically as operating parm backup
    // Returns num bytes written on success or neg failure code.
int smemWriteParms(void)
{
    byte d, *dPtr;
    dPtr = (byte *)&oP;
    for (d = 0; d < SMEM_ARRAY_SIZE; d++)
        ioBfr[d] = (byte) *(dPtr + d);
      
    return(smem_WriteArray(SMEM_ADDR_PARMS, ioBfr, SMEM_ARRAY_SIZE));
//    byte i;
//
//    dAx = 0;
//    smem_StuffUnsigned(sysStat);
//    smem_StuffUnsigned(dlRecordsSaved);
//    smem_StuffString(uniqueID, 11);
//    smem_StuffFlote(setPointTempF);
//    smem_StuffFlote(setPointRelHum);
//    smem_StuffFlote(temperCalFactor);
//    smem_StuffFlote(rhumidCalFactor);
//  //  for (i = 0; i < NUM_CHILDREN; i++) smem_StuffFlote(iSenCalFactor[i]);  TODO
//
//    return(smem_WriteArray(SMEM_ADDR_PARMS, ioBfr, dAx));

//    sprintf(ioBfr, "\t%d bytes saved.\n\r", dAx);      // DEB
//    putStr(ioBfr);
//    return(dAx);
}

void smem_StuffString(char *strPtr, byte strLen)
{
    byte ax;
    for (ax = 0; ax < strLen; ax++) 
    {
       // putChar(strPtr[ax]);    // DEB  This is fine
        ioBfr[dAx + ax] = strPtr[ax];
    }
    // ioBfr[dAx + ax] = 0;
    dAx += strLen;
}

    // ioBfr must contain ASCII chars, 32..127
int smem_UnstuffString(char *strPtr, byte strLen)
{
    byte ax;
    char ch;
    
    for (ax = 0; ax < strLen; ax++) 
    {
        ch = (char) ioBfr[dAx + ax];
        if (ch > 31 && ch < 128) strPtr[ax] = ch;
        else return(-1);
    }
    strPtr[strLen] = 0;
    dAx += strLen;
    return(strLen);
}

void smem_StuffFlote(float f)
{
    int f2i;

    f2i = f * 100;

//    sprintf(ioBfr, "(%4.03f, %04X)", f, f2i);  // DEB
//    putStr(ioBfr);  // DEB

    ioBfr[dAx] = (byte) (f2i & 0x00FF);        // LSB first

//    sprintf(ioBfr, "[%02X ", ioBfr[dAx]);  // DEB
//    putStr(ioBfr);  // DEB

    ioBfr[dAx + 1] = (byte) (f2i >> 8);

//    sprintf(ioBfr, "%02X] ", ioBfr[dAx + 1]);  // DEB
//    putStr(ioBfr);  // DEB
    
    dAx += 2;
}

void smem_StuffUnsigned(unsigned u)
{
    ioBfr[dAx] = (byte) u;        // LSB first
    ioBfr[dAx + 1] = (byte) (u >> 8);
    dAx += 2;
}

unsigned smem_UnstuffUnsigned(void)
{
    unsigned retVal;
    retVal = ioBfr[dAx + 1];        // MSB
    retVal <<= 8;
    retVal += ioBfr[dAx];
    dAx += 2;
}

float smem_UnstuffFlote(void)
{
    int i2f;

    i2f = ioBfr[dAx + 1];

//    sprintf(ioBfr, "[%02X %02X]", ioBfr[dAx], ioBfr[dAx + 1]);    // DEB
//    putStr(ioBfr);  /// DEB

    i2f <<= 8;
    i2f += ioBfr[dAx];


    dAx += 2;
   
    return((float) i2f / 100);
}

int smem_ReadArray(unsigned addr, byte *arrayPtr, unsigned arraySize)
{
    unsigned by;

    if (smem_Busy()) return(0);        // Pause till free
    SMEM_CS_n = 0;
    spi2_WRBuffer(SMEM_READ);
    spi2_WRBuffer(addr >> 8);
    spi2_WRBuffer(addr);

    for (by = 0; by < arraySize; by++)
    {
        arrayPtr[by] = spi2_WRBuffer(SMEM_DUMMY);
//        sprintf(ioBfr, "[%02X]", arrayPtr[by]);    // DEB
//        putStr(ioBfr);  /// DEB
    }
    SMEM_CS_n = 1;
    return(by);
}

int smem_WriteArray(unsigned addr, byte *arrayPtr, unsigned arraySize)
{
    unsigned by;
   
    if (smem_Busy()) return(0);        // Pause till free
    SMEM_CS_n = 0;
    spi2_WRBuffer(SMEM_WREN);    //
    SMEM_CS_n = 1;
    delay_us(1);
    SMEM_CS_n = 0;
    spi2_WRBuffer(SMEM_WRITE);
    spi2_WRBuffer(addr >> 8);
    spi2_WRBuffer(addr);

    for (by = 0; by < arraySize; by++)
    {
        if ((addr + by) % 64 == 0 && by != 0)  // Hit 64 byte page boundary
        {
            SMEM_CS_n = 1;
            if (smem_Busy()) return(0);        // Pause till free
            SMEM_CS_n = 0;
            spi2_WRBuffer(SMEM_WREN);
            SMEM_CS_n = 1;
            delay_us(1);
            SMEM_CS_n = 0;
            spi2_WRBuffer(SMEM_WRITE);
            spi2_WRBuffer((addr + by) >> 8);
            spi2_WRBuffer(addr + by);
        }
        spi2_WRBuffer(arrayPtr[by]);

//        if (addr >= SMEM_ADDR_DATALOG)
//        {
//            sprintf(smemBfr, "\n\r[%d:%02X]", addr + by, arrayPtr[by]);    // DEB
//            putStr(smemBfr);      // DEB
//        }

    }
    SMEM_CS_n = 1;
    return(arraySize);
}

bool smem_Busy(void)
{
    byte status = 0;
    unsigned timeOut = 0;

    do
    {
        SMEM_CS_n = 0;
        spi2_WRBuffer(SMEM_RDSR);    // Read status reg
        status = spi2_WRBuffer(0);  // Test datum
        SMEM_CS_n = 1;
        if (timeOut++ == 0xFFFF) { putChar('*'); return(true);  }  // DEB
    } while (status & 0x01);

    return(false);
}

 byte spi2_WRBuffer(byte datum)
 {
     SPI2BUF = datum;
     while (!SPI2STATbits.SPIRBF) ;
     return (SPI2BUF);
 }

//void smemSaveTest(void)
//{
//    const char testMsg[] = "Throw strikes. Home plate don't move.\r\n";
//    //byte ch;
//
//    //smem_Write(0x50, testMsg[0]);
//    smem_WriteArray(0x50, (byte *) testMsg, sizeof(testMsg));
//
//}


//
//    //
//byte smemInit(void)
//{
//
//}
