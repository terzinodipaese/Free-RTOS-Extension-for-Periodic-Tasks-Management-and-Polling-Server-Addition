#include "logger.h"
#include "uart.h"
#include "FreeRTOS.h"
#include "semphr.h"


//data
static LoggerEntry_t xLoggerBuffer[LOGGER_BUFFER_SIZE];
static uint16_t usHead=0, usTail=0, usCount=0;
static TickType_t ulIdleTicks = 0;
static SemaphoreHandle_t xLoggerSemaphore;


void vLoggerInit(void){
    ulIdleTicks = 0;
    usHead = usTail =  usCount = 0; 
    xLoggerSemaphore = xSemaphoreCreateBinary();
    if(xLoggerSemaphore != NULL) xSemaphoreGive(xLoggerSemaphore);

}


void vLoggerStore(const char* pcTaskName, LoggerEventType_t eEventType){
    //TODO

}


void vApplicationIdleHook(void){
    ulIdleTicks++;
}


void vLoggerPrint(void){

    LoggerEntry_t xLocalBuffer[LOGGER_BUFFER_SIZE];
    uint16_t usLocalCount = 0;

    //copy of the buffer 
    if(xSemaphoreTake(xLoggerSemaphore, portMAX_DELAY) ==pdTRUE){
        uint16_t usIndex = usTail;
        for(uint16_t i = 0;i< usCount;i++){
            xLocalBuffer[i] = xLoggerBuffer[usIndex];
            usIndex = (usIndex+1) % LOGGER_BUFFER_SIZE;
        }

        usLocalCount = usCount;
        usHead = usTail = usCount = 0;
        xSemaphoreGive(xLoggerSemaphore);
    }


    //Print outside the critical section
    char s[128];
    

    for(uint16_t i = 0;i<usLocalCount;i++){
        LoggerEntry_t *e = &xLocalBuffer[i];

        switch(e->eEventType){
            case LOGGER_TASK_RELEASE:
                snprintf(s,sizeof(s), 
                        "[%6lu] %8s RELEASE\r\n",
                        (unsigned long) e->ulTimestamp,
                        e->pcTaskName);
                break;
            case LOGGER_TASK_START:
                snprintf(s,sizeof(s),
                        "[%6lu] %8s START\r\n",
                        (unsigned long) e->ulTimestamp,
                        e->pcTaskName);
                break;
            case LOGGER_TASK_END:
                snprintf(s,sizeof(s),
                        "[%6lu] %8s COMPLETE\r\n",
                        (unsigned long) e->ulTimestamp,
                        e->pcTaskName);
                break;
            case LOGGER_TASK_DEADLINE_MISS:
                snprintf(s,sizeof(s),
                        "[%6lu] %8s DEADLINE_MISS (D=%lu @ tick %lu)\r\n",
                        (unsigned long) e->ulTimestamp,
                        e->pcTaskName,(unsigned long) e->ulDeadline,
                        (unsigned long) e->ulTimestamp);
                break;

            default:
                continue;

        }
         UART_printf(s); 
    }

    //CPU idle 
    TickType_t totalTicks = xTaskGetTickCount();
    if(totalTicks>0){
        float idle = ((float)ulIdleTicks/ (float)totalTicks) * 100.0; 
        UART_printf("CPU idle: %.2f %%\r\n",idle);
    }
    //vLoggerInit(); 

}

/*
void vLoggerTask(void *pvParameters){
    (void) pvParameters;
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        vLoggerPrint();
    }
}
    */
