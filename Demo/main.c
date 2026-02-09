/*#include "FreeRTOS.h"
#include "task.h"

#include "uart.h"
//X DEBUG
#include <stdio.h>

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

	TaskHandle_t xHandle1;

	UART_init();
	//xTaskCreatePeriodic(HelloTask, "MyTask", 2048, NULL, pdMS_TO_TICKS(100), pdMS_TO_TICKS(250), 2, &xHandle1);
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
/* ===========================================================
 * FUNZIONI DI TEST
 * =========================================================== */

/* Funzione per "bruciare" tempo (Busy Wait) */
void vBusyWait( int ticks_simulated )
{
    volatile int i;
    /* Moltiplicatore per QEMU. 
     * Se non vedi Overrun, aumenta questo numero (es. * 20000) */
    for( i = 0; i < ( ticks_simulated * 15000 ); i++ ) 
    {
        __asm volatile( "nop" );
    }
}

/* Task di Test per Policy KILL */
void vTestTask( void *pvParameters )
{
    const char *pcName = (const char *) pvParameters;
    
	char s[80];

    /* 1. START MSG */
    sprintf(s,"[%lu] %s: START Job\n", xTaskGetTickCount(), pcName);
    UART_printf(s);

    /* 2. Lavoro Lento (deve durare PIÙ del periodo) */
    /* Periodo impostato a 100, qui aspettiamo circa 250 */
	vBusyWait(250);
    
    /* 3. END MSG */
    /* - SKIP e CATCH_UP arriveranno qui (in ritardo).
     * - KILL non dovrebbe MAI arrivare qui (verrà ucciso durante il vBusyWait).*/
    sprintf(s,"[%lu] %s: END \n", xTaskGetTickCount(), pcName);
	UART_printf(s);
}

void vTestTask2( void *pvParameters )
{
    const char *pcName = (const char *) pvParameters;
    
	char s[80];

    /* 1. START MSG */
    sprintf(s,"[%lu] %s: START Job\n", xTaskGetTickCount(), pcName);
    UART_printf(s);

    /* 2. Lavoro Lento (deve durare PIÙ del periodo) */
    /* Periodo impostato a 100, qui aspettiamo circa 250 */
    //vBusyWait( 250 ); 
	vBusyWait(250);
    
    /* 3. END MSG */
    /* - SKIP e CATCH_UP arriveranno qui (in ritardo).
     * - KILL non dovrebbe MAI arrivare qui (verrà ucciso durante il vBusyWait).*/
    sprintf(s,"[%lu] %s: END \n", xTaskGetTickCount(), pcName);
	UART_printf(s);
}

void aperiodic( void *pvParameters ) {
	(void) pvParameters;
	const TickType_t xDelay = 30 / portTICK_PERIOD_MS;
	vTaskDelay(xDelay);
    UART_printf("Aperiodic 1 executed\n");
}

void aperiodic2( void *pvParameters ) {
	(void) pvParameters;
    UART_printf("Aperiodic 2 executed\n");
}

void aperiodic3( void *pvParameters ) {
	(void) pvParameters;
    UART_printf("Aperiodic 3 executed\n");
}

void aperiodic4( void *pvParameters ) {
	(void) pvParameters;
    UART_printf("Aperiodic 4 executed\n");
}

void aperiodic5( void *pvParameters ) {
	(void) pvParameters;
	const TickType_t xDelay = 70 / portTICK_PERIOD_MS;
	vTaskDelay(xDelay);
    UART_printf("Aperiodic 5 executed\n");
}


/* ===========================================================
 * MAIN
 * =========================================================== */
int main( void )
{

	UART_init();
	
    /* 1. Task config */
    PeriodicTaskConfig_t myTasks[] = {
        /* TASK 1: POLICY SKIP (Default 0) */
        { 
            .pcName = "TASK_KILL",
            .pxTaskCode = vTestTask,
            .pvParameters = "TASK_KILL",
            .usStackDepth = configMINIMAL_STACK_SIZE * 2,
            .uxPriority = 2,
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
			.uxPriority = 1,    
            .xPeriod = 100,      
            .xDeadline = 500,
			.xTaskPolicy = POLICY_SKIP
            
        },

        //TASK 3: POLICY CATCH_UP (2) 
        { 
            .pcName = "TASK_CATCH",
            .pxTaskCode = vTestTask,
            .pvParameters = "TASK_CATCH",
            .usStackDepth = configMINIMAL_STACK_SIZE * 2,
            .uxPriority = 2,
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
    
	
    xCreatePollingServer(20, 20, 1);

    xTaskCreateAperiodic(aperiodic, NULL, 1, APERIODIC_POLICY_OVERRUN);
    xTaskCreateAperiodic(aperiodic2, NULL, 1, APERIODIC_POLICY_KILL);
    xTaskCreateAperiodic(aperiodic3, NULL, 1, APERIODIC_POLICY_OVERRUN);
    xTaskCreateAperiodic(aperiodic4, NULL, 1, APERIODIC_POLICY_KILL);
    xTaskCreateAperiodic(aperiodic5, NULL, 1, APERIODIC_POLICY_OVERRUN);
    
    vConfigureScheduler( &myConfig );

    vTaskStartScheduler();

    /* Loop infinito di sicurezza */
    for( ;; );
    return 0;
}
