/*
 * FreeRTOS Kernel <DEVELOPMENT BRANCH>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/* Standard includes. */
#include <stdio.h>
#include <stddef.h>
#include <string.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"

/*-----------------------------------------------------------*/
/* HELPER FUNCTIONS                                          */
/*-----------------------------------------------------------*/

/**
 * @brief  Simulates a heavy computational load by burning CPU cycles.
 * @note   The loop count multiplier (30000) is hardware dependent.
 * Adjust if the simulated time does not match actual ticks.
 * @param  ticks_simulated: The approximate number of ticks to burn.
 */
void vBusyWait( int ticks_simulated )
{
    volatile int i;
    for( i = 0; i < ( ticks_simulated * 30000 ); i++ ) 
    {
        __asm volatile( "nop" );
    }
}

/*-----------------------------------------------------------*/
/* APERIODIC JOB CALLBACKS                                   */
/* These are executed by the Polling Server Worker.          */
/*-----------------------------------------------------------*/

/* Simple aperiodic job that prints a message. */
/*-----------------------------------------------------------*/

void vAperiodicJobCallback( void *pvParameters )
{
    /* Buffer to hold the formatted output string. */
    char cBuffer[ 80 ];

    /* Silence compiler warnings about unused parameters. */
    ( void ) pvParameters;

    /* Format the string including the current tick count. */
    sprintf( cBuffer, "{%lu} Aperiodic job: Spawned successfully by task.\n",
             ( unsigned long ) xTaskGetTickCount() );

    UART_printf( cBuffer );
}
/*-----------------------------------------------------------*/

/* Long running aperiodic job for testing purposes. */
void vAperiodicLongJob( void *pvParameters )
{
    /* Buffer to hold the formatted output string. */
    char cBuffer[ 80 ];

    /* Silence compiler warnings about unused parameters. */
    ( void ) pvParameters;

    /* Print the start time. */
    sprintf( cBuffer, "{%lu} Aperiodic Long Job: START\n",
             ( unsigned long ) xTaskGetTickCount() );
    UART_printf( cBuffer );

    /* Simulate work equivalent to 500ms (approx). */
    vBusyWait( 10000 );

    /* Print the end time. */
    sprintf( cBuffer, "{%lu} Aperiodic Long Job: END\n",
             ( unsigned long ) xTaskGetTickCount() );
    UART_printf( cBuffer );
}
/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/
/* PERIODIC TASK FUNCTIONS                                   */
/* These are the main tasks scheduled by your custom kernel. */
/*-----------------------------------------------------------*/

/**
 * @brief  Test Task for the "KILL" Policy.
 * It runs longer than its deadline. The scheduler should kill it
 * before it reaches the "END" print.
 */
void vTaskKillPolicyTest( void *pvParameters )
{
    const char *pcName = (const char *) pvParameters;
    char s[80];

    /* 1. START MSG */
    sprintf( s, "{%lu} %s: START Job\n", xTaskGetTickCount(), pcName );
    UART_printf( s );

    /* 2. Simulate Work (Overrun) */
    vBusyWait( 50 );
    
    /* 3. END MSG */
    /* If Policy is KILL, this line should NEVER be reached. */
    sprintf( s, "{%lu} %s: END (Error: Should have been KILLED)\n", xTaskGetTickCount(), pcName );
    UART_printf( s );
}

/**
 * @brief  Test Task for the "SKIP" Policy.
 * It runs longer than its deadline. The scheduler should let it finish
 * (Overrun), but skip the next activation to catch up.
 */
void vTaskSkipPolicyTest( void *pvParameters )
{
    const char *pcName = (const char *) pvParameters;
    char s[80];

    /* 1. START MSG */
    sprintf( s, "{%lu} %s: START Job\n", xTaskGetTickCount(), pcName );
    UART_printf( s );

    /* 2. Simulate Work (Overrun) */
    /* Period is 100 ticks, we wait for 250 ticks. */
    vBusyWait( 250 );
    
    /* 3. END MSG */
    /* SKIP and CATCH_UP policies will reach here (late). */
    sprintf( s, "{%lu} %s: END\n", xTaskGetTickCount(), pcName );
    UART_printf( s );
}

/**
 * @brief  Spawner Task.
 * Demonstrates creating an Aperiodic Job dynamically at runtime.
 */
void vTaskAperiodicSpawner( void *pvParameters )
{
    const char *pcName = (const char *) pvParameters;
    char s[80];

    /* 1. START MSG */
    sprintf( s, "{%lu} %s: START Job\n", xTaskGetTickCount(), pcName );
    UART_printf( s );

    /* 2. Spawn the Aperiodic Task */
    /* Parameters: Function, Args, SoftDeadline, Policy, StartTime (Now) */
    xTaskCreateAperiodic( vAperiodicLongJob, 
                          NULL, 
                          100, 
                          APERIODIC_POLICY_OVERRUN, 
                          xTaskGetTickCount() );

    UART_printf( "Spawner task: Request sent to Polling Server.\n" );
}

/*-----------------------------------------------------------*/
/* MAIN APPLICATION ENTRY                                    */
/*-----------------------------------------------------------*/

int main( void )
{
    /* Initialize Hardware */
    UART_init();
    vLoggerInit(); 
    
    /* ----------------------------------------------------- */
    /* 1. Task Configuration                                 */
    /* ----------------------------------------------------- */
    PeriodicTaskConfig_t myTasks[] = {
        /* TASK 1: Tests the KILL policy. 
           It will overrun its 100 tick deadline and be terminated. */
        { 
            .pcName         = "TASK_KILL",
            .pxTaskCode     = vTaskKillPolicyTest,
            .pvParameters   = "TASK_KILL",
            .usStackDepth   = configMINIMAL_STACK_SIZE * 2,
            .uxPriority     = tskIDLE_PRIORITY + 3,
            .xPeriod        = 1000,      
            .xDeadline      = 1000,
            .xTaskPolicy    = POLICY_KILL
        },

        /* TASK 2: Tests the SKIP policy. 
           It will overrun, finish late, and skip the next period. */
        { 
            .pcName         = "TASK_SKIP",
            .pxTaskCode     = vTaskSkipPolicyTest,
            .pvParameters   = "TASK_SKIP_PARAM",
            .usStackDepth   = configMINIMAL_STACK_SIZE * 2,
            .uxPriority     = tskIDLE_PRIORITY + 3,    
            .xPeriod        = 1000,      
            .xDeadline      = 5000, /* Larger deadline to allow completion */
            .xTaskPolicy    = POLICY_SKIP
        },

        /* TASK 3: Spawns Aperiodic Jobs. 
           Uses CATCH_UP policy just for demonstration. */
        { 
            .pcName         = "SPAWN_APERIODIC",
            .pxTaskCode     = vTaskAperiodicSpawner,
            .pvParameters   = "SPAWN_APERIODIC",
            .usStackDepth   = configMINIMAL_STACK_SIZE * 2,
            .uxPriority     = tskIDLE_PRIORITY + 2,
            .xPeriod        = 1000,      
            .xDeadline      = 1000,
            .xTaskPolicy    = POLICY_SKIP 
        }
    };

    /* ----------------------------------------------------- */
    /* 2. Scheduler Configuration                            */
    /* ----------------------------------------------------- */
    SchedulerConfig_t myConfig = {
        .globalPolicy   = POLICY_SKIP, 
        .trace_enabled  = pdTRUE,
        .uxNumTasks     = 3,
        .pxTasks        = myTasks
    };

    UART_printf( "\nSTART SCHEDULING\n\n" );
    
    /* ----------------------------------------------------- */
    /* 3. Create System Servers                              */
    /* ----------------------------------------------------- */
    
    /* Create the Polling Server for Aperiodic Tasks.
     * Period:   20 ticks
     * Deadline: 20 ticks
     * Priority: 1 (Background priority) 
     */
    xCreatePollingServer( 2000, 2000, tskIDLE_PRIORITY + 2);

    /* Configure and Start the Custom Scheduler */
    vConfigureScheduler( &myConfig );

    vTaskStartScheduler();

    /* Safety Infinite Loop 
       (Should never be reached if scheduler starts correctly) */
    for( ;; );
    
    return 0;
}