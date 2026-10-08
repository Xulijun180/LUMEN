/**
  ******************************************************************************
  * file           : main.c
  * brief          : Main program body
  *                  Calls target system initialization then loop in main.
  ******************************************************************************
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "lumen_port.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private functions prototype -----------------------------------------------*/

/**
  * brief:  The application entry point.
  * retval: none but we specify int to comply with C99 standard
  */
int main(void)
{
  /** System Init: this code placed in targets folder initializes your system.
    * It calls the initialization (and sets the initial configuration) of the peripherals.
    * You can use STM32CubeMX to generate and call this code or not in this project.
    * It also contains the HAL initialization and the initial clock configuration.
    */
  system_status_t st = mx_system_init();
  if (st != SYSTEM_OK)
  {
    lumen_diag.stage = LUMEN_STAGE_SYSINIT_FAILED;   /* visible in the debugger */
    lumen_diag.sysinit_err = (uint32_t)st;
    while (1) {}                                     /* stay here instead of returning */
  }
  else
  {
    /*
      * You can start your application code here
      */
    lumen_run();   /* Lumen UI main loop, never returns */
    while (1) {}
  }
} /* end main */

