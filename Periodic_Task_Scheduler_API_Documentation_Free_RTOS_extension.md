# Periodic Task Scheduler API Documentation -- Free RTOS extension

This document provides an overview of the functions used to manage periodic tasks, scheduling policies, and logging utilities

---

## Table of Contents
1. [vPeriodicWrapperTask](#vperiodicwrappertask)
2. [xTaskCreatePeriodic](#xtaskcreateperiodic)
3. [prvProcessPeriodicTasks](#prvprocessperiodictasks)
4. [prvUpdateNextPeriodicEventTick](#prvupdatenextperiodiceventtick)
5. [xTaskGetPeriod](#xtaskgetperiod)
6. [xTaskGetDeadline](#xtaskgetdeadline)
7. [vConfigureScheduler](#vconfigurescheduler)
8. [vBusyWait](#vbusywait)
9. [prvHardResetTask](#prvhardresettask)
10. [vLoggerInit](#vloggerinit)
11. [vLoggerStore](#vloggerstore)
12. [vLoggerPrint](#vloggerprint)
13. [vLoggerTask](#vloggertask)

---

## Task Management API Functions

#### vPeriodicWrapperTask
> *Description*: A static wrapper function used to simulate the lifecycle and execution of a periodic task
```c
static void vPeriodicWrapperTask(void *pvParameters);
```

#### xTaskCreatePeriodic
> *Description*: extended version of xTaskCreate(), accepts also deadline and priority as TickType_t
```c
BaseType_t xTaskCreatePeriodic(TaskFunction_t pxTaskCode,
                            const char * const pcName,
                            const configSTACK_DEPTH_TYPE uxStackDepth,
                            void * const pvParameters,
                            TickType_t xPeriod,
                            TickType_t xDeadline,
                            UBaseType_t uxPriority,
                            TaskHandle_t * const pxCreatedTask);
```

#### prvProcessPeriodicTasks
> *Description*: picks and execute the next available periodic task, return true if the released task has higher priority, so we must yield later
```c
static BaseType_t prvProcessPeriodicTasks(const TickType_t xTickCount) PRIVILEGED_FUNCTION;
```

#### prvUpdateNextPeriodicEventTick
> *Description*: goes through the periodic tasks list and update the next periodic event tick
```c
static void prvUpdateNextPeriodicEventTick(void) PRIVILEGED_FUNCTION;
```

#### xTaskGetPeriod
> *Description*: to get the period of a periodic task
```c
TickType_t xTaskGetPeriod(TaskHandle_t xTask);
```

#### xTaskGetDeadline
> *Description*: to get the deadline of a periodic task
```c
TickType_t xTaskGetDeadline(TaskHandle_t xTask);
```

#### vConfigureScheduler
> *Description*: set global policy and call xTaskCreatePeriodic to manage tasks
```c
void vConfigureScheduler(SchedulerConfig_t *pxCfg);
```

#### vBusyWait
> *Description*: do busy waiting for a time equal to ticks_simulated
```c
void vBusyWait(int ticks_simulated);
```

#### prvHardResetTask
> *Description*: executed in case the kill policy has to be applied
```c
static void prvHardResetTask(TCB_t *pxTCB, PeriodicWrap_t *pxConfig);
```

#### vLoggerInit
> *Description*: initialize the logger task
```c
void vLoggerInit(void);
```

#### vLoggerStore
> *Description*: store logger messages in a buffer
```c
void vLoggerStore(const char* pcTaskName, LoggerEventType_t eEventType, TickType_t ulValue);
```

#### vLoggerPrint
> *Description*: print logger messages
```c
void vLoggerPrint(void);
```

#### vLoggerTask
> *Description*: implement the logger task
```c
void vLoggerTask(void *pvParameters);
```
