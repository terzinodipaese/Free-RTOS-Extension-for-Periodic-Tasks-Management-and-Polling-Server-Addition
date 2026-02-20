import os
import subprocess
import time
import re
import shutil
import sys
import argparse

# --- Terminal color helpers ---
# Enable/disable color output easily (useful for CI or non-TTY environments)
USE_COLOR = True
COLOR_GREEN = "\033[32m"
COLOR_RED = "\033[31m"
COLOR_YELLOW = "\033[33m"
COLOR_RESET = "\033[0m"

def colorize(text, color):
    if not USE_COLOR:
        return text
    return f"{color}{text}{COLOR_RESET}"

# --- Configuration ---
QEMU_CMD = [
    "qemu-system-arm",
    "-machine", "mps2-an385",
    "-cpu", "cortex-m3",
    "-kernel", "./Output/demo.elf",
    "-monitor", "none",
    "-nographic",
    "-serial", "stdio" 
]

MAIN_C_PATH = "main.c"
BACKUP_MAIN_C_PATH = "main.c.bak"

# --- C Code Template ---
C_TEMPLATE = """
/* AUTO-GENERATED TEST FILE */
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include "logger.h"

#ifndef BUSY_WAIT_MULT
#define BUSY_WAIT_MULT 50000
#endif

/* --- Helper: Burn CPU Cycles --- */
void vBusyWait(int ticks_simulated) {
    if (ticks_simulated <= 0) return;
    
    volatile int i;
    int chunk = 5; 
    int remaining = ticks_simulated;

    while(remaining > 0) {
        int current = (remaining > chunk) ? chunk : remaining;
        for(i = 0; i < (current * BUSY_WAIT_MULT); i++) {
            __asm volatile("nop");
        }
        remaining -= current;
    }
}

/* --- Generic Periodic Task Function --- */
void vGenericPeriodicTask(void *pvParameters) {
    int workload = (int)(intptr_t)pvParameters;
    vBusyWait(workload);
    
    /* FIX: Tiny delay to allow Logger/Idle tasks to run. 
       Crucial for CATCH_UP tests where tasks run back-to-back. */
    vTaskDelay(1);
}

/* --- Aperiodic Worker Function --- */
void vAperiodicJobWork(void *pvParameters) {
    int workload = (int)(intptr_t)pvParameters;
    if (workload <= 0) workload = 10;
    
    UART_printf("APER_START\\n");
    vBusyWait(workload); 
    UART_printf("APER_DONE\\n");
}

/* --- Dynamic Spawner Task --- */
void vAperiodicSpawner(void *pvParameters) {
    (void)pvParameters;
    /* Injected Spawner Logic */
    %(spawner_code)s
    
    vTaskDelete(NULL); 
}

/* --- Main --- */
int main(void) {
    UART_init();
    vLoggerInit(); /* Runs at tskIDLE_PRIORITY + 1 */

    /* 1. Define Tasks */
    PeriodicTaskConfig_t testTasks[] = {
        %(tasks_config)s
    };

    /* 2. Scheduler Config */
    SchedulerConfig_t myConfig = {
        .globalPolicy = %(global_policy)s,
        .trace_enabled = pdTRUE,
        .uxNumTasks = sizeof(testTasks)/sizeof(PeriodicTaskConfig_t),
        .pxTasks = testTasks
    };

    UART_printf("TEST_START\\n");

    /* 3. Create Polling Server (if needed) */
    %(polling_server_code)s

    /* 4. Configure & Start */
    vConfigureScheduler(&myConfig);
    vTaskStartScheduler();

    for(;;);
    return 0;
}
"""

# --- 1. Helper Functions ---

def backup_main():
    if os.path.exists(MAIN_C_PATH):
        if os.path.exists(BACKUP_MAIN_C_PATH):
            os.remove(BACKUP_MAIN_C_PATH)
        shutil.copy(MAIN_C_PATH, BACKUP_MAIN_C_PATH)

def restore_main():
    if os.path.exists(BACKUP_MAIN_C_PATH):
        if os.path.exists(MAIN_C_PATH):
            os.remove(MAIN_C_PATH)
        shutil.move(BACKUP_MAIN_C_PATH, MAIN_C_PATH)

def generate_main_c(test_case):
    tasks_str = ""
    for t in test_case["tasks"]:
        prio = t['priority'] if isinstance(t['priority'], str) else str(t['priority'])
        stack = t.get('stack', 'configMINIMAL_STACK_SIZE * 2')
        
        tasks_str += f"""
        {{
            .pcName = "{t['name']}",
            .pxTaskCode = vGenericPeriodicTask,
            .pvParameters = (void*){t['workload']},
            .usStackDepth = {stack},
            .uxPriority = {prio},
            .xPeriod = {t['period']},
            .xDeadline = {t['deadline']},
            .xTaskPolicy = {t['policy']}
        }},"""

    polling_code = ""
    spawner_logic = ""
    
    if test_case.get("use_polling_server", False):
        prio = test_case['server_priority']
        polling_code = f"xCreatePollingServer({test_case['server_period']}, {test_case['server_deadline']}, {prio});"
        
        if "spawner_code" in test_case:
            spawner_logic = test_case["spawner_code"]
            tasks_str += """
            {
                .pcName = "SPAWNER",
                .pxTaskCode = vAperiodicSpawner,
                .pvParameters = NULL,
                .usStackDepth = configMINIMAL_STACK_SIZE * 4,
                .uxPriority = tskIDLE_PRIORITY + 2, 
                .xPeriod = 5000, 
                .xDeadline = 5000,
                .xTaskPolicy = POLICY_SKIP
            },"""

    mult = test_case.get('scale', 30000)
    final_content = f"#define BUSY_WAIT_MULT {mult}\n" + C_TEMPLATE % {
        "tasks_config": tasks_str,
        "global_policy": test_case.get("global_policy", "POLICY_SKIP"),
        "polling_server_code": polling_code,
        "spawner_code": spawner_logic
    }
    
    with open(MAIN_C_PATH, "w") as f:
        f.write(final_content)


class TestCaseBuilder:
    """Fluent API to build test case configs for generate_main_c().

    Usage examples:
      b = TestCaseBuilder().set_scale(30000).addPeriodic(...).usePollingServer(...)
      cfg = b.addAperiodic(...).build()
    """
    def __init__(self):
        self.scale = 30000
        self.tasks = []
        self.use_polling_server = False
        self.server_period = None
        self.server_deadline = None
        self.server_priority = None
        self.spawner_lines = []
        self.global_policy = "POLICY_SKIP"

    def set_scale(self, mult):
        self.scale = mult
        return self

    def addPeriodic(self, name, priority, period, deadline, policy, workload, stack=None):
        stack = stack or 'configMINIMAL_STACK_SIZE * 2'
        self.tasks.append({
            'name': name,
            'priority': priority,
            'period': period,
            'deadline': deadline,
            'policy': policy,
            'workload': workload,
            'stack': stack
        })
        return self

    def usePollingServer(self, period, deadline, priority):
        self.use_polling_server = True
        self.server_period = period
        self.server_deadline = deadline
        self.server_priority = priority
        return self

    def addAperiodic(self, workload, ticks_ms, policy, delay_after_ms=None, start_at_tick_expr='xTaskGetTickCount()'):
        # Single aperiodic spawn, optionally followed by a vTaskDelay
        # Provide a simple name for the aperiodic job and use the correct signature:
        # xTaskCreateAperiodic(pxTaskCode, pcName, pvParameters, xSoftDeadline, xPolicy, xStartReleaseTime);
        idx = len(self.spawner_lines) + 1
        line = f'xTaskCreateAperiodic(vAperiodicJobWork, "APER_{idx}", (void*){workload}, pdMS_TO_TICKS({ticks_ms}), {policy}, {start_at_tick_expr});'
        self.spawner_lines.append(line)
        if delay_after_ms is not None:
            self.spawner_lines.append(f'vTaskDelay(pdMS_TO_TICKS({delay_after_ms}));')
        return self

    def addAperiodicRepeated(self, count, workload, ticks_ms, policy, inter_delay_ms=0):
        # Repeats addAperiodic count times, inserting vTaskDelay(inter_delay_ms) between
        for i in range(count):
            self.addAperiodic(workload, ticks_ms, policy)
            if i != count-1 and inter_delay_ms > 0:
                self.spawner_lines.append(f'vTaskDelay(pdMS_TO_TICKS({inter_delay_ms}));')
        return self

    def addAperiodicLoopUntilFail(self, loop_count, workload, ticks_ms, policy, start_at_tick_expr='xTaskGetTickCount()+5000'):
        # Generates a C for-loop that keeps creating aperiodic tasks until create fails
        loop = []
        loop.append(f'for(int i=0; i<{loop_count}; i++) {{')
        loop.append(f'    if(xTaskCreateAperiodic(vAperiodicJobWork, "APER_LOOP", (void*){workload}, pdMS_TO_TICKS({ticks_ms}), {policy}, {start_at_tick_expr}) != pdPASS) {{')
        loop.append('        UART_printf("QUEUE_FULL\\n"); break;')
        loop.append('    }')
        loop.append('}')
        self.spawner_lines.append('\n'.join(loop))
        return self

    def set_global_policy(self, policy_str):
        self.global_policy = policy_str
        return self

    def build(self):
        return {
            'scale': self.scale,
            'tasks': self.tasks,
            'use_polling_server': self.use_polling_server,
            'server_period': self.server_period,
            'server_deadline': self.server_deadline,
            'server_priority': self.server_priority,
            'spawner_code': '\n'.join(self.spawner_lines),
            'global_policy': self.global_policy
        }

def run_build():
    subprocess.run(["make", "clean"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    result = subprocess.run(["make"], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if result.returncode != 0:
        print("Build Failed:")
        print(result.stderr.decode(errors='replace'))
        return False
    return True

def run_simulation(duration_sec=3):
    try:
        proc = subprocess.Popen(QEMU_CMD, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=False)
        time.sleep(duration_sec)
        proc.terminate()
        try:
            outs, errs = proc.communicate(timeout=1)
        except subprocess.TimeoutExpired:
            proc.kill()
            outs, errs = proc.communicate()
        return outs.decode('utf-8', errors='replace')
    except Exception as e:
        print(f"Simulation Error: {e}")
        return ""

def parse_logs(log_output):
    events = []
    regex = re.compile(r"\[\s*(\d+)\]\s+(\S+)\s+(.+)")
    for line in log_output.splitlines():
        match = regex.search(line)
        if match:
            events.append({
                "tick": int(match.group(1)),
                "task": match.group(2),
                "event": match.group(3).strip()
            })
    return events, log_output

# --- 2. Stats & Validators ---

def calculate_stats(logs, tasks_info):
    stats = {}
    for task in tasks_info:
        stats[task['name']] = {"est_runtime": 0.0, "misses": 0}

    if not logs: return stats, 1
    
    timestamps = [e["tick"] for e in logs]
    total_sim_ticks = max(timestamps) - min(timestamps) if timestamps else 1
    if total_sim_ticks < 100: total_sim_ticks = 100 

    for task in tasks_info:
        name = task['name']
        t_logs = [e for e in logs if e["task"] == name]
        
        # Count activations via RELEASE to accurately gauge periodic load
        activations = len([e for e in t_logs if "RELEASE" in e["event"]])
        if activations == 0:
            # Fallback for non-periodic tasks
            activations = len([e for e in t_logs if "START" in e["event"]])

        workload = task.get('workload', 0)
        if workload == 0: workload = 0.5 
            
        est_cpu_ticks = activations * workload
        misses = len([e for e in t_logs if "DEADLINE_MISS" in e["event"]])
        stats[name] = {"est_runtime": est_cpu_ticks, "misses": misses}
        
    return stats, total_sim_ticks

def run_test(test_name, conf, validator=None, print_only=False):
    print(f"--- {test_name} ---")
    print("  Generating...", end="")
    generate_main_c(conf)
    print(" Compiling...", end="")
    if not run_build(): return
    print(" Running...", end="")

    logs_events, logs_raw = parse_logs(run_simulation(3.5))
    print(" Done.")

    stats, total_ticks = calculate_stats(logs_events, conf["tasks"])

    # If print_only or no validator provided, skip validation and just print logs
    if print_only or validator is None:
        print("  RAW LOGS:")
        print(logs_raw)
        status_label = colorize("[RAW]", COLOR_YELLOW)
        msg = "Printed logs only (no validation)."
    else:
        passed, msg = validator(logs_events, stats, logs_raw, total_ticks)
        status_label = colorize("[PASS]", COLOR_GREEN) if passed else colorize("[FAIL]", COLOR_RED)

    print(f"  RESULT: {status_label} {msg}")

    if stats:
        print(f"  STATS (Sim Duration: {total_ticks} ticks):")
        total_cpu = 0
        for t, s in stats.items():
            cpu_pct = (s['est_runtime'] / total_ticks) * 100.0
            if cpu_pct > 100.0:
                cpu_pct = 100.0
            total_cpu += cpu_pct
            print(f"    {t}: Est.CPU={cpu_pct:.1f}%, Misses={s['misses']}")

        if "Overhead" in test_name:
            print(f"    TOTAL CPU Used by Tasks: {total_cpu:.1f}%")
            print(f"    ESTIMATED IDLE TIME: {100.0 - total_cpu:.1f}%")
    print("")

# --- 3. Validators ---

def val_stress(logs, stats, raw, total_ticks):
    misses = sum(s["misses"] for s in stats.values())
    if misses == 0: return True, "No deadline misses."
    return False, f"Total Misses: {misses}"

def val_preemption(logs, stats, raw, total_ticks):
    low = stats.get("T_LOW", {}).get("est_runtime", 0)
    high = stats.get("T_HIGH", {}).get("est_runtime", 0)
    if low > 0 and high > 0: return True, "Preemption Verified."
    return False, "One task starved."

def val_jitter(logs, stats, raw, total_ticks):
    if stats["J0"]["misses"] == 0: return True, "Jitter OK."
    return False, "Jitter caused miss."

def val_deadline_miss(logs, stats, raw, total_ticks):
    if stats["DM"]["misses"] > 0: return True, "Deadline Miss detected."
    return False, "No Deadline Miss detected."

def val_policy_skip(logs, stats, raw, total_ticks):
    if "SKIP" in raw: return True, "SKIP Policy applied."
    return False, "SKIP event not found."

def val_policy_kill(logs, stats, raw, total_ticks):
    if "KILL" in raw or stats["Kill"]["misses"] > 0: return True, "KILL Policy applied."
    return False, "KILL event not found."

def val_policy_catchup(logs, stats, raw, total_ticks):
    if "CATCH" in raw: return True, "CATCH_UP Policy applied (Log found)."
    return False, "CATCH_UP event not found in logs."

def val_rr(logs, stats, raw, total_ticks):
    if stats["RR1"]["est_runtime"] > 0 and stats["RR2"]["est_runtime"] > 0: return True, "Round Robin verified."
    return False, "One RR task starved."

def val_aper_basic(logs, stats, raw, total_ticks):
    if raw.count("APER_DONE") >= 3: return True, "3 Tasks Completed."
    return False, "Tasks failed."

def val_aper_kill(logs, stats, raw, total_ticks):
    miss = "DEADLINE_MISS" in raw or any("DEADLINE_MISS" in e["event"] for e in logs)
    done = "APER_DONE" in raw
    if miss and not done: return True, "Kill verified."
    return False, f"Miss={miss}, Done={done}"

def val_aper_overrun(logs, stats, raw, total_ticks):
    miss = "DEADLINE_MISS" in raw or any("DEADLINE_MISS" in e["event"] for e in logs)
    done = "APER_DONE" in raw
    if miss and done: return True, "Overrun verified."
    return False, f"Miss={miss}, Done={done}"

def val_aper_full(logs, stats, raw, total_ticks):
    if "QUEUE_FULL" in raw: return True, "Queue Full detected."
    return False, "Queue did not fill."

def val_aper_mixed(logs, stats, raw, total_ticks):
    per_miss = stats.get("PER_1", {}).get("misses", 0)
    aper_count = raw.count("APER_DONE")
    if per_miss == 0 and aper_count >= 1: return True, "Mixed load handled."
    return False, f"PerMiss={per_miss}, AperCount={aper_count}"

def val_overhead(logs, stats, raw, total_ticks):
    total_task_cpu = 0
    for s in stats.values():
        total_task_cpu += (s['est_runtime'] / total_ticks) * 100.0
    
    if total_task_cpu < 10.0:
        return True, f"Overhead OK ({total_task_cpu:.2f}% < 10%)"
    return False, f"High Overhead ({total_task_cpu:.2f}%)"

# --- Main ---

if __name__ == "__main__":
    try:
        backup_main()

        # Build a registry of tests: (id, name, config, validator)
        tests = []

        # 1. Stress Test
        builder1 = TestCaseBuilder().set_scale(30000)
        for i in range(1, 9):
            builder1.addPeriodic(f"T{i}", "tskIDLE_PRIORITY+3", i*10, i*10, "POLICY_SKIP", 2)
        tests.append((1, "1. StressTest_OverlappingHRT", builder1.build(), val_stress))

        # 2. Minimal Gap - Workload 1
        builder2 = TestCaseBuilder().set_scale(30000)
        builder2.addPeriodic("TA", "tskIDLE_PRIORITY+3", 10, 10, "POLICY_SKIP", 1)
        builder2.addPeriodic("TB", "tskIDLE_PRIORITY+3", 11, 11, "POLICY_SKIP", 1)
        tests.append((2, "2. TestEdge_MinimalTimeGap", builder2.build(), val_stress))

        # 3. Preemption
        builder3 = TestCaseBuilder().set_scale(30000)
        builder3.addPeriodic("T_LOW", "tskIDLE_PRIORITY+3", 50, 50, "POLICY_SKIP", 20)
        builder3.addPeriodic("T_HIGH", "tskIDLE_PRIORITY+4", 20, 20, "POLICY_SKIP", 5)
        tests.append((3, "3. Test_PreemptionHigher", builder3.build(), val_preemption))

        # 4. Jitter
        builder4 = TestCaseBuilder().set_scale(30000)
        builder4.addPeriodic("J0", "tskIDLE_PRIORITY+3", 20, 20, "POLICY_SKIP", 1)
        tests.append((4, "4. Test_ReleaseJitter", builder4.build(), val_jitter))

        # 5. Deadline Miss
        builder5 = TestCaseBuilder().set_scale(500000)
        builder5.addPeriodic("DM", "tskIDLE_PRIORITY+3", 10, 10, "POLICY_SKIP", 200)
        tests.append((5, "5. Test_DeadlineMiss", builder5.build(), val_deadline_miss))

        # 6. Policy SKIP
        builder6 = TestCaseBuilder().set_scale(500000)
        builder6.addPeriodic("Skip", "tskIDLE_PRIORITY+3", 10, 10, "POLICY_SKIP", 200)
        tests.append((6, "6. Test_OverrunPolicy_SKIP", builder6.build(), val_policy_skip))

        # 7. Policy KILL
        builder7 = TestCaseBuilder().set_scale(500000)
        builder7.addPeriodic("Kill", "tskIDLE_PRIORITY+3", 20, 20, "POLICY_KILL", 200)
        tests.append((7, "7. Test_OverrunPolicy_KILL", builder7.build(), val_policy_kill))

        # 8. Policy CATCH_UP - Workload 40, Prio 0
        builder8 = TestCaseBuilder().set_scale(500000)
        builder8.addPeriodic("Catch", "tskIDLE_PRIORITY+3", 20, 20, "POLICY_CATCH_UP", 40, stack="configMINIMAL_STACK_SIZE * 8")
        tests.append((8, "8. Test_OverrunPolicy_CATCH_UP", builder8.build(), val_policy_catchup))

        # 9. Round Robin
        builder9 = TestCaseBuilder().set_scale(30000)
        builder9.addPeriodic("RR1", "tskIDLE_PRIORITY+3", 50, 50, "POLICY_SKIP", 2000)
        builder9.addPeriodic("RR2", "tskIDLE_PRIORITY+3", 50, 50, "POLICY_SKIP", 2000)
        tests.append((9, "9. Test_RoundRobin", builder9.build(), val_rr))

        # 11. Aperiodic Basic (fluent)
        builder11 = TestCaseBuilder().set_scale(30000).usePollingServer(50, 50, "tskIDLE_PRIORITY+3")
        builder11.addAperiodic(5, 30, "APERIODIC_POLICY_OVERRUN", delay_after_ms=10)
        builder11.addAperiodic(5, 30, "APERIODIC_POLICY_OVERRUN", delay_after_ms=10)
        builder11.addAperiodic(5, 30, "APERIODIC_POLICY_OVERRUN")
        tests.append((11, "11. Test_AperiodicServer", builder11.build(), val_aper_basic))

        # 12. Aperiodic Kill (fluent)
        builder12 = TestCaseBuilder().set_scale(50000).usePollingServer(50, 50, "tskIDLE_PRIORITY+3")
        builder12.addAperiodic(1500, 20, "APERIODIC_POLICY_KILL")
        tests.append((12, "12. Test_AperiodicKill", builder12.build(), val_aper_kill))

        # 13. Aperiodic Overrun (fluent)
        builder13 = TestCaseBuilder().set_scale(50000).usePollingServer(50, 50, "tskIDLE_PRIORITY+3")
        builder13.addAperiodic(1500, 20, "APERIODIC_POLICY_OVERRUN")
        tests.append((13, "13. Test_AperiodicOverrun", builder13.build(), val_aper_overrun))

        # 14. Aperiodic Queue Full (fluent)
        builder14 = TestCaseBuilder().set_scale(30000).usePollingServer(5000, 5000, "tskIDLE_PRIORITY+3")
        builder14.addAperiodicLoopUntilFail(5000, 0, 50, "APERIODIC_POLICY_OVERRUN")
        tests.append((14, "14. Test_AperiodicQueueFull", builder14.build(), val_aper_full))

        # 15. Mixed (fluent)
        builder15 = TestCaseBuilder().set_scale(30000).usePollingServer(40, 40, "tskIDLE_PRIORITY+2")
        builder15.addPeriodic("PER_1", "tskIDLE_PRIORITY+4", 30, 30, "POLICY_SKIP", 5)
        builder15.addPeriodic("PER_2", "tskIDLE_PRIORITY+3", 50, 50, "POLICY_SKIP", 5)
        builder15.addAperiodicRepeated(3, 5, 50, "APERIODIC_POLICY_OVERRUN", inter_delay_ms=60)
        tests.append((15, "15. Test_AperiodicMixed", builder15.build(), val_aper_mixed))

        # 16. System Overhead
        builder16 = TestCaseBuilder().set_scale(30000)
        for i in range(1, 9):
            builder16.addPeriodic(f"T{i}", "tskIDLE_PRIORITY+2", 200, 200, "POLICY_SKIP", 1) # 1 tick tasks
        tests.append((16, "16. Test_SystemOverhead", builder16.build(), val_overhead))

        builder17 = TestCaseBuilder().set_scale(50000)
        # 17. Custom Test - User Defined (fluent)
        builder17.set_global_policy("POLICY_CATCH_UP")
        builder17.addPeriodic("Custom1", "tskIDLE_PRIORITY+3",1000,80, "POLICY_CATCH_UP", workload=500)
        tests.append((17, "17. Test_Custom_CATCH_UP", builder17.build(), None))


        # Argument parsing: allow selection of tests by number and a print-only flag
        parser = argparse.ArgumentParser(description="Run selected tests from the test suite")
        parser.add_argument('tests', nargs='*', type=int, help='Test numbers to run (e.g., 5 12 7)')
        parser.add_argument('-p', '--print-only', action='store_true', help='Do not validate; just print raw logs')
        args = parser.parse_args()

        # Determine run order
        if args.tests:
            # Run tests in the order requested by the user
            for tid in args.tests:
                matched = [t for t in tests if t[0] == tid]
                if not matched:
                    print(f"Skipping unknown test id: {tid}")
                    continue
                _id, name, conf, validator = matched[0]
                run_test(name, conf, validator if not args.print_only else None, print_only=args.print_only)
        else:
            # Run all tests in the registered order
            for _id, name, conf, validator in tests:
                run_test(name, conf, validator if not args.print_only else None, print_only=args.print_only)

    except KeyboardInterrupt:
        print("\nAborted.")
    except Exception as e:
        print(f"\nError: {e}")
    finally:
        restore_main()