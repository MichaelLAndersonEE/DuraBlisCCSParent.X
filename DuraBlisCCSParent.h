/* File:   BW-CCS.h  Project wide defines, typedefs, structs, bitfields & externs
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on DB-CCS-Main_v2.x board.  
 * Created: 16 May 14
 */

#ifndef DURABLISCCSPARENT_H
#define	DURABLISCCSPARENT_H

#include <stdbool.h>    // To use bool type throughout

#define SET_TEMP_MAX        95
#define SET_TEMP_MIN        55
#define REASONABLE_TEMP_MAX 120
#define REASONABLE_TEMP_MIN -50
#define SET_RH_MAX          80
#define SET_RH_MIN          25
#define LOCAL_RH_MAX        55    // Turn on IntFans
#define REASONABLE_RH_MAX  100
#define REASONABLE_RH_MIN    0
#define CAL_FACTOR_MAX      1.6
#define CAL_FACTOR_MIN      0.4
#define RESOURCE_EFFECTIVENESS_CRITERION    0.3
#define CHILD_FAULT_CURRENT 600     // mA, no channel may exceed
#define CHILD_MIA_MAX       4
#define DATALOG_PERIOD_MINS  5     // Valid btwn 2 - 60 mins.  Must be divisor of 60.
#define CRITICAL_LOW_SEC_TEMP  50   // oF, if secondary temp falls below, fault.  'Boiler temp'

    
#define SYS_FREQ	(25000000)  //  Primary external osc freq
#define IO_BFR_SIZE     65          // Volatile general purpose IO buffer
#define NUM_CHILDREN    8
#define NUM_CHILDREN_SLOTS  4       // For now, stuff only 4 channel slots on main PCB.  Mainly affects iSen[]
#define UID_SIZE        7           // Number chars including ASCII-0 in Unique Identifier, 000000 - 999999
#define SID_SIZE        11          // Zip + 4

typedef unsigned char byte;
extern char ioBfr[];
void delay_us(unsigned t);        // Dumb delay fcn.

  // This is awkward, but need to keep these packaged for SEEPROM
struct operatingParms_struct
{
    byte initializer;           // [1]   
    unsigned sysStat;           // [2 bytes]
    unsigned dlRecordsSaved;    // [2]
    double setPointTempF;       // [4]
    double setPointRelHum;      // [4] All hums normalized to 100
    double temperCalFactor;     // [4] For Fahrenheit
    double rhumidCalFactor;     // [4]
    double toleranceCZtemperF;  // [4] Typ 5.0 oF.  Control is based on +/- tolerance
    double toleranceCZrhumid;   // [4] Typ 8.0 %
    byte initCtr;       // TODO check in smem
    byte checkSum;              // [1]  Just a space, smem will calc & fill
};

    // Parent sysFault bits, in rough priority order
    // These may be cleared by user in Main window, or in pwr cycle.
#define FLT_LOSTCHILD           0x8000      // Violation of Pnet syntax or no response
#define FLT_CHILDFAULT          0x4000
#define FLT_FLOODDETECTED       0x2000
#define FLT_OVER_CURRENT        0x1000

    // Below here cleared at midnight
#define FLT_SENSORSNOTCALIB     0x0800      // Clear both T & RH cal'd
#define FLT_SEC_TEMP_LOW        0x0400
#define FLT_PARENT_TSENSOR      0x0200
#define FLT_PARENT_HSENSOR      0x0100
#define FLT_CANNOTCONTROLTEMP   0x0080
#define FLT_CANNOTCONTROLHUM    0x0040
#define FLT_SMEM                0x0020
#define FLT_PARENT_TSEN_NOISY   0x0010
#define FLT_PARENT_RHSEN_NOISY  0x0008
//#define FLT_COMMUNICATION       0x0004
#define FLT_IICFAIL             0x0002     

    // Parent sysStat bits.  Saved every 5 sec to SEEPROM.
#define ST_DEBUG_MODE           0x80
#define ST_DATALOG_ON           0x40
#define ST_USE_CELSIUS          0x20
#define ST_AIR_EXCHGR_ON        0x10        // Overrides use of HVAC appliances
#define ST_SHOW_TIME            0x08        // Show RTCC vals
#define ST_SECURITY_LAPSED      0x04        // More than 30 days or 10 power cycles have lapsed
#define ST_SECURITY_PASS        0x02        // A valid code has been entered, nihil ostat

    /**** Hardware mapping definitions...   ****/

    // Analog channels
#define ANCH_ISEN1      22   // AN22 U7.1 = RE5
#define ANCH_ISEN2      23   // AN23 U7.2 = RE6
#define ANCH_ISEN3      27   // AN27 U7.3 = RE7
#define ANCH_ISEN4       5   // AN5 U7.11 = RB5
#define ANCH_ISEN5       4   // AN4 U7.12 = RB4
#define ANCH_ISEN6       1   // AN9 U7.15 = RB1
#define ANCH_ISEN7       6   // AN6 U7.17 = RB6
#define ANCH_ISEN8       7   // AN7 U7.18 = RB7
#define ANCH_RELHUM      8   // AN8 U7.21 = RB8
#define ANCH_TEMPER      9   // AN9 U7.22 = RB9

    // Hardware lines
#define LED_GREEN   LATBbits.LATB2      // o0 pin 14
#define LED_RED     LATBbits.LATB3      // o0 pin 13
#define BUZZER      PORTFbits.RF6       // Uses PWM on RPF6

#define LCD_WR_n    LATDbits.LATD3      // o0 IC4.51
#define LCD_RD_n    LATEbits.LATE0      // o0 IC4.60
#define LCD_A0      LATEbits.LATE1      // o0 IC4.61
#define LCD_RES_n   LATDbits.LATD2      // o1 IC4.50
#define LCD_CS1_n   LATDbits.LATD1      // o1 IC4.49
#define LCD_WD0     LATDbits.LATD4      // o0 U7.52
#define LCD_WD1     LATDbits.LATD5      // o0 U7.53
#define LCD_WD2     LATDbits.LATD6      // o0 U7.54
#define LCD_WD3     LATDbits.LATD7      // o0 U7.55
#define LCD_WD4     LATDbits.LATD8      // o0 U7.42
#define LCD_WD5     LATDbits.LATD9      // o0 U7.43
#define LCD_WD6     LATDbits.LATD10     // o0 U7.44
#define LCD_WD7     LATDbits.LATD11     // o0 U7.45
#define LCD_RD0     PORTDbits.RD4       // For reads..
#define LCD_RD1     PORTDbits.RD5
#define LCD_RD2     PORTDbits.RD6
#define LCD_RD3     PORTDbits.RD7
#define LCD_RD4     PORTDbits.RD8
#define LCD_RD5     PORTDbits.RD9
#define LCD_RD6     PORTDbits.RD10
#define LCD_RD7     PORTDbits.RD11

//#define RS232_ENA_n  LATFbits.LATF5       // o1 U7.32
//#define RS232_SHDN_n LATFbits.LATF4       // o1 U7.31
#define RS485_RE_n   LATBbits.LATB13      // o1 IC4.28
#define RS485_DE     LATBbits.LATB12      // o1 IC4.27
#define SMEM_CS_n    LATGbits.LATG9       // o1 IC4.8
#define RELAY1       LATBbits.LATB15      // o1 IC4.30
#define PARENT_K_AirCon    LATBbits.LATB15
#define RELAY2       LATBbits.LATB14      // o1 IC4.29
#define PARENT_K_Heater LATBbits.LATB14
//#define IIC_SCL     PORTGbits.RG2
#define IIC_SCL     LATGbits.LATG2
//#define IIC_SDA     PORTGbits.RG3
#define IIC_SDA     LATGbits.LATG3
#define IIC_SDA_TRIS    TRISGbits.TRISG3

#define STD_KEY1    PORTFbits.RF4       // Mechanical MOM keys
#define STD_KEY2    PORTFbits.RF5
#define STD_KEY3    PORTGbits.RG3
#define STD_KEY4    PORTGbits.RG2
#define SWI_DEBUG   PORTBbits.RB11
#endif	
