/* File:   child.h  Child emulation
 * *** For reference only. ***
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on BW-CCS-Main_v2 board
 * Created: 25 Jun 14
 */

#ifndef CHILD_H
#define	CHILD_H

#ifdef	__cplusplus
extern "C" {
#endif

//void childInits(void);
void assignNodeNumber(byte childNodeNumberOld, byte childNodeNumber);
void childModeStMach(char charCh);   // Child emulation
void childReportInfo(void);

bool realityCheckEq(byte newEqWord);
void respondAttention(byte ch);
void respondRHumidity(byte ch);
void respondServiceQuery(byte ch);
void respondServiceRequest(byte ch, char relayNo, char onOff);
void respondStatus(byte ch);
void respondTemperature(byte ch);

#ifdef	__cplusplus
}
#endif

#endif	/* CHILD_H */

