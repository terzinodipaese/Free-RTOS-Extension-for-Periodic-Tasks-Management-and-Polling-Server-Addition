#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "uart.h"
#include "logger.h"

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
	//logger initialization
	vLoggerInit();

	xTaskCreatePeriodic(HelloTask, "MyTask", 2048, NULL, pdMS_TO_TICKS(100), pdMS_TO_TICKS(250), 2, &xHandle1);
	//xTaskSetPeriod(xHandle1, 1000);
	//xTaskCreatePeriodic(HelloTask2, "MyTask2", 2048, NULL, pdMS_TO_TICKS(450), 2, NULL);

	xTaskCreatePeriodic(HelloTask2, "MyTask2", 2048, NULL, pdMS_TO_TICKS(500), pdMS_TO_TICKS(10000), 2, NULL);

	//xTaskCreate(HelloTask2, "Hello2", configMINIMAL_STACK_SIZE + 64, NULL, tskIDLE_PRIORITY + 1, &xHandle);
	//xTaskSetPeriod( xHandle, 100 );   
	vTaskStartScheduler();
	for( ; ; );
}


