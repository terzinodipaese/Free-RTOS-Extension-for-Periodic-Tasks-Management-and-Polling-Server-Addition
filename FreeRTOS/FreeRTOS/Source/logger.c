#include "logger.h"
#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"


//data
static LoggerEntry_t xLoggerBuffer[LOGGER_BUFFER_SIZE];

//indexes usage
static uint16_t usCount = 0;
static TickType_t ulIdleTicks = 0;

//pointers 
static LoggerEntry_t *pxHead = NULL;
static LoggerEntry_t *pxTail = NULL;
static const LoggerEntry_t *pxBufferEnd = xLoggerBuffer + LOGGER_BUFFER_SIZE;

static TaskHandle_t xLoggerTaskHandle = NULL;


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


void vLoggerStore(const char* pcTaskName, LoggerEventType_t eEventType, TickType_t ulValue){
    taskENTER_CRITICAL();
    {
        pxHead->pcTaskName = pcTaskName;
        pxHead->eEventType = eEventType;
        pxHead->ulTimestamp = xTaskGetTickCount();
        pxHead->ulValue = ulValue;

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

static TickType_t lastTick = 0;

void vApplicationIdleHook(void){
    TickType_t currentTick = xTaskGetTickCount();
    if(currentTick != lastTick){
       ulIdleTicks++; 
       lastTick = currentTick;
    }
    
}


void vLoggerPrint(void){

    static LoggerEntry_t xLocalBuffer[LOGGER_BUFFER_SIZE];
    uint16_t usLocalCount = 0;
    TickType_t localIdleTicks,lastTimestamp,currentIdleTicks,currentTimestamp = 0;

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
        currentIdleTicks = ulIdleTicks;
        currentTimestamp = xTaskGetTickCount();

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
                        e->pcTaskName,(unsigned long) e->ulValue,
                        (unsigned long) e->ulTimestamp);
                break;
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
            

            default:
                continue;

        }
         UART_printf(s); 
    }

    //CPU idle 
    if(lastTimestamp > 0){
        //added snprintf and used buffer
        uint32_t idle = ((currentIdleTicks-localIdleTicks)*100U/(currentTimestamp-lastTimestamp)); 
        
        if(idle>100) idle = 100;
        
        
        char idle_buffer[64];
        snprintf(idle_buffer, sizeof(idle_buffer), "CPU idle: %.2f %%\r\n", idle);
        UART_printf(idle_buffer);
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

bool LoggerHasEvent(LoggerEventType_t type){
    bool found = false;

    taskENTER_CRITICAL();
    {
        LoggerEntry_t *p = pxTail;
        for(uint16_t i = 0; i < usCount; i++){
            if(p->eEventType == type){
                found = true;
                break;
            }
            p++;
            if(p >= pxBufferEnd){
                p = xLoggerBuffer;
            }
        }
    }
    taskEXIT_CRITICAL();
    return found;
}




uint32_t LoggerCountEvent(const char *taskname, LoggerEventType_t type){
    uint32_t count = 0;

    taskENTER_CRITICAL();
    {
        LoggerEntry_t *p = pxTail;
        for(uint16_t i = 0;i< usCount;i++){
            if(p->eEventType == type && strcmp(p->pcTaskName,taskname)==0){
            count++;
        }
            p++;
            if(p >= pxBufferEnd){
                p = xLoggerBuffer;
            }
        }
    }
    
    taskEXIT_CRITICAL();

    return count;
}

bool LoggerDeadlineMiss(LoggerEventType_t type){
    return LoggerHasEvent(type);
}