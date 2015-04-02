/* File:   smem.c  SEEPROM SPI2-based functions, incl datalog
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * SEEPROM: 25AA128, 16 Kbyte, 256 64-byte pages
 * Microchip is little-endian, so I keep to this convention here. Eg, 0x01020304 is
 * stored byte sequentially as 0x04, 0x03, 0x02, 0x01.
 * Created: 02 Oct 14
 */

#include <xc.h>
#include <string.h>
#include "DuraBlisCCSParent.h"
#include "smem.h"
#include "serial.h"
#include "time.h"
#include "resources.h"

    // SMEM semantics
#define SMEM_DUMMY  0x00
#define SMEM_READ   0x03
#define SMEM_WRITE  0x02
#define SMEM_WRDI   0x04
#define SMEM_WREN   0x06
#define SMEM_RDSR   0x05
#define SMEM_WRSR   0x01

#define SMEM_NUM_PAGES      256
#define SMEM_ARRAY_SIZE      64     // Max bytes in buffer for any one rd/wr block.  Equals Page size.
#define SMEM_ADDR_TIME        0     // Page 0 for time save.
#define SMEM_ADDR_PARMS      64     // Page 1 for parms (cal consts, etc)
#define SMEM_ADDR_UID       128     // Page 2 for UID string
#define SMEM_ADDR_ISEN_CAL  192     // Page 3 for iSen cal factors
#define SMEM_ADDR_RESOURCES 256     // Page 4 for resources
#define SMEM_ADDR_DATALOG   320     // Pages 5..250 for DL

#define DL_MAX_RECORDS     1960     // 8 doubles per page x 245 pgs
#define DL_RECORD_SIZE        8     // Bytes, 2 doubles now
#define DL_RECORDS_PER_PAGE   8     // 64 bytes per pg / (4 bytes per double * 2 doubles per record)
#define DL_PAGES            245

static int smem_ReadArray(unsigned addr);       //, byte *arrayPtr, unsigned arraySize);
static int smem_WriteArray(unsigned addr);      //, byte *arrayPtr, unsigned arraySize);
static bool smem_Busy(void);
static byte spi2_WRBuffer(byte datum);
static void packDbl(byte idx, double datum, byte *cs);
static double unpackDbl(byte idx, byte *cs);
static void packUns(byte idx, unsigned datum, byte *cs);
static unsigned unpackUns(byte idx, byte *cs);
static void packByte(byte idx, unsigned datum, byte *cs);
static byte unpackByte(byte idx, byte *cs);

extern struct t_struct time;                    // Page 0
extern struct operatingParms_struct oP;         // Page 1
extern char uniqueID[UID_SIZE];                 // Page 2
extern char siteID[SID_SIZE];                   // Page 2
extern double iSenCalFactor [NUM_CHILDREN];     // Page 3

    // Volatile
static byte smemBfr[SMEM_ARRAY_SIZE];   // All smem traffic here.  Use ioBfr for auxilliary.
static union pack_union
{
    byte byte_pk[4];
    double dubPrec_pk;          // XC32 double is 4 byte
    unsigned long unsLong_pk;   // Ditto
    unsigned unsShort_pk;       // 2 byte, jagged sizes okay in unions
} pack;

    // ------------ Public functions --------------

    // Called every time t (nom 5 min).  Smem writes latest T & RH as doubles.
 // TODO allow variable periods
int smemDatalog(byte opMode)
{
    extern float temperNowF, rhumidNow;
    static unsigned dlPage;     // Leave these exposed so that they can be reset
    static byte dlPageIdx;      // Bytes on a page
    static byte dlBfr[SMEM_ARRAY_SIZE];
    byte by;

   // byte debBy;
   // char debBfr[21];

    if (opMode == SMEM_DL_RESET)
    {
        dlPage = 0;
        dlPageIdx = 0;
        oP.dlRecordsSaved = 0;
        for (by = 0; by < SMEM_ARRAY_SIZE; by++) dlBfr[by] = 0;
        return(0);
    }
    if (opMode != SMEM_DL_RECORD) return(-1);

    pack.dubPrec_pk = temperNowF;
    dlBfr[dlPageIdx] = pack.byte_pk[0];       // Mchip is little-endian.  Preserve that here.
    dlBfr[dlPageIdx + 1] = pack.byte_pk[1];
    dlBfr[dlPageIdx + 2] = pack.byte_pk[2];
    dlBfr[dlPageIdx + 3] = pack.byte_pk[3];
    pack.dubPrec_pk = rhumidNow;
    dlBfr[dlPageIdx + 4] = pack.byte_pk[0];
    dlBfr[dlPageIdx + 5] = pack.byte_pk[1];
    dlBfr[dlPageIdx + 6] = pack.byte_pk[2];
    dlBfr[dlPageIdx + 7] = pack.byte_pk[3];

    for (by = 0; by < SMEM_ARRAY_SIZE; by++) smemBfr[by] = dlBfr[by];       // Write whole page every time
    if (smem_WriteArray(SMEM_ADDR_DATALOG + (dlPage * SMEM_ARRAY_SIZE)) < SMEM_ARRAY_SIZE) return(-1);

//    putStr("\r\n ");        // DEB
//    for (debBy = 0; debBy < 16; debBy++)
//    {
//        sprintf(ioBfr, "%02X ", dlBfr[debBy]);      // DEB
//        putStr(ioBfr);
//        if (debBy % 4 == 3) putStr(" | ");
//    }
//    putStr("\r\n ");
//    for (debBy = 16; debBy < 32; debBy++)
//    {
//        sprintf(ioBfr, "%02X ", dlBfr[debBy]);      // DEB
//        putStr(ioBfr);
//        if (debBy % 4 == 3) putStr(" | ");
//    }
//    putStr("\r\n ");
//    for (debBy = 32; debBy < 48; debBy++)
//    {
//        sprintf(ioBfr, "%02X ", dlBfr[debBy]);      // DEB
//        putStr(ioBfr);
//        if (debBy % 4 == 3) putStr(" | ");
//    }
//    putStr("\r\n ");
//    for (debBy = 48; debBy < 64; debBy++)
//    {
//        sprintf(ioBfr, "%02X ", dlBfr[debBy]);      // DEB
//        putStr(ioBfr);
//        if (debBy % 4 == 3) putStr(" | ");
//    }
//
//    sprintf(ioBfr, " - pg%d idx%d\r\n", dlPage, dlPageIdx);
//    putStr(ioBfr);        // DEB


    dlPageIdx += DL_RECORD_SIZE;
    if (dlPageIdx >= SMEM_ARRAY_SIZE)
    {
        dlPageIdx = 0;
        for (by = 0; by < SMEM_ARRAY_SIZE; by++) dlBfr[by] = 0;     // Not strictly necessary but helpful for debug
        if (dlPage++ > DL_PAGES) oP.sysStat &= ~ST_DATALOG_ON;      // Turn off
    }
    oP.dlRecordsSaved++;
    if (smemWriteParms(SMEM_SILENT) < 0) return(-2);       // To record dlRecordsSaved

    return(oP.dlRecordsSaved);
}

    // TODO regenerate actual times   
int smemDatalogReport(void)
{   
    double T, RH;   
    unsigned address = SMEM_ADDR_DATALOG;
    byte page, pagesUsed, record, index, recordsRead = 0;

    if (oP.dlRecordsSaved == 0)
    {      
        putStr(" No datalog records saved.\n\r");
        return(0);
    }                      

        // Start at present location, then work down.
        // address = SMEM_ADDR_DATALOG + dlPage * DL_RECORDS_PER_PAGE + dlPageIdx;
    sprintf(ioBfr, "\n\r# Sample period = %d min", DATALOG_PERIOD_MINS);
    putStr(ioBfr);
    sprintf(ioBfr, "\n\r# Records = %d", oP.dlRecordsSaved);
    putStr(ioBfr);
    putStr("\n\r# idx,\toF,\t%RH\n\r");

    pagesUsed = (oP.dlRecordsSaved / DL_RECORDS_PER_PAGE) + 1;

    for (page = 0; page < pagesUsed; page++)
    {
        smem_ReadArray(address);
        index = 0;
        for (record = 0; record < DL_RECORDS_PER_PAGE; record++)
        {
            pack.byte_pk[0]  = smemBfr[index];
            pack.byte_pk[1]  = smemBfr[index + 1];
            pack.byte_pk[2]  = smemBfr[index + 2];
            pack.byte_pk[3]  = smemBfr[index + 3];
            T = pack.dubPrec_pk;
            pack.byte_pk[0]  = smemBfr[index + 4];
            pack.byte_pk[1]  = smemBfr[index + 5];
            pack.byte_pk[2]  = smemBfr[index + 6];
            pack.byte_pk[3]  = smemBfr[index + 7];
            RH = pack.dubPrec_pk;

//            sprintf(ioBfr, "\r\n out[%02X %02X %02X %02X | %02X %02X %02X %02X]\r\n",
//                smemBfr[index], smemBfr[index + 1], smemBfr[index + 2], smemBfr[index + 3],
//                smemBfr[index + 4], smemBfr[index+ 5], smemBfr[index + 6], smemBfr[index + 7]);     // DEB
//            putStr(ioBfr);      // DEB

            index += DL_RECORD_SIZE;        // Resets to 0 after inner loop
            if ((T > REASONABLE_TEMP_MIN && T < REASONABLE_TEMP_MAX) && (RH > REASONABLE_RH_MIN && RH < REASONABLE_RH_MAX))
            {
                sprintf(ioBfr, "%3d, \t%2.1f, \t%2.1f,\n\r", recordsRead + 1, T, RH);
                putStr(ioBfr);
            }
            else
            {
                putStr("\r\n Fail: Bad datalog read.\r\n");
                return(-1);
            }
            if (++recordsRead >= oP.dlRecordsSaved) return(recordsRead);
        }
        address += SMEM_ARRAY_SIZE;
        petTheDog();
    }

    return(-3);     // Error
}

    // Destructive erasure of entire SEEPROM.
void smemEraseAll(byte opMode)
{
    unsigned page, d;

    for (d = 0; d < SMEM_ARRAY_SIZE; d++) smemBfr[d] = 0xFF;

    if (opMode == SMEM_LOQUACIOUS) putStr("\tsmem destructive erase all...\r\n");
    for (page = 0; page < SMEM_NUM_PAGES; page++)
    {
         if (smem_WriteArray(page * SMEM_ARRAY_SIZE) < SMEM_ARRAY_SIZE)      //, smemBfr, SMEM_ARRAY_SIZE) != SMEM_ARRAY_SIZE)
         {
             if (opMode == SMEM_LOQUACIOUS) putStr(" smemEraseAll failed.\r\n");
             return;
         }
         petTheDog();
    }
    if (opMode == SMEM_LOQUACIOUS)
    {
        sprintf(ioBfr, " Erased %d pages of smem.\r\n", SMEM_NUM_PAGES);
        putStr(ioBfr);
    }
}

    // Called on startup reset and as factory setting restore of operating parms
    // TODO One non-recursive retry. 
    // Returns num pages read on success or neg failure code.
int smemReadParms(byte mode)
{
    byte chkSum = 0;
    struct operatingParms_struct oPBfr;
    byte d, dd, ch, index;

    if (mode == SMEM_LOQUACIOUS) putStr("\tsRP Test\r\n");
    if (smem_ReadArray(SMEM_ADDR_PARMS) < SMEM_ARRAY_SIZE)
    {
        if (mode == SMEM_LOQUACIOUS) putStr(" sRA read 1 fail.\r\n");
        return(-1);
    }

    if (smemBfr[0] != 0x5A)
    {
        if (mode == SMEM_LOQUACIOUS) putStr(" Blank smem");
        factoryHardInit();
        smemWriteParms(mode);
        return(0);
    }

        // Matches order in sWP and oP generally
    index = 1;
    oPBfr.sysStat = unpackUns(index, &chkSum);          index += sizeof(oPBfr.sysStat);
    oPBfr.dlRecordsSaved = unpackUns(index, &chkSum);   index += sizeof(oPBfr.dlRecordsSaved);
    oPBfr.setPointTempF = unpackDbl(index, &chkSum);    index += sizeof(oPBfr.setPointTempF);
    oPBfr.setPointRelHum = unpackDbl(index, &chkSum);   index += sizeof(oPBfr.setPointRelHum);
    oPBfr.temperCalFactor = unpackDbl(index, &chkSum);  index += sizeof(oPBfr.temperCalFactor);
    oPBfr.rhumidCalFactor = unpackDbl(index, &chkSum);  index += sizeof(oPBfr.rhumidCalFactor);
    oPBfr.toleranceCZrhumid = unpackDbl(index, &chkSum); index += sizeof(oPBfr.toleranceCZrhumid);
    oPBfr.toleranceCZtemperF = unpackDbl(index, &chkSum); index += sizeof(oPBfr.toleranceCZtemperF);
 
    if (smemBfr[index] == chkSum)
    {
        oP = oPBfr;
        if (mode == SMEM_LOQUACIOUS) putStr(" sRP 1 confirmed\n\r");
    }
    else if (mode == SMEM_LOQUACIOUS) putStr(" sRP 1 CS fail\n\r");

    if (mode == SMEM_LOQUACIOUS)
    {
        sprintf(ioBfr, " sysStat: %04X\r\n", oPBfr.sysStat); putStr(ioBfr);
        sprintf(ioBfr, " dlRS: %d\r\n", oPBfr.dlRecordsSaved); putStr(ioBfr);
        sprintf(ioBfr, " sPTF: %3.02f\r\n", oPBfr.setPointTempF); putStr(ioBfr);
        sprintf(ioBfr, " sPRH: %3.02f\r\n", oPBfr.setPointRelHum); putStr(ioBfr);
        sprintf(ioBfr, " tCalF: %3.02f\r\n", oPBfr.temperCalFactor); putStr(ioBfr);
        sprintf(ioBfr, " rhCalF: %3.02f\r\n", oPBfr.rhumidCalFactor); putStr(ioBfr);
        sprintf(ioBfr, " tolCZhu: %3.02f\r\n", oPBfr.toleranceCZrhumid); putStr(ioBfr);
        sprintf(ioBfr, " tolCZte: %3.02f\r\n", oPBfr.toleranceCZtemperF); putStr(ioBfr);
        sprintf(ioBfr, " CS sto: %02X, calc: %02X\r\n", smemBfr[index], chkSum); putStr(ioBfr);
    }
    petTheDog();
                // Now do UID
    if (smem_ReadArray(SMEM_ADDR_UID) < SMEM_ARRAY_SIZE)
    {
        if (mode == SMEM_LOQUACIOUS) putStr(" sRA read 2 fail.\r\n");
        return(-12);
    }
   
    for (d = 0; d < UID_SIZE; d++)
    {
        ch = smemBfr[d];        // Limit to ASCII-Z and printables
        if ((ch == 0) || ((ch >= 32) && (ch <= 127)))
            uniqueID[d] = ch;
    }
    uniqueID[d] = 0;                // Stringize it
    if (mode == SMEM_LOQUACIOUS)
    {
        sprintf(ioBfr, " uID: %s\r\n", uniqueID);
        putStr(ioBfr);
    }

    for (dd = 0; dd < SID_SIZE; dd++)
    {
        ch = smemBfr[d + dd];        // Limit to ASCII-Z and printables
        if ((ch == 0) || ((ch >= 32) && (ch <= 127)))
            siteID[dd] = ch;
    }
    siteID[dd] = 0;
    if (mode == SMEM_LOQUACIOUS)
    {
        sprintf(ioBfr, " sID: %s\r\n", siteID);
        putStr(ioBfr);
    }
    petTheDog();

    if (smem_ReadArray(SMEM_ADDR_RESOURCES) < SMEM_ARRAY_SIZE)
    {
        if (mode == SMEM_LOQUACIOUS) putStr(" sRA read 3 fail.\r\n");        
        return(-20);
    }

    chkSum = 0;
    for (d = 1; d < SMEM_ARRAY_SIZE; d++) chkSum += smemBfr[d];
    if (chkSum != smemBfr[0])
    {
        if (mode == SMEM_LOQUACIOUS) putStr(" sRP CS 3 fail.\r\n");
        return(-21);
    }

    index = 1;
    child[0].status = unpackUns(index, &chkSum);    index += sizeof(child[0].status);
    child[0].relay1 = unpackByte(index, &chkSum);   index++;    // Even though these are enums (ints) we store as bytes for compression
    child[0].relay2 = unpackByte(index, &chkSum);   index++;
    child[0].swiPwr1 = unpackByte(index, &chkSum);  index++;    
    child[0].swiPwr2 = unpackByte(index, &chkSum);  index++;

    child[1].status = unpackUns(index, &chkSum);    index += sizeof(child[1].status);
    child[1].relay1 = unpackByte(index, &chkSum);   index++;
    child[1].relay2 = unpackByte(index, &chkSum);   index++;
    child[1].swiPwr1 = unpackByte(index, &chkSum);  index++;
    child[1].swiPwr2 = unpackByte(index, &chkSum);  index++;

    child[2].status = unpackUns(index, &chkSum);    index += sizeof(child[2].status);
    child[2].relay1 = unpackByte(index, &chkSum);   index++;   
    child[2].relay2 = unpackByte(index, &chkSum);   index++;
    child[2].swiPwr1 = unpackByte(index, &chkSum);  index++;  
    child[2].swiPwr2 = unpackByte(index, &chkSum);  index++;

    child[3].status = unpackUns(index, &chkSum);    index += sizeof(child[3].status);
    child[3].relay1 = unpackByte(index, &chkSum);   index++;
    child[3].relay2 = unpackByte(index, &chkSum);   index++;
    child[3].swiPwr1 = unpackByte(index, &chkSum);  index++;
    child[3].swiPwr2 = unpackByte(index, &chkSum);  index++;

    child[4].status = unpackUns(index, &chkSum);    index += sizeof(child[4].status);
    child[4].relay1 = unpackByte(index, &chkSum);   index++;
    child[4].relay2 = unpackByte(index, &chkSum);   index++;
    child[4].swiPwr1 = unpackByte(index, &chkSum);  index++;
    child[4].swiPwr2 = unpackByte(index, &chkSum);  index++;

    child[5].status = unpackUns(index, &chkSum);    index += sizeof(child[5].status);
    child[5].relay1 = unpackByte(index, &chkSum);   index++;
    child[5].relay2 = unpackByte(index, &chkSum);   index++;
    child[5].swiPwr1 = unpackByte(index, &chkSum);  index++;
    child[5].swiPwr2 = unpackByte(index, &chkSum);  index++;

    child[6].status = unpackUns(index, &chkSum);    index += sizeof(child[6].status);
    child[6].relay1 = unpackByte(index, &chkSum);   index++;
    child[6].relay2 = unpackByte(index, &chkSum);   index++;
    child[6].swiPwr1 = unpackByte(index, &chkSum);  index++;
    child[6].swiPwr2 = unpackByte(index, &chkSum);  index++;

    child[7].status = unpackUns(index, &chkSum);    index += sizeof(child[7].status);
    child[7].relay1 = unpackByte(index, &chkSum);   index++;
    child[7].relay2 = unpackByte(index, &chkSum);   index++;
    child[7].swiPwr1 = unpackByte(index, &chkSum);  index++;
    child[7].swiPwr2 = unpackByte(index, &chkSum);  index++;

    return(3);      // 3 pages read
}

    // Dumps each page byte buffer
void smemReadTest(void)
{
    unsigned page;
    byte by;

    for (page = 0; page < SMEM_NUM_PAGES; page++)
    {
         if (smem_ReadArray(page * SMEM_ARRAY_SIZE) < SMEM_ARRAY_SIZE)
         {
             putStr(" smemReadTest failed.\r\n");
             return;
         }
         else
         {
            sprintf(ioBfr, "\n\rPage %003d: \r\n", page);
            putStr(ioBfr);

            for (by = 0; by < 20; by++)
            {
                sprintf(ioBfr, "%02X ", smemBfr[by]);
                putStr(ioBfr);
            }
            putStr("\r\n");
            for (by = 21; by < 40; by++)
            {
                sprintf(ioBfr, "%02X ", smemBfr[by]);
                putStr(ioBfr);
            }
            putStr("\r\n");
            for (by = 41; by < SMEM_ARRAY_SIZE; by++)
            {
                sprintf(ioBfr, "%02X ", smemBfr[by]);
                putStr(ioBfr);
            }
            putStr("\r\n");
         }
         petTheDog();
    }

    putStr("\n\r");
}

   // Called at reset.  Has 1 non-recursive retry.
int smemReadTime(void)
{
    extern unsigned sysFault;
    bool success = true;

    if (smem_ReadArray(SMEM_ADDR_TIME) < SMEM_ARRAY_SIZE) success = false;

    if (smemBfr[0] > 0 && smemBfr[0] < 100) time.year = smemBfr[0];   // Years from (20)14 - (20)99
    else success = false;
    if (smemBfr[1] > 0 && smemBfr[1] < 13) time.month = smemBfr[1];
    else success = false;
    if (smemBfr[2] > 0 && smemBfr[2] < 32) time.dayMonth = smemBfr[2];
    else success = false;
    if (smemBfr[3] < 60) time.hour = smemBfr[3];
    else success = false;
    if (smemBfr[4] < 60) time.minute = smemBfr[4];
    else success = false;

    if (success == false)
    {
        if (smem_ReadArray(SMEM_ADDR_TIME) < SMEM_ARRAY_SIZE) return(-1);

        if (smemBfr[0] > 0 && smemBfr[0] < 100) time.year = smemBfr[0];
        else return(-2);
        if (smemBfr[1] > 0 && smemBfr[1] < 13) time.month = smemBfr[1];
        else return(-3);
        if (smemBfr[2] > 0 && smemBfr[2] < 32) time.dayMonth = smemBfr[2];
        else return(-4);
        if (smemBfr[3] < 60) time.hour = smemBfr[3];
        else return(-5);
        if (smemBfr[4] < 60) time.minute = smemBfr[4];
        else return(-6);
    }

  //  sysFault &= ~FLT_CLOCKNOTSET;       // Assume that we have an approx clock now
    return (5);
}
    // Simplest test of smem functionality.  UID will be properly written in sWP.
void smemTest(void)
{
    byte readBy;

    putStr("\n\r\tSmem simple test. ");
    SMEM_CS_n = 0;
    spi2_WRBuffer(SMEM_WREN);       // Write enable
    SMEM_CS_n = 1;
    delay_us(5);

    SMEM_CS_n = 0;
    spi2_WRBuffer(SMEM_RDSR);
    readBy = spi2_WRBuffer(SMEM_DUMMY);     // Should be WPEN, X, X, X | BP1, BP0, WEL = 1, WIP
    SMEM_CS_n = 1;
    delay_us(1);
    sprintf(ioBfr, "\n\r RDSR %02Xh, WEL: %s", readBy, readBy & 0x02 ? "OK" : "BAD");
    putStr(ioBfr);

    delay_us(5);

    putStr("\r\n\tNow trying a page write\r\n");
    strcpy(smemBfr, "Oh, be a fine girl, kiss me right now, sweetheart!\r\n");
    smem_WriteArray(SMEM_ADDR_UID);
    for (readBy = 0; readBy < SMEM_ARRAY_SIZE; readBy++) smemBfr[readBy] = 0;
    smem_ReadArray(SMEM_ADDR_UID);
    putStr(" Read: ");
    putStr(smemBfr);
}

    // Called periodically as operating parm backup
    // Returns num pages written on success or neg failure code.
    // Assume data in struct / union not contiguous.
int smemWriteParms(byte opMode)
{
    byte d, dd, chkSum = 0, index = 1;

    smemBfr[0] = 0x5A;                  // Initializer, not checksummed
    packUns(index, oP.sysStat, &chkSum); index += sizeof(oP.sysStat);
    packUns(index, oP.dlRecordsSaved, &chkSum); index += sizeof(oP.dlRecordsSaved);
    packDbl(index, oP.setPointTempF, &chkSum); index += sizeof(oP.setPointTempF);
    packDbl(index, oP.setPointRelHum, &chkSum); index += sizeof(oP.setPointRelHum);
    packDbl(index, oP.temperCalFactor, &chkSum); index += sizeof(oP.temperCalFactor);
    packDbl(index, oP.rhumidCalFactor, &chkSum); index += sizeof(oP.rhumidCalFactor);
    packDbl(index, oP.toleranceCZrhumid, &chkSum); index += sizeof(oP.toleranceCZrhumid);
    packDbl(index, oP.toleranceCZtemperF, &chkSum); index += sizeof(oP.toleranceCZtemperF);
    smemBfr[index] = chkSum;

    if (opMode == SMEM_LOQUACIOUS)
    {
        putStr("\n\r\tsWP bytes:\r\n");
        for (d = 0; d < SMEM_ARRAY_SIZE; d++)
        {
            sprintf(ioBfr, "%02X ", smemBfr[d]);
            putStr(ioBfr);
            if (d % 16 == 15) putStr("\r\n");
        }
    }

    if (smem_WriteArray(SMEM_ADDR_PARMS) < SMEM_ARRAY_SIZE)
    {
        if (opMode == SMEM_LOQUACIOUS) putStr(" s_WA fail -1\n\r");
        return(-1);
    }

    for (d = 0; d < UID_SIZE; d++)       
        smemBfr[d] = uniqueID[d];
    smemBfr[d] = 0;

   for (dd = 0; dd < SID_SIZE; dd++)
        smemBfr[d + dd] = siteID[dd];
    smemBfr[d + dd] = 0;

    if (opMode == SMEM_LOQUACIOUS)
    {
        putStr("\n\r\tPage 2:\r\n");
        for (d = 0; d < SMEM_ARRAY_SIZE; d++)
        {
            sprintf(ioBfr, "%02X ", smemBfr[d]);
            putStr(ioBfr);
            if (d % 16 == 15) putStr("\r\n");
        }
    }

    if (smem_WriteArray(SMEM_ADDR_UID) < SMEM_ARRAY_SIZE)
    {
        if (opMode == SMEM_LOQUACIOUS) putStr(" s_WA fail UID (-2)\n\r");
        return(-2);
    }

    chkSum = 0; index = 1;
    for (d = 0; d < SMEM_ARRAY_SIZE; d++) smemBfr[d] = 0;   // Need this for checksum
    packUns(index, child[0].status, &chkSum);   index += sizeof(child[0].status);
    packByte(index, child[0].relay1, &chkSum);  index++;    // Even though these are enums (ints) we truncate to bytes
    packByte(index, child[0].relay2, &chkSum);  index++;
    packByte(index, child[0].swiPwr1, &chkSum); index++;
    packByte(index, child[0].swiPwr2, &chkSum); index++;    // Nominally 6 bytes per child

    packUns(index, child[1].status, &chkSum);   index += sizeof(child[1].status);
    packByte(index, child[1].relay1, &chkSum);  index++;
    packByte(index, child[1].relay2, &chkSum);  index++;
    packByte(index, child[1].swiPwr1, &chkSum); index++;
    packByte(index, child[1].swiPwr2, &chkSum); index++;

    packUns(index, child[2].status, &chkSum);   index += sizeof(child[2].status);
    packByte(index, child[2].relay1, &chkSum);  index++;
    packByte(index, child[2].relay2, &chkSum);  index++;
    packByte(index, child[2].swiPwr1, &chkSum); index++;
    packByte(index, child[2].swiPwr2, &chkSum); index++;

    packUns(index, child[3].status, &chkSum);   index += sizeof(child[3].status);
    packByte(index, child[3].relay1, &chkSum);  index++;
    packByte(index, child[3].relay2, &chkSum);  index++;
    packByte(index, child[3].swiPwr1, &chkSum); index++;
    packByte(index, child[3].swiPwr2, &chkSum); index++;

    packUns(index, child[4].status, &chkSum);   index += sizeof(child[4].status);
    packByte(index, child[4].relay1, &chkSum);  index++;
    packByte(index, child[4].relay2, &chkSum);  index++;
    packByte(index, child[4].swiPwr1, &chkSum); index++;
    packByte(index, child[4].swiPwr2, &chkSum); index++;

    packUns(index, child[5].status, &chkSum);   index += sizeof(child[5].status);
    packByte(index, child[5].relay1, &chkSum);  index++;
    packByte(index, child[5].relay2, &chkSum);  index++;
    packByte(index, child[5].swiPwr1, &chkSum); index++;
    packByte(index, child[5].swiPwr2, &chkSum); index++;

    packUns(index, child[6].status, &chkSum);   index += sizeof(child[6].status);
    packByte(index, child[6].relay1, &chkSum);  index++;
    packByte(index, child[6].relay2, &chkSum);  index++;
    packByte(index, child[6].swiPwr1, &chkSum); index++;
    packByte(index, child[6].swiPwr2, &chkSum); index++;

    packUns(index, child[7].status, &chkSum);   index += sizeof(child[7].status);
    packByte(index, child[7].relay1, &chkSum);  index++;
    packByte(index, child[7].relay2, &chkSum);  index++;
    packByte(index, child[7].swiPwr1, &chkSum); index++;
    packByte(index, child[7].swiPwr2, &chkSum); index++;

    smemBfr[0] = chkSum;
    if (opMode == SMEM_LOQUACIOUS)
    {
        putStr("\n\r\tPage 3:\r\n");
        for (d = 0; d < SMEM_ARRAY_SIZE; d++)
        {
            sprintf(ioBfr, "%02X ", smemBfr[d]);
            putStr(ioBfr);
            if (d % 16 == 15) putStr("\r\n");
        }
    }

    if (smem_WriteArray(SMEM_ADDR_RESOURCES) < SMEM_ARRAY_SIZE)
    {
         if (opMode == SMEM_LOQUACIOUS) putStr(" s_WA fail RES (-3)\n\r");
         return(-3);
    }
        
    return(3);      // 3 pages written
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
         sprintf(smemBfr, "Page %03d : ", page);
         strcat(smemBfr, testStr);
         if (smem_WriteArray(page * SMEM_ARRAY_SIZE) < SMEM_ARRAY_SIZE)
         {
             putStr(" smemWriteTest failed.\r\n");
             return;
         }
         else
         {
             putStr(smemBfr);
             putStr("\r\n");
         }
         petTheDog();
    }

    sprintf(ioBfr, "\n\r Wrote %d pages to smem.\r\n", SMEM_NUM_PAGES);
    putStr(ioBfr);
}

    // Called nominally every 5 mins to keep a rough nonvol clock
int smemWriteTime(void)
{
    smemBfr[0] = time.year;
    smemBfr[1] = time.month;
    smemBfr[2] = time.dayMonth;
    smemBfr[3] = time.hour;
    smemBfr[4] = time.minute;

    return (smem_WriteArray(SMEM_ADDR_TIME));
}

    // ----------- Private functions --------------

int smem_ReadArray(unsigned addr)   //, byte *arrayPtr, unsigned arraySize)
{
    unsigned by;

    if (smem_Busy()) return(0);        // Pause till free
    SMEM_CS_n = 0;
    spi2_WRBuffer(SMEM_READ);
    spi2_WRBuffer(addr >> 8);
    spi2_WRBuffer(addr);

    for (by = 0; by < SMEM_ARRAY_SIZE; by++)
    {
        smemBfr[by] = spi2_WRBuffer(SMEM_DUMMY);   // Dummy because we are not writing anything here. We are reading.
    }
    SMEM_CS_n = 1;
    return(by);
}

int smem_WriteArray(unsigned addr)   //, byte *arrayPtr, unsigned arraySize)
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

    for (by = 0; by < SMEM_ARRAY_SIZE; by++)
    {
//        if ((addr + by) % 64 == 0 && by != 0)  // Hit 64 byte page boundary
//        {
//            SMEM_CS_n = 1;
//            if (smem_Busy()) return(0);        // Pause till free
//            SMEM_CS_n = 0;
//            spi2_WRBuffer(SMEM_WREN);
//            SMEM_CS_n = 1;
//            delay_us(1);
//            SMEM_CS_n = 0;
//            spi2_WRBuffer(SMEM_WRITE);
//            spi2_WRBuffer((addr + by) >> 8);
//            spi2_WRBuffer(addr + by);
//        }   
        spi2_WRBuffer(smemBfr[by]);
    }
    SMEM_CS_n = 1;
    return(SMEM_ARRAY_SIZE);
}

    // Returns true if times out busy, which is a fault.
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
        if (timeOut++ == 0xFFFF) return(true); 
    } while (status & 0x01);

//    sprintf(ioBfr, "[t%u, %02X]", timeOut, status); // DEB
//    putStr(ioBfr);
    
    return(false);
}

 byte spi2_WRBuffer(byte datum)
 {
     interruptsEnable(false);
     SPI2BUF = datum;
     while (!SPI2STATbits.SPIRBF) ;
     interruptsEnable(true);
     return (SPI2BUF);
 }

    // Little-endian preserving double pack
void packDbl(byte idx, double datum, byte *cs)
{
    pack.dubPrec_pk = datum;
    *cs += (smemBfr[idx] = pack.byte_pk[0]);    // LSB
    *cs += (smemBfr[idx+1] = pack.byte_pk[1]);
    *cs += (smemBfr[idx+2] = pack.byte_pk[2]);
    *cs += (smemBfr[idx+3] = pack.byte_pk[3]);
}

double unpackDbl(byte idx, byte *cs)
{
    double retVal;

    *cs += (pack.byte_pk[0] = smemBfr[idx]);    // LSB
    *cs += (pack.byte_pk[1] = smemBfr[idx+1]);
    *cs += (pack.byte_pk[2] = smemBfr[idx+2]);
    *cs += (pack.byte_pk[3] = smemBfr[idx+3]);
    retVal = pack.dubPrec_pk;
    return(retVal);
}
 
    // Little-endian unsigned pack
void packUns(byte idx, unsigned datum, byte *cs)
{
    *cs += (smemBfr[idx] = (byte) datum);
    *cs += (smemBfr[idx+1] = (byte) (datum >> 8));
}

unsigned unpackUns(byte idx, byte *cs)
{
    unsigned retVal;
    *cs += (retVal = smemBfr[idx]);  // Little byte first
    retVal += ((unsigned) smemBfr[idx+1]) << 8;     // Big byte
    *cs += smemBfr[idx+1];
    return(retVal);
}

    // Truncate unsigned to byte.  Okay for enums.
void packByte(byte idx, unsigned datum, byte *cs)
{
    *cs += (smemBfr[idx] = (byte) datum);
}

byte unpackByte(byte idx, byte *cs)
{
    byte retVal;
    *cs += (retVal = smemBfr[idx]);
    return (retVal);
}