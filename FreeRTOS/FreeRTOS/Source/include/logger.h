#ifndef LOGGER_H
#define LOGGER_H

#include "FreeRTOS.h"
#include "task.h"

#define LOGGER_BUFFER_SIZE 1024

static TickType_t ulIdleTicks = 0;

typedef enum {
    LOGGER_TASK_RELEASE,
    LOGGER_TASK_START,
    LOGGER_TASK_END,
    LOGGER_TASK_DEADLINE_MISS,
    LOGGER_IDLE
} LoggerEventType_t;


typedef struct {
    const char* pcTaskName;
    TickType_t ulTimestamp;
    LoggerEventType_t eEventType;
    TickType_t ulDeadline; 
} LoggerEntry_t;

void vLoggerInit(void);


void vLoggerStore(const char* pcTaskName, LoggerEventType_t eEventType);


void vLoggerPrint(void);


//void vLoggerTask(void *pvParameters);

#endif