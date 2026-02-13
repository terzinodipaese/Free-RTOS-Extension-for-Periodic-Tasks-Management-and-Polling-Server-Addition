/**
 * @file test_suite.c
 * @brief Test suite for aperiodic and periodic task scheduler
 */

#include "test_suite.h"
#include "logger.h"
#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* configuration */

#define configMAX_TEST_TASKS 10
#define configJITTER_MAX_TASKS 4
#define configTEST_DELAY_MS 800

/* Event group */

static EventGroupHandle_t xTestEventGroup = NULL;
#define TEST_RUNNING_BIT (1 << 0)
#define TEST_CLEANUP_BIT (1 << 1)

/* globals */

static TaskHandle_t xTestTasks[configMAX_TEST_TASKS] = {0};
static uint32_t ulTestTaskCount = 0;

/* types */

typedef struct {
    TickType_t last_release;
    uint32_t max_jitter;
    uint32_t releases;
    TickType_t expected_period;
    bool initialized;
} JitterTaskData_t;

static JitterTaskData_t xJitterData[configJITTER_MAX_TASKS] = {0}; 

typedef struct {
    char pcName[32];
    uint32_t ms;
} AperiodicTaskData_t;


/* Synchronization */

static void vTestEventGroupInit(void)
{
    if(xTestEventGroup == NULL)
    {
        xTestEventGroup = xEventGroupCreate();
        configASSERT(xTestEventGroup != NULL);
    }
    xEventGroupClearBits(xTestEventGroup, TEST_RUNNING_BIT | TEST_CLEANUP_BIT);
}

static void vTestStart(void)
{
    xEventGroupSetBits(xTestEventGroup, TEST_RUNNING_BIT);
    xEventGroupClearBits(xTestEventGroup, TEST_CLEANUP_BIT);
}

static void vTestStop(void)
{
    xEventGroupClearBits(xTestEventGroup, TEST_RUNNING_BIT);
    xEventGroupSetBits(xTestEventGroup, TEST_CLEANUP_BIT);
}

/**
 * @brief Check if test is running
 * @return true if test is running, false otherwise
 */
static bool bIsTestRunning(void)
{
    EventBits_t bits = xEventGroupGetBits(xTestEventGroup);
    return (bits & TEST_RUNNING_BIT) != 0;
}

/**
 * @brief Check if test cleanup is requested
 * @return true if cleanup is requested, false otherwise
 */
static bool bIsCleanupSignaled(void)
{
    EventBits_t bits = xEventGroupGetBits(xTestEventGroup);
    return (bits & TEST_CLEANUP_BIT) != 0;
}

/* Busy-wait delay */

static void BusyMs(uint32_t ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t delay = pdMS_TO_TICKS(ms);
    
    while((xTaskGetTickCount() - start) < delay)
    {
        if(bIsCleanupSignaled()) break;
        __asm volatile("nop");
    }
}

/**
 * @brief Jitter measurement task
 * @param pvParameters Packed parameter containing task ID and period
 */
void vTask_Jitter(void *pvParameters)
{
    uint32_t packed = (uint32_t)(uintptr_t)pvParameters;
    uint8_t id = (packed >> 16) & 0xFF;
    TickType_t xPeriodTicks = pdMS_TO_TICKS(packed & 0xFFFF);

    if(id >= configJITTER_MAX_TASKS) return;
    
    TickType_t xNow = xTaskGetTickCount(); /* timestamp */
    JitterTaskData_t *d = &xJitterData[id];

    if(d->initialized){
        TickType_t xDelta = xNow - d->last_release; /* it must be equal to the period */
        TickType_t jitter = (xDelta > xPeriodTicks) ? (xDelta - xPeriodTicks) : (xPeriodTicks - xDelta);
        
        if(jitter > d->max_jitter) 
            d->max_jitter = jitter;
        
        d->releases++;
    }else{
        d->expected_period = xPeriodTicks;
        d->initialized = true;
    }

    d->last_release = xNow;

    BusyMs(1); 
}

/**
 * @brief Generic periodic task
 * @param pvParameters Execution time in milliseconds (as uint32_t)
 */
void vTask_Generic(void *pvParameters)
{
    uint32_t exec_ms = (uint32_t)(uintptr_t)pvParameters;
    
    if(!bIsTestRunning()) return;
    
    BusyMs(exec_ms);

}

/**
 * @brief Generic aperiodic task
 * @param pvParameters Pointer to AperiodicTaskData_t structure
 */
void vTask_Aperiodic(void *pvParameters)
{
    AperiodicTaskData_t *pParam = (AperiodicTaskData_t *)pvParameters;

    if(!bIsTestRunning()) return;
    
    //vLoggerStore(pParam->pcName, LOGGER_TASK_START, 0);
    BusyMs(pParam->ms);
    //vLoggerStore(pParam->pcName, LOGGER_TASK_END, 0);
}

/* Cleanup functions */

void vDeleteTestTasks(void)
{
    for(uint8_t i = 0; i < configMAX_TEST_TASKS; i++)
    {
        if(xTestTasks[i] != NULL)
        {
            vTaskDeletePeriodic(xTestTasks[i]);
            xTestTasks[i] = NULL;
        }
    }
    ulTestTaskCount = 0;
}

/**
 * @brief Stop scheduler and cleanup test resources
 */
void vStopScheduler(void)
{
    vTestStop();
    vTaskDelay(pdMS_TO_TICKS(50));
    vDeleteTestTasks(); 
    memset(xJitterData, 0, sizeof(xJitterData));
    vLoggerInit();
}

/* PERIODIC TASK TESTS - PROJECT 2 */

/**
 * @brief Test 1: Stress test with 8 overlapping HRT tasks
 * @details Check if there aren't any deadline miss
 */
static TestResult_t xTest_StressOverlappingHRT(void)
{
    TestResult_t res = {.passed = false, .details = ""};

    vTestStart();

    xTaskCreatePeriodic(vTask_Generic, "T10", 512, (void*)2,
        pdMS_TO_TICKS(10), pdMS_TO_TICKS(10), tskIDLE_PRIORITY+3, &xTestTasks[0], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "T20", 512, (void*)3,
        pdMS_TO_TICKS(20), pdMS_TO_TICKS(20), tskIDLE_PRIORITY+3, &xTestTasks[1], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "T30", 512, (void*)4,
        pdMS_TO_TICKS(30), pdMS_TO_TICKS(30), tskIDLE_PRIORITY+3, &xTestTasks[2], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "T40", 512, (void*)4,
        pdMS_TO_TICKS(40), pdMS_TO_TICKS(40), tskIDLE_PRIORITY+3, &xTestTasks[3], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "T50", 512, (void*)5,
        pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+3, &xTestTasks[4], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "T60", 512, (void*)5,
        pdMS_TO_TICKS(60), pdMS_TO_TICKS(60), tskIDLE_PRIORITY+3, &xTestTasks[5], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "T70", 512, (void*)5,
        pdMS_TO_TICKS(70), pdMS_TO_TICKS(70), tskIDLE_PRIORITY+3, &xTestTasks[6], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "T80", 512, (void*)5,
        pdMS_TO_TICKS(80), pdMS_TO_TICKS(80), tskIDLE_PRIORITY+3, &xTestTasks[7], POLICY_SKIP);


    ulTestTaskCount = 8;

    vTaskDelay(pdMS_TO_TICKS(400));
    vTestStop();

    uint32_t deadline_misses = 0;
    for(uint8_t i = 0; i < 8; i++){
        char name[8];
        snprintf(name, sizeof(name), "T%u0", (i+1)*10);
        deadline_misses += ulLoggerCountEvent(name, LOGGER_TASK_DEADLINE_MISS);
    }

    if(deadline_misses == 0){
        res.passed = true;
    }else{
        snprintf(res.details, sizeof(res.details), "Deadline misses: %lu", deadline_misses);
    }
    

    return res;
}

/**
 * @brief Test 2: Test Minimal Time Gap
 * @details Check if there aren't collisions
 */
static TestResult_t xTest_EdgeMinimalTimeGap(void)
{
    TestResult_t res = {.passed = false, .details = ""};

    vTestStart();

    xTaskCreatePeriodic(vTask_Generic, "T_A", 512, (void*)4,
    pdMS_TO_TICKS(10), pdMS_TO_TICKS(10), tskIDLE_PRIORITY+2, &xTestTasks[0], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "T_B", 512, (void*)5,
    pdMS_TO_TICKS(11), pdMS_TO_TICKS(11), tskIDLE_PRIORITY+2, &xTestTasks[1], POLICY_SKIP);

   
    ulTestTaskCount = 2;

    vTaskDelay(pdMS_TO_TICKS(200));
    vTestStop();
    vTaskDelay(pdMS_TO_TICKS(10));

    uint32_t TA_misses = ulLoggerCountEvent("T_A", LOGGER_TASK_DEADLINE_MISS);
    uint32_t TB_misses = ulLoggerCountEvent("T_B", LOGGER_TASK_DEADLINE_MISS);
    uint32_t deadline_misses = TA_misses + TB_misses;
    
    if(deadline_misses == 0){
        res.passed = true;
    }else{
        snprintf(res.details, sizeof(res.details), 
                "Deadline misses: %lu, T_A: %lu, T_B: %lu", 
                deadline_misses, TA_misses, TB_misses);
    }

    return res;
}

/**
 * @brief Test 3: checking tasks preemption
 * @details A higher priority task must run more times than a lower priority task
 */
static TestResult_t xTest_PreemptionHigher(void)
{
    TestResult_t res = {.passed = false, .details = ""};

    vTestStart();

    xTaskCreatePeriodic(vTask_Generic, "T_Low", 512, (void*)15,
        pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+1, &xTestTasks[0], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "T_High", 512, (void*)6,
        pdMS_TO_TICKS(10), pdMS_TO_TICKS(10), tskIDLE_PRIORITY+4, &xTestTasks[1], POLICY_SKIP);


    ulTestTaskCount = 2;

    vTaskDelay(pdMS_TO_TICKS(150));
    vTestStop();

    uint32_t high_starts = ulLoggerCountEvent("T_High", LOGGER_TASK_START);
    uint32_t low_starts  = ulLoggerCountEvent("T_Low", LOGGER_TASK_START);
    uint32_t high_misses = ulLoggerCountEvent("T_High", LOGGER_TASK_DEADLINE_MISS);

    if(high_starts > low_starts && high_starts > 0 && low_starts > 0 && high_misses == 0){
        res.passed = true;        
    }else if(high_starts <= low_starts){
        snprintf(res.details, sizeof(res.details),"Preemption failed: T_High=%lu, T_Low= %lu", high_starts, low_starts);
    }else if(high_misses != 0){
        snprintf(res.details, sizeof(res.details), "T_High missed: %lu", high_misses);
    }else{
        snprintf(res.details, sizeof(res.details), "High starts and/or low starts are equal to 0");
    }

    return res;
}

/**
 * @brief Test 4: Test about jitter requirement 
 * @details Release jitter must be ≤ 1 ticks
 */
static TestResult_t xTest_ReleaseJitter(void)
{
    TestResult_t res = {.passed = false, .details = ""};

    memset(xJitterData, 0, sizeof(xJitterData));

    vTestStart();

    uint32_t p0 = (0 << 16) | 20;
    uint32_t p1 = (1 << 16) | 30;
    uint32_t p2 = (2 << 16) | 50;
    uint32_t p3 = (3 << 16) | 100;

    xTaskCreatePeriodic(vTask_Jitter, "J0", 512, (void*)(uintptr_t)p0,
        pdMS_TO_TICKS(20), pdMS_TO_TICKS(20), tskIDLE_PRIORITY+2, &xTestTasks[0], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Jitter, "J1", 512, (void*)(uintptr_t)p1,
        pdMS_TO_TICKS(30), pdMS_TO_TICKS(30), tskIDLE_PRIORITY+2, &xTestTasks[1], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Jitter, "J2", 512, (void*)(uintptr_t)p2,
        pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2, &xTestTasks[2], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Jitter, "J3", 512, (void*)(uintptr_t)p3,
        pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+2, &xTestTasks[3], POLICY_SKIP);

    ulTestTaskCount = 4;

    vTaskDelay(pdMS_TO_TICKS(600));
    vTestStop();

    bool all_ok = true;
    char details[128] = "";

    for(int i = 0; i < 4; i++)
    {
        char tmp[48];
        snprintf(tmp, sizeof(tmp), "J%d rel:%lu jit:%lu | ",
                 i, xJitterData[i].releases, xJitterData[i].max_jitter);
        strcat(details, tmp);

        if(xJitterData[i].releases < 3 || xJitterData[i].max_jitter >= 2)
            all_ok = false; 
    }

    res.passed = all_ok;
    strncpy(res.details, details, sizeof(res.details) - 1);

    return res;
}

/**
 * @brief Test 5: Task deadline miss
 * @details Verifying task deadline miss
 */
static TestResult_t xTest_DeadlineMiss(void)
{
    TestResult_t res = {.passed = false, .details = ""};

    vTestStart();

    xTaskCreatePeriodic(vTask_Generic, "DM", 512, (void*)15,
        pdMS_TO_TICKS(10), pdMS_TO_TICKS(10), tskIDLE_PRIORITY+3, &xTestTasks[0], POLICY_SKIP);

    ulTestTaskCount = 1;

    vTaskDelay(pdMS_TO_TICKS(200));
    vTestStop();

    uint32_t misses = ulLoggerCountEvent("DM", LOGGER_TASK_DEADLINE_MISS);
    
    if(misses > 0){
        res.passed = true;
    }else{
        snprintf(res.details, sizeof(res.details), "No deadline miss detected");
    }

    return res;
}

/**
 * @brief Test 6: SKIP overrun policy test
 * @details Overrun policy that must be used: SKIP
 */
static TestResult_t xTest_OverrunPolicySkip(void)
{
    TestResult_t res = {.passed = false, .details = ""};
    
    vTestStart();

    xTaskCreatePeriodic(vTask_Generic, "Skip", 512, (void*)15, 
            pdMS_TO_TICKS(10), pdMS_TO_TICKS(10), tskIDLE_PRIORITY+2, &xTestTasks[0], POLICY_SKIP);

    ulTestTaskCount = 1; 

    vTaskDelay(pdMS_TO_TICKS(200));
    vTestStop();

    uint32_t skips = ulLoggerCountEvent("Skip", LOGGER_TASK_OVERRUN_SKIP);
    uint32_t kills = ulLoggerCountEvent("Skip", LOGGER_TASK_OVERRUN_KILL);
    uint32_t catchups = ulLoggerCountEvent("Skip", LOGGER_TASK_OVERRUN_CATCH_UP);
    
    if(skips >= 10 && kills == 0 && catchups == 0){
        res.passed = true;
    }
    else{
        snprintf(res.details, sizeof(res.details), 
                "Skips: %lu, Kills: %lu, Catch-ups: %lu", skips, kills, catchups);
    }
    
    return res;
}

/**
 * @brief Test 7: CATCH UP overrun policy test
 * @details Overrun policy that must be used: CATCH UP
 */
static TestResult_t xTest_OverrunPolicyCatchup(void)
{
    TestResult_t res = {.passed = false, .details = ""};
    
    vTestStart();

    xTaskCreatePeriodic(vTask_Generic, "CatchUp", 512, (void*)25,
        pdMS_TO_TICKS(20), pdMS_TO_TICKS(20), tskIDLE_PRIORITY+2, &xTestTasks[0], POLICY_CATCH_UP);

    ulTestTaskCount = 1; 

    vTaskDelay(pdMS_TO_TICKS(200));
    vTestStop();

    uint32_t skips = ulLoggerCountEvent("CatchUp", LOGGER_TASK_OVERRUN_SKIP);
    uint32_t kills = ulLoggerCountEvent("CatchUp", LOGGER_TASK_OVERRUN_KILL);
    uint32_t catchups = ulLoggerCountEvent("CatchUp", LOGGER_TASK_OVERRUN_CATCH_UP);
    
    if(skips == 0 && kills == 0 && catchups >= 10){
        res.passed = true;
    }
    else{
        snprintf(res.details, sizeof(res.details),
                "Skips: %lu, Kills: %lu, Catch-ups: %lu", skips, kills, catchups);
    }
    
    return res;
}

/**
 * @brief Test 8: KILL overrun policy test
 * @details Overrun policy that must be used: KILL
 */
static TestResult_t xTest_OverrunPolicyKill(void){
    TestResult_t res = {.passed = false, .details = ""};
    
    vTestStart();

    xTaskCreatePeriodic(vTask_Generic, "Kill", 512, (void*)25,
        pdMS_TO_TICKS(20), pdMS_TO_TICKS(20), tskIDLE_PRIORITY+2, &xTestTasks[0], POLICY_KILL);
    
    ulTestTaskCount = 1; 

    vTaskDelay(pdMS_TO_TICKS(200));
    vTestStop();

    uint32_t skips = ulLoggerCountEvent("Kill", LOGGER_TASK_OVERRUN_SKIP);
    uint32_t kills = ulLoggerCountEvent("Kill", LOGGER_TASK_OVERRUN_KILL);
    uint32_t catchups = ulLoggerCountEvent("Kill", LOGGER_TASK_OVERRUN_CATCH_UP);
    
    if(skips == 0 && kills >= 10 && catchups == 0)
    {
        res.passed = true;
    }else{
        snprintf(res.details, sizeof(res.details), 
                "Skips: %lu, Kills: %lu, Catch-ups: %lu", skips, kills, catchups);
    }
    
    return res;
}

/**
 * @brief Test 9: Round-robin scheduling
 * @details The algorithm used should be RR
 */
static TestResult_t xTest_RoundRobin(void)
{
    TestResult_t res = {.passed = false, .details = ""};
    
    vTestStart();
    
    /* Two tasks with same priority and same period */
    xTaskCreatePeriodic(vTask_Generic, "RR1", 512, (void*)8,
        pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2, &xTestTasks[0], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "RR2", 512, (void*)8,
        pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2, &xTestTasks[1], POLICY_SKIP);

    ulTestTaskCount = 2;

    vTaskDelay(pdMS_TO_TICKS(300));
    vTestStop();

    uint32_t rr1_starts = ulLoggerCountEvent("RR1", LOGGER_TASK_START);
    uint32_t rr2_starts = ulLoggerCountEvent("RR2", LOGGER_TASK_START);

    /* They should run multiple times */
    if(rr1_starts >= 3 && rr2_starts >= 3){
        res.passed = true;
    }else{
        snprintf(res.details, sizeof(res.details), "They should run more - RR1: %lu, RR2: %lu", rr1_starts, rr2_starts);
    }
    
    return res;    
}

/**
 * @brief Test 10: CPU overhead measurement
 * @details Verifying that Overhead: ≤ 10% CPU (≤8 tasks)
 */
static TestResult_t xTest_CPUOverhead(void)
{
    TestResult_t res = {.passed = false, .details = ""};

    vLoggerResetIdleTime();
    
    vTestStart();

    xTaskCreatePeriodic(vTask_Generic, "C1", 512, (void*)1,
        pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[0], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "C2", 512, (void*)1,
        pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[1], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "C3", 512, (void*)1,
        pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[2], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "C4", 512, (void*)1,
        pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[3], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "C5", 512, (void*)1,
        pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[4], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "C6", 512, (void*)1,
        pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[5], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "C7", 512, (void*)1,
        pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[6], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "C8", 512, (void*)1, 
        pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[7], POLICY_SKIP);
    
    ulTestTaskCount = 8;

    TickType_t start = xTaskGetTickCount();
    vTaskDelay(pdMS_TO_TICKS(1000));
    TickType_t end = xTaskGetTickCount();


    vTestStop();

    /* CPU usage computation */
    uint32_t idle_ticks = ulLoggerGetIdleTime();
    uint32_t total_ticks = end - start;

    if(total_ticks ==0)  total_ticks = 1;

    /* expected 8% */
    uint32_t cpu_busy = 100 - ((idle_ticks * 100) / total_ticks);
    uint32_t overhead = (cpu_busy > 8) ? (cpu_busy - 8) : 0;

    if(overhead <= 10){
        res.passed = true;
    }
    else{
        snprintf(res.details, sizeof(res.details), 
                "CPU overhead exceeds the limit, actual: %lu%%", overhead);
    }

    
    return res;
}

/* APERIODIC TASK TESTS */

/**
 * @brief Test 11: Basic aperiodic task execution
 * @details All tasks must be executed
 */
// static TestResult_t xTest_AperiodicServer(void)
// {
//     TestResult_t res = {.passed = false, .details = ""};

//     vTestStart();

//     xCreatePollingServer(pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2);

//     static AperiodicTaskData_t aTasks[4] = {
//         {"APER_1", 5},
//         {"APER_2", 5},
//         {"APER_3", 5},
//         {"APER_4", 5}
//     };

//     xTaskCreateAperiodic(vTask_Aperiodic, &aTasks[0], pdMS_TO_TICKS(30), APERIODIC_POLICY_OVERRUN);
//     vTaskDelay(pdMS_TO_TICKS(10));
//     xTaskCreateAperiodic(vTask_Aperiodic, &aTasks[1], pdMS_TO_TICKS(30), APERIODIC_POLICY_OVERRUN);
//     vTaskDelay(pdMS_TO_TICKS(10));
//     xTaskCreateAperiodic(vTask_Aperiodic, &aTasks[2], pdMS_TO_TICKS(30), APERIODIC_POLICY_OVERRUN);
//     vTaskDelay(pdMS_TO_TICKS(10));
//     xTaskCreateAperiodic(vTask_Aperiodic, &aTasks[3], pdMS_TO_TICKS(30), APERIODIC_POLICY_OVERRUN);
    
//     vTaskDelay(pdMS_TO_TICKS(200));
//     vTestStop();

//     uint32_t starts = 0, ends = 0;

//     for(int i = 0; i < 4; i++)
//     {
//         if(xLoggerHasEvent(LOGGER_TASK_START)){
//             if(ulLoggerCountEvent(aTasks[i].pcName, LOGGER_TASK_START) > 0) starts++;
//         }
//         if(xLoggerHasEvent(LOGGER_TASK_END)){
//             if(ulLoggerCountEvent(aTasks[i].pcName, LOGGER_TASK_END) > 0) ends++;
//         }
//     }
    
//     if(starts >= 3 && ends >= 3){
//         res.passed = true;
//     }
//     else{
//         snprintf(res.details, sizeof(res.details), "Starts: %lu, Ends: %lu", starts, ends);
//     }

//     return res;
// }

// /**
//  * @brief Test 12: Aperiodic task KILL Policy
//  * @details Task must be terminated 
//  */
// static TestResult_t xTest_AperiodicKill(void)
// {
//     TestResult_t res = {.passed = false, .details = ""};
    
//     vTestStart();

//     xCreatePollingServer(pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+2);

//     static AperiodicTaskData_t aTask = {"APER_KILL", 40};
//     xTaskCreateAperiodic(vTask_Aperiodic, &aTask, pdMS_TO_TICKS(20), APERIODIC_POLICY_KILL);
    
//     vTaskDelay(pdMS_TO_TICKS(200));
//     vTestStop();

//     uint32_t misses = ulLoggerCountEvent("APER_KILL", LOGGER_TASK_DEADLINE_MISS);
//     uint32_t ends = ulLoggerCountEvent("APER_KILL", LOGGER_TASK_END);

//     if(misses >= 1 && ends == 0){
//         res.passed = true;
//     }
//     else if(misses >= 1 && ends >= 1){
//         snprintf(res.details, sizeof(res.details), "Task terminated");
//     }
//     else if(misses == 0){
//         snprintf(res.details, sizeof(res.details), "Overrun didn't happen");
//     }

//     return res;
// }

// /**
//  * @brief Test 13: Aperiodic task OVERRUN Policy
//  * @details Task should continue 
//  */
// static TestResult_t xTest_AperiodicOverrun(void)
// {
//     TestResult_t res = {.passed = false, .details = ""};
    
//     vTestStart();

//     xCreatePollingServer(pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+2);

//     static AperiodicTaskData_t aTask = {"APER_OVERRUN", 40};
//     xTaskCreateAperiodic(vTask_Aperiodic, &aTask, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN);
    
//     vTaskDelay(pdMS_TO_TICKS(200));
//     vTestStop();

//     uint32_t misses = ulLoggerCountEvent("APER_OVERRUN", LOGGER_TASK_DEADLINE_MISS);
//     uint32_t ends = ulLoggerCountEvent("APER_OVERRUN", LOGGER_TASK_END);

//     if(misses >= 1 && ends >= 1){
//         res.passed = true;
//     }else if(misses >= 1 && ends == 0){
//         snprintf(res.details, sizeof(res.details), "Task didn't terminate");
//     }else if(misses == 0){
//         snprintf(res.details, sizeof(res.details), "Overrun didn't happen");
//     }
    
//     return res;
// }

// /**
//  * @brief Test 14: Queue full handling
//  * @details Check if the queue is full
//  */
// static TestResult_t xTest_AperiodicQueueFull(void)
// {
//     TestResult_t res = {.passed = false, .details = ""};
//     bool queue_full = false;

//     vTestStart();

//     xCreatePollingServer(pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2);

//     static AperiodicTaskData_t aTasks[11];
//     static char names[11][12];

//     for(int i = 0; i < 11; i++)
//     {
//         snprintf(names[i], sizeof(names[i]), "APER_Q%d", i);
//         strncpy(aTasks[i].pcName, names[i], sizeof(aTasks[i].pcName) - 1);
//         aTasks[i].pcName[sizeof(aTasks[i].pcName) - 1] = '\0';
//         aTasks[i].ms = 5;

//         BaseType_t result = xTaskCreateAperiodic(vTask_Aperiodic, &aTasks[i], pdMS_TO_TICKS(50), APERIODIC_POLICY_OVERRUN);
//         if(result != pdPASS){
//             queue_full = true;
//             break;
//         }
//     }

//     vTaskDelay(pdMS_TO_TICKS(2000));
//     vTestStop();

//     if(queue_full){
//         res.passed = true;
//     }
//     else{
//         snprintf(res.details, sizeof(res.details), "Queue full not detected");
//     }

//     return res;
// }

// /**
//  * @brief Test 15: Aperiodic tasks with periodic tasks
//  * @details Periodic tasks must run more times
//  */
// static TestResult_t xTest_AperiodicWithPeriodicTasks(void)
// {
//     TestResult_t res = {.passed = false, .details = ""};
    
//     vTestStart();

//     xTaskCreatePeriodic(vTask_Generic, "PER_1", 512, (void*)5, 
//                        pdMS_TO_TICKS(30), pdMS_TO_TICKS(30), 
//                        tskIDLE_PRIORITY+4, &xTestTasks[0], POLICY_SKIP);
//     xTaskCreatePeriodic(vTask_Generic, "PER_2", 512, (void*)5, 
//                        pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), 
//                        tskIDLE_PRIORITY+3, &xTestTasks[1], POLICY_SKIP);

//     xCreatePollingServer(pdMS_TO_TICKS(40), pdMS_TO_TICKS(40), tskIDLE_PRIORITY+2);
    
//     ulTestTaskCount = 2;

//     static AperiodicTaskData_t aTask = {"APER_MIX", 5};
    
//     for(int i = 0; i < 5; i++){
//         xTaskCreateAperiodic(vTask_Aperiodic, &aTask, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN);
//         vTaskDelay(pdMS_TO_TICKS(60));
//     }
    
//     vTaskDelay(pdMS_TO_TICKS(100));
//     vTestStop();

//     uint32_t per1_starts = ulLoggerCountEvent("PER_1", LOGGER_TASK_START);
//     uint32_t per2_starts = ulLoggerCountEvent("PER_2", LOGGER_TASK_START);
//     uint32_t aper_starts = ulLoggerCountEvent("APER_MIX", LOGGER_TASK_START);
//     uint32_t per1_misses = ulLoggerCountEvent("PER_1", LOGGER_TASK_DEADLINE_MISS);

//     if(per1_starts >= 8 && per2_starts >= 5 && aper_starts >= 3 && per1_misses == 0){
//         res.passed = true;
//     }else{
//         snprintf(res.details, sizeof(res.details), 
//                 "PER1: %lu (exp. 8), PER2: %lu (exp. 5), APER: %lu (exp. 3), PER1 miss: %lu (exp. 0)",
//                 per1_starts, per2_starts, aper_starts, per1_misses);
//     }

//     return res;
// }

/* CFG TASK TESTS */



/**
 * @brief Test 16: cfg basic configuration
 * @details Check config
 */
static TestResult_t xTest_ConfigBasic(void)
{
    TestResult_t res = {.passed = false, .details = ""};

    vTestStart();

    static PeriodicTaskConfig_t cfgTasks[] = {
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "CfgA",
            .pvParameters = (void*)5,
            .usStackDepth = 1024,
            .uxPriority = tskIDLE_PRIORITY+3,
            .xPeriod = pdMS_TO_TICKS(20),
            .pxCreatedTask = &xTestTasks[0],
            .xDeadline = pdMS_TO_TICKS(20),
            
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "CfgB",
            .pvParameters = (void*)8,
            .usStackDepth = 1024,
            .uxPriority = tskIDLE_PRIORITY+2,
            .xPeriod = pdMS_TO_TICKS(30),
            .pxCreatedTask = &xTestTasks[1],
            .xDeadline = pdMS_TO_TICKS(30),
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "CfgC",
            .pvParameters = (void*)10,
            .usStackDepth = 1024,
            .uxPriority = tskIDLE_PRIORITY+2,
            .xPeriod = pdMS_TO_TICKS(50),
            .pxCreatedTask = &xTestTasks[2],
            .xDeadline = pdMS_TO_TICKS(50),
            .xTaskPolicy = POLICY_SKIP
        }
    };
    
    SchedulerConfig_t cfg = {
        .globalPolicy = POLICY_SKIP,
        .max_tasks = 20,
        .trace_enabled = pdTRUE,
        .uxNumTasks = 3,
        .pxTasks = cfgTasks
    };
    
    vConfigureScheduler(&cfg);
    vTaskDelay(pdMS_TO_TICKS(300));
    vTestStop();
    
    uint32_t a_runs = ulLoggerCountEvent("CfgA", LOGGER_TASK_START);
    uint32_t b_runs = ulLoggerCountEvent("CfgB", LOGGER_TASK_START);
    uint32_t c_runs = ulLoggerCountEvent("CfgC", LOGGER_TASK_START);
    
    if(a_runs >= 10 && b_runs >= 8 && c_runs >= 5){
        res.passed = true;
    }
    else{
        snprintf(res.details, sizeof(res.details), 
                "CfgA: %lu (exp. ≈15), CfgB: %lu (exp. ≈10), CfgC: %lu (exp. ≈6) runs",
                a_runs, b_runs, c_runs);
    }

    return res;
}

/**
 * @brief Test 17: Config with different priorities
 * @details Verify priority ordering is respected
 */
static TestResult_t xTest_ConfigPriorities(void)
{
    TestResult_t res = {.passed = false, .details = ""};

    vTestStart();
    
    static PeriodicTaskConfig_t cfgTasks[] = {
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "P1",
            .pvParameters = (void*)5,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+1,
            .xPeriod = pdMS_TO_TICKS(50),
            .pxCreatedTask = &xTestTasks[0],
            .xDeadline = pdMS_TO_TICKS(50),
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "P3",
            .pvParameters = (void*)5,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+3,
            .xPeriod = pdMS_TO_TICKS(50),
            .pxCreatedTask = &xTestTasks[1],
            .xDeadline = pdMS_TO_TICKS(50),
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "P5",
            .pvParameters = (void*)5,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+5,
            .xPeriod = pdMS_TO_TICKS(50),
            .pxCreatedTask = &xTestTasks[2],
            .xDeadline = pdMS_TO_TICKS(50),
            .xTaskPolicy = POLICY_SKIP
        }
    };
    
    SchedulerConfig_t cfg = {
        .globalPolicy = POLICY_SKIP,
        .max_tasks = 20,
        .trace_enabled = pdTRUE,
        .uxNumTasks = 3,
        .pxTasks = cfgTasks
    };
    
    vConfigureScheduler(&cfg);
    vTaskDelay(pdMS_TO_TICKS(200));
    vTestStop();
    
    uint32_t p1_starts = ulLoggerCountEvent("P1", LOGGER_TASK_START);
    uint32_t p3_starts = ulLoggerCountEvent("P3", LOGGER_TASK_START);
    uint32_t p5_starts = ulLoggerCountEvent("P5", LOGGER_TASK_START);
    
    if(p1_starts >= 3 && p3_starts >= 3 && p5_starts >= 3){
        res.passed = true;
    }
    else{
        snprintf(res.details, sizeof(res.details), "P1: %lu, P3: %lu, P5: %lu", p1_starts, p3_starts, p5_starts);
    }

    return res;
}

/**
 * @brief Test 18: Config max tasks
 * @details Verifying schedulability
 */
static TestResult_t xTest_ConfigMaxTasks(void)
{
    TestResult_t res = {.passed = false, .details = ""};

    vTestStart();
    
    static PeriodicTaskConfig_t cfgTasks[] = {
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "M1",
            .pvParameters = (void*)5,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+4,
            .xPeriod = pdMS_TO_TICKS(50),
            .pxCreatedTask = &xTestTasks[0],
            .xDeadline = pdMS_TO_TICKS(50),
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "M2",
            .pvParameters = (void*)8,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+3,
            .xPeriod = pdMS_TO_TICKS(100),
            .pxCreatedTask = &xTestTasks[1],
            .xDeadline = pdMS_TO_TICKS(100),
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "M3",
            .pvParameters = (void*)12,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+3,
            .xPeriod = pdMS_TO_TICKS(150),
            .pxCreatedTask = &xTestTasks[2],
            .xDeadline = pdMS_TO_TICKS(150),
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "M4",
            .pvParameters = (void*)15,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+2,
            .xPeriod = pdMS_TO_TICKS(200),
            .pxCreatedTask = &xTestTasks[3],
            .xDeadline = pdMS_TO_TICKS(200),
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "M5",
            .pvParameters = (void*)18,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+2,
            .xPeriod = pdMS_TO_TICKS(250),
            .pxCreatedTask = &xTestTasks[4],
            .xDeadline = pdMS_TO_TICKS(250),
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "M6",
            .pvParameters = (void*)20,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+2,
            .xPeriod = pdMS_TO_TICKS(300),
            .pxCreatedTask = &xTestTasks[5],
            .xDeadline = pdMS_TO_TICKS(300),
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "M7",
            .pvParameters = (void*)25,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+1,
            .xPeriod = pdMS_TO_TICKS(400),
            .pxCreatedTask = &xTestTasks[6],
            .xDeadline = pdMS_TO_TICKS(400),
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "M8",
            .pvParameters = (void*)30,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+1,
            .xPeriod = pdMS_TO_TICKS(500),
            .pxCreatedTask = &xTestTasks[7],
            .xDeadline = pdMS_TO_TICKS(500),
            .xTaskPolicy = POLICY_SKIP
        }
    };

    SchedulerConfig_t cfg = {
        .globalPolicy = POLICY_SKIP,
        .max_tasks = 20,
        .trace_enabled = pdTRUE,
        .uxNumTasks = 8,
        .pxTasks = cfgTasks
    };

    vConfigureScheduler(&cfg);
    vTaskDelay(pdMS_TO_TICKS(1000));
    vTestStop();
    
    /* Count how many tasks ran and deadline missed */
    uint32_t tasks_ran = 0;
    uint32_t total_misses = 0;
    
    for(int i = 1; i <= 8; i++){
        char name[8];
        snprintf(name, sizeof(name), "M%d", i);
        
        uint32_t runs = ulLoggerCountEvent(name, LOGGER_TASK_START);
        if(runs > 0) tasks_ran++;
        
        uint32_t misses = ulLoggerCountEvent(name, LOGGER_TASK_DEADLINE_MISS);
        total_misses += misses;
    }

    if(tasks_ran == 8 && total_misses == 0){
        res.passed = true;
    }else{
        snprintf(res.details, sizeof(res.details), 
                "Tasks ran: %lu/8, Total misses: %lu", tasks_ran, total_misses);
    }

    return res;
}

/**
 * @brief Test 19: Config vs Dynamic comparison
 * @details Compare synchronization of config vs dynamic 
 */
static TestResult_t xTest_ConfigVsDynamic(void)
{
    TestResult_t res = {.passed = false, .details = ""};

    /* Part 1: Cfg creation */
    vLoggerInit();
    
    static PeriodicTaskConfig_t cfgTasks[] = {
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "D1",
            .pvParameters = (void*)5,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+2,
            .xPeriod = pdMS_TO_TICKS(50),
            .pxCreatedTask = NULL,
            .xDeadline = pdMS_TO_TICKS(50),
            .xTaskPolicy = POLICY_SKIP
        },
        {
            .pxTaskCode = vTask_Generic,
            .pcName = "D2",
            .pvParameters = (void*)5,
            .usStackDepth = 512,
            .uxPriority = tskIDLE_PRIORITY+2,
            .xPeriod = pdMS_TO_TICKS(50),
            .pxCreatedTask = NULL,
            .xDeadline = pdMS_TO_TICKS(50),
            .xTaskPolicy = POLICY_SKIP
        }
    };
    
    SchedulerConfig_t cfg = {
        .globalPolicy = POLICY_SKIP,
        .max_tasks = 20,
        .trace_enabled = pdTRUE,
        .uxNumTasks = 2,
        .pxTasks = cfgTasks
    };
    
    vTestStart();
    vConfigureScheduler(&cfg);
    vTaskDelay(pdMS_TO_TICKS(300));
    
    uint32_t cfg1_runs = ulLoggerCountEvent("D1", LOGGER_TASK_START);
    uint32_t cfg2_runs = ulLoggerCountEvent("D2", LOGGER_TASK_START);
    uint32_t cfg_diff = (cfg1_runs > cfg2_runs) ? (cfg1_runs - cfg2_runs) : (cfg2_runs - cfg1_runs);
    
    vStopScheduler();
    vTestStop();
    vTaskDelay(pdMS_TO_TICKS(50));
    vLoggerInit();
    
    /* Part 2: Dynamic creation */
    vTestStart();
    
    xTaskCreatePeriodic(vTask_Generic, "Dyn1", 512, (void*)5,
        pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2, &xTestTasks[0], POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic, "Dyn2", 512, (void*)5,
        pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2, &xTestTasks[1], POLICY_SKIP);
    
    ulTestTaskCount = 2;
    
    vTaskDelay(pdMS_TO_TICKS(300));
    
    uint32_t dyn1_runs = ulLoggerCountEvent("Dyn1", LOGGER_TASK_START);
    uint32_t dyn2_runs = ulLoggerCountEvent("Dyn2", LOGGER_TASK_START);
    uint32_t dyn_diff = (dyn1_runs > dyn2_runs) ? (dyn1_runs - dyn2_runs) : (dyn2_runs - dyn1_runs);
    
    vTestStop();
    vDeleteTestTasks();

    /* They should work similarly */
    if(cfg1_runs >= 5 && cfg2_runs >= 5 && dyn1_runs >= 5 && dyn2_runs >= 5 && cfg_diff <= 2){
        res.passed = true;
    }else{
        snprintf(res.details, sizeof(res.details),"Cfg: %lu/%lu (diff=%lu), Dyn: %lu/%lu (diff=%lu)",cfg1_runs, cfg2_runs, cfg_diff, dyn1_runs, dyn2_runs, dyn_diff);
    }

    return res;
}

/* TEST SUITE */

static TestDef_t xTests[] = {
    /* Periodic Task Tests (Project 2) */
    {"1. StressTest_OverlappingHRT",       xTest_StressOverlappingHRT},
    {"2. TestEdge_MinimalTimeGap",         xTest_EdgeMinimalTimeGap},
    {"3. Test_PreemptionHigher",           xTest_PreemptionHigher},
    {"4. Test_ReleaseJitter",              xTest_ReleaseJitter},
    {"5. Test_DeadlineMiss",               xTest_DeadlineMiss},
    {"6. Test_OverrunPolicy_SKIP",         xTest_OverrunPolicySkip},
    {"7. Test_OverrunPolicy_KILL",         xTest_OverrunPolicyKill},
    {"8. Test_OverrunPolicy_CATCH_UP",     xTest_OverrunPolicyCatchup},
    {"9. Test_RoundRobin",                 xTest_RoundRobin},
    {"10. Test_CPUOverhead",               xTest_CPUOverhead},

    // /* Aperiodic Task Tests (Project 3) */
    // {"11. Test_AperiodicServer",           xTest_AperiodicServer},
    // {"12. Test_AperiodicKill",             xTest_AperiodicKill},
    // {"13. Test_AperiodicOverrun",          xTest_AperiodicOverrun},
    // {"14. Test_AperiodicQueueFull",        xTest_AperiodicQueueFull},
    // {"15. Test_AperiodicWithPeriodic",     xTest_AperiodicWithPeriodicTasks},

    /* Config Tests */
    {"16. Test_ConfigBasic",               xTest_ConfigBasic},
    {"17. Test_ConfigPriorities",          xTest_ConfigPriorities},
    {"18. Test_ConfigMaxTasks",            xTest_ConfigMaxTasks},
    {"19. Test_ConfigVsDynamic",           xTest_ConfigVsDynamic}
};

#define NUM_TESTS (sizeof(xTests) / sizeof(xTests[0]))

void vTestSuite(void)
{
    uint32_t ulPassed = 0;
    char s[160];

    vTestEventGroupInit();

    for(uint32_t i = 0; i < NUM_TESTS; i++)
    {
        /* Reset logger */
        vLoggerInit();

        TestResult_t result = xTests[i].func();
        vStopScheduler();

        snprintf(s, sizeof(s), "STEL Test %s: %s  %s\r\n", 
                xTests[i].pcName,
                result.passed ? "PASSED" : "FAILED", 
                result.details);

        UART_printf(s);

        if(result.passed) ulPassed++;

        vTaskDelay(pdMS_TO_TICKS(configTEST_DELAY_MS));
    }

    snprintf(s, sizeof(s), "Tests Passed: %lu/%u\r\n", ulPassed, NUM_TESTS);
    UART_printf(s);

    if(xTestEventGroup != NULL){
        vEventGroupDelete(xTestEventGroup);
        xTestEventGroup = NULL;        
    }

    vTaskSuspend(NULL);
}