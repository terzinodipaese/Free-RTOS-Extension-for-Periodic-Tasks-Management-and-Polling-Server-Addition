#ifndef LOGGER_H
#define LOGGER_H

#include "FreeRTOS.h"
#include "task.h"

<<<<<<< HEAD
#define LOGGER_BUFFER_SIZE 100
#define LOGGER_PRINT_PERIOD_MS 500
=======
#define LOGGER_BUFFER_SIZE 1000
#define LOGGER_PRINT_PERIOD_MS 100
>>>>>>> dev3_logger


typedef enum {
    LOGGER_TASK_RELEASE,
    LOGGER_TASK_START,
    LOGGER_TASK_END,
    LOGGER_TASK_DEADLINE_MISS,
<<<<<<< HEAD
    LOGGER_IDLE
=======
    LOGGER_TASK_OVERRUN_SKIP,
    LOGGER_TASK_OVERRUN_KILL,
    LOGGER_TASK_OVERRUN_CATCH_UP,
    LOGGER_DEBUG
>>>>>>> dev3_logger
} LoggerEventType_t;


typedef struct {
    const char* pcTaskName;
    TickType_t ulTimestamp;
    LoggerEventType_t eEventType;
<<<<<<< HEAD
    TickType_t ulDeadline; 
    // generic field
=======
    void *pvValue; // generic field
>>>>>>> dev3_logger
} LoggerEntry_t;

void vLoggerInit(void);


<<<<<<< HEAD
void vLoggerStore(const char* pcTaskName, LoggerEventType_t eEventType, TickType_t ulValue);
=======
void vLoggerStore(const char* pcTaskName, LoggerEventType_t eEventType, void *pvValue);


void vLoggerStoreFromISR(const char* pcTaskName, LoggerEventType_t eEventType, void *pvValue);
>>>>>>> dev3_logger


void vLoggerPrint(void);


void vLoggerTask(void *pvParameters);

#endif