/* File:   child.c  Child emulation for T&D.  May be useful to maintain.
 * Author: Michael L Anderson
 * Contact: MichaelLAndersonEE@gmail.com
 * Platform: PIC32MX350F256H on BW-CCS-Main_v2 board. Specifically the Exar SP3222 RS232 driver
 * Created: 25 Jun 14
 */

#include <plib.h>
#include "DuraBlisCCSParent.h"
#include "child.h"

byte childNodeSetting = 3;        // Fake. WHat child reads locally on DIP sw
unsigned childEqWord = CHILD_EQPT_EXTFAN_K1 +  CHILD_EQPT_EXTFAN_K2 + CHILD_EQPT_TH_OUTSIDE;
    // Fake. WHat child reads locally on DIP sw on reset
unsigned newEqWord;
float childTemperature = 95.6;    // Fake.
float childRHumidity = 45.6;      // Fake.
unsigned childStatus = CHILD_TSEN_OKAY + CHILD_HSEN_OKAY;   // Fake.
    // TODO for child, check impossible combos

void assignEqWord(byte childNodeNumber, unsigned newEqWord)
{
    if (childNodeNumber == childNodeSetting)
    {
        childEqWord = newEqWord;
        putPrompt();
        putStr("spak");
        putChar('0' + childNodeNumber);
        putUns2Hex(childEqWord);
        putPrompt();
    }
    else return;
}

    // --------------------------
void assignNodeNumber(byte childNodeNumberOld, byte childNodeNumber)
{
    if (childNodeNumberOld == childNodeSetting)
    {
        childNodeSetting = childNodeNumber;
        putPrompt();
        putStr("prak");
        putChar('0' + childNodeNumberOld);
        putChar('0' + childNodeNumber);
        putPrompt();
    }
    else return;
}
    // TODO assure version compat
    // TODO timeout
    // TODO the putPrompt()'s here are solely for emulation
    // Refer to PNet Communication Protocol doc
void childModeStMach(char charCh)   // Child emulation
{
    static unsigned int stMach = 0;
    static byte childNodeNumber, childNodeNumberOld;
    static byte childRelayNumber;

    switch(stMach)
    {
        case 0:
            if (charCh == 'A') stMach = 10;
            else if (charCh == 'S') stMach = 20;
            else if (charCh == 'T') stMach = 30;
            else if (charCh == 'H') stMach = 40;
            else if (charCh == 'V') stMach = 50;
            else if (charCh == 'P') stMach = 60;
            else if (charCh == 'R') stMach = 70;
            break;

        case 10:        // Reading A messages
             if (charCh == 'T') stMach = 11;
             else stMach = 0;
             break;

        case 11:
            stMach = 0;     // Terminus
            if (charCh > '0' && charCh < '9')
            {                
                childNodeNumber = charCh - '0';
                respondAttention(childNodeNumber);
            }
            break;

        case 20:        // Reading S messages, status or SVPR
            if (charCh == '?') stMach = 21;
            else if (charCh == 'V') stMach = 23;
            else stMach = 0;     // Terminus
            break;

        case 21:
            stMach = 0;     // Terminus
            if (charCh > '0' && charCh < '9')
            {               
                childNodeNumber = charCh - '0';
                respondStatus(childNodeNumber);
            }
            break;

        case 23:
            if (charCh == 'P') stMach = 24;
            else stMach = 0;
            break;

        case 24:
            if (charCh == 'R') stMach = 25;
            else stMach = 0;
            break;

        case 25:
            if (charCh >= '0' && charCh < '9')  // Sic
            {
                stMach = 26;
                childNodeNumber = charCh - '0';
            }
            else stMach = 0;
            break;

        case 26:        // Note, in VER 1 PNet, we limit this to 3 nibbles
            if (charCh >= '0' && charCh <= 'F')
            {
                stMach = 27;
                newEqWord = (charCh - '0') << 8;
            }
            else stMach = 0;
            break;

        case 27:
            if (charCh >= '0' && charCh <= 'F')
            {
                stMach = 28;
                newEqWord += (charCh - '0') << 4;
            }
            else stMach = 0;
            break;

         case 28:
            stMach = 0;
            if (charCh >= '0' && charCh <= 'F')
            {
                newEqWord += (charCh - '0');
                if (realityCheckEq(newEqWord))
                {
                    assignEqWord(childNodeNumber, newEqWord);
                }
                else
                {
                    putStr("sp");
                    putChar('0' + childNodeSetting);
                    putStr("ir");
                }
            }         
            break;

        case 30:        // Reading T messages
            if (charCh == '?') stMach = 31;
            else stMach = 0;
            break;

        case 31:
            stMach = 0;     // Terminus
            if (charCh > '0' && charCh < '9')
            {               
                childNodeNumber = charCh - '0';
                respondTemperature(childNodeNumber);
            }
            break;

        case 40:        // Reading H messages
            if (charCh == '?') stMach = 41;
            else stMach = 0;
            break;

        case 41:
            stMach = 0;     // Terminus
            if (charCh > '0' && charCh < '9')
            {                
                childNodeNumber = charCh - '0';
                respondRHumidity(childNodeNumber);
            }
            break;

        case 50:        // Reading V service requests
            if (charCh > '0' && charCh < '9')
            {
                stMach = 51;
                childNodeNumber = charCh - '0';
            }
            else stMach = 0;
            break;

        case 51:
            if (charCh == '1' || charCh == '2')
            {
                stMach = 52;
                childRelayNumber = charCh;  // Here use char
            }
            else stMach = 0;
            break;

        case 52:
            stMach = 0;     // Terminus
            if (charCh == '+') respondServiceRequest(childNodeNumber, childRelayNumber, '+');
            else if (charCh == '-') respondServiceRequest(childNodeNumber, childRelayNumber, '-');
            break;

        case 60:            // Reading PROGmn
            if (charCh == 'R') stMach = 61;
            else stMach = 0;
            break;

        case 61:
            if (charCh == 'O') stMach = 62;
            else stMach = 0;
            break;

        case 62:
            if (charCh == 'G') stMach = 63;
            else stMach = 0;
            break;

        case 63:
            stMach = 64;
            if (charCh >= '0' && charCh < '9')  // Sic
                 childNodeNumberOld = charCh - '0';
            else stMach = 0;
            break;

        case 64:
            stMach = 0;         // Terminus
            if (charCh >= '0' && charCh < '9')  // Sic
            {
                 childNodeNumber = charCh - '0';
                 assignNodeNumber(childNodeNumberOld, childNodeNumber);
            }           
            break;

        case 70:
            if (charCh == 'E') stMach = 71;
            else stMach = 0;
            break;

        case 71:
            if (charCh == 'S') stMach = 72;
            else stMach = 0;
            break;

        case 72:
            if (charCh == 'E') stMach = 73;
            else stMach = 0;
            break;

        case 73:
            stMach = 0;
            if (charCh == 'T')
            {
                SYSKEY = 0xAA996655;
                SYSKEY = 0x556699AA;
                RSWRSTbits.SWRST = 1;
                newEqWord = RSWRSTbits.SWRST;
                while(1);       // Seppuku
            }
            break;  


        default:
            stMach = 0;     // Any syntax error resets
    }
}

     // ---------------------------
void childReportInfo(void)
{
    putStr("\n\r Child node setting: ");
    putChar('0' + childNodeSetting);
    putStr("\n\r Child Eq word: ");
    putUns2Hex(childEqWord);
    putChar('h');
    putStr("\n\r Child status: ");
    putUns2Hex(childStatus);
    putChar('h');
}

    // ---------------------------
    // Screen unacceptable combos.
bool realityCheckEq(byte newEqWord)
{
    byte sum = 0;
    if ((newEqWord & CHILD_EQPT_TH_OUTSIDE) && (newEqWord & CHILD_EQPT_TH_INTFAN)) return false;
    if (newEqWord & CHILD_EQPT_DEHUM_K1) sum++;
    if (newEqWord & CHILD_EQPT_EXTFAN_K1) sum++;
    if (newEqWord & CHILD_EQPT_HUMIDIFIER_K1) sum++;
    if (sum > 1) return false;
    sum = 0;
    if (newEqWord & CHILD_EQPT_DEHUM_K2) sum++;
    if (newEqWord & CHILD_EQPT_EXTFAN_K2) sum++;
    if (newEqWord & CHILD_EQPT_HUMIDIFIER_K2) sum++;
    if (sum > 1) return false;
    return true;
}

    // ---------------------------
void respondAttention(byte ch)
{
    if (ch == childNodeSetting)
    {
        putPrompt();
        putStr("ak");
        putChar('0' + childNodeSetting);
        putByte2Hex(childEqWord);
        putPrompt();
    }
    else return;
}

    // ---------------------------
void respondRHumidity(byte ch)
{
    char outBfr[11];

    if (ch == childNodeSetting)
    {
        putPrompt();
        putStr("hk");
        putChar('0' + childNodeSetting);
        if (childEqWord & CHILD_EQPT_TH_OUTSIDE)
        {
            if (childStatus & CHILD_HSEN_OKAY)
            {
                sprintf(outBfr, "%04.1f", childRHumidity);
                putStr(outBfr);
            }
            else
            {
                putStr("sf");
                putUns2Hex(childStatus);
            }
        }
        else
        {
            putStr("ir");
        }
        putPrompt();
    }
    else return;
}

    // --------------------------
void respondServiceQuery(byte ch)
{
    if (ch == childNodeSetting)
    {
        putPrompt();
        putChar('v');
        putChar('0' + childNodeSetting);
        putChar('1');
        if (childStatus & CHILD_K1_ON) putChar('+');
        else putChar('-');
        putChar('2');
        if (childStatus & CHILD_K2_ON) putChar('+');
        else putChar('-');
    }
}

    // ------------------------
void respondServiceRequest(byte ch, char relayNo, char onOff)
{
    if (ch == childNodeSetting)
    {
        putPrompt();
        putStr("vk");
        putChar('0' + childNodeSetting);
        if (relayNo == '1')
        {
            if (childEqWord & (CHILD_EQPT_EXTFAN_K1 | CHILD_EQPT_TH_INTFAN  |
                    CHILD_EQPT_DEHUM_K1 | CHILD_EQPT_HUMIDIFIER_K1))
            {
                // TODO...
                putChar('1');
                putChar(onOff);
            }
            else
            {
                putStr("ir");
            }
        }
        else if (relayNo == '2')
        {
            if (childEqWord & (CHILD_EQPT_EXTFAN_K2 | CHILD_EQPT_TH_INTFAN  |
                    CHILD_EQPT_DEHUM_K2 | CHILD_EQPT_HUMIDIFIER_K2))
               {
                // TODO...
                putChar('2');
                putChar(onOff);
            }
            else
            {
                putStr("ir");
            }
        }
        putPrompt();
    }
    else return;
}

    // ---------------------------
void respondStatus(byte ch)
{
    if (ch == childNodeSetting)
    {
        putPrompt();
        putStr("sk");
        putChar('0' + childNodeSetting);
        putChar('t');
        if (!(childEqWord & CHILD_EQPT_TH_OUTSIDE)) putChar('0');
        else if (childStatus & CHILD_TSEN_OKAY) putChar('+');
        else putChar('-');

        putChar('h');
        if (!(childEqWord & CHILD_EQPT_TH_OUTSIDE)) putChar('0');
        else if (childStatus & CHILD_HSEN_OKAY) putChar('+');
        else putChar('-');

        putChar('1');
        if (childStatus & CHILD_K1_ON) putChar('+');
        else putChar('-');

        putChar('2');
        if (childStatus & CHILD_K2_ON) putChar('+');
        else putChar('-');
        putPrompt();
    }
    else return;
}

    // ---------------------------
void respondTemperature(byte ch)
{
    char outBfr[11];

    if (ch == childNodeSetting)
    {
        putPrompt();
        putStr("tk");
        putChar('0' + childNodeSetting);
        if (childEqWord & CHILD_EQPT_TH_OUTSIDE)
        {
            if (childStatus & CHILD_TSEN_OKAY)
            {
                sprintf(outBfr, "%04.1f", childTemperature);
                putStr(outBfr);
            }
            else
            {
                putStr("sf");
                putUns2Hex(childStatus);
            }
        }
        else
        {
            putStr("ir");
        }
        putPrompt();
    }
    else return;
}



 


