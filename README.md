# Lab3.1 FreeRTOS Tasks Creation


## Getting started

Scope: learn to create, schedule, pause, and destroy tasks in FreeRTOS.

You will practice:

* Creating one-shot and periodic tasks (`xTaskCreate`, `vTaskDelete`)
* Time management with vTaskDelay and vTaskDelayUntil
* Controlling CPU usage and avoiding drift
* Priorities and preemption effects
* Suspending and resuming tasks (`vTaskSuspend`, `vTaskResume`)
* Dynamic task lifecycles (spawn/cleanup patterns)


## Exercise 1 - One-shot “Hello” task (self-delete)

Create a task that runs once, prints a message with its name and the stack available space, then deletes itself.

Requirements

* Create a task HelloTask with priority `tskIDLE_PRIORITY + 1`
* Inside the task printi its name and available stack using `pcTaskGetName` and `uxTaskGetStackHighWaterMark` system calls
* After printing, call `vTaskDelete`;
* Main should continue running (scheduler and Idle task active)

Look at the documentation of the system calls to become familiar with them. Experiment by changing the stack size.


## Exercise 2 – Two periodic tasks with vTaskDelay (baseline)

Create two periodic tasks at the same priority that print their tick timestamps at 500 ms and 200 ms periods using `vTaskDelay`.

Requirements

* Blink500 task: `vTaskDelay(pdMS_TO_TICKS(500))` loop
* Blink200 task: `vTaskDelay(pdMS_TO_TICKS(200))` loop
* Show interleaved prints (note possible drift due to work time + delay)

This solution is drift prone. If the internal loop performs long operations the timing might be wrong. Try to generate this situation using busy waiting delays.

## Exercise 3 – Periodic tasks with vTaskDelayUntil (no drift)

Repeat Exercise 2 using vTaskDelayUntil to avoid drift.

Requirements

* Use an `TickType_t last = xTaskGetTickCount();`
* Replace vTaskDelay() with `vTaskDelayUntil(&last, periodTicks);`
* Show more regular timestamps (bounded jitter from preemption only)

## Exercise 4 – Priority & preemption experiment

Observe how priorities affect CPU sharing.

Requirements

* Task A (high priority = IDLE+3): busy loops for ~5 ms (simulate with for-loop or `small delay_ms(5))`, then `vTaskDelay(1)`
* Task B (low priority = IDLE+1): same behavior
* Run first with no `vTaskDelay()` in the high-prio loop and observe starvation
* Add `vTaskDelay(1)` and observe improved fairness


## Exercise 5 – Suspend & resume a worker task

A controller task periodically suspends and resumes a worker task.

Requirements:

*	Worker prints a counter every 100 ms
*	Controller waits 1 s, calls `vTaskSuspend(workerHandle`), waits 1 s, calls `vTaskResume(workerHandle)`, repeat

## Exercise 6 – Dynamic task spawner (bounded pool)

A spawner task periodically creates short-lived worker tasks that self-delete after N prints. Limit the max concurrent workers to 3.

Hints:

*	Keep a simple volatile uint8_t activeCount
*	Increment before creating (or immediately after), decrement in worker just before delete
*	If activeCount >= 3, the spawner just waits and retries later (polling via delay)


## Exercise 7 – Phased periodic schedule (phase offsets)

Start three periodic tasks with the same period (e.g., 1000 ms) but different phase offsets (0 ms, 250 ms, 500 ms). Use vTaskDelayUntil and an initial startup delay to align phases.

Requirements

* T0: phase 0 ms
* T1: first `vTaskDelay(pdMS_TO_TICKS(250))` then enter `vTaskDelayUntil(&last, 1000)`
* T2: first `vTaskDelay(pdMS_TO_TICKS(500))` then enter `vTaskDelayUntil(&last, 1000)`
* Show that prints are interleaved at 250 ms offsets

