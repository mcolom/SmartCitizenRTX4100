/*
 * Copyright (C) 2014 Miguel Colom - http://mcolom.info
 * This file is part of the SmartCitizen RTX4100 module firmware
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

/****************************************************************************
*                               Include files
****************************************************************************/

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <Core/RtxCore.h>
#include <Ros/RosCfg.h>
#include <PortDef.h>
#include <Api/Api.h>
#include <Cola/Cola.h>
#include <Protothreads/Protothreads.h>
#include <SwClock/SwClock.h>
#include <BuildInfo/BuildInfo.h>
#include <NetUtils/NetUtils.h>

#include <PtApps/AppCommon.h>
#include <PtApps/AppLed.h>
#include <PtApps/AppSocket.h>
#include <PtApps/AppWifi.h>
#include <PtApps/AppSntp.h>
#include <Drivers/DrvLeuart.h>
#include <Drivers/DrvButtons.h>

/****************************************************************************
*                              Macro definitions
****************************************************************************/
#define TMP_STR_LENGTH 200

#define PRINT(x) UartPrint((rsuint8*)x)
#define PRINTLN(x) UartPrintLn((rsuint8*)x)

/****************************************************************************
*                     Enumerations/Type definitions/Structs
****************************************************************************/

//

/****************************************************************************
*                            Global variables/const
****************************************************************************/

static char strbuf[TMP_STR_LENGTH]; // Used by sprintf, snprintf

/****************************************************************************
*                            Local variables/const
****************************************************************************/
static RsListEntryType PtList; // Protothreads list


/****************************************************************************
*                          Local Function prototypes
****************************************************************************/

//

/****************************************************************************
*                                Implementation
***************************************************************************/

// Send a string to the dock (without adding a carriage return and the end)
static void UartPrint(rsuint8 *str) {
  DrvLeuartTxBuf(str, strlen((char*)str));
}

// Send a string to the dock, adding a carriage return and the end
static void UartPrintLn(rsuint8 *str) {
  UartPrint(str);
  DrvLeuartTx('\r');
  DrvLeuartTx('\n');
}

void WiFi_poweron(struct pt *Pt, const RosMailType* Mail) {
  static struct pt childPt;
  PT_SPAWN(Pt, &childPt, PtAppWifiPowerOn(&childPt, Mail));
}

void WiFi_poweroff(struct pt *Pt, const RosMailType* Mail) {
  static struct pt childPt;
  PT_SPAWN(Pt, &childPt, PtAppWifiPowerOff(&childPt, Mail));
}

int WiFi_power_status() {
  return (int)AppWifiIsPowerOn();  
}

void WiFi_info() {
  PRINTLN(strbuf);
  
  rsbool powered_on = AppWifiIsPowerOn();
  sprintf(strbuf, "AppWifiIsPowerOn() --> %d", powered_on);
  PRINTLN(strbuf);

  const ApiWifiMacAddrType *pMacAddr = AppWifiGetMacAddr();
  const AppWifiIpv6AddrType *pIpv6Addr = AppWifiIpv6GetAddr();

  if (AppWifiIpConfigIsStaticIp())
  {
    PRINTLN("Static IP\n");
  }
  else
  {
    PRINTLN("DHCP enabled\n");
  }
  PRINTLN("MAC address ................. : ");
  sprintf(strbuf, "%02X-%02X-%02X-%02X-%02X-%02X", (*pMacAddr)[0], (*pMacAddr)[1], (*pMacAddr)[2], (*pMacAddr)[3], (*pMacAddr)[4], (*pMacAddr)[5]);
  PRINTLN(strbuf);

  inet_ntoa(AppWifiIpv4GetAddress(), strbuf);
  PRINTLN("\nIPv4 address ................ : ");
  PRINTLN(strbuf);
  inet_ntoa(AppWifiIpv4GetSubnetMask(), strbuf);
  PRINTLN("\nIPv4 SubnetMask ............. : ");
  PRINTLN(strbuf);
  inet_ntoa(AppWifiIpv4GetGateway(), strbuf);
  PRINTLN("\nIPv4 Gateway ................ : ");
  PRINTLN(strbuf);
  if (AppWifiIpv4GetPrimaryDns())
  {
    inet_ntoa(AppWifiIpv4GetPrimaryDns(), strbuf); 
    PRINTLN("\nIPv4 Primary DNS server...... : ");
    PRINTLN(strbuf);
  }
  if (AppWifiIpv4GetSecondaryDns())
  {
    inet_ntoa(AppWifiIpv4GetSecondaryDns(), strbuf); 
    PRINTLN("\nIPv4 Secondary DNS server.... : ");
    PRINTLN(strbuf);
  }
  PRINTLN("\n");

  if (pIpv6Addr->LocalAddress.Addr[0])
  {
    PRINTLN("Link-local IPv6 Address ..... : ");
    PRINTLN(inet6_ntoa((char *)pIpv6Addr->LocalAddress.Addr, strbuf));
    if (pIpv6Addr->LinkPrefix)
    {
      sprintf(strbuf, "/%d", (int)pIpv6Addr->LinkPrefix);
      PRINTLN(strbuf);
    }
    PRINTLN("\n");

    PRINTLN("Global IPv6 Address ......... : ");
    PRINTLN(inet6_ntoa((char *)pIpv6Addr->GlobalAddress.Addr, strbuf));
    if (pIpv6Addr->GlobalPrefix)
    {
      sprintf(strbuf, "/%d", (int)pIpv6Addr->GlobalPrefix);
      PRINTLN(strbuf);
    }
    PRINTLN("\n");

    PRINTLN("Default Gateway Address ..... : ");
    PRINTLN(inet6_ntoa((char *)pIpv6Addr->Gateway.Addr, strbuf));
    if (pIpv6Addr->GatewayPrefix)
    {
      sprintf(strbuf, "/%d", (int)pIpv6Addr->GatewayPrefix);
      PRINTLN(strbuf);
    }
    PRINTLN("\n");

    PRINTLN("Global IPv6 Address 2 ....... : ");
    PRINTLN(inet6_ntoa((char *)pIpv6Addr->LinkAddrExtd.Addr, strbuf));
    if (pIpv6Addr->LinkAddrExtdPrefix)
    {
      sprintf(strbuf, "/%d", (int)pIpv6Addr->LinkAddrExtdPrefix);
      PRINTLN(strbuf);
    }
    PRINTLN("\n");
  }
}





// Process a line the user send to the terminal
void process_terminal_line(rsuint8 *buffer,
                           struct pt *Pt,
                           const RosMailType* Mail) {
  sprintf(strbuf, "Linea: %s", buffer);
  
  if (!strcmp(buffer, "poweron")) {
    WiFi_poweron(Pt, Mail);
  }
  else if (!strcmp(buffer, "poweroff")) {
    WiFi_poweroff(Pt, Mail);
  }
  else if (!strcmp(buffer, "powerstatus")) {
	int power_status = WiFi_power_status();
    sprintf(strbuf, "power status: %d", power_status);
    PRINTLN(strbuf);
  }
  else if (!strcmp(buffer, "info")) {
    WiFi_info();
  }
  else
    if (strlen(buffer) > 0)
      PRINTLN("Unknown command");  
}

// Main thread
static PT_THREAD(PtMain(struct pt *Pt, const RosMailType *Mail))
{
  PT_BEGIN(Pt);
  
  // Terminal buffer
  static rsuint8 buffer[TMP_STR_LENGTH];
  static unsigned buf_ptr = 0;
  
  // Init LUART
  static struct pt childPt;
  PT_SPAWN(Pt, &childPt, PtDrvLeuartInit(&childPt, Mail));

  // WiFi reset.
  // Simply powering it up doesn't seem to be enough
  PT_SPAWN(Pt, &childPt, PtAppWifiReset(&childPt, Mail));  

  // Shell loop
  while (1) {
    //Flush UART RX buffer
    DrvLeuartRxFlush();

    // Read from UART to we have a command line
    while (1) {
      // read lines
      rsuint8 c;
      while (DrvLeuartRx(&c, 1)) {
		if (buf_ptr >= TMP_STR_LENGTH || c == '\r' || c == '\n') {
          PRINTLN("");
          buffer[buf_ptr] = 0; // Add end-of-string zero
          process_terminal_line(buffer, Pt, Mail);
          buf_ptr = 0;
          PRINT("debug$ "); // Prompt
		}
		else {
          rsuint8 echoed[2] = {c, 0};
          PRINT(echoed); // Echo character

		  if (c == 127) { // Backspace
            if (buf_ptr > 0) buf_ptr--;
          }
          else
            buffer[buf_ptr++] = c; // Add a new character to the line            
        }
      }

      // Allow other tasks to run
      PT_YIELD(Pt);
    }
    
    PRINTLN("WARNING: EXITED FROM INNER LOOP");
  }

  PT_END(Pt);
}



void ColaTask(const RosMailType *Mail)
{
  // Pre-dispatch mail handling
  switch (Mail->Primitive) {
    case INITTASK:
      // Init the Protothreads lib
      PtInit(&PtList);
      
      // Init the WiFi management application
      AppWifiInit(&PtList);      
    
      // Start the Main protothread
      PtStart(&PtList, PtMain, NULL, NULL);
      break;

    case TERMINATETASK:
      RosTaskTerminated(ColaIf->ColaTaskId);
      break;
  }

  // Dispatch mail to all protothreads started
  PtDispatchMail(&PtList, Mail);
}

// End of file.

