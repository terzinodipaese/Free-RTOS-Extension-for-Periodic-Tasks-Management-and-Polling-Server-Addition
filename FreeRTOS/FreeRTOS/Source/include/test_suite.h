/**
* @file test_suite.h
* @brief Header of test_suite
*/




#ifndef TEST_SUITE_H
#define TEST_SUITE_H




#include "FreeRTOS.h"
#include "task.h"
#include "stdint.h"
#include <stdbool.h>




#define configUSE_PERIODIC_TESTS                    1
#define configUSE_APERIODIC_TESTS                   0
#define configUSE_CONFIG_TESTS                      1
#define configUSE_REQUIREMENTS_TESTS                1


typedef struct{
   bool passed;
   char details[160];
} TestResult_t;


typedef TestResult_t (*TestFunc_t)(void);


typedef struct{
   const char *pcName;
   TestFunc_t func;
} TestDef_t;




/* TEST TASK FUNCTIONS */


void vTask_Jitter(void *pvParameters);


void vTask_Generic(void *pvParameters);


void vTask_Aperiodic(void *pvParameters);


/* CLEANUP FUNCTIONS */


void vDeleteTestTasks(void);




void vStopScheduler(void);




/* TEST SUITE CONTROL */




void vTestSuite(void);


void vQEMUExit(void);


#endif
