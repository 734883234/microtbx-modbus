/************************************************************************************//**
* \file         tbxmb_event.c
* \brief        Modbus event handler source file.
* \internal
*----------------------------------------------------------------------------------------
*                          C O P Y R I G H T
*----------------------------------------------------------------------------------------
*   Copyright (c) 2023 by Feaser     www.feaser.com     All rights reserved
*
*----------------------------------------------------------------------------------------
*                            L I C E N S E
*----------------------------------------------------------------------------------------
*
* SPDX-License-Identifier: GPL-3.0-or-later
*
* This file is part of MicroTBX-Modbus. MicroTBX-Modbus is free software: you can
* redistribute it and/or modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* MicroTBX-Modbus is distributed in the hope that it will be useful, but WITHOUT ANY
* WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
* PARTICULAR PURPOSE. See the GNU General Public License for more details.
*
* You have received a copy of the GNU General Public License along with MicroTBX-Modbus.
* If not, see www.gnu.org/licenses/.
*
* \endinternal
****************************************************************************************/

/****************************************************************************************
* Include files
****************************************************************************************/
#include "microtbx.h"                            /* MicroTBX module                    */
#include "microtbxmodbus.h"                      /* MicroTBX-Modbus module             */
#include "tbxmb_event_private.h"                 /* MicroTBX-Modbus event private      */
#include "tbxmb_osal_private.h"                  /* MicroTBX-Modbus OSAL private       */


/****************************************************************************************
* Type definitions
****************************************************************************************/
/** \brief Event task interface function to detect events in a polling manner. */
typedef void (* tTbxMbEventPoll)   (void        * context);


/** \brief Event processor interface function for processing events. */
typedef void (* tTbxMbEventProcess)(tTbxMbEvent * event);


/** \brief Minimal context for accessing the event poll and process functions. Think of
 *         it as the base type for all the other context (client/server/tp). That's the
 *         reason why these other context start with similar entries at exactly the same
 *         location. 
 */
typedef struct
{
  /* The following three entries must always be at the start and not change order. They
   * form the base that other context derive from.
   */
  void               * instancePtr;              /**< Reserved for C++ wrapper.        */
  tTbxMbEventPoll      pollFcn;                  /**< Event poll function.             */
  tTbxMbEventProcess   processFcn;               /**< Event process function.          */
} tTbxMbEventCtx;


/************************************************************************************//**
** \brief     Task function that drives the entire Modbus stack. It processes internally
**            generated events. 
** \details   How to call this function depends on the selected operating system
**            abstraction layer (OSAL):
**            - In a traditional superloop application (tbxmb_superloop.c), call this
**              function continuously in the infinite program loop.
**            - When using an RTOS (e.g. tbxmb_freertos.c), create a new task during
**              application initialization and call this function from this task's 
**              infinite loop.
**
****************************************************************************************/
void TbxMbEventTask(void)
{
  static tTbxList       * pollerList = NULL;
  static uint8_t          pollerListInitialized = TBX_FALSE;
  static const uint16_t   defaultWaitTimeoutMs = 5000U;
  static uint16_t         waitTimeoutMS = defaultWaitTimeoutMs;
  tTbxMbEvent             newEvent = { 0 };

  /* Only initialize the event poller once, */
  if (pollerListInitialized == TBX_FALSE)
  {
    pollerListInitialized = TBX_TRUE;
    /* Ceate the queue for storing context of which the pollFcn should be called. */
    pollerList = TbxListCreate();
    /* Verify that the queue creation succeeded. If this assertion fails, increase the
     * heap size using configuration macro TBX_CONF_HEAP_SIZE.
     */
    TBX_ASSERT(pollerList != NULL);
  }

  /* Wait for a new event to be posted to the event queue. Note that that wait time only
   * applies in case an RTOS is configured for the OSAL. Otherwise (TBX_MB_OPT_OSAL_NONE)
   * this function returns immediately.
   */
  if (TbxMbOsalEventWait(&newEvent, waitTimeoutMS) == TBX_TRUE)
  {
    /* Check the opaque context pointer. */
    TBX_ASSERT(newEvent.context != NULL);
    /* Only continue with a valid opaque context pointer. */
    if (newEvent.context != NULL)
    {
      /* Filter on the event identifier. */
      switch (newEvent.id)
      {
        case TBX_MB_EVENT_ID_START_POLLING:
        {
          /* Add the context at the end of the event poller list. */
          uint8_t insertResult = TbxListInsertItemBack(pollerList, newEvent.context);
          /* Check that the item could be added to the queue. If not, then the heaps size
           * is configured too small. In this case increase the heap size using
           * configuration macro TBX_CONF_HEAP_SIZE. 
           */
          TBX_ASSERT(insertResult == TBX_OK);
        }
        break;
      
        case TBX_MB_EVENT_ID_STOP_POLLING:
        {
          /* Remove the context from the event poller list. */
          TbxListRemoveItem(pollerList, newEvent.context);
        }
        break;

        default:
        {
          /* Convert the opaque pointer to the event context structure. */
          tTbxMbEventCtx * eventCtx = (tTbxMbEventCtx *)newEvent.context;
          /* Pass the event on to the context's event processor. */
          if (eventCtx->processFcn != NULL)
          {
            eventCtx->processFcn(&newEvent);
          }
        }
        break;
      }
    }
  }

  /* Iterate over the event poller list. */
  void * listItem = TbxListGetFirstItem(pollerList);
  while (listItem != NULL)
  {
    /* Convert the opaque pointer to the event context structure. */
    tTbxMbEventCtx * eventPollCtx = (tTbxMbEventCtx *)listItem;
    /* Call its poll function if configured. */
    if (eventPollCtx->pollFcn != NULL)
    {
      eventPollCtx->pollFcn(listItem);
    }
    /* Move on to the next item in the list. */
    listItem = TbxListGetNextItem(pollerList, listItem);
  }

  /* Set the event wait timeout for the next call to this task function. If the event
   * poller list is not empty, keep the wait time short to make sure the poll functions
   * get continuously called. Otherwise go back to the default wait time to not hog up
   * CPU time unnecessarily.
   */
  waitTimeoutMS = (TbxListGetSize(pollerList) > 0U) ? 1U : defaultWaitTimeoutMs;
} /*** end of TbxMbEventTask ***/


/*********************************** end of tbxmb_event.c ******************************/
