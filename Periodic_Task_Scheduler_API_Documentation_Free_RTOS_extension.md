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
14. [xTaskCreateAperiodic](#xtaskcreateaperiodic)
15. [prvPollingServerFunction](#prvpollingserverfunction)
16. [xCreatePollingServer](#xcreatepollingserver)
17. [vTaskDeletePeriodic](#vtaskdeleteperiodic)
18. [vTestTask](#vtesttask)
19. [vTestTask2](#vtesttask2)
20. [vTestEventGroupInit](#vtesteventgroupinit)
21. [vTestStart](#vteststart)
22. [vTestStop](#vteststop)
23. [bIsTestRunning](#bistestrunning)
24. [bIsCleanupSignaled](#biscleanupsignaled)
25. [xTest_StressOverlappingHRT](#xtest_stressoverlappinghrt)
26. [prvAperiodicWorker](#prvaperiodicworker)
27. [vLoggerStoreFromISR](#vloggerstorefromisr)
28. [vApplicationIdleHook](#vapplicationidlehook)
29. [vTestSuite](#vtestsuite)
30. [xLoggerDeadlineMiss](#xloggerdeadlinemiss)
31. [BusyMs](#busyms)
32. [vTask_Jitter](#vtask_jitter)
33. [vTask_Generic](#vtask_generic)
34. [vTask_Aperiodic](#vtask_aperiodic)
35. [vDeleteTestTasks](#vdeletetesttasks)
36. [xTest_EdgeMinimalTimeGap](#xtest_edgeminimaltimegap)
37. [xTest_PreemptionHigher](#xtest_preemptionhigher)
38. [xTest_CPUOverhead2](#xtest_cpuoverhead2)
39. [xTest_CPUOverhead_WithAperiodic](#xtest_cpuoverhead_withaperiodic)
40. [vQEMUExit](#vqemuexit)
41. [xTest_ReleaseJitter](#xtest_releasejitter)
42. [xTest_DeadlineMiss](#xtest_deadlinemiss)
43. [xTest_OverrunPolicySkip](#xtest_overrunpolicyskip)
44. [xTest_OverrunPolicyCatchup](#xtest_overrunpolicycatchup)
45. [xTest_OverrunPolicyKill](#xtest_overrunpolicykill)
46. [xTest_RoundRobin](#xtest_roundrobin)
47. [xTest_CPUOverhead](#xtest_cpuoverhead)
48. [xTest_AperiodicServer](#xtest_aperiodicserver)
49. [xTest_AperiodicKill](#xtest_aperiodickill)
50. [xTest_AperiodicOverrun](#xtest_aperiodicoverrun)
51. [xTest_ReleaseJitter_WithAperiodic](#xtest_releasejitter_withaperiodic)
52. [xTest_AllPolicies](#xtest_allpolicies)
53. [xTest_OverrunPolicies_GlobalStress](#xtest_overrunpolicies_globalstress)
54. [vStopScheduler](#vstopscheduler)
55. [LoggerResetIdleTime](#loggerresetidletime)
56. [xTest_AperiodicQueueFull](#xtest_aperiodicqueuefull)
57. [xTest_AperiodicWithPeriodicTasks](#xtest_aperiodicwithperiodictasks)
58. [xTest_ConfigBasic](#xtest_configbasic)
59. [xTest_ConfigPriorities](#xtest_configpriorities)
60. [xTest_ConfigMaxTasks](#xtest_configmaxtasks)
61. [xTest_ConfigVsDynamic](#xtest_configvsdynamic)
62. [xLoggerGetFirstEventTime](#xloggergetfirsteventtime)
63. [ulLoggerCountEvent](#ulloggercountevent)
64. [xLoggerHasEvent](#xloggerhasevent)
65. [ulLoggerGetIdleTime](#ulloggergetidletime)

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
                                TaskHandle_t * const pxCreatedTask,
                                OverrunPolicy_t xTaskPolicy);
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

#### xTaskCreateAperiodic
> *Description*: API function to create an aperiodic task, managed by the polling server
```c
BaseType_t xTaskCreateAperiodic(TaskFunction_t pxTaskCode,
                                    void *pvParameters,
                                    TickType_t xSoftDeadline,
                                    BaseType_t xPolicy,
                                    TickType_t xStartReleaseTime);
```

#### prvPollingServerFunction
> *Description*: implement the polling server functionality
```c
static void prvPollingServerFunction(void *pvParameters);
```

#### xCreatePollingServer
> *Description*: instantiate the polling server as a task
```c
BaseType_t xCreatePollingServer(TickType_t xPeriod, 
                                 TickType_t xDeadline, 
                                 UBaseType_t uxPriority);

```

#### vTaskDeletePeriodic
> *Description*: delete periodic task
```c
void vTaskDeletePeriodic(TaskHandle_t xTaskToDelete);

```

#### vTestTask
> *Description*: test task for KILL policy
```c
void vTestTask(void *pvParameters);

```

#### vTestTask2
> *Description*: another test task
```c
void vTestTask2(void *pvParameters);

```

#### vTestEventGroupInit
> *Description*: to set event group to a clean initial state
```c
static void vTestEventGroupInit(void);

```

#### vTestStart
> *Description*: to start a test
```c
static void vTestStart(void);

```

#### vTestStop
> *Description*: to stop a test
```c
static void vTestStop(void);

```

#### bIsTestRunning
> *Description*: check if test is running, true if test is running, false otherwise
```c
static bool bIsTestRunning(void);

```

#### bIsCleanupSignaled
> *Description*: check if test cleanup is requested, true if cleanup is requested, false otherwise
```c
static bool bIsCleanupSignaled(void);

```

#### xTest_StressOverlappingHRT
> *Description*: stress test with 8 overlapping HRT tasks, check if there aren't any deadline miss
```c
static TestResult_t xTest_StressOverlappingHRT(void);

```

#### prvAperiodicWorker
> *Description*: execute the aperiodic task function and notify the polling server that we are done
```c
static void prvAperiodicWorker(void *pvParameters);

```

#### vLoggerStoreFromISR
> *Description*: version of vLoggerStore for ISR contexts
```c
void vLoggerStoreFromISR(const char* pcTaskName, LoggerEventType_t eEventType, void *pvValue);

```

#### vApplicationIdleHook
> *Description*: increments idle tick
```c
void vApplicationIdleHook(void);

```

#### vTestSuite
> *Description*: call every specific test suite function
```c
void vTestSuite(void);

```

#### xLoggerDeadlineMiss
> *Description*: looks for deadline miss logger events
```c
BaseType_t xLoggerDeadlineMiss(LoggerEventType_t type);

```

#### BusyMs
> *Description*: busy waiting for a specified number of milliseconds
```c
static void BusyMs(unsigned ms);

```

#### vTask_Jitter
> *Description*: jitter measurement task
```c
void vTask_Jitter(void *pvParameters);

```

#### vTask_Generic
> *Description*: generic periodic task
```c
void vTask_Generic(void *pvParameters);

```

#### vTask_Aperiodic
> *Description*: generic aperiodic task
```c
void vTask_Aperiodic(void *pvParameters);

```

#### vDeleteTestTasks
> *Description*: deletes all tasks related to test functions
```c
void vDeleteTestTasks(void);

```

#### xTest_EdgeMinimalTimeGap
> *Description*: test Minimal Time Gap, check if there aren't collisions
```c
static TestResult_t xTest_EdgeMinimalTimeGap(void);

```

#### xTest_PreemptionHigher
> *Description*: checking tasks preemption, a higher priority task must run more times than a lower priority task
```c
static TestResult_t xTest_PreemptionHigher(void);

```

#### xTest_CPUOverhead2
> *Description*: CPU overhead test with variable overload
```c
static TestResult_t xTest_CPUOverhead2(void);

```

#### xTest_CPUOverhead_WithAperiodic
> *Description*: CPU overhead with periodic and aperiodic tasks, 6 periodic + polling server, overhead less than or equal to 10%
```c
static TestResult_t xTest_CPUOverhead_WithAperiodic(void);

```

#### vQEMUExit
> *Description*: exit via semihosting ARM
```c
void vQEMUExit(void);

```

#### xTest_ReleaseJitter
> *Description*: test about jitter requirement, release jitter must be less than or equal to 1 ticks
```c
static TestResult_t xTest_ReleaseJitter(void);

```

#### xTest_DeadlineMiss
> *Description*: verifying task deadline miss
```c
static TestResult_t xTest_DeadlineMiss(void);

```

#### xTest_OverrunPolicySkip
> *Description*: SKIP overrun policy test, Overrun policy that must be used: SKIP
```c
static TestResult_t xTest_OverrunPolicySkip(void);

```

#### xTest_OverrunPolicyCatchup
> *Description*: CATCH UP overrun policy test, Overrun policy that must be used: CATCH UP
```c
static TestResult_t xTest_OverrunPolicyCatchup(void);

```

#### xTest_OverrunPolicyKill
> *Description*: KILL overrun policy test, Overrun policy that must be used: KILL
```c
static TestResult_t xTest_OverrunPolicyKill(void);

```

#### xTest_RoundRobin
> *Description*: the algorithm used should be RR
```c
static TestResult_t xTest_RoundRobin(void);

```

#### xTest_CPUOverhead
> *Description*: CPU overhead measurement test, verifying that overhead is less that 10% CPU (less than or equal to 8 tasks)
```c
static TestResult_t xTest_CPUOverhead(void);

```

#### xTest_AperiodicServer
> *Description*: basic aperiodic task execution, all tasks must be executed
```c
static TestResult_t xTest_AperiodicServer(void);

```

#### xTest_AperiodicKill
> *Description*: aperiodic task KILL Policy, task must be terminated
```c
static TestResult_t xTest_AperiodicKill(void);

```

#### xTest_AperiodicOverrun
> *Description*: aperiodic task OVERRUN Policy, task should continue
```c
static TestResult_t xTest_AperiodicOverrun(void);

```

#### xTest_ReleaseJitter_WithAperiodic
> *Description*: jitter with polling server, verifying jitter
```c
static TestResult_t xTest_ReleaseJitter_WithAperiodic(void);

```

#### xTest_AllPolicies
> *Description*: complete system stress test, periodic + aperiodic
```c
static TestResult_t xTest_AllPolicies(void);

```

#### xTest_OverrunPolicies_GlobalStress
> *Description*: function for all overrun policies stress test, 3 periodic tasks (1 per policy)
```c
static TestResult_t xTest_OverrunPolicies_GlobalStress(void);

```

#### vStopScheduler
> *Description*: trigger vDeleteTestTasks
```c
void vStopScheduler(void);

```

#### LoggerResetIdleTime
> *Description*: reset idle time tick
```c
void LoggerResetIdleTime(void);

```

#### xTest_AperiodicQueueFull
> *Description*: check if the aperiodic queue is full
```c
static TestResult_t xTest_AperiodicQueueFull(void);

```

#### xTest_AperiodicWithPeriodicTasks
> *Description*: test aperiodic tasks with periodic tasks
```c
static TestResult_t xTest_AperiodicWithPeriodicTasks(void);

```

#### xTest_ConfigBasic
> *Description*: check basic config
```c
static TestResult_t xTest_ConfigBasic(void);

```

#### xTest_ConfigPriorities
> *Description*: verify priority ordering is respected
```c
static TestResult_t xTest_ConfigPriorities(void);

```

#### xTest_ConfigMaxTasks
> *Description*: verifying schedulability
```c
static TestResult_t xTest_ConfigMaxTasks(void);

```

#### xTest_ConfigVsDynamic
> *Description*: compare synchronization of config vs dynamic
```c
static TestResult_t xTest_ConfigVsDynamic(void);

```

#### xLoggerGetFirstEventTime
> *Description*: get timestamp of first event of a certain type
```c
TickType_t xLoggerGetFirstEventTime(const char *taskname, LoggerEventType_t type);

```

#### ulLoggerCountEvent
> *Description*: count logger events
```c
uint32_t ulLoggerCountEvent(const char *taskname, LoggerEventType_t type);

```

#### xLoggerHasEvent
> *Description*: check for availability of an event of that type among logger events
```c
BaseType_t xLoggerHasEvent(LoggerEventType_t type);

```

#### ulLoggerGetIdleTime
> *Description*: get idle time tick
```c
uint32_t ulLoggerGetIdleTime(void);

```
