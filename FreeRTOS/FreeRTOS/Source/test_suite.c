#include "test_suite.h"
#include "logger.h"
#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_TASKS 10
static volatile bool gTestRunning = false; 
TaskHandle_t xTestTasks[MAX_TASKS] = {0};


/*Busy wait*/
static void BusyMs(unsigned ms){
    
    TickType_t start = xTaskGetTickCount();
    while((xTaskGetTickCount()-start)<pdMS_TO_TICKS(ms)){
        __asm volatile("nop");
    }
    
}




void vTask_Jitter(void *pvParameters){

    uint32_t p = (uint32_t)(uintptr_t) pvParameters;
    static TickType_t last_release = 0;
    TickType_t now = xTaskGetTickCount(); //timestamp 

    if(last_release != 0){
        TickType_t delta = now - last_release; //it must be equal to the period
        vLoggerStore(pcTaskGetName(NULL),LOGGER_TASK_RELEASE,delta);
    }

    last_release = now;

    vLoggerStore(pcTaskGetName(NULL),LOGGER_TASK_START,0);
    BusyMs(p); 
    vLoggerStore(pcTaskGetName(NULL),LOGGER_TASK_END,0);
    
}



void vTask_DeadlineMiss(void *pvParameters){

    
    uint32_t p = (uint32_t)(uintptr_t) pvParameters;

    vLoggerStore(pcTaskGetName(NULL),LOGGER_TASK_START,0);
    BusyMs(p); 
    vLoggerStore(pcTaskGetName(NULL),LOGGER_TASK_END,0);

}




void vTask_Generic(void *pvParameters){

    uint32_t p = (uint32_t)(uintptr_t) pvParameters;
    vLoggerStore(pcTaskGetName(NULL), LOGGER_TASK_START,0);
    BusyMs(p);
    vLoggerStore(pcTaskGetName(NULL),
                    LOGGER_TASK_END,
                    0);

}



//tests

//8 tasks overlapped->check if there's no deadline miss

static bool vStressTest_OverlappingHRT(void)
{
    gTestRunning = true;

    xTaskCreatePeriodic(vTask_Generic,"T10",512,(void*)8,
        pdMS_TO_TICKS(10),pdMS_TO_TICKS(10),3,&xTestTasks[0],POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic,"T20",512,(void*)12,
        pdMS_TO_TICKS(20),pdMS_TO_TICKS(20),3,&xTestTasks[1],POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic,"T30",512,(void*)16,
        pdMS_TO_TICKS(30),pdMS_TO_TICKS(30),3,&xTestTasks[2],POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic,"T40",512,(void*)20,
        pdMS_TO_TICKS(40),pdMS_TO_TICKS(40),3,&xTestTasks[3],POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic,"T50",512,(void*)24,
        pdMS_TO_TICKS(50),pdMS_TO_TICKS(50),3,&xTestTasks[4],POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic,"T60",512,(void*)28,
        pdMS_TO_TICKS(60),pdMS_TO_TICKS(60),3,&xTestTasks[5],POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic,"T70",512,(void*)32,
        pdMS_TO_TICKS(70),pdMS_TO_TICKS(70),3,&xTestTasks[6],POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic,"T80",512,(void*)36, 
        pdMS_TO_TICKS(80),pdMS_TO_TICKS(80),3,&xTestTasks[7],POLICY_SKIP);



    vTaskDelay(pdMS_TO_TICKS(400));

    gTestRunning = false;

    vTaskDelay(pdMS_TO_TICKS(20));

    return !LoggerDeadlineMiss(LOGGER_TASK_DEADLINE_MISS);


}

//test if there aren't collision
static bool vTestEdge_MinimalTimeGap(void){
    gTestRunning = true;
    xTaskCreatePeriodic(vTask_Generic,"T_A",512,(void*)10,
       pdMS_TO_TICKS(10),pdMS_TO_TICKS(10),2,&xTestTasks[0],POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic,"T_B",512,(void*)10,
       pdMS_TO_TICKS(11),pdMS_TO_TICKS(11),2,&xTestTasks[1],POLICY_SKIP);



   
    vTaskDelay(pdMS_TO_TICKS(200));

    gTestRunning = false;

    vTaskDelay(pdMS_TO_TICKS(10));

    return !LoggerDeadlineMiss(LOGGER_TASK_DEADLINE_MISS);
}

//check priority
static bool vTest_PreemptionHigher(void){
    gTestRunning = true;


    xTaskCreatePeriodic(vTask_Generic,"T_Low",512,(void*)30,
        pdMS_TO_TICKS(50),pdMS_TO_TICKS(50),1,&xTestTasks[0],POLICY_SKIP);
    xTaskCreatePeriodic(vTask_Generic,"T_High",512,(void*)10,
        pdMS_TO_TICKS(10),pdMS_TO_TICKS(10),4,&xTestTasks[1],POLICY_SKIP);


    vTaskDelay(pdMS_TO_TICKS(150));

    gTestRunning = false;

    uint32_t high_starts = LoggerCountEvent("T_High", LOGGER_TASK_START);
    uint32_t low_starts  = LoggerCountEvent("T_Low",  LOGGER_TASK_START);

    return (high_starts > 0) && (low_starts > 0) && (high_starts > low_starts);
}



static bool vTest_ReleaseJitter(void){
    gTestRunning = true;


    xTaskCreatePeriodic(vTask_Jitter,"Jitter",512,(void*)5,
        pdMS_TO_TICKS(20),pdMS_TO_TICKS(20),2,&xTestTasks[0],POLICY_SKIP);


    vTaskDelay(pdMS_TO_TICKS(300));

    gTestRunning = false;

    return LoggerCountEvent("Jitter", LOGGER_TASK_RELEASE) > 5;

}



static bool vTest_DeadlineMiss(void){
   gTestRunning = true;


   xTaskCreatePeriodic(vTask_DeadlineMiss,"DM",512,(void*)15,
       pdMS_TO_TICKS(10),pdMS_TO_TICKS(10),2,&xTestTasks[0],POLICY_SKIP);


   vTaskDelay(pdMS_TO_TICKS(200));
   gTestRunning = false;


   return LoggerCountEvent("DM",LOGGER_TASK_DEADLINE_MISS) >= 1;


}
static bool vTest_OverrunPolicySkip(void){
    gTestRunning = true;


    xTaskCreatePeriodic(vTask_Generic,"Skip",512,(void*)15,
        pdMS_TO_TICKS(10),pdMS_TO_TICKS(10),2,&xTestTasks[0],POLICY_SKIP);


    vTaskDelay(pdMS_TO_TICKS(200));

    gTestRunning = false;

    return (LoggerCountEvent("Skip", LOGGER_TASK_OVERRUN_SKIP)    >= 1) &&
            (LoggerCountEvent("Skip", LOGGER_TASK_OVERRUN_KILL)    == 0) &&
            (LoggerCountEvent("Skip", LOGGER_TASK_OVERRUN_CATCH_UP) == 0);
    }
    

static bool vTest_OverrunPolicyCatchup(void){
    gTestRunning = true;


    xTaskCreatePeriodic(vTask_Generic,"CatchUp",512,(void*)25,
        pdMS_TO_TICKS(20),pdMS_TO_TICKS(20),2,&xTestTasks[0],POLICY_CATCH_UP);


    vTaskDelay(pdMS_TO_TICKS(200));
    gTestRunning = false;

    return (LoggerCountEvent("CatchUp", LOGGER_TASK_OVERRUN_SKIP)     == 0) &&
           (LoggerCountEvent("CatchUp", LOGGER_TASK_OVERRUN_KILL)     == 0) &&
           (LoggerCountEvent("CatchUp", LOGGER_TASK_OVERRUN_CATCH_UP) >= 1);
}

static bool vTest_OverrunPolicyKill(void){
    gTestRunning = true;


    xTaskCreatePeriodic(vTask_Generic,"Kill",512,(void*)25,
        pdMS_TO_TICKS(20),pdMS_TO_TICKS(20),2,NULL,POLICY_KILL);


    vTaskDelay(pdMS_TO_TICKS(200));
    gTestRunning = false;


    return (LoggerCountEvent("Kill", LOGGER_TASK_OVERRUN_SKIP)     == 0) &&
           (LoggerCountEvent("Kill", LOGGER_TASK_OVERRUN_KILL)     >= 1) &&
           (LoggerCountEvent("Kill", LOGGER_TASK_OVERRUN_CATCH_UP) == 0);
}


static bool vTest_RoundRobin(void){
    gTestRunning = true;

    xTaskCreatePeriodic(vTask_Generic,"RR1",512,(void*)8,
        pdMS_TO_TICKS(50),pdMS_TO_TICKS(50),2,NULL,POLICY_CATCH_UP);
    xTaskCreatePeriodic(vTask_Generic,"RR2",512,(void*)8,
        pdMS_TO_TICKS(50),pdMS_TO_TICKS(50),2,NULL,POLICY_CATCH_UP);


    vTaskDelay(pdMS_TO_TICKS(200));
    gTestRunning = false;

    uint32_t a = LoggerCountEvent("RR1",LOGGER_TASK_START);
    uint32_t b = LoggerCountEvent("RR2",LOGGER_TASK_START);
    bool ok = (a >= 1) && (b>=1) && (a+b >=3);
    return ok;    
}


void vDeleteTestTasks(void){
    for(uint8_t i = 0;i<MAX_TASKS;i++){
        if(xTestTasks[i]!= NULL){
            vTaskDelete(xTestTasks[i]);
            xTestTasks[i] = NULL;
        }
    }
}

void vStopScheduler(void){
    gTestRunning = false;
    vTaskDelay(pdMS_TO_TICKS(50));

    vDeleteTestTasks(); 

    vLoggerInit();
}


TestDef_t xTests[] = {
    {"1. StressTest_OverlappingHRT", vStressTest_OverlappingHRT},
    {"2. TestEdge_MinimalTimeGap", vTestEdge_MinimalTimeGap},
    {"3. Test_PreemptionHigher", vTest_PreemptionHigher},
    {"4. Test_ReleaseJitter",vTest_ReleaseJitter},
    {"5. Test_DeadlineMiss", vTest_DeadlineMiss},
    {"6. Test_OverrunPolicy- SKIP", vTest_OverrunPolicySkip},
    {"7. Test_OverrunPolicy- KILL", vTest_OverrunPolicyKill},
    {"8. Test_OverrunPolicy- CATCH_UP", vTest_OverrunPolicyCatchup},
    {"9. Test_RoundRobin", vTest_RoundRobin}
};

#define NUM_TESTS (sizeof(xTests)/ sizeof(xTests[0]))


void vTestSuite(void){

    uint32_t ulPassed = 0;
    char s[128];

    for(uint32_t i = 0; i< NUM_TESTS; i++){

        vLoggerInit();

        bool result = xTests[i].func();
        vStopScheduler();

        snprintf(s, sizeof(s), "Test %s: %s\r\n", xTests[i].pcName,
                result ? "PASSED" : "FAILED");

        UART_printf(s);
        if(result) ulPassed++;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
    snprintf(s,sizeof(s),"Test Passed: %lu/%lu \r\n",ulPassed,NUM_TESTS);
    UART_printf(s);
    vTaskSuspend(NULL);
}


 