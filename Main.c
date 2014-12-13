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
#define PRINT(x) UartPrint(x)

/****************************************************************************
*                     Enumerations/Type definitions/Structs
****************************************************************************/

//

/****************************************************************************
*                            Global variables/const
****************************************************************************/

//

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

static void UartPrint(char *pStr) {
  while (*pStr != '\0') {
    if (*pStr == '\n')
      DrvLeuartTx('\r');
    DrvLeuartTx(*pStr++);
  }
}

static PT_THREAD(PtMain(struct pt *Pt, const RosMailType *Mail))
{
  PT_BEGIN(Pt);

  // Init LUART
  static struct pt childPt;
  PT_SPAWN(Pt, &childPt, PtDrvLeuartInit(&childPt, Mail));

  // Shell loop:
  while (1) {
    //Flush UART RX buffer
    DrvLeuartRxFlush();
    PRINT("> ");
    PRINT("miau, miau, miau\r\n");


    // Read from UART to we have a command line
    while (1) {
      rsuint8 c;
      PRINT("@\r\n");

      // read all
      while (DrvLeuartRx(&c, 1)) {
        // Send data to the PC
        DrvLeuartTxBuf("Saludos, humano\r\n", 17);
      }

      // Allow other tasks to run
      PT_YIELD(Pt);
    }
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

