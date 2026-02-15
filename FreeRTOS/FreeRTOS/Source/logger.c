#include "logger.h"
#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

//data
static LoggerEntry_t xLoggerBuffer[LOGGER_BUFFER_SIZE];

//indexes usage
static uint16_t usCount = 0;
static TickType_t ulIdleTicks = 0;
static TickType_t ulLastIdleTick = 0;

//pointers 
static LoggerEntry_t *pxHead = NULL;
static LoggerEntry_t *pxTail = NULL;
static const LoggerEntry_t *pxBufferEnd = xLoggerBuffer + LOGGER_BUFFER_SIZE;

static TaskHandle_t xLoggerTaskHandle = NULL;
void vLoggerTask(void *pvParameters);


void vLoggerInit(void){
    taskENTER_CRITICAL();
    {
        ulIdleTicks = 0;
        usCount = 0;
        //added pointers for head and tail
        pxHead = xLoggerBuffer;
        pxTail = xLoggerBuffer;
    }
    taskEXIT_CRITICAL();

    //create logger task 
    xTaskCreate(vLoggerTask,
                    "Logger",
                    256,
                    NULL,
                    tskIDLE_PRIORITY, //add +1
                    &xLoggerTaskHandle);

}


void vLoggerStore(const char* pcTaskName, LoggerEventType_t eEventType, void *pvValue){
    //check if buffer is initialized
    if (pxHead == NULL) return;

    taskENTER_CRITICAL();
    {
        pxHead->pcTaskName = pcTaskName;
        pxHead->eEventType = eEventType;
        pxHead->ulTimestamp = xTaskGetTickCount();
        pxHead->pvValue = pvValue;

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


void vLoggerStoreFromISR(const char* pcTaskName, LoggerEventType_t eEventType, void *pvValue)
{
    //check if buffer is initialized
    if (pxHead == NULL) return;

    UBaseType_t uxSavedInterruptStatus;
    
    uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR(); 
    {
        pxHead->pcTaskName = pcTaskName;
        pxHead->eEventType = eEventType;
        pxHead->ulTimestamp = xTaskGetTickCountFromISR();
        pxHead->pvValue = pvValue;

        // buffer management
        pxHead++;
        if(pxHead >= pxBufferEnd) pxHead = xLoggerBuffer;

        if(usCount < LOGGER_BUFFER_SIZE) {
            usCount++;
        } else {
            pxTail++;
            if(pxTail >= pxBufferEnd) pxTail = xLoggerBuffer;
        }
    }
    taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
}




void vApplicationIdleHook(void) {
    TickType_t now = xTaskGetTickCountFromISR();
    if (now != ulLastIdleTick){
        ulIdleTicks++;
        ulLastIdleTick = now;
    }
}

void vLoggerResetIdleTime(void){
    
    ulIdleTicks = 0;
    ulLastIdleTick = xTaskGetTickCount();
    
}

uint32_t ulLoggerGetIdleTime(void)
{
    return ulIdleTicks;
}


void vLoggerPrint(void){

    static LoggerEntry_t xLocalBuffer[LOGGER_BUFFER_SIZE];
    uint16_t usLocalCount = 0;

    //copy of the buffer + critical section
    taskENTER_CRITICAL();
    {
        LoggerEntry_t *p = pxTail;

        for(uint16_t i = 0;i< usCount;i++){
            xLocalBuffer[i] = *p;
            p++;
            if(p >= pxBufferEnd){
                p = xLoggerBuffer;
            }
        }

        usLocalCount = usCount;

        //reset pointers
        usCount = 0;
        pxHead = xLoggerBuffer;
        pxTail = xLoggerBuffer;
    }
    taskEXIT_CRITICAL();


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
                        e->pcTaskName,(unsigned long) e->pvValue,
                        (unsigned long) e->ulTimestamp);
                break;

                  UART_printf(s);       
            case LOGGER_TASK_OVERRUN_SKIP:
                snprintf(s,sizeof(s),
                        "[%6lu] %8s OVERRUN → SKIP\r\n",
                        (unsigned long) e->ulTimestamp,
                        e->pcTaskName);
                break;
            case LOGGER_TASK_OVERRUN_KILL:
                snprintf(s,sizeof(s),
                        "[%6lu] %8s OVERRUN → KILL\r\n",
                        (unsigned long) e->ulTimestamp,
                        e->pcTaskName);
                break;

            case LOGGER_TASK_OVERRUN_CATCH_UP:
                snprintf(s,sizeof(s),
                        "[%6lu] %8s OVERRUN → CATCH_UP\r\n",
                        (unsigned long) e->ulTimestamp,
                        e->pcTaskName);
                break;
            
            //added case for debug messages
            case LOGGER_DEBUG:
                snprintf(s, sizeof(s),
                        "[%6lu] %8s DEBUG: %s\r\n",
                        (unsigned long) e->ulTimestamp,
                        e->pcTaskName,
                        (char *)e->pvValue); 
                break;

            default:
                continue;

        }
         UART_printf(s); 
    }

    // Buffer usage info
    if (usLocalCount > 0)
    {
        uint32_t ulUsagePercent = ((uint32_t)usLocalCount * 100) / LOGGER_BUFFER_SIZE;
        
        char usage_buffer[64];
        snprintf(usage_buffer, sizeof(usage_buffer), 
                 "Buffer usage: %lu %% (%u/%d)\r\n", 
                 (unsigned long)ulUsagePercent, 
                 usLocalCount, 
                 LOGGER_BUFFER_SIZE);
        UART_printf(usage_buffer);
    }

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

//for test suite

BaseType_t xLoggerHasEvent(LoggerEventType_t type){
    
    BaseType_t xFound = pdFALSE;

    taskENTER_CRITICAL();
    {
        LoggerEntry_t *p = pxTail;
        for(uint16_t i = 0; i < usCount; i++){
            if(p->eEventType == type){
                xFound = pdTRUE;
                break;
            }
            p++;
            if(p >= pxBufferEnd){
                p = xLoggerBuffer;
            }
        }
    }
    taskEXIT_CRITICAL();
    return xFound;
}

uint32_t ulLoggerCountEvent(const char *taskname, LoggerEventType_t type){
    uint32_t ulCount = 0;

    taskENTER_CRITICAL();
    {
        LoggerEntry_t *p = pxTail;
        for(uint16_t i = 0;i< usCount;i++){
            if(p->eEventType == type && strcmp(p->pcTaskName,taskname)==0){
            ulCount++;
        }
            p++;
            if(p >= pxBufferEnd){
                p = xLoggerBuffer;
            }
        }
    }
    
    taskEXIT_CRITICAL();

    return ulCount;
}

BaseType_t xLoggerDeadlineMiss(LoggerEventType_t type){
    return xLoggerHasEvent(type);
}

TickType_t xLoggerGetFirstEventTime(const char *taskname, LoggerEventType_t type){
    
    TickType_t xFirst = 0;

    taskENTER_CRITICAL();

    {
        LoggerEntry_t *p = pxTail;

        for(uint16_t i = 0; i < usCount; i++)
        {
            if(p->eEventType == type && strcmp(p->pcTaskName, taskname) == 0)
            {
                xFirst = p->ulTimestamp;
                break;
            }

            p++;
            if(p >= pxBufferEnd)
            {
                p = xLoggerBuffer;
            }
        }
    }

    taskEXIT_CRITICAL();

    return xFirst;  // 0 if not found
}
