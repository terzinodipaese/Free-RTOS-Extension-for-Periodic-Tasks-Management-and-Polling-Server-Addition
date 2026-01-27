#include "FreeRTOS.h"
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
		.globalPolicy = POLICY_KILL,
		.trace_enabled = pdTRUE,
		.uxNumTasks=2,
		.pxTasks = myTasks
	};

	// Configure and create everything in one go
	vConfigureScheduler(&myConfig);


	vTaskStartScheduler();
	for( ; ; );
}