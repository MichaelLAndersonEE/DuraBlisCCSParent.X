/* File:        configs.h 
 * Purpose:     Low level config bit inits
 * Author:      Michael L Anderson
 * Contact:     MichaelLAndersonEE@gmail.com
 * Platform:    PIC32MX350F256H on DB-CCS-Main_v2.x board
 * Created:     16 May 14
 */

    // Configuration Bits
#pragma config FSRSSEL  = PRIORITY_0    // Shadow register set priority select
#pragma config PMDL1WAY = OFF           // Allow multiple peripheral reconfigs
#pragma config IOL1WAY = OFF            // Allow multiple peripheral pin reconfigs
#pragma config FPLLIDIV = DIV_1         // PLL input divider = 1x
#pragma config FPLLMUL = MUL_20         // PLL mult = 20x
#pragma config FPLLODIV = DIV_1         // PLL out clk div by 1
#pragma config FNOSC = PRI              // Osc select XT, HS, EC
#pragma config FSOSCEN = ON             // Secondary osc disa TODO
#pragma config IESO = OFF               // Int/ext switchover disa
#pragma config POSCMOD = HS             // Pri osc config
#pragma config OSCIOFNC = OFF           // CLKO output disa
#pragma config FPBDIV = DIV_1           // Peri clk divisor is /1
#pragma config FCKSM = CSDCMD           // Clk swi disa, FSCM disa
#pragma config WDTPS = PS4096           // WDT postscaler 1:4096
    // WDT clocked by LPRC @ 32 kHz => 128 ms
#pragma config WINDIS = OFF             // WDT in non-window mode
#pragma config FWDTEN = ON              // WDT disa
#pragma config FWDTWINSZ = WINSZ_50     // WDT window 50%
#pragma config DEBUG = OFF              // Background debugger disa
#pragma config JTAGEN = OFF             // JTAG disa
#pragma config ICESEL = ICS_PGx3        // ICE/ICD on PGEx1
#pragma config PWP = OFF                // Program flash write protect range, disa
#pragma config BWP = OFF                // Boot flash write protect bit
#pragma config CP = OFF                 // Code protect disa
