/*
 *
 * This file is part of the RTX4100 API firmware
 * Miguel Colom, 2014 - http://mcolom.info
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

// Decomment to activate debugging using terminal LUART
//#define USE_LUART_TERMINAL

// Decomment to activate normal operation using SPI
#define SPI_COMMUNICATION

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

#include <Core/RtxCore.h>
#include <Ros/RosCfg.h>
#include <PortDef.h>
#include <Api/Api.h>
#include <Cola/Cola.h>
#include <Protothreads/Protothreads.h>
#include <NetUtils/NetUtils.h>
#include <Drivers/DrvButtons.h>

#include <PtApps/AppCommon.h>
#include <PtApps/AppLed.h>
#include <PtApps/AppSocket.h>
#include <PtApps/AppWifi.h>


#ifdef USE_LUART_TERMINAL
#include <Drivers/DrvLeuart.h>
#endif

#ifdef SPI_COMMUNICATION
#include <Drivers/DrvSpi.h>
#endif



/****************************************************************************
*                              Macro definitions
****************************************************************************/
// Max WiFi transmission power
#define MAX_TX_POWER 18

// Macros to print with the LUART
#define PRINT(x) UartPrint((rsuint8*)x)
#define PRINTLN(x) UartPrintLn((rsuint8*)x)

// Buffer sizes
#define TMP_STR_LENGTH 100
#define CMD_STR_LENGTH TMP_STR_LENGTH
#define TX_BUFFER_LENGTH 500

// Maximum number of arguments for terminal commands
#define MAX_ARGV 3

// Timeout in seconds for DNS resolutions
#define APP_DNS_RESOLVE_RSP_TIMEOUT (10*RS_T1SEC)

/****************************************************************************
*                     Enumerations/Type definitions/Structs
****************************************************************************/
// Application data stored at the NVS
typedef struct {
  ApInfoType ap_info;
  rsuint8 use_dhcp;
  ApiSocketAddrType static_address, static_subnet, static_gateway;
} AppDataType;


/****************************************************************************
*                            Global variables/const
****************************************************************************/
// Static application data
static AppDataType app_data;

// Timers
static const RosTimerConfigType PacketDelayTimer =
  ROSTIMER(COLA_TASK, APP_PACKET_DELAY_TIMEOUT,
  APP_PACKET_DELAY_TIMER);

static const RosTimerConfigType DnsRspTimer =
  ROSTIMER(COLA_TASK, APP_DNS_RSP_TIMEOUT,
  APP_DNS_RSP_TIMER);

// TCP flags
static char TCP_is_connected; // True when the TCP connection has been stablished
static char TCP_received; // True when data has been received at the TCP connection

static int socketHandle; // The socket ID of the TCP connection
static rsuint8 *TCP_receive_buffer_ptr; // TCP receive buffer
static int TCP_Rx_bufferLength; // Number of bytes received at the TCP buffer

// Energy control
static rsuint8 is_suspended;


/****************************************************************************
*                            Local variables/const
****************************************************************************/
static RsListEntryType PtList; // Protothread control

#ifdef USE_LUART_TERMINAL  
// Buffers for LUART terminal I/O 
static char TmpStr[TMP_STR_LENGTH];
static char CmdStr[CMD_STR_LENGTH];
rsuint16 ShellRxIdx;

rsuint16 argc;
char *argv[MAX_ARGV];
#endif  

// TCP send/receive/ buffers
rsuint8 tx_buffer[TX_BUFFER_LENGTH];
rsuint8 rx_buffer[TX_BUFFER_LENGTH];


/****************************************************************************
*                                Implementation
***************************************************************************/

#ifdef USE_LUART_TERMINAL
/**
 * @brief Send a string to the dock (without adding a carriage return
 * and the end
 * @param str : string to print
 **/
static void UartPrint(rsuint8 *str) {
  DrvLeuartTxBuf(str, strlen((char*)str));
}

/**
 * @brief Send a string to the dock, adding a carriage return at the end
 * @param str : string to print
 **/
static void UartPrintLn(rsuint8 *str) {
  UartPrint(str);
  DrvLeuartTx('\r');
  DrvLeuartTx('\n');
}

/**
 * @brief Prints the IP and MAC addresses after DHCP or static config
 **/
static void print_IP_config(void) {
  const ApiWifiMacAddrType *pMacAddr = AppWifiGetMacAddr();

  if (AppWifiIpConfigIsStaticIp())
    PRINTLN("Static IP");
  else
    PRINTLN("DHCP");

  sprintf(TmpStr, "MAC: %02X-%02X-%02X-%02X-%02X-%02X", (*pMacAddr)[0],
    (*pMacAddr)[1], (*pMacAddr)[2], (*pMacAddr)[3], (*pMacAddr)[4],
    (*pMacAddr)[5]);
  PRINTLN(TmpStr);

  inet_ntoa(AppWifiIpv4GetAddress(), TmpStr);
  PRINT("IP: ");
  PRINTLN(TmpStr);
  //
  inet_ntoa(AppWifiIpv4GetSubnetMask(), TmpStr);
  PRINT("Subnet: ");
  PRINTLN(TmpStr);
  //
  inet_ntoa(AppWifiIpv4GetGateway(), TmpStr);
  PRINT("Gateway: ");
  PRINTLN(TmpStr);
  
  if (AppWifiIpv4GetPrimaryDns()) {
    inet_ntoa(AppWifiIpv4GetPrimaryDns(), TmpStr); 
    PRINT("Primary DNS: ");
    PRINTLN(TmpStr);
  }
  if (AppWifiIpv4GetSecondaryDns()) {
    inet_ntoa(AppWifiIpv4GetSecondaryDns(), TmpStr); 
    PRINT("Secondary DNS: ");
    PRINTLN(TmpStr);
  }
  PRINTLN("");
}

/**
 * @brief Prints the SSID of the connected WiFi network
 **/
static void print_SSID(void) {
  AppLedSetLedState(LED_STATE_CONNECTED);
  sprintf(TmpStr, "WiFi connected to SSID: %s\n",
    AppWifiGetSsid(AppWifiGetCurrentApIdx()));
  PRINTLN(TmpStr);
}

/**
 * @brief Checks if the given character is valid terminal input
 * @param c : character to check
 **/
static rsbool is_valid_cmd_char(char c) {
  if (isprint((rsuint8)c))
    return TRUE;

  switch (c) {
  case '\r':
  case 0x8: // backspace;
  case 0x7F: // backspace;
  case 0x1A: // ctrl-z
    return TRUE;
  }
  return FALSE;
}

/**
 * @brief Extract the terminal arguments for the given input
 * @param str : input string
 **/
rsuint16 ParseArgs(char *str, char *argv[]) {
  rsuint16 i = 0;
  char *ch = str;

  if (*ch == '\r')
    return 0;

  while (*ch != '\0') {
    i++;

    // Check if length exceeds
    if (i > MAX_ARGV) {
      #ifdef USE_LUART_TERMINAL
      PRINT("Too many arguments\n");
      #endif
      return 0;
    }

    argv[i - 1] = ch;
    while (*ch != ' ' && *ch != '\0' && *ch != '\r') {
      if (*ch == '"') { // Allow space characters inside double quotes
        ch++;
        argv[i - 1] = ch; // Drop the first double quote char
        while (*ch != '"') {
          if (*ch == '\0' || *ch == '\r') {
            #ifdef USE_LUART_TERMINAL
            PRINTLN("Syntax error");
            #endif
            return 0;
          }
          ch++; // Record until next double quote char
        }
        break;
      }
      else {
        ch++;
      }
    }

    if (*ch == '\r')
      break;

    if (*ch != '\0') {
      *ch = '\0';
      ch++;
      while (*ch == ' ') {
        ch++;
      }
    }
  }
  
  return i;
}
#endif

/**
 * @brief Helper function to extract a substring from a string
 * @param dest : extracted substring pointer
 * @param orig : input string pointer
 * @param orig_ptr : offset at the input string to extract next string
 **/
int extract_substring(rsuint8 *dest, rsuint8 *orig, int orig_ptr) {
  int dest_ptr = 0;
  
  if (orig[orig_ptr] == '\n' || orig[orig_ptr] == 0)
    return -1; // No more data
  
  while (orig[orig_ptr] != '\n' && orig[orig_ptr] != 0)
    dest[dest_ptr++] = orig[orig_ptr++];

  dest[dest_ptr] = 0; // End-of-string zero  
  return 1 + orig_ptr; // Return next position after '\n'
}

/**
 * @brief Saves the application info object contents to NVS
 **/
void Wifi_save_appInfo_to_NVS() {
  NvsWrite(NVS_OFFSET(Free),
           sizeof(AppDataType),
           (rsuint8*)&app_data);
}

/**
 * @brief Retrieves the application info object contents to NVS
 **/
void Wifi_read_appInfo_from_NVS() {
  NvsRead(NVS_OFFSET(Free),
          sizeof(AppDataType),
          (rsuint8 *)&app_data);
}

/**
 * @brief Fulfills an ApInfoType object from a string
 * @param ap_data : input string
 * @param ap_info : AP info object to fulfill
 **/
void get_ap_info_from_str(rsuint8 *ap_data, ApInfoType *ap_info) {
  int ap_ptr = 0;
  
  ap_info->KeyIndex = 0;

  // Extract SSID
  ap_ptr = extract_substring(ap_info->Ssid, ap_data, ap_ptr);
  ap_info->SsidLength = (rsuint8)strlen((char*)ap_info->Ssid);
  #ifdef USE_LUART_TERMINAL
  sprintf(TmpStr, "SSID=%s, ssid_len=%d", ap_info->Ssid, ap_info->SsidLength); PRINTLN(TmpStr);
  #endif

  // Extract encryption algorithm
  rsuint8 securityType_str[100];
  ap_ptr = extract_substring(securityType_str, ap_data, ap_ptr);
  #ifdef USE_LUART_TERMINAL
  sprintf(TmpStr, "Encryption=%s", securityType_str); PRINTLN(TmpStr);  
  #endif

  // Extract key
  ap_ptr = extract_substring(ap_info->Key, ap_data, ap_ptr);
  #ifdef USE_LUART_TERMINAL
  sprintf(TmpStr, "key=%s", ap_info->Key); PRINTLN(TmpStr);
  #endif
  //
  ap_info->KeyLength = (rsuint8)strlen((char*)ap_info->Key);

  // Set securityType and cipher according to securityType_str
  ap_info->SecurityType = AWST_NONE; // Just to prevent the warning
  if (!strcasecmp((char*)securityType_str, "WPA")) {
    #ifdef USE_LUART_TERMINAL
    PRINTLN("Set WPA");
    #endif
    ap_info->SecurityType = AWST_WPA;
    ap_info->Mcipher = AWCT_TKIP; 
    ap_info->Ucipher = AWCT_TKIP;
  }
  else if (!strcasecmp((char*)securityType_str, "WPA2")) {
    #ifdef USE_LUART_TERMINAL
    PRINTLN("Set WPA2");
    #endif
    ap_info->SecurityType = AWST_WPA2;
    ap_info->Mcipher = AWCT_CCMP; 
    ap_info->Ucipher = AWCT_CCMP;
  }
  else if (!strcasecmp((char*)securityType_str, "NONE")) {
    #ifdef USE_LUART_TERMINAL
    PRINTLN("Set encryption NONE");
    #endif
    ap_info->SecurityType = AWST_NONE;
  }

  // Extract extra parameter with encryption subalgorithm, if given
  ap_ptr = extract_substring(securityType_str, ap_data, ap_ptr);
  if (ap_ptr != -1) {
    #ifdef USE_LUART_TERMINAL
    PRINT("subAlgo="); PRINTLN(securityType_str);
    #endif
    
    // Update cipher subalgorithm
    if (!strcasecmp((char*)securityType_str, "TKIP")) {
      #ifdef USE_LUART_TERMINAL
      PRINTLN("Set TKIP");
      #endif
      ap_info->Mcipher = AWCT_TKIP; 
      ap_info->Ucipher = AWCT_TKIP;
    }
    else if (!strcasecmp((char*)securityType_str, "AES")) {
      #ifdef USE_LUART_TERMINAL
      PRINTLN("Set AES");
      #endif
      ap_info->Mcipher = AWCT_CCMP; 
      ap_info->Ucipher = AWCT_CCMP;
    }
    else {
      #ifdef USE_LUART_TERMINAL
      PRINT("Unknown encryption ");
      PRINTLN(securityType_str);
      #endif
    }    
  }
  #ifdef USE_LUART_TERMINAL
  else
    PRINTLN("No extra subalgo param");
  #endif
}

/**
 * @brief Setups an AP
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 * @param ap_data : input configuration string. If NULL, it will use
 * the data stored at the NVS to configure the AP
 **/
static PT_THREAD(PtWifi_setup_AP(struct pt *Pt, const RosMailType *Mail, rsuint8 *ap_data)) {
  // Examples of AP data strings:
  // "SSID\nWPA2\npassword\n"
  // "SSID\nWPA2\password\nAES\n"  
  
  static struct pt childPt;
  
  PT_BEGIN(Pt);

  ApInfoType *ap_info = &app_data.ap_info;
  // Decode AP configuration, only if a config. string is given.
  // If not, the default config (read from NVS at the beginning)
  // will be used.
  if (ap_data != NULL)
    get_ap_info_from_str(ap_data, ap_info);
   
  // Save AP information to NVS. Connect must be called afterwards
  rsuint8 ssid_len = (rsuint8)strlen((char*)ap_info->Ssid);
  
  ApiWifiCipherInfoType cipher;
  cipher.Ucipher = ap_info->Ucipher;
  cipher.Mcipher = ap_info->Mcipher;
  
  AppWifiSetApInfo(0, ssid_len, ap_info->Ssid, ap_info->SecurityType,
                   cipher, 0, ap_info->KeyLength, ap_info->Key);
  AppWifiWriteApInfoToNvs();

  // Store AP configuration, if a config. string is given
  if (ap_data == NULL) {
    #ifdef USE_LUART_TERMINAL
    sprintf(TmpStr, "NULL ap_data. Using SSID %s in NVS", ap_info->Ssid);
    PRINTLN(TmpStr);
    #endif
  }
  else
    Wifi_save_appInfo_to_NVS();

  // Disconnect, if associated to an old AP
  if (AppWifiIsAssociated()) {
    PT_SPAWN(Pt, &childPt, PtAppWifiDisconnect(&childPt, Mail));
    #ifdef USE_LUART_TERMINAL
    if (IS_RECEIVED(API_WIFI_DISCONNECT_IND)) {
      PRINTLN("Disconnected from old AP\n");
    }
    #endif
  }

  PT_END(Pt);
}

/**
 * @brief Suspends the WiFi chip
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 **/
static PT_THREAD(PtWifi_suspend(struct pt *Pt, const RosMailType *Mail)) {
  PT_BEGIN(Pt);
  is_suspended = true;
  POWER_TEST_PIN_TOGGLE;
  SendApiWifiSuspendReq(COLA_TASK, 10*60*1000); // ms
  PT_WAIT_UNTIL(Pt, IS_RECEIVED(API_WIFI_SUSPEND_CFM));
  POWER_TEST_PIN_TOGGLE;
  EMU_EnterEM2(); // uC enter suspend, too. Use an external interrupt to wake up!
  PT_END(Pt);
}


/**
 * @brief Resumes the suspended WiFi chip
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 **/
static PT_THREAD(PtWifi_resume(struct pt *Pt, const RosMailType *Mail)) {
  PT_BEGIN(Pt);
  POWER_TEST_PIN_TOGGLE;
  SendApiWifiResumeReq(COLA_TASK);
  PT_WAIT_UNTIL(Pt, IS_RECEIVED(API_WIFI_RESUME_CFM));
  POWER_TEST_PIN_TOGGLE;
  is_suspended = false;
  PT_END(Pt);
}

/**
 * @brief Sets the powersave profile
 * @param profile : powersave profile, 0: low power, 1: medium power,
 * 2: high power, 3: max power
 **/
void Wifi_set_power_save_profile(rsuint8 profile) {
  rsuint8 p;
  switch (profile) {
    case 0: p = POWER_SAVE_LOW_IDLE; break; // low power
    case 1: p = POWER_SAVE_MEDIUM_IDLE; break; // medium power
    case 2: p = POWER_SAVE_HIGH_IDLE; break; // high power
    case 3: p = POWER_SAVE_MAX_POWER; break; // max power
    default: p = 0xff;
  }
  
  if (p != 0xff)
    AppWifiSetPowerSaveProfile(p);
}

/**
 * @brief Associates and connects to an already configured AP
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 **/
static PT_THREAD(PtWifi_connect(struct pt *Pt, const RosMailType *Mail)) {
  static struct pt childPt;

  PT_BEGIN(Pt);
  
  if (!is_suspended) {
    Wifi_set_power_save_profile(3); // max power
    AppWifiSetTxPower(MAX_TX_POWER);
    
    // Read AP info
    #ifdef USE_LUART_TERMINAL
    PRINTLN("SendApiGetApinfoReq...");
    #endif
    SendApiGetApinfoReq(COLA_TASK);
    PT_YIELD_UNTIL(Pt, IS_RECEIVED(API_GET_APINFO_CFM));
    
    // Scan for known AP's
    #ifdef USE_LUART_TERMINAL
    PRINTLN("PtAppWifiScan...");
    #endif
    PT_SPAWN(Pt, &childPt, PtAppWifiScan(&childPt, Mail));

    // Connect to AP if it is available
    if (AppWifiIsApAvailable()) {
      #ifdef USE_LUART_TERMINAL
      PRINTLN("AppWifiIsApAvailable: YES");
      PRINTLN("PtWifi_connect spawning PtAppWifiConnect...");
      #endif

      AppLedSetLedState(LED_STATE_CONNECTING);
      PT_SPAWN(Pt, &childPt, PtAppWifiConnect(&childPt, Mail));
      AppLedSetLedState(LED_STATE_IDLE);
      //
      if (AppWifiIsConnected()) {
        // Connected to AP
        #ifdef USE_LUART_TERMINAL
        print_SSID();
        print_IP_config();
        #endif
        PtMailHandled = TRUE;

        // Update DNS client with default gateway addr
        SendApiDnsClientAddServerReq(COLA_TASK, AppWifiIpv4GetGateway(), AppWifiIpv6GetAddr()->Gateway);
      }    
    }
    else {
      #ifdef USE_LUART_TERMINAL
      PRINTLN("AppWifiIsApAvailable: NO");
      #endif
      // Avoid to store a corrupt SSID
      SendApiWifiSetSsidReq(COLA_TASK, 0, NULL);
    }
  }

  PT_END(Pt);
}

/**
 * @brief Checks if associated and connected to the AP
 * @return True if associated and connected to the AP. False otherwise
 **/
rsuint8 Wifi_is_connected() {
  return AppWifiIsConnected();
}

/**
 * @brief Sends a query to close the currently open TCP connection
 **/
void Wifi_TCP_close() {
  if (is_suspended)
    return;
  SendApiSocketCloseReq(COLA_TASK, socketHandle);
  socketHandle = 0;
}

/**
 * @brief Sends len bytes in tx_buffer using the TCP connection
 * @param len : number of bytes to send
 **/
void Wifi_TCP_send(rsuint16 len) {
  if (is_suspended)
    return;

  #ifdef USE_LUART_TERMINAL
  PRINTLN("Send...");
  #endif
  SendApiSocketSendReq(COLA_TASK, socketHandle, tx_buffer, len, 0);
}

/**
 * @brief Receive data in the RX buffer. Must be called by the user when
 * it polls the status and sees that TCP_received is activated.
 * @param len : number of bytes to send
 **/
char Wifi_TCP_receive() {
  if (is_suspended)
    return false;
  
  if (!TCP_received) {
    #ifdef USE_LUART_TERMINAL
    PRINTLN("No TCP data received!");
    #endif
    return false;
  }

  #ifdef USE_LUART_TERMINAL
  sprintf(TmpStr, "TCP received BufferLength: %d", TCP_Rx_bufferLength);
  PRINTLN(TmpStr);

  int i;
  for (i = 0; i < TCP_Rx_bufferLength; i++) {
    char chr[] = {0, 0};
    chr[0] = rx_buffer[i];
    PRINT(chr);
  }
  PRINTLN("");
  #endif

  SendApiSocketFreeBufferReq(COLA_TASK,
                             socketHandle,
                             TCP_receive_buffer_ptr);
  TCP_received = false; // clear flag
  return true;
}

/**
 * @brief Obtains the system status
 * @return bit wise system status. Bit 0: Wifi connected,
 * bit 1: TCP connected, bit 1: TCP data received.
 **/
rsuint8 Wifi_get_status() {
  rsuint8 status = 0;
  status |= ((Wifi_is_connected() & 1) << 0);
  status |= ((TCP_is_connected & 1) << 1);
  status |= ((TCP_received & 1) << 2);
  status |= ((is_suspended & 1) << 3);
  return status;
}

/**
 * @brief Sets the transmit wireless transmit power
 * @param power : wireless transmit power
 **/
void Wifi_set_tx_power(rsuint8 power) {
  if (power > MAX_TX_POWER)
    power = MAX_TX_POWER;
  AppWifiSetTxPower(power);
}

/**
 * @brief Powers on/off the WiFi chip
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 * @param on : true for poweron, false for poweroff
 **/
static PT_THREAD(PtWifi_power_on_off(struct pt *Pt,
                 const RosMailType *Mail,
                 char on)) {
  static struct pt childPt;
  #ifdef USE_LUART_TERMINAL
  PRINTLN("SPAWN PtWifi_power_on_off");
  #endif

  PT_BEGIN(Pt);
  if (on)
    PT_SPAWN(Pt, &childPt, PtAppWifiPowerOn(&childPt, Mail));
  else
    PT_SPAWN(Pt, &childPt, PtAppWifiPowerOff(&childPt, Mail));
  PT_END(Pt);
}

/**
 * @brief Disassociates and disconnects from the WiFi AP
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 **/
static PT_THREAD(PtWifi_disconnect(struct pt *Pt, const RosMailType *Mail)) {
  static struct pt childPt;
  PT_BEGIN(Pt);  
  PT_SPAWN(Pt, &childPt, PtAppWifiDisconnect(&childPt, Mail));
  PT_END(Pt);
}

/**
 * @brief Configures the IP connection (DHCP, static)
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 * @param config : configuration string. NULL to read configuration from
 * the NVS
 **/
static PT_THREAD(PtWifi_IP_config(struct pt *Pt, const RosMailType *Mail, rsuint8 *config)) {
  static const char *ip_format = "%d.%d.%d.%d";
  static char buffer[30];

  PT_BEGIN(Pt);
  
  // Read IP config parameters
  char load_from_NVS = (config == NULL);
  if (load_from_NVS) {
    #ifdef USE_LUART_TERMINAL
    PRINTLN("Reading IP config info from NVS");
    #endif
    Wifi_read_appInfo_from_NVS();
  }
  else {
    app_data.use_dhcp = (config[0] == 'd' || config[0] == 'D');

    if (!app_data.use_dhcp) {
      // Extract IP address
      sprintf(buffer, ip_format, config[1], config[2], config[3], config[4]);
      inet_aton(buffer, &app_data.static_address.Ip.V4.Addr);
      #ifdef USE_LUART_TERMINAL
      sprintf(TmpStr, "IP address: %s", buffer); PRINTLN(TmpStr);
      #endif

      // Extract subnet
      sprintf(buffer, ip_format, config[5], config[6], config[7], config[8]);
      inet_aton(buffer, &app_data.static_subnet.Ip.V4.Addr);
      #ifdef USE_LUART_TERMINAL
      sprintf(TmpStr, "Subnet: %s", buffer); PRINTLN(TmpStr);    
      #endif
      
      // Extract gateway
      sprintf(buffer, ip_format, config[9], config[10], config[11], config[12]);
      inet_aton(buffer, &app_data.static_gateway.Ip.V4.Addr);
      #ifdef USE_LUART_TERMINAL
      sprintf(TmpStr, "Gateway: %s", buffer); PRINTLN(TmpStr);
      #endif
    }

    // Store IP config information to NVS
    Wifi_save_appInfo_to_NVS();
  }

  // Once the config has been read, do IP config now
  if (app_data.use_dhcp) {
    // DHCP
    #ifdef USE_LUART_TERMINAL
    PRINTLN("Do DHCP");
    #endif
    AppWifiIpv4Config(FALSE, 0, 0, 0, 0);
    if (AppWifiIsConnected())
      PT_WAIT_UNTIL(Pt, IS_RECEIVED(APP_EVENT_IP_ADDR_RECEIVED) ||
                        IS_RECEIVED(API_WIFI_DISCONNECT_IND));
  }
  else {
    // Static IP address
    #ifdef USE_LUART_TERMINAL
    PRINTLN("Do static IP");

    // Print IP address
    inet_ntoa(app_data.static_address.Ip.V4.Addr, buffer);
    sprintf(TmpStr, "IP address: %s", buffer); PRINTLN(TmpStr);

    // Print subnet
    inet_ntoa(app_data.static_subnet.Ip.V4.Addr, buffer);
    sprintf(TmpStr, "Subnet: %s", buffer); PRINTLN(TmpStr);    

    // Extract gateway
    inet_ntoa(app_data.static_gateway.Ip.V4.Addr, buffer);
    sprintf(TmpStr, "Gateway: %s", buffer); PRINTLN(TmpStr);
    #endif

    AppWifiIpv4Config(TRUE, app_data.static_address.Ip.V4.Addr,
                            app_data.static_subnet.Ip.V4.Addr,
                            app_data.static_gateway.Ip.V4.Addr, 0);
    AppWifiWriteStaticIpToNvs();
  }

  PT_END(Pt);
}

/**
 * @brief Resolves a domain name using the DNS service
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 * @param name : domain name to resolve
 * @param o_response : resolved IP address
 **/
static PT_THREAD(PtWifi_DNS_resolve(struct pt *Pt, const RosMailType *Mail, rsuint8 *name, rsuint32 *o_response)) {
  *o_response = 0;
  
  PT_BEGIN(Pt);
 
  SendApiDnsClientResolveReq(COLA_TASK, 0, strlen((char*)name), name);

  // Wait for response from DNS Client
  RosTimerStart(APP_DNS_RSP_TIMER, APP_DNS_RESOLVE_RSP_TIMEOUT, &DnsRspTimer);

  PT_WAIT_UNTIL(Pt, (IS_RECEIVED(API_DNS_CLIENT_RESOLVE_CFM) ||
                     IS_RECEIVED(APP_DNS_RSP_TIMEOUT)) &&
                     !PtMailHandled);
  PtMailHandled = TRUE;
  if (IS_RECEIVED(API_DNS_CLIENT_RESOLVE_CFM)) {
    RosTimerStop(APP_DNS_RSP_TIMER);
    if (((ApiDnsClientResolveCfmType *)Mail)->Status == RSS_SUCCESS) {
      #ifdef USE_LUART_TERMINAL
      PRINTLN("DNS success");
      #endif
      
      // Store the resolved IP (a 32-bit unsigned integer)
      *o_response = (rsuint32)((ApiDnsClientResolveCfmType *)Mail)->IpV4;

      char buffer[50];
      inet_ntoa(*o_response, buffer);

      #ifdef USE_LUART_TERMINAL
      sprintf(TmpStr, "DNS response for %s: %s", name, buffer);
      PRINTLN(TmpStr);
      #endif
    }
    else {
      #ifdef USE_LUART_TERMINAL
      PRINTLN("DNS Failed!");
      #endif
    }
  }
  else {
    #ifdef USE_LUART_TERMINAL
    PRINTLN("No response from DNS client");
    #endif
  }

  PT_END(Pt);
}

/**
 * @brief Event handled. Fired when the TCP connection has been
 * stablished
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 **/
static PT_THREAD(PtWifi_TCP_on_connect(struct pt *Pt, const RosMailType *Mail)) {
  AppSocketDataType *pInst = (AppSocketDataType *)PtInstDataPtr;
  socketHandle = pInst->SocketHandle;

  PT_BEGIN(Pt);
  #ifdef USE_LUART_TERMINAL
  PRINTLN("PtWifi_TCP_on_connect FIRED");
  #endif
  
  TCP_is_connected = true;
                     
  // Do not exit from the protothread until the TCP socket is closed
  PT_WAIT_UNTIL(Pt, IS_RECEIVED(API_SOCKET_CLOSE_IND));
  
  PT_END(Pt);
}

/**
 * @brief Starts a new TCP connection to the given server
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 * @param addr : server IP address and TCP port
 **/
static PT_THREAD(PtWifi_TCP_start(struct pt *Pt, const RosMailType *Mail, ApiSocketAddrType addr)) {
  #ifdef USE_LUART_TERMINAL
  AppSocketDataType *pInst = (AppSocketDataType *)PtInstDataPtr;
  #endif

  PT_BEGIN(Pt);
  
  if (!is_suspended) {
    TCP_is_connected = false;
    
    AppSocketStartTcpClient(&PtList, addr, PtWifi_TCP_on_connect);  

    #ifdef USE_LUART_TERMINAL
    if (pInst->LastError != RSS_SUCCESS) {
      sprintf(TmpStr, "AppSocketStartTcpClient ERROR! pInst->LastError == %d", pInst->LastError);
      PRINTLN(TmpStr);
    }
    #endif
  }

  PT_END(Pt);
}

/**
 * @brief Test procedure which can be called from the debug terminal
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 **/
static PT_THREAD(PtTest(struct pt *Pt, const RosMailType *Mail)) {
  #ifdef USE_LUART_TERMINAL  
  static struct pt childPt;
  #endif  
  
  PT_BEGIN(Pt);

  #ifdef USE_LUART_TERMINAL  
  PRINTLN("*** This is PtTest");

  // Setup a new AP
  PRINTLN("* Test SPAWN PtWifi_setup_AP");
  //rsuint8 *ap_data = (rsuint8*)"SSID\nWPA2\npassword\nAES\n"; // ESSID, cipher, key
  rsuint8 *ap_data = NULL;
  //rsuint8 *ap_data = NULL;
  PT_SPAWN(Pt, &childPt, PtWifi_setup_AP(&childPt, Mail, ap_data));
  PRINTLN("");
  

  
  
  /*PRINTLN("* Test SPAWN PtWifi_IP_config static IP");
  rsuint8 config_static[] = {'s',
                             1, 2, 3, 4,     // IP address
                             5, 6, 7, 8,     // subnet
                             9, 10, 11, 12}; // gateway
  PT_SPAWN(Pt, &childPt, PtWifi_IP_config(&childPt, Mail, config_static));
  PRINTLN("");
  
  PRINTLN("* Test SPAWN PtWifi_IP_config static IP");
  // Read from NVS
  PT_SPAWN(Pt, &childPt, PtWifi_IP_config(&childPt, Mail, NULL));
  PRINTLN("");
  return;*/
  
  
  /*PRINTLN("* Test SPAWN PtWifi_IP_config DHCP");
  rsuint8 config_dhcp[] = {'d'};
  PT_SPAWN(Pt, &childPt, PtWifi_IP_config(&childPt, Mail, config_dhcp));
  PRINTLN("");*/

  PRINTLN("* Test SPAWN PtWifi_IP_config DHCP");
  // Read from NVS
  PT_SPAWN(Pt, &childPt, PtWifi_IP_config(&childPt, Mail, NULL));
  PRINTLN("");

  PRINTLN("* Test SPAWN PtWifi_connect");
  
  if (!Wifi_is_connected()) {
    PRINTLN("Not connected to AP");
    PT_SPAWN(Pt, &childPt, PtWifi_connect(&childPt, Mail));
  }
  else
    PRINTLN("Already connected to AP");
  PRINTLN("");
  
  /*PRINTLN("* Test SPAWN PtWifi_disconnect");
  PT_SPAWN(Pt, &childPt, PtWifi_disconnect(&childPt, Mail));
  PRINTLN("");
    
  PRINTLN("* Test SPAWN PtWifi_connect");
  PT_SPAWN(Pt, &childPt, PtWifi_connect(&childPt, Mail));
  PRINTLN("");
  

  PRINTLN("* Test SPAWN PtWifi_DNS_resolve");
  const char *name = "www.uoc.edu";
  rsuint32 response;
  PT_SPAWN(Pt, &childPt, PtWifi_DNS_resolve(&childPt, Mail, (rsuint8*)name, &response));
  sprintf(TmpStr, "response is %u", (size_t)response);
  PRINTLN(TmpStr);  
  PRINTLN("");*/


  /*PRINTLN("* Test SPAWN PtWifi_disconnect");
  PT_SPAWN(Pt, &childPt, PtWifi_disconnect(&childPt, Mail));
  PRINTLN("");
  
  PRINTLN("* Test SPAWN PtWifi_disconnect");
  PT_SPAWN(Pt, &childPt, PtWifi_disconnect(&childPt, Mail));
  PRINTLN("");*/
  #endif

  PT_END(Pt);
}

/**
 * @brief Main protothread. It controls the SPI or the debug terminal
 * @param Pt : current protothread pointer
 * @param Mail : protothread mail
 **/
static PT_THREAD(PtMain(struct pt *Pt, const RosMailType *Mail)) {
  static struct pt childPt;
  
  PT_BEGIN(Pt);
  
  // Read the app configuration from NVS
  Wifi_read_appInfo_from_NVS();
  
  // Reset the Atheros WiFi chip
  AppLedSetLedState(LED_STATE_ACTIVE);
  PT_SPAWN(Pt, &childPt, PtAppWifiReset(&childPt, Mail));
  SendApiCalibrateLfrcoReq(COLA_TASK, 3600); // Calibrate LFRCO every hour
  AppLedSetLedState(LED_STATE_IDLE);

  // Use LEUART1 for debug messages
  #ifdef USE_LUART_TERMINAL
  PT_SPAWN(Pt, &childPt, PtDrvLeuartInit(&childPt, Mail));
  #endif

  #ifdef USE_LUART_TERMINAL
  PRINTLN(""); PRINTLN("Ready"); PRINTLN("");
  #endif

  // Shell terminal (only if USE_LUART_TERMINAL defined)
  #ifdef USE_LUART_TERMINAL
  while (1) {
    //Flush UART RX buffer
    DrvLeuartRxFlush();
    #ifdef USE_LUART_TERMINAL
    PRINT("> ");
    #endif

    // Read from UART to we have a command line
    while (1) {
      char c;

      // read all
      while (DrvLeuartRx((rsuint8 *)&c, 1)) {
        if (is_valid_cmd_char(c)) {
          if (c == 0x8 || c == 0x7F) { // backspace
            if (ShellRxIdx) {
              ShellRxIdx--;
              DrvLeuartTx(c);
            }
          }
          else {
            // Echo char back to PC terminal
            DrvLeuartTx(c);

            // Check for  end of line
            if (c == '\r') {
              // end of line
              TmpStr[ShellRxIdx++] = '\0';
              goto ProcessCommandLine;
            }
            else {
              // Append char
              TmpStr[ShellRxIdx++] = c;
              if (ShellRxIdx == TMP_STR_LENGTH) {
                // Temp buffer full
                goto ProcessCommandLine;
              }
            }
          }
        }
      }

      // Allow other task to run
      PT_YIELD(Pt);
    }

  
  ProcessCommandLine:
    DrvLeuartTx('\n');

    // Process the command line received
    memcpy(CmdStr, TmpStr, ShellRxIdx);
    argc = ParseArgs(CmdStr, argv);
    if (argc) {
      if (strcmp(argv[0], "test") == 0) {
        PT_SPAWN(Pt, &childPt, PtTest(&childPt, Mail));
      }      
      else if (strcmp(argv[0], "tcpstart") == 0) {
        // Resolve DNS name
        const char *name = "www.example.com";
        rsuint32 response;
        PT_SPAWN(Pt, &childPt, PtWifi_DNS_resolve(&childPt, Mail, (rsuint8*)name, &response));
        sprintf(TmpStr, "DNS response (rsuint32) is %u", (size_t)response);
        PRINTLN(TmpStr);  
        PRINTLN("");        

        // Configure IP address
        ApiSocketAddrType addr;
        addr.Ip.V4.Addr = response;
        addr.Domain = ASD_AF_INET;
        addr.Port = 80;
        // Start TCP connection
        PT_SPAWN(Pt, &childPt, PtWifi_TCP_start(&childPt, Mail, addr));
      }
      else if (strcmp(argv[0], "status") == 0 || strcmp(argv[0], "s") == 0) {
        rsuint8 status = Wifi_get_status();
        sprintf(TmpStr, "status: %d", status); PRINTLN(TmpStr);
        sprintf(TmpStr, "Wifi_is_connected(): %d", status & 1 << 0); PRINTLN(TmpStr);
        sprintf(TmpStr, "TCP_is_connected: %d", status & 1 << 1); PRINTLN(TmpStr);
        sprintf(TmpStr, "TCP_received: %d", status & 1 << 2); PRINTLN(TmpStr);
        sprintf(TmpStr, "is_suspended: %d", status & 1 << 3); PRINTLN(TmpStr);

        AppSocketDataType *pInst = (AppSocketDataType *)PtInstDataPtr;
        sprintf(TmpStr, "pInst->LastError: %d", pInst->LastError); PRINTLN(TmpStr);
      }
      else if (strcmp(argv[0], "tcpclose") == 0) {
        Wifi_TCP_close();
      }
      else if (strcmp(argv[0], "disc") == 0) {
        PT_SPAWN(Pt, &childPt, PtWifi_disconnect(&childPt, Mail));
      }
      else if (strcmp(argv[0], "send") == 0) {
        PRINTLN("Send...");  
        strcpy((char*)tx_buffer, "GET / HTTP/1.0\n\n");
        size_t bytes_to_send = strlen((char*)tx_buffer);

        sprintf(TmpStr, "Sending %s (%d bytes)", (char*)tx_buffer, strlen((char*)tx_buffer));
        PRINTLN(TmpStr);        
        
        Wifi_TCP_send(bytes_to_send);
      }
      else if (strcmp(argv[0], "receive") == 0) {
        Wifi_TCP_receive();
      }
      else if (strcmp(argv[0], "disc") == 0) {
        if (AppWifiIsAssociated())
          PT_SPAWN(Pt, &childPt, PtAppWifiDisconnect(&childPt, Mail));
      }
      else if (strcmp(argv[0], "suspend") == 0) {
        PT_SPAWN(Pt, &childPt, PtWifi_suspend(&childPt, Mail));
      }
      else if (strcmp(argv[0], "resume") == 0) {
        PT_SPAWN(Pt, &childPt, PtWifi_resume(&childPt, Mail));
      }
      else
      {
        sprintf(TmpStr, "unknown cmd: %s", argv[0]);
        PRINTLN(TmpStr);
      }
    }
    ShellRxIdx = 0;
  }
  #endif
  
  #ifdef SPI_COMMUNICATION
  // Init SPI
  const rsuint32 baud_rate = 9600;
  PT_SPAWN(Pt, &childPt, PtDrvSpiInit(&childPt, Mail, baud_rate));
  DrvSpiInit(baud_rate);
  
  while (1) {
    // Wait until SPI data is received
    PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
    
    // Read SPI command
    rsuint8 command;
    DrvSpiRx(&command, sizeof(command));
    PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
    
    switch (command) {
      case 1: { // get status
        rsuint8 status = Wifi_get_status();
        DrvSpiTxStart(&status, 1);
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_TX_DONE));
        break;
      }
      case 2: { // DNS resolve
        // Read name to resolve (ex: "www.example.com")

        // First read the size of the name
        rsuint8 name_size;
        DrvSpiRx(&name_size, sizeof(name_size));
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
        
        // Second, read the name
        rsuint8 name[100];
        DrvSpiRx((rsuint8*)&name, name_size);
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
        name[name_size] = 0; // put trailing zero

        // Resolve
        rsuint32 response;
        PT_SPAWN(Pt, &childPt, PtWifi_DNS_resolve(&childPt, Mail, name, &response));
        
        // Send response
        DrvSpiTxStart((rsuint8*)&response, sizeof(response));
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_TX_DONE));
        break;
      }
      case 3: { // IP config
        // First read the size of the config
        rsuint8 config_size;
        DrvSpiRx(&config_size, sizeof(config_size));
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));

        // Second, read the config
        rsuint8 config[100];
        if (config_size > 0) {
          DrvSpiRx((rsuint8*)&config, config_size);
          PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
        }

        // Do IP config        
        PT_SPAWN(Pt, &childPt, PtWifi_IP_config(&childPt, Mail,
                                      config_size > 0 ? config : NULL));
        break;
      }
      case 4: { // TCP start
        // Given the IP address of the server (rsuint32), start the connection.
        // The upper layer must poll in order to check when the connection
        // has been stablished.
        ApiSocketAddrType addr;
        addr.Domain = ASD_AF_INET;

        // Read the IP of the TCP server (rsuint32)
        DrvSpiRx((rsuint8*)&addr.Ip.V4.Addr, sizeof(addr.Ip.V4.Addr));
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));

        // Read the port of the TCP server
        DrvSpiRx((rsuint8*)&addr.Port, sizeof(addr.Port));
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));

        // Start TCP connection
        PT_SPAWN(Pt, &childPt, PtWifi_TCP_start(&childPt, Mail, addr));
        break;
      }
      case 5: { // Associate & connect to the WiFi AP
        PT_SPAWN(Pt, &childPt, PtWifi_connect(&childPt, Mail));
        break;
      }
      case 6: { // WiFi AP deassociate & disconnect
        PT_SPAWN(Pt, &childPt, PtWifi_disconnect(&childPt, Mail));
        break;
      }
      case 7: { // setup AP
        // Read ap_data size
        rsuint8 ap_data_size;
        DrvSpiRx(&ap_data_size, sizeof(ap_data_size));
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
        
        // Read ap_data
        rsuint8 ap_data[100];
        if (ap_data_size > 0) {
          DrvSpiRx((rsuint8*)&ap_data, ap_data_size);
          PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
        }        
        
        PT_SPAWN(Pt, &childPt, PtWifi_setup_AP(&childPt, Mail,
                                  ap_data_size > 0 ? ap_data : NULL));
        break;
      }
      case 8: { // TCP socket close
        Wifi_TCP_close();
        break;
      }
      case 9: { // TCP receive
        // The TCP data is already in rx_buffer
        // Number of bytes: TCP_Rx_bufferLength
        DrvSpiTxStart(rx_buffer, TCP_Rx_bufferLength);
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_TX_DONE));         
        break;
      }
      case 10: { // TCP send
        // Read the number of bytes to send (rsuint16)
        rsuint16 len;
        DrvSpiRx((rsuint8*)&len, sizeof(len));
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
        
        if (len > TX_BUFFER_LENGTH)
          len = TX_BUFFER_LENGTH;
          
        // Read data to send into tx_buffer
        DrvSpiRx(tx_buffer, len);
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
        
        // Send data using the TCP socket
        SendApiSocketSendReq(COLA_TASK, socketHandle, tx_buffer, len, 0);
        break;
      }
      case 11: { // Query: TCP is connected?
        rsuint8 connected = Wifi_is_connected();
        // Send response
        DrvSpiTxStart(&connected, sizeof(connected));
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_TX_DONE));        
        break;
      }
      case 12: { // Wifi chip power on/off        
        // Read parameter (0=off, 1=on)
        rsuint8 param;
        DrvSpiRx(&param, sizeof(param));
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
        
        PT_SPAWN(Pt, &childPt, PtWifi_power_on_off(&childPt, Mail,
                                                   param));        
        break;
      }
      case 13: { // Wifi set powersave profile      
        // Read parameter
        // 0: low power, 1: medium power, 2: high power, 3: max power
        rsuint8 param;
        DrvSpiRx(&param, sizeof(param));
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
        
        // Set powersave profile
        Wifi_set_power_save_profile(param);
        break;
      }
      case 14: { // Wifi set transmit power        
        // Read parameter
        rsuint8 param;
        DrvSpiRx(&param, sizeof(param));
        PT_WAIT_UNTIL(Pt, IS_RECEIVED(SPI_RX_DATA));
        
        // Set transmit power
        Wifi_set_tx_power(param);
        break;
      }
      case 15: { // Wifi chip suspend
        PT_SPAWN(Pt, &childPt, PtWifi_suspend(&childPt, Mail));
        break;
      }
      case 16: { // Wifi chip resume
        PT_SPAWN(Pt, &childPt, PtWifi_resume(&childPt, Mail));
        break;
      }

    }

  }
  #endif

  PT_END(Pt);
}

/**
 * @brief Main CoLa task event handler
 * @param Mail : protothread mail
 **/
void ColaTask(const RosMailType *Mail) {
  // Pre-dispatch mail handling
  switch (Mail->Primitive) {
    case INITTASK:
      // Init GPIO PIN used for timing of POWER measurements
      POWER_TEST_PIN_INIT;

      // Init the Buttons driver
      DrvButtonsInit();

      // Init the Protothreads lib
      PtInit(&PtList);

      // Init the LED application
      AppLedInit(&PtList);

      // Init the WiFi management application
      AppWifiInit(&PtList);

      // Start the Main protothread
      PtStart(&PtList, PtMain, NULL, NULL);
      break;

    case TERMINATETASK:
      RosTaskTerminated(ColaIf->ColaTaskId);
      break;
      
    case API_SOCKET_SEND_CFM:
      #ifdef USE_LUART_TERMINAL
      PRINTLN("API_SOCKET_SEND_CFM (send confirmation)");    
      
      if (((ApiSocketSendCfmType *)Mail)->Status == RSS_SUCCESS)
        PRINTLN("Send OK");
      else
        PRINTLN("Send ERROR");
      #endif
      break;

    case APP_EVENT_SOCKET_CLOSED:
      #ifdef USE_LUART_TERMINAL
      PRINTLN("APP_EVENT_SOCKET_CLOSED");
      #endif
      TCP_is_connected = false;
      break;    

    case API_SOCKET_CLOSE_IND:
      #ifdef USE_LUART_TERMINAL
      PRINTLN("API_SOCKET_CLOSE_IND");
      #endif
      TCP_is_connected = false;
      break;

    case API_SOCKET_RECEIVE_IND: {
      #ifdef USE_LUART_TERMINAL
        PRINTLN("API_SOCKET_RECEIVE_IND");
      #endif

      // Save pointer to TCP allocated buffer.
      // The buffer will be freed in Wifi_TCP_receive().
      ApiSocketReceiveIndType *socket = (ApiSocketReceiveIndType *)Mail;
      TCP_receive_buffer_ptr = socket->BufferPtr;
      TCP_Rx_bufferLength = socket->BufferLength;
      
      // Move data to rx_buffer
      TCP_Rx_bufferLength = socket->BufferLength;
      if (TCP_Rx_bufferLength >= TX_BUFFER_LENGTH)
        TCP_Rx_bufferLength = TX_BUFFER_LENGTH;
      memcpy(rx_buffer, socket->BufferPtr, TCP_Rx_bufferLength);

      // Activate the flag that indicates that TCP data has been received.
      // The buffer is not freed. The data must be read with Wifi_TCP_receive,
      // which will read the data clear and clear the buffer.    
      TCP_received = true;
      break;
    }
  }

  // Dispatch mail to all protothreads started
  PtDispatchMail(&PtList, Mail);
}

// End of file.
