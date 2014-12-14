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

#include <Core/RtxCore.h>
#include <Ros/RosCfg.h>
//#include <PortDef.h>
//#include <Api/Api.h>
#include <Cola/Cola.h>
#include <Protothreads/Protothreads.h>
//#include <SwClock/SwClock.h>
//#include <BuildInfo/BuildInfo.h>
//#include <NetUtils/NetUtils.h>

//#include <ctype.h>
//#include <string.h>
//#include <stdio.h>
//#include <stdlib.h>

//#include <PtApps/AppCommon.h>
//#include <PtApps/AppLed.h>
//#include <PtApps/AppSocket.h>
//#include <PtApps/AppWifi.h>
//#include <PtApps/AppSntp.h>
#include <Drivers/DrvLeuart.h>
//#include <Drivers/DrvButtons.h>

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

// Process a line the user send to the terminal
void process_terminal_line(rsuint8 *buffer) {
  sprintf(strbuf, "Linea: %s", buffer);
  PRINTLN(strbuf);
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

  // Shell loop:
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
          process_terminal_line(buffer);
          buf_ptr = 0;
          PRINT("debug$ "); // Prompt
		}
		else {
          rsuint8 echoed[2] = {c, 0};
		  PRINT(echoed); // Echo character
          buffer[buf_ptr++] = c; // Add a new character to the line
        }
      }

      // Allow other tasks to run
      PT_YIELD(Pt);
    }
    
    PRINTLN("EXIT FROM INNER LOOP");
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

