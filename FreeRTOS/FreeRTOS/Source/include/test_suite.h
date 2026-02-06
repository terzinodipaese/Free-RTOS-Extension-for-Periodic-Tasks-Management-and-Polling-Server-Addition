#ifndef TEST_SUITE_H
#define TEST_SUITE_H


#include "FreeRTOS.h"
#include "logger.h"
#include <stdbool.h>

typedef bool (*TestFunc_t)(void);

typedef struct{
    const char *pcName;
    TestFunc_t func;
} TestDef_t;

void vTestSuite(void);
static bool vStressTest_OverlappingHRT(void);
static bool vTestEdge_MinimalTimeGap(void);
static bool vTest_PreemptionHigher(void);
static bool vTest_ReleaseJitter(void);
static bool vTest_DeadlineMiss(void);
static bool vTest_OverrunPolicySkip(void);
static bool vTest_OverrunPolicyKill(void);
static bool vTest_OverrunPolicyCatchup(void);
static bool vTest_RoundRobin(void);


#endif