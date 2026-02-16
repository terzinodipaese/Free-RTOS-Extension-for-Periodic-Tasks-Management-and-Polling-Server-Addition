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
#include <stdlib.h>


/* configuration */


#define configMAX_TEST_TASKS 10
#define configJITTER_MAX_TASKS 4
#define configTEST_DELAY_MS 700


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


/* Synchronization */


static void vTestEventGroupInit(void){


   if(xTestEventGroup == NULL){
       xTestEventGroup = xEventGroupCreate();
       configASSERT(xTestEventGroup != NULL);
   }
  
   xEventGroupClearBits(xTestEventGroup, TEST_RUNNING_BIT | TEST_CLEANUP_BIT);
}


static void vTestStart(void){
   xEventGroupSetBits(xTestEventGroup, TEST_RUNNING_BIT);
   xEventGroupClearBits(xTestEventGroup, TEST_CLEANUP_BIT);
}


static void vTestStop(void){
   xEventGroupClearBits(xTestEventGroup, TEST_RUNNING_BIT);
   xEventGroupSetBits(xTestEventGroup, TEST_CLEANUP_BIT);
}


/**
* @brief Check if test is running
* @return true if test is running, false otherwise
*/
static bool bIsTestRunning(void){
   EventBits_t bits = xEventGroupGetBits(xTestEventGroup);
   return (bits & TEST_RUNNING_BIT) != 0;
}


/**
* @brief Check if test cleanup is requested
* @return true if cleanup is requested, false otherwise
*/
static bool bIsCleanupSignaled(void){
   EventBits_t bits = xEventGroupGetBits(xTestEventGroup);
   return (bits & TEST_CLEANUP_BIT) != 0;
}


/* Busy-wait delay */


static void BusyMs(uint32_t ms){


   TickType_t start = xTaskGetTickCount();
  
   while((xTaskGetTickCount() - start) < pdMS_TO_TICKS(ms)){
      
       if(bIsCleanupSignaled()) break;
       __asm volatile("nop");
   }
  
}


/**
* @brief Jitter measurement task
* @param pvParameters Packed parameter containing task ID and period
*/
void vTask_Jitter(void *pvParameters){


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


void vTask_Generic(void *pvParameters){
   uint32_t exec_ms = (uint32_t)(uintptr_t)pvParameters;
  
   if(!bIsTestRunning()) return;
  
   BusyMs(exec_ms);


}


/**
* @brief Generic aperiodic task
* @param pvParameters Pointer to AperiodicTaskData_t structure
*/
void vTask_Aperiodic(void *pvParameters){


   uint32_t exec_ms = (uint32_t)(uintptr_t)pvParameters;
  
   if(!bIsTestRunning()) return;
  
   BusyMs(exec_ms);
//inutile
}


/* Cleanup functions */


void vDeleteTestTasks(void){


   for(uint8_t i = 0; i < ulTestTaskCount; i++){
       if(xTestTasks[i] != NULL){
           vTaskDeletePeriodic(xTestTasks[i]);
           xTestTasks[i] = NULL;
       }
   }
   ulTestTaskCount = 0;


}


/**
* @brief Stop scheduler and cleanup test resources
*/
void vStopScheduler(void){


   vTestStop();
   vTaskDelay(pdMS_TO_TICKS(50));
   vDeleteTestTasks();
  
}



/* PERIODIC TASK TESTS - PROJECT 2 */


#if (configUSE_PERIODIC_TESTS)


/**
* @brief Test 1: Stress test with 8 overlapping HRT tasks
* @details Check if there aren't any deadline miss for higher priority tasks
*/




static TestResult_t xTest_StressOverlappingHRT(void){


   TestResult_t res = {.passed = false, .details = ""};


   vTestStart();


   xTaskCreatePeriodic(vTask_Generic, "T10", 512, (void*)5,  pdMS_TO_TICKS(10),  pdMS_TO_TICKS(6),  tskIDLE_PRIORITY+8, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "T20", 512, (void*)6,  pdMS_TO_TICKS(20),  pdMS_TO_TICKS(12), tskIDLE_PRIORITY+7, &xTestTasks[1], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "T30", 512, (void*)7,  pdMS_TO_TICKS(25),  pdMS_TO_TICKS(15), tskIDLE_PRIORITY+6, &xTestTasks[2], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "T40", 512, (void*)8,  pdMS_TO_TICKS(40),  pdMS_TO_TICKS(20), tskIDLE_PRIORITY+5, &xTestTasks[3], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "T50", 512, (void*)9,  pdMS_TO_TICKS(50),  pdMS_TO_TICKS(25), tskIDLE_PRIORITY+4, &xTestTasks[4], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "T60", 512, (void*)10, pdMS_TO_TICKS(80),  pdMS_TO_TICKS(30), tskIDLE_PRIORITY+3, &xTestTasks[5], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "T70", 512, (void*)12, pdMS_TO_TICKS(100), pdMS_TO_TICKS(35), tskIDLE_PRIORITY+2, &xTestTasks[6], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "T80", 512, (void*)15, pdMS_TO_TICKS(200), pdMS_TO_TICKS(60),tskIDLE_PRIORITY+1, &xTestTasks[7], POLICY_SKIP);


   ulTestTaskCount = 8;


   vTaskDelay(pdMS_TO_TICKS(2000));
   vTestStop();


   uint32_t high_misses = 0;
   uint32_t low_misses = 0;
   uint32_t critical_misses = 0;


   for(uint8_t i = 0; i < 8; i++){
       char name[8];
       snprintf(name, sizeof(name), "T%u0", (i+1)*10);


       uint32_t misses = ulLoggerCountEvent(name, LOGGER_TASK_DEADLINE_MISS);


       if(i == 0) critical_misses = misses;  // T10
       if(i < 3)  high_misses += misses;     // T10, T20, T30
       if(i >= 5) low_misses  += misses;     // T60, T70, T80
   }


   //T10 shouldn't miss
   if(critical_misses == 0 && high_misses <= 4 && low_misses < 20){
       res.passed = true;
   }else{
       snprintf(res.details, sizeof(res.details), "Crit(exp.0):%lu Hi(exp.<5):%lu Low(exp.<20):%lu",critical_misses, high_misses, low_misses);
   }
   return res;
}




/**
* @brief Test 2: Test Minimal Time Gap
* @details Check if there aren't collisions
*/
static TestResult_t xTest_EdgeMinimalTimeGap(void){


   TestResult_t res = {.passed = false, .details = ""};


   vTestStart();


   xTaskCreatePeriodic(vTask_Generic, "TA", 512, (void*)3, pdMS_TO_TICKS(20), pdMS_TO_TICKS(18), tskIDLE_PRIORITY+5, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "TB", 512, (void*)8, pdMS_TO_TICKS(20), pdMS_TO_TICKS(18), tskIDLE_PRIORITY+3, &xTestTasks[1], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "TC", 512, (void*)9, pdMS_TO_TICKS(20), pdMS_TO_TICKS(18), tskIDLE_PRIORITY+2, &xTestTasks[2], POLICY_SKIP);
  
   //it will disturb the other tasks
   xTaskCreatePeriodic(vTask_Generic, "INT", 512, (void*)7, pdMS_TO_TICKS(15), pdMS_TO_TICKS(15), tskIDLE_PRIORITY+6, &xTestTasks[3], POLICY_SKIP);
  
   ulTestTaskCount = 4;
   vTaskDelay(pdMS_TO_TICKS(1500));
   vTestStop();


   uint32_t TA_misses = ulLoggerCountEvent("TA", LOGGER_TASK_DEADLINE_MISS);
   uint32_t TB_misses = ulLoggerCountEvent("TB", LOGGER_TASK_DEADLINE_MISS);
   uint32_t TC_misses = ulLoggerCountEvent("TC", LOGGER_TASK_DEADLINE_MISS);
   uint32_t INT_misses = ulLoggerCountEvent("INT", LOGGER_TASK_DEADLINE_MISS);
  
   uint32_t TA_starts = ulLoggerCountEvent("TA", LOGGER_TASK_START);
   uint32_t TB_starts = ulLoggerCountEvent("TB", LOGGER_TASK_START);


   if(TA_misses == 0 && INT_misses == 0 && (TB_misses + TC_misses) > 5 && TA_starts>=70 && TB_starts > 40)
       res.passed = true;
   else
       snprintf(res.details, sizeof(res.details), "TA(exp.0):%lu TB:%lu TC:%lu INT(exp.0):%lu", TA_misses, TB_misses, TC_misses, INT_misses);
   return res;
}


/**
* @brief Test 3: checking tasks preemption
* @details A higher priority task must run more times than a lower priority task
*/


static TestResult_t xTest_PreemptionHigher(void){


   TestResult_t res = {.passed = false, .details = ""};


   vTestStart();


   xTaskCreatePeriodic(vTask_Generic, "T_Low",  512, (void*)40, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+1, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "T_Med1", 512, (void*)15, pdMS_TO_TICKS(50),  pdMS_TO_TICKS(50),  tskIDLE_PRIORITY+3, &xTestTasks[1], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "T_Med2", 512, (void*)15, pdMS_TO_TICKS(50),  pdMS_TO_TICKS(50),  tskIDLE_PRIORITY+3, &xTestTasks[2], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "T_High", 512, (void*)7,  pdMS_TO_TICKS(10),  pdMS_TO_TICKS(10),  tskIDLE_PRIORITY+6, &xTestTasks[3], POLICY_SKIP);


   ulTestTaskCount = 4;


   vTaskDelay(pdMS_TO_TICKS(200));
   vTestStop();


   uint32_t high_releases = ulLoggerCountEvent("T_High", LOGGER_TASK_RELEASE);
   uint32_t low_releases  = ulLoggerCountEvent("T_Low", LOGGER_TASK_RELEASE);
   uint32_t high_misses = ulLoggerCountEvent("T_High", LOGGER_TASK_DEADLINE_MISS);
   uint32_t med1_releases = ulLoggerCountEvent("T_Med1", LOGGER_TASK_RELEASE);
   uint32_t med2_releases = ulLoggerCountEvent("T_Med2", LOGGER_TASK_RELEASE);




   //medium p. tasks with high starts because it's divided by the preemption of high p.t.


   if(high_releases > (med1_releases+med2_releases+low_releases) && high_releases >= 18 && low_releases > 0 && high_misses == 0 && med1_releases >= 3 && med2_releases  >= 3 ){
       res.passed = true;       
   }else{
       snprintf(res.details, sizeof(res.details), "H(rel exp.>18/misses==0):%lu/%lu M1(>3):%lu M2(>3):%lu L(>0):%lu", high_releases, high_misses, med1_releases, med2_releases, low_releases);
   }


   return res;
}




/**
* @brief Test 5: Task deadline miss
* @details Verifying task deadline miss
*/
static TestResult_t xTest_DeadlineMiss(void){


   TestResult_t res = {.passed = false, .details = ""};


   vTestStart();


   xTaskCreatePeriodic(vTask_Generic, "DM1", 512, (void*)15, pdMS_TO_TICKS(10), pdMS_TO_TICKS(10), tskIDLE_PRIORITY+5, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "DM2", 512, (void*)12, pdMS_TO_TICKS(30), pdMS_TO_TICKS(15), tskIDLE_PRIORITY+3, &xTestTasks[1], POLICY_SKIP);
  
   // Interferer
   xTaskCreatePeriodic(vTask_Generic, "INT", 512, (void*)8,  pdMS_TO_TICKS(20), pdMS_TO_TICKS(20), tskIDLE_PRIORITY+6, &xTestTasks[2], POLICY_SKIP);


   ulTestTaskCount = 3;


   vTaskDelay(pdMS_TO_TICKS(300));


   vTestStop();


   uint32_t dm1_misses = ulLoggerCountEvent("DM1", LOGGER_TASK_DEADLINE_MISS);
   uint32_t dm2_misses = ulLoggerCountEvent("DM2", LOGGER_TASK_DEADLINE_MISS);
   uint32_t int_misses = ulLoggerCountEvent("INT", LOGGER_TASK_DEADLINE_MISS);
  
   //int mustn't miss while the other two yes
   if(dm1_misses > 0 && dm2_misses > 0 && int_misses == 0){
       res.passed = true;
   }else{
       snprintf(res.details, sizeof(res.details), "DM1:%lu DM2:%lu INT:%lu", dm1_misses, dm2_misses, int_misses);
   }


   return res;
}


/**
* @brief Test 9: Round-robin scheduling
* @details The algorithm used should be RR
*/
static TestResult_t xTest_RoundRobin(void){
  
   TestResult_t res = {.passed = false, .details = ""};
  
   vTestStart();
  
   xTaskCreatePeriodic(vTask_Generic, "RR1", 512, (void*)8,  pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+3, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "RR2", 512, (void*)8, pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+3, &xTestTasks[1], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "RR3", 512, (void*)8, pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+3, &xTestTasks[2], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "RR4", 512, (void*)8,  pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+3, &xTestTasks[3], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "RR5", 512, (void*)8, pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+3, &xTestTasks[4], POLICY_SKIP);
  
   // High priority interferer
   xTaskCreatePeriodic(vTask_Generic, "INT", 512, (void*)5, pdMS_TO_TICKS(25), pdMS_TO_TICKS(25), tskIDLE_PRIORITY+6, &xTestTasks[5], POLICY_SKIP);


   ulTestTaskCount = 6;


   // U(RR) = (8*5)/50 = 40/50 = 80%
   // U(INT) = 5/25 = 20%
   // U(tot) = 100%
  


   vTaskDelay(pdMS_TO_TICKS(400));
   vTestStop();


   uint32_t rr1_starts = ulLoggerCountEvent("RR1", LOGGER_TASK_START);
   uint32_t rr2_starts = ulLoggerCountEvent("RR2", LOGGER_TASK_START);
   uint32_t rr3_starts = ulLoggerCountEvent("RR3", LOGGER_TASK_START);
   uint32_t rr4_starts = ulLoggerCountEvent("RR4", LOGGER_TASK_START);
   uint32_t rr5_starts = ulLoggerCountEvent("RR5", LOGGER_TASK_START);
  
   uint32_t rr_starts[5] = {rr1_starts,rr2_starts,rr3_starts,rr4_starts,rr5_starts};


   uint32_t rr_max = rr_starts[0];
   uint32_t rr_min = rr_starts[0];


   for(uint16_t i = 1; i < 5; i++){
       if(rr_starts[i] > rr_max) rr_max = rr_starts[i];
       if(rr_starts[i] < rr_min) rr_min = rr_starts[i];
   }
   uint32_t diff = rr_max-rr_min;




   /* They should run multiple times */
   if(rr_min >= 6 && diff <= 20 && rr1_starts > 0 && rr2_starts > 0 && rr3_starts > 0 && rr4_starts > 0 && rr5_starts > 0){
       res.passed = true;
   }else{
       snprintf(res.details, sizeof(res.details), "They should run more - RR1: %lu, RR2: %lu, RR3: %lu, RR4: %lu, RR5: %lu", rr1_starts, rr2_starts, rr3_starts, rr4_starts, rr5_starts);
   }
  
   return res;   
}


/**
* @brief Test 24: All overrun policies stress test
* @details 3 periodic tasks (1 per policy)
*/
static TestResult_t xTest_OverrunPolicies_GlobalStress(void){


   TestResult_t res = {.passed = false, .details = ""};
  
   vTestStart();
   xTaskCreatePeriodic(vTask_Generic, "GS1", 512, (void*)22, pdMS_TO_TICKS(30), pdMS_TO_TICKS(30), tskIDLE_PRIORITY+5, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "GC1", 512, (void*)18, pdMS_TO_TICKS(25), pdMS_TO_TICKS(25), tskIDLE_PRIORITY+6, &xTestTasks[1], POLICY_CATCH_UP);
   xTaskCreatePeriodic(vTask_Generic, "GK1", 512, (void*)20, pdMS_TO_TICKS(20), pdMS_TO_TICKS(20), tskIDLE_PRIORITY+7, &xTestTasks[2], POLICY_KILL);
  
   ulTestTaskCount = 3;


   vTaskDelay(pdMS_TO_TICKS(360));
   vTestStop();
   vTaskDelay(pdMS_TO_TICKS(100));


   uint32_t total_skips = 0, total_catchups = 0, total_kills = 0;
   uint32_t skip_wrong = 0, catchup_wrong = 0, kill_wrong = 0;




   total_skips += ulLoggerCountEvent("GS1", LOGGER_TASK_OVERRUN_SKIP);
   skip_wrong += ulLoggerCountEvent("GS1", LOGGER_TASK_OVERRUN_CATCH_UP) + ulLoggerCountEvent("GS1", LOGGER_TASK_OVERRUN_KILL);
  
   total_catchups += ulLoggerCountEvent("GC1", LOGGER_TASK_OVERRUN_CATCH_UP);
   catchup_wrong += ulLoggerCountEvent("GC1", LOGGER_TASK_OVERRUN_SKIP) + ulLoggerCountEvent("GC1", LOGGER_TASK_OVERRUN_KILL);
  
   total_kills += ulLoggerCountEvent("GK1", LOGGER_TASK_OVERRUN_KILL);
   kill_wrong += ulLoggerCountEvent("GK1", LOGGER_TASK_OVERRUN_SKIP) +ulLoggerCountEvent("GK1", LOGGER_TASK_OVERRUN_CATCH_UP);




   if(total_skips >= 10 && total_catchups >= 10 && total_kills >= 10 && skip_wrong == 0 && catchup_wrong == 0 && kill_wrong == 0){
       res.passed = true;
   }else{
       snprintf(res.details, sizeof(res.details), "SK(tot./wrong policies):%lu/%lu CU:%lu/%lu KL:%lu/%lu", total_skips, skip_wrong, total_catchups, catchup_wrong, total_kills, kill_wrong);
   }


   return res;
}




/**
* @brief Test 6: SKIP overrun policy test
* @details Overrun policy that must be used: SKIP
*/
static TestResult_t xTest_OverrunPolicySkip(void){


   TestResult_t res = {.passed = false, .details = ""};
  
   vTestStart();


   xTaskCreatePeriodic(vTask_Generic, "Skip1", 512, (void*)18,pdMS_TO_TICKS(20), pdMS_TO_TICKS(20), tskIDLE_PRIORITY+4, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "Skip2", 512, (void*)25, pdMS_TO_TICKS(30), pdMS_TO_TICKS(30), tskIDLE_PRIORITY+3, &xTestTasks[1], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "Skip3", 512, (void*)32, pdMS_TO_TICKS(40), pdMS_TO_TICKS(40), tskIDLE_PRIORITY+2, &xTestTasks[2], POLICY_SKIP);
  
   // Interferer
   xTaskCreatePeriodic(vTask_Generic, "INT", 512, (void*)8, pdMS_TO_TICKS(15), pdMS_TO_TICKS(15), tskIDLE_PRIORITY+6, &xTestTasks[3], POLICY_SKIP);


   ulTestTaskCount = 4;


   vTaskDelay(pdMS_TO_TICKS(400));
   vTestStop();


  
   uint32_t skip1_skips = ulLoggerCountEvent("Skip1", LOGGER_TASK_OVERRUN_SKIP);
   uint32_t skip2_skips = ulLoggerCountEvent("Skip2", LOGGER_TASK_OVERRUN_SKIP);
   uint32_t skip3_skips = ulLoggerCountEvent("Skip3", LOGGER_TASK_OVERRUN_SKIP);
   uint32_t skips = skip1_skips+skip2_skips+skip3_skips;


   uint32_t kills = ulLoggerCountEvent("Skip1", LOGGER_TASK_OVERRUN_KILL)+ulLoggerCountEvent("Skip2", LOGGER_TASK_OVERRUN_KILL)+ulLoggerCountEvent("Skip3", LOGGER_TASK_OVERRUN_KILL);
   uint32_t catchups = ulLoggerCountEvent("Skip1", LOGGER_TASK_OVERRUN_CATCH_UP)+ulLoggerCountEvent("Skip2", LOGGER_TASK_OVERRUN_CATCH_UP)+ulLoggerCountEvent("Skip3", LOGGER_TASK_OVERRUN_CATCH_UP);
  
  
   bool pass_skip_occurred = (skips >= 17 && skip1_skips >= 7 && skip2_skips >= 5 && skip3_skips >= 4);
  
   bool pass_other_policies = (kills == 0 && catchups == 0);
  
   uint32_t skip1_starts = ulLoggerCountEvent("Skip1", LOGGER_TASK_START);
   uint32_t skip1_ends = ulLoggerCountEvent("Skip1", LOGGER_TASK_END);
   uint32_t skip2_starts = ulLoggerCountEvent("Skip2", LOGGER_TASK_START);
   uint32_t skip2_ends = ulLoggerCountEvent("Skip2", LOGGER_TASK_END);
   uint32_t skip3_starts = ulLoggerCountEvent("Skip3", LOGGER_TASK_START);
   uint32_t skip3_ends = ulLoggerCountEvent("Skip3", LOGGER_TASK_END);
  
   bool pass_jobs_completed = (skip1_ends <= skip1_starts) && (skip2_ends <= skip2_starts) && (skip3_ends <= skip3_starts) && (skip1_ends > 5) && (skip2_ends > 5) && (skip3_ends > 5);


   if (!pass_skip_occurred) {
       snprintf(res.details, sizeof(res.details),"No skip occurred | skips:%lu (S1:%lu S2:%lu S3:%lu)", skips, skip1_skips, skip2_skips, skip3_skips);


   }else if (!pass_other_policies) {
       snprintf(res.details, sizeof(res.details),"Contamination detected | K:%lu CU:%lu",kills, catchups);


   }else if (!pass_jobs_completed) {
       snprintf(res.details, sizeof(res.details), "Tasks must complete");
   }else {
       res.passed = true;
   }
}


/**
* @brief Test 7: CATCH UP overrun policy test
* @details Overrun policy that must be used: CATCH UP
*/
static TestResult_t xTest_OverrunPolicyCatchup(void){
   TestResult_t res = {.passed = false, .details = ""};
  
   vTestStart();


   // Catch1: 11/25 =44%
   xTaskCreatePeriodic(vTask_Generic, "Catch1", 512, (void*)11,pdMS_TO_TICKS(25), pdMS_TO_TICKS(25), tskIDLE_PRIORITY+5, &xTestTasks[0], POLICY_CATCH_UP);


   // Catch2: 15/35 = 43%
   xTaskCreatePeriodic(vTask_Generic, "Catch2", 512, (void*)15, pdMS_TO_TICKS(35), pdMS_TO_TICKS(35), tskIDLE_PRIORITY+4, &xTestTasks[1], POLICY_CATCH_UP);


   // Catch3: 18/50 = 36%
   xTaskCreatePeriodic(vTask_Generic, "Catch3", 512, (void*)18, pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+3, &xTestTasks[2], POLICY_CATCH_UP);


   // INT: 6/20 = 30%
   xTaskCreatePeriodic(vTask_Generic, "INT", 512, (void*)6, pdMS_TO_TICKS(20), pdMS_TO_TICKS(20), tskIDLE_PRIORITY+7, &xTestTasks[3], POLICY_SKIP);


  
   ulTestTaskCount = 4;
  


   vTaskDelay(pdMS_TO_TICKS(300));
   vTestStop();
  
   vTaskDelay(pdMS_TO_TICKS(100));
  
   uint32_t catch1_catchups = ulLoggerCountEvent("Catch1", LOGGER_TASK_OVERRUN_CATCH_UP);
   uint32_t catch2_catchups = ulLoggerCountEvent("Catch2", LOGGER_TASK_OVERRUN_CATCH_UP);
   uint32_t catch3_catchups = ulLoggerCountEvent("Catch3", LOGGER_TASK_OVERRUN_CATCH_UP);
  
   uint32_t catchups = catch1_catchups + catch2_catchups + catch3_catchups;
  
   uint32_t kills = ulLoggerCountEvent("Catch1", LOGGER_TASK_OVERRUN_KILL) + ulLoggerCountEvent("Catch2", LOGGER_TASK_OVERRUN_KILL) + ulLoggerCountEvent("Catch3", LOGGER_TASK_OVERRUN_KILL);
                   
   uint32_t skips = ulLoggerCountEvent("Catch1", LOGGER_TASK_OVERRUN_SKIP) + ulLoggerCountEvent("Catch2", LOGGER_TASK_OVERRUN_SKIP) + ulLoggerCountEvent("Catch3", LOGGER_TASK_OVERRUN_SKIP);
  
  
   bool pass_catchups = (catchups >= 10 && catch2_catchups >= 2 && catch3_catchups >= 6);
  
   bool pass_other_policies = (skips == 0 && kills == 0);
  
   uint32_t catch1_starts = ulLoggerCountEvent("Catch1", LOGGER_TASK_START);
   uint32_t catch2_starts = ulLoggerCountEvent("Catch2", LOGGER_TASK_START);
   uint32_t catch3_starts = ulLoggerCountEvent("Catch3", LOGGER_TASK_START);
   uint32_t catch1_ends = ulLoggerCountEvent("Catch1", LOGGER_TASK_END);
   uint32_t catch2_ends = ulLoggerCountEvent("Catch2", LOGGER_TASK_END);
   uint32_t catch3_ends = ulLoggerCountEvent("Catch3", LOGGER_TASK_END);
  


   bool pass_no_jobs_skipped = (catch1_starts >= 12) && (catch2_starts >= 8) && (catch3_starts >= 6);
      
   if (!pass_catchups){
       snprintf(res.details, sizeof(res.details),"No enough catchups | catchups(10):%lu)",catchups);


   }else if(!pass_other_policies) {
       snprintf(res.details, sizeof(res.details),"Other policies detected | skips:%lu kills:%lu", skips, kills);


   }else if(!pass_no_jobs_skipped) {
       snprintf(res.details, sizeof(res.details),"Skipped jobs| C1:%lu/12 C2:%lu/8 C3:%lu/6",catch1_starts, catch2_starts, catch3_starts);


   }else {
       res.passed = true;
   }


}


/**
* @brief Test 8: KILL overrun policy test
* @details Overrun policy that must be used: KILL
*/


static TestResult_t xTest_OverrunPolicyKill(void){


   TestResult_t res = {.passed = false, .details = ""};
  
   vTestStart();


   xTaskCreatePeriodic(vTask_Generic, "Kill1", 512, (void*)20, pdMS_TO_TICKS(15), pdMS_TO_TICKS(15), tskIDLE_PRIORITY+6, &xTestTasks[0], POLICY_KILL);
   xTaskCreatePeriodic(vTask_Generic, "Kill2", 512, (void*)28, pdMS_TO_TICKS(30), pdMS_TO_TICKS(30), tskIDLE_PRIORITY+4, &xTestTasks[1], POLICY_KILL);
   xTaskCreatePeriodic(vTask_Generic, "Kill3", 512, (void*)40, pdMS_TO_TICKS(45), pdMS_TO_TICKS(45), tskIDLE_PRIORITY+3, &xTestTasks[2], POLICY_KILL);
      
   ulTestTaskCount = 3;


   vTaskDelay(pdMS_TO_TICKS(450));


   vTestStop();
  
   uint32_t kill1_kills = ulLoggerCountEvent("Kill1", LOGGER_TASK_OVERRUN_KILL);
   uint32_t kill2_kills = ulLoggerCountEvent("Kill2", LOGGER_TASK_OVERRUN_KILL);
   uint32_t kill3_kills = ulLoggerCountEvent("Kill3", LOGGER_TASK_OVERRUN_KILL);
   uint32_t kills = kill1_kills + kill2_kills + kill3_kills;


   uint32_t skips = ulLoggerCountEvent("Kill1", LOGGER_TASK_OVERRUN_SKIP)+ ulLoggerCountEvent("Kill2", LOGGER_TASK_OVERRUN_SKIP)+ ulLoggerCountEvent("Kill3", LOGGER_TASK_OVERRUN_SKIP);
   uint32_t catchups = ulLoggerCountEvent("Kill1", LOGGER_TASK_OVERRUN_CATCH_UP)+ ulLoggerCountEvent("Kill2", LOGGER_TASK_OVERRUN_CATCH_UP)+ ulLoggerCountEvent("Kill3", LOGGER_TASK_OVERRUN_CATCH_UP);
  
  
   bool pass_kill_occurred = (kills >= 20 && kill1_kills >= 10 && kill2_kills >= 5 && kill3_kills >= 3);
  
   bool pass_other_policies = (skips == 0 && catchups == 0);
  
   // (ends << starts)
   uint32_t kill1_starts = ulLoggerCountEvent("Kill1", LOGGER_TASK_START);
   uint32_t kill2_starts = ulLoggerCountEvent("Kill2", LOGGER_TASK_START);
   uint32_t kill3_starts = ulLoggerCountEvent("Kill3", LOGGER_TASK_START);
  
   uint32_t kill1_ends = ulLoggerCountEvent("Kill1", LOGGER_TASK_END);
   uint32_t kill2_ends = ulLoggerCountEvent("Kill2", LOGGER_TASK_END);
   uint32_t kill3_ends = ulLoggerCountEvent("Kill3", LOGGER_TASK_END);
  
   bool pass_jobs_killed = (kill1_ends <= kill1_starts / 2) && (kill2_ends <= kill2_starts / 2) && (kill3_ends <= kill3_starts / 2);
  
   // Kill rate
   float kill1_rate = kill1_starts > 0 ? (float)kill1_kills / kill1_starts * 100.0f : 0;
   float kill2_rate = kill2_starts > 0 ? (float)kill2_kills / kill2_starts * 100.0f : 0;
   float kill3_rate = kill3_starts > 0 ? (float)kill3_kills / kill3_starts * 100.0f : 0;
  
   bool pass_kill_rate = (kill1_rate >= 40.0f) && (kill2_rate >= 30.0f) && (kill3_rate >= 20.0f);
  
   bool pass_task_restarted = (kill1_starts >= 20) && (kill2_starts >= 10) && (kill3_starts >= 6);
      
   if (!pass_kill_occurred) {
       snprintf(res.details, sizeof(res.details),
               "Kill didn't happen | kills:%lu (K1:%lu K2:%lu K3:%lu)",kills, kill1_kills, kill2_kills, kill3_kills);


   } else if (!pass_other_policies) {
       snprintf(res.details, sizeof(res.details),
               "Contamination | SK:%lu CU:%lu",skips, catchups);


   } else if (!pass_jobs_killed) {
       snprintf(res.details, sizeof(res.details),
               "Job killed not enough| K1:%lu/%lu K2:%lu/%lu K3:%lu/%lu",kill1_ends, kill1_starts, kill2_ends, kill2_starts, kill3_ends, kill3_starts);


   } else if (!pass_kill_rate) {
       snprintf(res.details, sizeof(res.details),
               "Kill rate insufficient | K1:%.1f%% K2:%.1f%% K3:%.1f%%", kill1_rate, kill2_rate, kill3_rate);


   } else if (!pass_task_restarted) {
       snprintf(res.details, sizeof(res.details),
               "Not enough starts| starts K1:%lu K2:%lu K3:%lu",kill1_starts, kill2_starts, kill3_starts);


   } else {
       res.passed = true;
   }


}


#endif




/* APERIODIC TASK TESTS */


#if (configUSE_APERIODIC_TESTS)


/**
* @brief Test 11: Basic aperiodic task execution
* @details All tasks must be executed
*/
static TestResult_t xTest_AperiodicServer(void)
{
   TestResult_t res = {.passed = false, .details = ""};


   vTestStart();


   xCreatePollingServer(pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2);


   uint32_t ids[4];


   for(int i = 0; i < 4; i++){
       ids[i] = xTaskCreateAperiodic(vTask_Aperiodic, NULL, pdMS_TO_TICKS(30),APERIODIC_POLICY_OVERRUN,xTaskGetTickCount());
      
       vTaskDelay(pdMS_TO_TICKS(10));
   }


   vTaskDelay(pdMS_TO_TICKS(300));
   vTestStop();


   uint32_t starts = 0, ends = 0;


   for(int i = 0; i < 4; i++){
       if(ulLoggerCountAperiodicByID(ids[i], LOGGER_TASK_START) > 0) starts++;
       if(ulLoggerCountAperiodicByID(ids[i], LOGGER_TASK_END) > 0)   ends++;
   }


   if(starts >= 3 && ends >= 3){
       res.passed = true;
   }else{
       snprintf(res.details, sizeof(res.details), "Starts:%lu Ends:%lu", starts, ends);
   }


   return res;
}


/**
* @brief Test 12: Aperiodic task KILL Policy
* @details Task must be terminated
*/
static TestResult_t xTest_AperiodicKill(void){
   TestResult_t res = {.passed = false, .details = ""};
  
   vTestStart();


   xCreatePollingServer(pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+2);


   xTaskCreateAperiodic(vTask_Aperiodic, NULL, pdMS_TO_TICKS(20), APERIODIC_POLICY_KILL,xTaskGetTickCount());
   xTaskCreateAperiodic(vTask_Aperiodic, NULL, pdMS_TO_TICKS(20),APERIODIC_POLICY_KILL,xTaskGetTickCount());


   vTaskDelay(pdMS_TO_TICKS(200));
   vTestStop();


   uint32_t misses  = ulLoggerCountAperiodicByID(id, LOGGER_TASK_OVERRUN_KILL);   
   uint32_t ends   = ulLoggerCountAperiodicByID(id, LOGGER_TASK_END);


   if(misses >= 1 && ends == 0){
       res.passed = true;
   }
   else if(misses >= 1 && ends >= 1){
       snprintf(res.details, sizeof(res.details), "Task terminated");
   }
   else if(misses == 0){
       snprintf(res.details, sizeof(res.details), "Overrun didn't happen");
   }


   return res;
}


/**
* @brief Test 13: Aperiodic task OVERRUN Policy
* @details Task should continue
*/
static TestResult_t xTest_AperiodicOverrun(void){


   TestResult_t res = {.passed = false, .details = ""};
  
   vTestStart();


   uint32_t id = xTaskCreateAperiodic(vTask_Generic, NULL,pdMS_TO_TICKS(20),APERIODIC_POLICY_OVERRUN,xTaskGetTickCount());




   vTaskDelay(pdMS_TO_TICKS(300));
   vTestStop();


   uint32_t misses = ulLoggerCountAperiodicByID(id, LOGGER_TASK_DEADLINE_MISS);
   uint32_t ends = ulLoggerCountAperiodicByID(id, LOGGER_TASK_END);


   if(misses >= 1 && ends >= 1){
       res.passed = true;
   }else if(misses >= 1 && ends == 0){
       snprintf(res.details, sizeof(res.details), "Task didn't terminate");
   }else if(misses == 0){
       snprintf(res.details, sizeof(res.details), "Overrun didn't happen");
   }
  
   return res;
}


/**
* @brief Test 14: Queue full handling
* @details Check if the queue is full
*/
static TestResult_t xTest_AperiodicQueueFull(void){


   TestResult_t res = {.passed = false, .details = ""};
   bool queue_full = false;


   vTestStart();


   xCreatePollingServer(pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2);




   for(int i = 0; i < 50; i++){
       BaseType_t ret = xTaskCreateAperiodic(vTask_Aperiodic, NULL,pdMS_TO_TICKS(50),APERIODIC_POLICY_OVERRUN,xTaskGetTickCount());
       if(ret != pdPASS){
           queue_full = true;
           break;
       }
   }


   vTaskDelay(pdMS_TO_TICKS(500));
   vTestStop();


   if(queue_full){
       res.passed = true;
   }
   else{
       snprintf(res.details, sizeof(res.details), "Queue full not detected");
   }


   return res;
}


/**
* @brief Test 15: Aperiodic tasks with periodic tasks
* @details Periodic tasks must run more times
*/
static TestResult_t xTest_AperiodicWithPeriodicTasks(void){


   TestResult_t res = {.passed = false, .details = ""};
  
   vTestStart();


   xTaskCreatePeriodic(vTask_Generic, "PER_1", 512, (void*)5, pdMS_TO_TICKS(30), pdMS_TO_TICKS(30), tskIDLE_PRIORITY+4, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "PER_2", 512, (void*)5, pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+3, &xTestTasks[1], POLICY_SKIP);


   xCreatePollingServer(pdMS_TO_TICKS(40), pdMS_TO_TICKS(40), tskIDLE_PRIORITY+2);
  
   ulTestTaskCount = 2;
  
   for(int i = 0; i < 5; i++){
       xTaskCreateAperiodic(vTask_Aperiodic, NULL, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN);
       vTaskDelay(pdMS_TO_TICKS(60));
   }
  
   vTaskDelay(pdMS_TO_TICKS(100));
   vTestStop();


   uint32_t per1_starts = ulLoggerCountEvent("PER_1", LOGGER_TASK_START);
   uint32_t per2_starts = ulLoggerCountEvent("PER_2", LOGGER_TASK_START);
   uint32_t aper_starts = ulLoggerCountEvent("APER_MIX", LOGGER_TASK_START);
   uint32_t per1_misses = ulLoggerCountEvent("PER_1", LOGGER_TASK_DEADLINE_MISS);


   if(per1_starts >= 8 && per2_starts >= 5 && aper_starts >= 3 && per1_misses == 0){
       res.passed = true;
   }else{
       snprintf(res.details, sizeof(res.details),
               "PER1: %lu (exp. 8), PER2: %lu (exp. 5), APER: %lu (exp. 3), PER1 miss: %lu (exp. 0)",
               per1_starts, per2_starts, aper_starts, per1_misses);
   }


   return res;
}


#endif


/* CFG TASK TESTS */


#if (configUSE_CONFIG_TESTS)


/**
* @brief Test 16: cfg basic configuration
* @details Check config
*/
static TestResult_t xTest_ConfigBasic(void){


   TestResult_t res = {.passed = false, .details = ""};


   vTestStart();


   static PeriodicTaskConfig_t cfgTasks[] = {
       {   .pxTaskCode = vTask_Generic,.pcName = "CfgA",.pvParameters = (void*)5,.usStackDepth = 1024,
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


   ulTestTaskCount = 3;
  
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
       snprintf(res.details, sizeof(res.details), "CfgA: %lu (exp. ≈15), CfgB: %lu (exp. ≈10), CfgC: %lu (exp. ≈6) runs",a_runs, b_runs, c_runs);
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
       {.pxTaskCode = vTask_Generic, .pcName = "P1", .pvParameters = (void*)5, .usStackDepth = 512,.uxPriority = tskIDLE_PRIORITY+1, .xPeriod = pdMS_TO_TICKS(50),.pxCreatedTask = &xTestTasks[0], .xDeadline = pdMS_TO_TICKS(50), .xTaskPolicy = POLICY_SKIP},
       {.pxTaskCode = vTask_Generic, .pcName = "P3", .pvParameters = (void*)5, .usStackDepth = 512,.uxPriority = tskIDLE_PRIORITY+3,.xPeriod = pdMS_TO_TICKS(50),.pxCreatedTask = &xTestTasks[1],.xDeadline = pdMS_TO_TICKS(50),.xTaskPolicy = POLICY_SKIP},
       {.pxTaskCode = vTask_Generic, .pcName = "P5", .pvParameters = (void*)5, .usStackDepth = 512,.uxPriority = tskIDLE_PRIORITY+5,.xPeriod = pdMS_TO_TICKS(50),.pxCreatedTask = &xTestTasks[2],.xDeadline = pdMS_TO_TICKS(50),.xTaskPolicy = POLICY_SKIP}
   };
  
   SchedulerConfig_t cfg = {
       .globalPolicy = POLICY_SKIP,
       .max_tasks = 20,
       .trace_enabled = pdTRUE,
       .uxNumTasks = 3,
       .pxTasks = cfgTasks
   };


   ulTestTaskCount = 3;


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
   ulTestTaskCount = 8;


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
       snprintf(res.details, sizeof(res.details), "Tasks ran: %lu/8, Total misses: %lu", tasks_ran, total_misses);
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


   /* Cfg creation */
  
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


  
   /* Then Dynamic creation wih xtask*/
   vTestStart();
  
   xTaskCreatePeriodic(vTask_Generic, "Dyn1", 512, (void*)5,
       pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "Dyn2", 512, (void*)5,
       pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2, &xTestTasks[1], POLICY_SKIP);
  
   ulTestTaskCount = 4;
  
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


#endif



#if (configUSE_REQUIREMENTS_TESTS)


/**
* @brief Test 10: CPU overhead measurement
* @details Verifying that Overhead: ≤ 10% CPU (≤8 tasks)
*/
static TestResult_t xTest_CPUOverhead(void){


   TestResult_t res = {.passed = false, .details = ""};


   vLoggerResetIdleTime();
  
   vTestStart();


   xTaskCreatePeriodic(vTask_Generic, "C1", 512, (void*)1, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C2", 512, (void*)1, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[1], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C3", 512, (void*)1, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[2], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C4", 512, (void*)1, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[3], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C5", 512, (void*)1, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[4], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C6", 512, (void*)1, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[5], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C7", 512, (void*)1, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[6], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C8", 512, (void*)1, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[7], POLICY_SKIP);
  
   ulTestTaskCount = 8;


   TickType_t start = xTaskGetTickCount();
   vTaskDelay(pdMS_TO_TICKS(1000));
   TickType_t end = xTaskGetTickCount();


   vTestStop();


   uint32_t idle_ticks = ulLoggerGetIdleTime();
   uint32_t total_ticks = end - start;


   if(total_ticks ==0)  total_ticks = 1;


   /* expected 8% */
   uint32_t cpu_busy = 100 - ((idle_ticks * 100) / total_ticks);
   uint32_t overhead = (cpu_busy > 8) ? (cpu_busy - 8) : 0;


   if(overhead <= 10){
       res.passed = true;
   }else{
       snprintf(res.details, sizeof(res.details),"CPU overhead exceeds the limit, actual: %lu%%", overhead);
   }
  
   return res;
}


/**
* @brief Test 10.1: CPU overhead con mix di carichi variabili
* @details another CPU test
*/
static TestResult_t xTest_CPUOverhead2(void){


   TestResult_t res = {.passed = false, .details = ""};


   vLoggerResetIdleTime();
  
   vTestStart();


   //added bigger WCET
   xTaskCreatePeriodic(vTask_Generic, "C1", 512, (void*)1,  pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+7, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C2", 512, (void*)2,  pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+6, &xTestTasks[1], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C3", 512, (void*)1,  pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+5, &xTestTasks[2], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C4", 512, (void*)2,  pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+4, &xTestTasks[3], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C5", 512, (void*)1,  pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[4], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C6", 512, (void*)1,  pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[5], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C7", 512, (void*)1,  pdMS_TO_TICKS(200), pdMS_TO_TICKS(200), tskIDLE_PRIORITY+2, &xTestTasks[6], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "C8", 512, (void*)1,  pdMS_TO_TICKS(200), pdMS_TO_TICKS(200), tskIDLE_PRIORITY+2, &xTestTasks[7], POLICY_SKIP);
  
   ulTestTaskCount = 8;


   // Total U = (1+2+1+2+1+1)/100 + (1+1)/200 = 8/100 + 2/200 = 9%


   TickType_t start = xTaskGetTickCount();
   vTaskDelay(pdMS_TO_TICKS(1000));
   TickType_t end = xTaskGetTickCount();


   vTestStop();


   uint32_t idle_ticks = ulLoggerGetIdleTime();
   uint32_t total_ticks = end - start;


   if(total_ticks == 0) total_ticks = 1;


   uint32_t cpu_busy = 100 - ((idle_ticks * 100) / total_ticks);


   uint32_t overhead = (cpu_busy > 9) ? (cpu_busy - 9) : 0;


   // Context switch
   uint32_t total_starts = 0;
   const char* names[] = {"C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8"};
  
   for(uint8_t i = 0; i < 8; i++){
       total_starts += ulLoggerCountEvent(names[i], LOGGER_TASK_START);
   }


   if(overhead <= 10 && total_starts >= 90){
       res.passed = true;
   }else{
       snprintf(res.details, sizeof(res.details), "Overhead(MUST be ≤10%%)=%lu%% Context switches=%lu", overhead, cpu_busy, total_starts);
   }
   return res;
}




/**
* @brief Test 4: Test about jitter requirement
* @details Release jitter must be ≤ 1 ticks
*/
static TestResult_t xTest_ReleaseJitter(void){


   TestResult_t res = {.passed = false, .details = ""};


   memset(xJitterData, 0, sizeof(xJitterData));


   vTestStart();


   uint32_t p0 = (0 << 16) | 20;
   uint32_t p1 = (1 << 16) | 30;
   uint32_t p2 = (2 << 16) | 50;
   uint32_t p3 = (3 << 16) | 100;


   xTaskCreatePeriodic(vTask_Jitter, "J0", 512, (void*)(uintptr_t)p0, pdMS_TO_TICKS(20), pdMS_TO_TICKS(20), tskIDLE_PRIORITY+2, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Jitter, "J1", 512, (void*)(uintptr_t)p1, pdMS_TO_TICKS(30), pdMS_TO_TICKS(30), tskIDLE_PRIORITY+2, &xTestTasks[1], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Jitter, "J2", 512, (void*)(uintptr_t)p2, pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2, &xTestTasks[2], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Jitter, "J3", 512, (void*)(uintptr_t)p3, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+2, &xTestTasks[3], POLICY_SKIP);


   ulTestTaskCount = 4;


   vTaskDelay(pdMS_TO_TICKS(600));
   vTestStop();


   bool all_ok = true;
   char details[128] = "";


   for(int i = 0; i < 4; i++){
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


#endif




#if (configUSE_APERIODIC_TESTS && configUSE_PERIODIC_TESTS)


/**
* @brief Test 25: Complete system stress test
* @details Periodic+aperiodic
*/
static TestResult_t xTest_AllPolicies(void){


   TestResult_t res = {.passed = false, .details = ""};
  
   vTestStart();




   xTaskCreatePeriodic(vTask_Generic, "MS1", 512, (void*)22, pdMS_TO_TICKS(30), pdMS_TO_TICKS(30), tskIDLE_PRIORITY+5, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "MC1", 512, (void*)18, pdMS_TO_TICKS(25), pdMS_TO_TICKS(25), tskIDLE_PRIORITY+6, &xTestTasks[1], POLICY_CATCH_UP);
   xTaskCreatePeriodic(vTask_Generic, "MK1", 512, (void*)20, pdMS_TO_TICKS(20), pdMS_TO_TICKS(20), tskIDLE_PRIORITY+7, &xTestTasks[2], POLICY_KILL);
  
   ulTestTaskCount = 3;


   // Polling Server
   xCreatePollingServer(pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+2);


   // Aperiodic tasks (distributed over time)
   xTaskCreateAperiodic(vTask_Aperiodic, (void*)8, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
   vTaskDelay(pdMS_TO_TICKS(50));
   xTaskCreateAperiodic(vTask_Aperiodic, (void*)8, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
   vTaskDelay(pdMS_TO_TICKS(50));
   xTaskCreateAperiodic(vTask_Aperiodic, (void*)25, pdMS_TO_TICKS(15), APERIODIC_POLICY_KILL, xTaskGetTickCount());
   vTaskDelay(pdMS_TO_TICKS(50));
   xTaskCreateAperiodic(vTask_Aperiodic, (void*)8, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
   vTaskDelay(pdMS_TO_TICKS(50));
   xTaskCreateAperiodic(vTask_Aperiodic, (void*)25, pdMS_TO_TICKS(15), APERIODIC_POLICY_KILL, xTaskGetTickCount());
   vTaskDelay(pdMS_TO_TICKS(50));
   xTaskCreateAperiodic(vTask_Aperiodic, (void*)8, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
  
   vTaskDelay(pdMS_TO_TICKS(100));
   vTestStop();
   vTaskDelay(pdMS_TO_TICKS(100));


   uint32_t skip_events = ulLoggerCountEvent("MS1", LOGGER_TASK_OVERRUN_SKIP);
   uint32_t catchup_events = ulLoggerCountEvent("MC1", LOGGER_TASK_OVERRUN_CATCH_UP;
   uint32_t kill_events = ulLoggerCountEvent("MK1", LOGGER_TASK_OVERRUN_KILL);


   if(skip_events >= 3 && catchup_events >= 3 && kill_events >= 3){
       res.passed = true;
   }else{
       snprintf(res.details, sizeof(res.details), "SK:%lu CU:%lu KL:%lu", skip_events, catchup_events, kill_events);
   }


   return res;
}


/**
* @brief Test 22: CPU overhead with periodic and aperiodic tasks
* @details 6 periodic + polling server, overhead ≤10%
*/
static TestResult_t xTest_CPUOverhead_WithAperiodic(void){


   TestResult_t res = {.passed = false, .details = ""};


   vLoggerResetIdleTime();
  
   vTestStart();


   xTaskCreatePeriodic(vTask_Generic, "CP1", 512, (void*)1, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+5, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "CP2", 512, (void*)2, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+4, &xTestTasks[1], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "CP3", 512, (void*)1, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+4, &xTestTasks[2], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "CP4", 512, (void*)2, pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+3, &xTestTasks[3], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "CP5", 512, (void*)1, pdMS_TO_TICKS(200), pdMS_TO_TICKS(200), tskIDLE_PRIORITY+2, &xTestTasks[4], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Generic, "CP6", 512, (void*)1, pdMS_TO_TICKS(200), pdMS_TO_TICKS(200), tskIDLE_PRIORITY+2, &xTestTasks[5], POLICY_SKIP);
  
   ulTestTaskCount = 6;


   xCreatePollingServer(pdMS_TO_TICKS(100), pdMS_TO_TICKS(100), tskIDLE_PRIORITY+1);


   TickType_t start = xTaskGetTickCount();


   for(int i = 0; i < 5; i++){
       xTaskCreateAperiodic(vTask_Aperiodic, (void*)3, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
       vTaskDelay(pdMS_TO_TICKS(200));
   }


   TickType_t end = xTaskGetTickCount();


   vTestStop();


   uint32_t idle_ticks = ulLoggerGetIdleTime();
   uint32_t total_ticks = end - start;


   if(total_ticks == 0) total_ticks = 1;


   uint64_t idle_pct = ((uint64_t)idle_ticks * 100ULL) / total_ticks;
   if(idle_pct > 100) idle_pct = 100;
  
   uint32_t cpu_busy = (uint32_t)(100 - idle_pct);
  
   // Expected: 8% periodic + 1.5% aperiodic = 9.5%
   uint32_t overhead = (cpu_busy > 10) ? (cpu_busy - 10) : 0;


   uint32_t periodic_starts = 0;
   uint32_t aperiodic_completed = 0;
  
   const char* periodic_names[] = {"CP1", "CP2", "CP3", "CP4", "CP5", "CP6"};


   for(uint8_t i = 0; i < 6; i++){
       periodic_starts += ulLoggerCountEvent(periodic_names[i], LOGGER_TASK_START);
   }


   if(overhead <= 12 && periodic_starts >= 50){
       res.passed = true;
   }else{
       snprintf(res.details, sizeof(res.details),"overhead=%lu%% CPU=%lu%% periodic_starts=%lu",overhead, cpu_busy, periodic_starts);
   }
  
   return res;
}


/**
* @brief Test 23: Jitter with polling server
* @details Verifying jitter
*/
static TestResult_t xTest_ReleaseJitter_WithAperiodic(void){


   TestResult_t res = {.passed = false, .details = ""};


   memset(xJitterData, 0, sizeof(xJitterData));
  
   vTestStart();


   // Jitter periodic tasks
   uint32_t p0 = (0 << 16) | 20;
   uint32_t p1 = (1 << 16) | 30;
   uint32_t p2 = (2 << 16) | 50;


   xTaskCreatePeriodic(vTask_Jitter, "JA0", 512, (void*)(uintptr_t)p0, pdMS_TO_TICKS(20), pdMS_TO_TICKS(20), tskIDLE_PRIORITY+5, &xTestTasks[0], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Jitter, "JA1", 512, (void*)(uintptr_t)p1,pdMS_TO_TICKS(30), pdMS_TO_TICKS(30), tskIDLE_PRIORITY+4, &xTestTasks[1], POLICY_SKIP);
   xTaskCreatePeriodic(vTask_Jitter, "JA2", 512, (void*)(uintptr_t)p2,pdMS_TO_TICKS(50), pdMS_TO_TICKS(50), tskIDLE_PRIORITY+3, &xTestTasks[2], POLICY_SKIP);


   ulTestTaskCount = 3;


   // Polling Server
   xCreatePollingServer(pdMS_TO_TICKS(40), pdMS_TO_TICKS(40), tskIDLE_PRIORITY+2);


   // Aperiodic tasks
   for(int i = 0; i < 8; i++){
       xTaskCreateAperiodic(vTask_Generic, (void*)5, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
       if(i < 7) vTaskDelay(pdMS_TO_TICKS(70));
   }


   vTaskDelay(pdMS_TO_TICKS(50));
   vTestStop();


   bool all_ok = true;
  
   for(int i = 0; i < 3; i++){
       uint32_t period_ms = (i == 0) ? 20 : (i == 1) ? 30 : 50;


       if(xJitterData[i].releases < 5 || xJitterData[i].max_jitter > 2)
           all_ok = false;
   }


   if(all_ok){
       res.passed = true;
   }else{
       snprintf(res.details, sizeof(res.details), "Jit: J0=%lu J1=%lu J2=%lu", xJitterData[0].max_jitter, xJitterData[1].max_jitter, xJitterData[2].max_jitter);
   }


   return res;
}






#endif




/* TEST SUITE */


static TestDef_t xTests[] = {


#if ( configUSE_PERIODIC_TESTS == 1)


   {"1. StressTest_OverlappingHRT",       xTest_StressOverlappingHRT},
   {"2. TestEdge_MinimalTimeGap",         xTest_EdgeMinimalTimeGap},
   {"3. Test_PreemptionHigher",           xTest_PreemptionHigher},
   {"4. Test_DeadlineMiss",               xTest_DeadlineMiss},
   {"5. Test_OverrunPolicy_SKIP",         xTest_OverrunPolicySkip},
   {"6. Test_OverrunPolicy_KILL",         xTest_OverrunPolicyKill},
   {"7. Test_OverrunPolicy_CATCH_UP",     xTest_OverrunPolicyCatchup},
   {"8. Test_RoundRobin",                 xTest_RoundRobin},
   {"9. Test_AllOverrunPolicies",         xTest_OverrunPolicies_GlobalStress},


#endif


#if( configUSE_REQUIREMENTS_TESTS)


   {"10. Test_CPUOverhead",                xTest_CPUOverhead},
   {"10.1. Test_CPUOverhead2",             xTest_CPUOverhead2},
   {"11. Test_ReleaseJitter",              xTest_ReleaseJitter},


#endif


#if ( configUSE_CONFIG_TESTS == 1)
   {"12. Test_ConfigBasic",               xTest_ConfigBasic},
   {"13. Test_ConfigPriorities",          xTest_ConfigPriorities},
   {"14. Test_ConfigMaxTasks",            xTest_ConfigMaxTasks},
   {"15. Test_ConfigVsDynamic",           xTest_ConfigVsDynamic},


#endif


#if ( configUSE_APERIODIC_TESTS == 1)


   {"16. Test_AperiodicServer",           xTest_AperiodicServer},
   {"17. Test_AperiodicKill",             xTest_AperiodicKill},
   {"18. Test_AperiodicOverrun",          xTest_AperiodicOverrun},
   {"19. Test_AperiodicQueueFull",        xTest_AperiodicQueueFull},
   {"20. Test_AperiodicWithPeriodic",     xTest_AperiodicWithPeriodicTasks},


#endif


#if ( configUSE_APERIODIC_TESTS == 1 && configUSE_PERIODIC_TESTS == 1)


   {"21. Test_AllPolicies",               xTest_AllPolicies},
   {"22. Test_CPUOverhead_Aperiodic",     xTest_CPUOverhead_WithAperiodic},
   {"23. Test_Jitter_with_Aperiodic",     xTest_ReleaseJitter_WithAperiodic},


#endif


};


#define NUM_TESTS (sizeof(xTests) / sizeof(xTests[0]))





void vQEMUExit(void){


   vTaskDelay(pdMS_TO_TICKS(100));
  
   // Exit via semihosting ARM
   __asm volatile (
       "mov r0, #0x18\n"           // angel_SWIreason_ReportException
       "ldr r1, =0x20026\n"        // ADP_Stopped_ApplicationExit
       "bkpt 0xAB\n"
       :
       :
       : "r0", "r1"
   );
  
   while(1);
}




void vTestSuite(void){


   uint32_t ulPassed = 0;
   char s[160];


   vTestEventGroupInit();


   for(uint32_t i = 0; i < NUM_TESTS; i++){


       TestResult_t result = xTests[i].func();


       vStopScheduler();


       snprintf(s, sizeof(s), "Test %s: %s  %s\r\n",
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
   };
  
   vQEMUExit(); 
}











