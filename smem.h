/* File:   smem.h  SEEPROM functions
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * Created: 02 Oct 14
 */


#ifndef SMEM_H
#define	SMEM_H

#ifdef	__cplusplus
extern "C" {
#endif

#define SMEM_SILENT     0x01
#define SMEM_LOQUACIOUS 0x02
#define SMEM_DL_RESET   0x01
#define SMEM_DL_RECORD  0x02

int smemDatalog(byte);
int smemDatalogReport(void);
//void smemDatalogReset(void);
void smemEraseAll(byte);
int smemReadParms(byte mode);
void smemReadTest(void);
int smemReadTime(void);
void smemTest(void);
int smemWriteParms(byte mode);
void smemWriteTest(void);
int smemWriteTime(void);

#ifdef	__cplusplus
}
#endif

#endif	/* SMEM_H */

