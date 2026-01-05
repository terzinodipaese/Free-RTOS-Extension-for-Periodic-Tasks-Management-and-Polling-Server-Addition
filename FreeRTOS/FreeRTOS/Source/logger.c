#include "logger.h"
#include "uart.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>

//data
static LoggerEntry_t xLoggerBuffer[LOGGER_BUFFER_SIZE];

//indexes usage
static uint16_t usHead=0, usTail=0, usCount=0;
static TickType_t ulIdleTicks = 0;
static SemaphoreHandle_t xLoggerSemaphore;

//pointers 
static LoggerEntry_t *pxHead = NULL;
static LoggerEntry_t *pxTail = NULL;
static const LoggerEntry_t *pxBufferEnd = xLoggerBuffer + LOGGER_BUFFER_SIZE;

static TaskHandle_t xLoggerTaskHandle = NULL;


void vLoggerInit(void){
    ulIdleTicks = 0;
    usHead = usTail = 0;

    //added pointers for head and tail
    pxHead = xLoggerBuffer;
    pxTail = xLoggerBuffer;

    usCount = 0;
    xLoggerSemaphore = xSemaphoreCreateBinary();
    if(xLoggerSemaphore != NULL) xSemaphoreGive(xLoggerSemaphore);

    //create logger task 
    xTaskCreate(vLoggerTask,
                    "Logger",
                    256,
                    NULL,
                    tskIDLE_PRIORITY,
                    &xLoggerTaskHandle);

}


void vLoggerStore(const char* pcTaskName, LoggerEventType_t eEventType, TickType_t ulValue){
    taskENTER_CRITICAL();
    {
        pxHead->pcTaskName = pcTaskName;
        pxHead->eEventType = eEventType;
        pxHead->ulTimestamp = xTaskGetTickCount();
        pxHead->ulDeadline = ulValue;

        pxHead++;
        if(pxHead >= pxBufferEnd){
            pxHead = xLoggerBuffer;
        }

        if(usCount < LOGGER_BUFFER_SIZE){
            usCount++;
        }
        else {
            //buffer full, move tail forward
            pxTail++;
            if(pxTail >= pxBufferEnd){
                pxTail = xLoggerBuffer;
            }
        }
    }
    taskEXIT_CRITICAL();
}


void vApplicationIdleHook(void){
    ulIdleTicks++;
}


void vLoggerPrint(void){

    static LoggerEntry_t xLocalBuffer[LOGGER_BUFFER_SIZE];
    uint16_t usLocalCount = 0;

    //copy of the buffer 
    if(xSemaphoreTake(xLoggerSemaphore, portMAX_DELAY) ==pdTRUE){

        //added critical section
        taskENTER_CRITICAL();
        {
            //parse the pointer to get the tail index
            usTail = (uint16_t)(pxTail - xLoggerBuffer);

            uint16_t usIndex = usTail;
            for(uint16_t i = 0;i< usCount;i++){
                xLocalBuffer[i] = xLoggerBuffer[usIndex];
                usIndex = (usIndex+1) % LOGGER_BUFFER_SIZE;
            }

            usLocalCount = usCount;
            usHead = usTail = usCount = 0;

            //reset pointers
            pxHead = xLoggerBuffer;
            pxTail = xLoggerBuffer;
        }
        taskEXIT_CRITICAL();

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
        //added snprintf and used buffer
        float idle = ((float)ulIdleTicks/ (float)totalTicks) * 100.0; 
        char idle_buffer[64];
        snprintf(idle_buffer, sizeof(idle_buffer), "CPU idle: %.2f %%\r\n", idle);
        UART_printf(idle_buffer);
    }
    //vLoggerInit(); 

}


void vLoggerTask(void *pvParameters){
    (void) pvParameters;
    const TickType_t xFrequency = pdMS_TO_TICKS(LOGGER_PRINT_PERIOD_MS);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for(;;){
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        vLoggerPrint();
    }
}
