/* File:   wifi.c
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board
 * Created: 02 Oct 14
 */

#ifndef WIFI_H
#define	WIFI_H

#ifdef	__cplusplus
extern "C" {
#endif


byte wifiIP[4];
byte wifiMask[4];
bool wifiDHPC;

void wifiInit(void);

#ifdef	__cplusplus
}
#endif

#endif	/* WIFI_H */

