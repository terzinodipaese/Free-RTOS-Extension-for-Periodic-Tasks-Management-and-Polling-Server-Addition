/*#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "uart.h"
<<<<<<< HEAD
#include "logger.h"


#ifdef TEST_SUITE
#include "test_suite.h"
#endif

extern BaseType_t xTaskCreatePeriodic( TaskFunction_t pxTaskCode,
                                    const char * const pcName,
                                    const configSTACK_DEPTH_TYPE uxStackDepth,
                                    void * const pvParameters,
                                    TickType_t xPeriod,
                                    TickType_t xDeadline,
                                    UBaseType_t uxPriority,
                                    TaskHandle_t * const pxCreatedTask );

extern TickType_t xTaskGetPeriod( TaskHandle_t xTask );
extern TickType_t xTaskGetDeadline( TaskHandle_t xTask );
=======
//X DEBUG
#include <stdio.h>
>>>>>>> dev3_logger

#define mainTASK_PRIORITY    ( tskIDLE_PRIORITY + 2 )

static void HelloTask(void *arg) {
	(void)arg;
	char s[50];
	sprintf(s,"Hello from %s with period %u and deadline %u \n",
			pcTaskGetName(NULL),
			xTaskGetPeriod(NULL),
			xTaskGetDeadline(NULL)
			);
	UART_printf(s);
}

static void HelloTask2(void *arg) {
	(void)arg;
	char s[50];
	sprintf(s,"Hello from %s with period %u and deadline %u \n",
			pcTaskGetName(NULL),
			xTaskGetPeriod(NULL),
			xTaskGetDeadline(NULL)
			);
	UART_printf(s);
	//vTaskDelete(NULL);
}

void SimpleTask(void *arg){
	(void)arg;
    UART_printf("Hello\n");
}


int main(int argc, char **argv){

	(void) argc;
	(void) argv;

	UART_init();
	//logger initialization
	vLoggerInit();

#ifdef TEST_SUITE
	UART_printf("\r\n--TEST SUITE--\r\n");
	SchedulerConfig cfg = {
		.globalPolicy = POLICY_SKIP,
		.trace_enabled = pdTRUE,
		.maxtasks = 8,
		.pxTasks = NULL
	};
	vConfigureScheduler(&cfg);
	xTaskCreate(vTestSuite, "TestSuite",2048,NULL,tskIDLE_PRIORITY+2,NULL);
	vTaskStartScheduler();
#else
	

	TaskHandle_t xHandle1;

<<<<<<< HEAD
	
	xTaskCreatePeriodic(HelloTask, "MyTask", 2048, NULL, pdMS_TO_TICKS(100), pdMS_TO_TICKS(250), 2, &xHandle1);
=======
	UART_init();
	//xTaskCreatePeriodic(HelloTask, "MyTask", 2048, NULL, pdMS_TO_TICKS(100), pdMS_TO_TICKS(250), 2, &xHandle1);
>>>>>>> dev3_logger
	//xTaskSetPeriod(xHandle1, 1000);
	//xTaskCreatePeriodic(HelloTask2, "MyTask2", 2048, NULL, pdMS_TO_TICKS(450), 2, NULL);

	//xTaskCreatePeriodic(HelloTask2, "MyTask2", 2048, NULL, pdMS_TO_TICKS(500), pdMS_TO_TICKS(10000), 2, NULL);

	//xTaskCreate(HelloTask2, "Hello2", configMINIMAL_STACK_SIZE + 64, NULL, tskIDLE_PRIORITY + 1, &xHandle);
	//xTaskSetPeriod( xHandle, 100 );   


	PeriodicTaskConfig_t myTasks[] = {
		{ HelloTask, "MyTask", 2048 ,NULL, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100),1 }, // Periodo 100, Deadline 100
		{ HelloTask2,"MyTask2", 2048,NULL, pdMS_TO_TICKS(200), pdMS_TO_TICKS(150),2 }  // Periodo 200, Deadline 150
	};

	SchedulerConfig_t myConfig = {
		.globalPolicy = POLICY_SKIP,
		.trace_enabled = pdTRUE,
		.uxNumTasks=2,
		.pxTasks = myTasks
	};

	// Configure and create everything in one go
	vConfigureScheduler(&myConfig);


	vTaskStartScheduler();

#endif

	for( ; ; );
}*/
/* Standard includes. */
#include <stdio.h>
#include <stddef.h>
#include <string.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include "logger.h"
/* ===========================================================
 * FUNZIONI DI TEST
 * =========================================================== */

/*
// Funzione per "bruciare" tempo (Busy Wait) 
void vBusyWait( int ticks_simulated )
{
    volatile int i;
    // Moltiplicatore per QEMU. 
    // Se non vedi Overrun, aumenta questo numero (es. * 20000) 
    for( i = 0; i < ( ticks_simulated * 15000 ); i++ ) 
    {
        __asm volatile( "nop" );
    }
}*/

void vBusyWait( int ticks_simulated )
{
    // Define how often to yield (e.g., every 10 ticks of work)
    const int chunk_size = 10; 
    
    int remaining = ticks_simulated;
    volatile int i;
    
    // Calibrate this multiplier for your QEMU speed!
    // If tasks finish too fast, increase this (e.g. 50000 or 100000)
    const int loops_per_tick = 25000; 

    while (remaining > 0)
    {
        // 1. Determine how much work to do in this chunk
        int current_chunk = (remaining > chunk_size) ? chunk_size : remaining;
        
        // 2. BURN CPU (Simulate Work)
        for( i = 0; i < ( current_chunk * loops_per_tick ); i++ ) 
        {
            __asm volatile( "nop" );
        }
        
        // 3. FORCE CONTEXT SWITCH
        // vTaskDelay(1) puts this task in Blocked state for 1 tick.
        // This allows Lower Priority tasks (Logger) to execute immediately.
        vTaskDelay(1);

        // 4. Update remaining time
        remaining -= current_chunk;
    }
}


/* Task di Test per Policy KILL */
void vTestTask( void *pvParameters )
{
    const char *pcName = (const char *) pvParameters;
    
	char s[80];

    /* 1. START MSG */
    sprintf(s,"{%u} %s: START Job\n", xTaskGetTickCount(), pcName);
    UART_printf(s);

    /* 2. Lavoro Lento (deve durare PIÙ del periodo) */
    /* Periodo impostato a 100, qui aspettiamo circa 250 */
	vBusyWait(250);
    
    /* 3. END MSG */
    /* - SKIP e CATCH_UP arriveranno qui (in ritardo).
     * - KILL non dovrebbe MAI arrivare qui (verrà ucciso durante il vBusyWait).*/
    sprintf(s,"{%u} %s: END \n", xTaskGetTickCount(), pcName);
	UART_printf(s);
}

void vTestTask2( void *pvParameters )
{
    const char *pcName = (const char *) pvParameters;
    
	char s[80];

    /* 1. START MSG */
    sprintf(s,"{%u} %s: start Job\n", xTaskGetTickCount(), pcName);
    UART_printf(s);

    /* 2. Lavoro Lento (deve durare PIÙ del periodo) */
    /* Periodo impostato a 100, qui aspettiamo circa 250 */
    //vBusyWait( 250 ); 
	vBusyWait(250);
    
    /* 3. END MSG */
    /* - SKIP e CATCH_UP arriveranno qui (in ritardo).
     * - KILL non dovrebbe MAI arrivare qui (verrà ucciso durante il vBusyWait).*/
    sprintf(s,"{%u} %s: end \n", xTaskGetTickCount(), pcName);
	UART_printf(s);
}


/* ===========================================================
 * MAIN
 * =========================================================== */
int main( void )
{

	UART_init();
    vLoggerInit(); // Inizializza il logger prima di avviare lo scheduler
	
    /* 1. Task config */
    PeriodicTaskConfig_t myTasks[] = {
        /* TASK 1: POLICY SKIP (Default 0) */
        { 
            .pcName = "TASK_KILL",
            .pxTaskCode = vTestTask,
            .pvParameters = "TASK_KILL",
            .usStackDepth = configMINIMAL_STACK_SIZE * 2,
            .uxPriority = 1,
            .xPeriod = 100,      
            .xDeadline = 100,
            .xTaskPolicy = POLICY_KILL
        },

        /* TASK 2: POLICY KILL (1) */
        { 
            .pcName = "TASK_SKIP",
            .pxTaskCode = vTestTask2,
            .pvParameters = "TASK_SKIP",
            .usStackDepth = configMINIMAL_STACK_SIZE * 2,
            /* Priorità leggermente più alta per vederlo emergere nel log */
            //.uxPriority = 2, 
			.uxPriority=1,    
            .xPeriod = 100,      
            .xDeadline = 500,
			.xTaskPolicy = POLICY_SKIP
            
        },

        //TASK 3: POLICY CATCH_UP (2) 
        { 
            .pcName = "TASK_CATCH",
            .pxTaskCode = vTestTask,
            .pvParameters = "T_CATCH",
            .usStackDepth = configMINIMAL_STACK_SIZE * 2,
            .uxPriority = 1,
            .xPeriod = 100,      
            .xDeadline = 100,
            .xTaskPolicy = POLICY_CATCH_UP 
        }
    };

    /* 2. Configurazione Scheduler */
    SchedulerConfig_t myConfig = {
        .globalPolicy = POLICY_SKIP, 
        .trace_enabled = pdTRUE,
        .uxNumTasks = 3,
        .pxTasks = myTasks
    };
	
	UART_printf("\nSTART SCHEDULING\n\n");

	vConfigureScheduler( &myConfig );
    
    vTaskStartScheduler();

    /* Loop infinito di sicurezza */
    for( ;; );
    return 0;
}