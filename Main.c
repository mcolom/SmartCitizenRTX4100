/****************************************************************************
* Program/file: Main.c
*
* Copyright (C) by RTX A/S, Denmark.
* These computer program listings and specifications, are the property of
* RTX A/S, Denmark and shall not be reproduced or copied or used in
* whole or in part without written permission from RTX A/S, Denmark.
*
* DESCRIPTION: Co-Located Application (COLA).
*
****************************************************************************/


/****************************************************************************
*                                  PVCS info
*****************************************************************************

$Author:   lka  $
$Date:   26 Sep 2013 15:43:42  $
$Revision:   1.0  $
$Modtime:   05 Sep 2012 11:21:52  $
$Archive:   J:/sw/Projects/Amelie/COLApps/Scripts/TemplateApp/vcs/Main.c_v  $

*/

/****************************************************************************
*                               Include files
****************************************************************************/

#include <Core/RtxCore.h>
#include <Ros/RosCfg.h>
#include <PortDef.h>
#include <Cola/Cola.h>
#include <em_gpio.h>

/****************************************************************************
*                              Macro definitions
****************************************************************************/
#define LEDPORT GREEN_LED_PORT
#define LEDPIN  GREEN_LED_PIN

/****************************************************************************
*                     Enumerations/Type definitions/Structs
****************************************************************************/

/****************************************************************************
*                            Global variables/const
****************************************************************************/

/****************************************************************************
*                            Local variables/const
****************************************************************************/
RosTimerConfigType AppLedTimer = ROSTIMER(COLA_TASK, TIMEOUT, APP_LED_TIMER);

/****************************************************************************
*                          Local Function prototypes
****************************************************************************/

/****************************************************************************
*                                Implementation
***************************************************************************/
void ColaTask(const RosMailType* Mail)
{
  switch (Mail->Primitive)
  {
    case INITTASK:
      GPIO_PinModeSet(LEDPORT, LEDPIN, gpioModePushPull, 1);
      RosTimerStart(APP_LED_TIMER, 1 * RS_T1SEC, &AppLedTimer);
      break;

    case TERMINATETASK:
      RosTaskTerminated(ColaIf->ColaTaskId);
      break;

    case TIMEOUT:
      switch (Mail->Timeout.Parameter)
      {
        case APP_LED_TIMER:
          GPIO_PinOutToggle(LEDPORT, LEDPIN);
          RosTimerStart(APP_LED_TIMER, 5 * RS_T100MS, &AppLedTimer);
          break;
      }
      break;

    default:
      break;
  }
}

// End of file.

