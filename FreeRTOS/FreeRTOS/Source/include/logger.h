#ifndef LOGGER_H
#define LOGGER_H

#include "FreeRTOS.h"
#include "task.h"

#define LOGGER_BUFFER_SIZE 1000
#define LOGGER_PRINT_PERIOD_MS 100


typedef enum {
    LOGGER_TASK_RELEASE,
    LOGGER_TASK_START,
    LOGGER_TASK_END,
    LOGGER_TASK_SUSPEND,
    LOGGER_TASK_DEADLINE_MISS,
    LOGGER_TASK_OVERRUN_SKIP,
    LOGGER_TASK_OVERRUN_KILL,
    LOGGER_TASK_OVERRUN_CATCH_UP,
    LOGGER_DEBUG
} LoggerEventType_t;


typedef struct {
    const char* pcTaskName;
    TickType_t ulTimestamp;
    LoggerEventType_t eEventType;
    void *pvValue; // generic field
} LoggerEntry_t;

void vLoggerInit(void);


void vLoggerStore(const char* pcTaskName, LoggerEventType_t eEventType, void *pvValue);


void vLoggerStoreFromISR(const char* pcTaskName, LoggerEventType_t eEventType, void *pvValue);


void vLoggerPrint(void);


void vLoggerTask(void *pvParameters);

#endif