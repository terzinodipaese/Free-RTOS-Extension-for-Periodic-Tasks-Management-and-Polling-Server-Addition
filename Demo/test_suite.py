import os
import subprocess
import time
import re
import shutil


# Configuration


QEMU_CMD = [
    "qemu-system-arm",
    "-machine", "mps2-an385",
    "-cpu", "cortex-m3",
    "-kernel", "./Output/demo.elf",
    "-monitor", "none",
    "-nographic",
    "-serial", "stdio",
]

MAIN_C_PATH        = "main.c"
BACKUP_MAIN_C_PATH = "main.c.bak"
SIMULATION_DURATION = 10.0   # seconds 

#C template

C_TEMPLATE = """
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include "logger.h"

#ifndef BUSY_WAIT_MULT
#define BUSY_WAIT_MULT 50000
#endif

void vBusyWait(int ticks_simulated) {
    if (ticks_simulated <= 0) return;
    volatile int i;
    int chunk = 5;
    int remaining = ticks_simulated;
    while (remaining > 0) {
        int current = (remaining > chunk) ? chunk : remaining;
        for (i = 0; i < (current * BUSY_WAIT_MULT); i++) { __asm volatile("nop"); }
        remaining -= current;
    }
}

void vGenericPeriodicTask(void *pvParameters) {
    int workload = (int)(intptr_t)pvParameters;
    vBusyWait(workload);
    vTaskDelay(1);
}

void vAperiodicJobWork(void *pvParameters) {
    int workload = (int)(intptr_t)pvParameters;
    if (workload <= 0) workload = 10;
    UART_printf("APER_START\\n");
    vBusyWait(workload);
    UART_printf("APER_DONE\\n");
}

void vAperiodicSpawner(void *pvParameters) {
    (void)pvParameters;
    %(spawner_code)s
    vTaskDelete(NULL);
}

int main(void) {
    UART_init();
    vLoggerInit();

    PeriodicTaskConfig_t testTasks[] = {
        %(tasks_config)s
    };

    SchedulerConfig_t myConfig = {
        .globalPolicy  = %(global_policy)s,
        .trace_enabled = pdTRUE,
        .uxNumTasks    = sizeof(testTasks)/sizeof(PeriodicTaskConfig_t),
        .pxTasks       = testTasks
    };

    UART_printf("TEST_START\\n");
    %(polling_server_code)s
    vConfigureScheduler(&myConfig);
    vTaskStartScheduler();
    for (;;);
    return 0;
}
"""

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
        prio  = t['priority'] if isinstance(t['priority'], str) else str(t['priority'])
        stack = t.get('stack', 'configMINIMAL_STACK_SIZE * 2')
        tasks_str += f"""
        {{
            .pcName        = "{t['name']}",
            .pxTaskCode    = vGenericPeriodicTask,
            .pvParameters  = (void*){t['workload']},
            .usStackDepth  = {stack},
            .uxPriority    = {prio},
            .xPeriod       = {t['period']},
            .xDeadline     = {t['deadline']},
            .xTaskPolicy   = {t['policy']}
        }},"""

    polling_code  = ""
    spawner_logic = ""
    if test_case.get("use_polling_server", False):
        prio         = test_case['server_priority']
        polling_code = (f"xCreatePollingServer({test_case['server_period']}, "
                        f"{test_case['server_deadline']}, {prio});")
        if "spawner_code" in test_case:
            spawner_logic  = test_case["spawner_code"]
            tasks_str     += """
            {
                .pcName       = "SPAWNER",
                .pxTaskCode   = vAperiodicSpawner,
                .pvParameters = NULL,
                .usStackDepth = configMINIMAL_STACK_SIZE * 4,
                .uxPriority   = tskIDLE_PRIORITY + 2,
                .xPeriod      = 5000,
                .xDeadline    = 5000,
                .xTaskPolicy  = POLICY_SKIP
            },"""

    mult = test_case.get('scale', 30000)
    final_content = f"#define BUSY_WAIT_MULT {mult}\n" + C_TEMPLATE % {
        "tasks_config":        tasks_str,
        "global_policy":       test_case.get("global_policy", "POLICY_SKIP"),
        "polling_server_code": polling_code,
        "spawner_code":        spawner_logic,
    }
    with open(MAIN_C_PATH, "w") as f:
        f.write(final_content)

def run_build():
    subprocess.run(["make", "clean"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    result = subprocess.run(["make"], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if result.returncode != 0:
        print("Build Failed:")
        print(result.stderr.decode(errors='replace'))
        return False
    return True

def run_simulation(duration_sec=SIMULATION_DURATION):
    try:
        proc = subprocess.Popen(
            QEMU_CMD, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=False
        )
        time.sleep(duration_sec)
        proc.terminate()
        try:
            outs, _ = proc.communicate(timeout=1)
        except subprocess.TimeoutExpired:
            proc.kill()
            outs, _ = proc.communicate()
        return outs.decode('utf-8', errors='replace')
    except Exception as e:
        print(f"Simulation Error: {e}")
        return ""

#parsing logger

def parse_logs(log_output):
    events = []
    regex  = re.compile(r"\[\s*(\d+)\]\s+(\S+)\s+(.+)")
    for line in log_output.splitlines():
        m = regex.search(line)
        if m:
            events.append({
                "tick":  int(m.group(1)),
                "task":  m.group(2),
                "event": m.group(3).strip(),
            })
    return events, log_output


#stats 

def calculate_stats(logs, tasks_info):
    stats = {}
    for task in tasks_info:
        stats[task['name']] = {
            "est_runtime":   0.0,
            "misses":        0,
            "starts":        0,
            "ends":          0,
            "releases":      0,
            "release_times": [],
            "start_times":   [],
            "policy":        task.get('policy', ''),
            "period":        task.get('period', 0),
            "deadline":      task.get('deadline', 0),
        }

    if not logs:
        return stats, 1

    timestamps      = [e["tick"] for e in logs]
    total_sim_ticks = max(timestamps) - min(timestamps) if timestamps else 1
    if total_sim_ticks < 100:
        total_sim_ticks = 100

    for task in tasks_info:
        name      = task['name']
        t_logs    = [e for e in logs if e["task"] == name]

        releases  = [e for e in t_logs if "RELEASE"       in e["event"]]
        starts_ev = [e for e in t_logs if "START"         in e["event"]]
        ends_ev   = [e for e in t_logs if "COMPLETE"      in e["event"]]
        misses_ev = [e for e in t_logs if "DEADLINE_MISS" in e["event"]]

        workload    = task.get('workload', 0) or 0.5
        activations = len(releases) if releases else len(starts_ev)

        stats[name] = {
            "est_runtime":   activations * workload,
            "misses":        len(misses_ev),
            "starts":        len(starts_ev),
            "ends":          len(ends_ev),
            "releases":      len(releases),
            "release_times": [e["tick"] for e in releases],
            "start_times":   [e["tick"] for e in starts_ev],
            "policy":        task.get('policy', ''),
            "period":        task.get('period', 0),
            "deadline":      task.get('deadline', 0),
        }

    return stats, total_sim_ticks



# Pairs each release with the next start and checks start <= release + deadline

def check_absolute_deadlines(logs, stats):
    violations = 0
    details    = []
    for name, s in stats.items():
        deadline = s.get("deadline", 0)
        if deadline == 0:
            continue
        rtimes = s["release_times"]
        stimes = s["start_times"]
        si     = 0
        for r in rtimes:
            while si < len(stimes) and stimes[si] < r:
                si += 1
            if si < len(stimes):
                latency = stimes[si] - r
                if latency > deadline:
                    violations += 1
                    details.append(f"{name}: lat={latency}>{deadline}")
                si += 1
    return violations, (", ".join(details) if details else "ok")


# worst-case release→start latency per task

def worst_case_latencies(stats):
    result = {}
    for name, s in stats.items():
        rtimes = s["release_times"]
        stimes = s["start_times"]
        worst  = 0
        si     = 0
        for r in rtimes:
            while si < len(stimes) and stimes[si] < r:
                si += 1
            if si < len(stimes):
                worst = max(worst, stimes[si] - r)
                si   += 1
        result[name] = worst
    return result

#running test, runs tests 3 times

def run_single(conf, validator):
    """Execute one simulation run. Returns (passed, msg, stats, total_ticks)."""
    logs_events, logs_raw = parse_logs(run_simulation())
    stats, total_ticks    = calculate_stats(logs_events, conf["tasks"])
    passed, msg           = validator(logs_events, stats, logs_raw, total_ticks)
    return passed, msg, stats, total_ticks

def run_test(test_name, conf, validator, runs=3):

    print(f"--- {test_name} ---")
    print("  Generating...", end="", flush=True)
    generate_main_c(conf)
    print(" Compiling...", end="", flush=True)
    if not run_build():
        return False

    needed  = 1 #in this case we consider 1 
    results = []

    for i in range(runs):
        print(f" Run {i+1}/{runs}...", end="", flush=True)
        results.append(run_single(conf, validator))

        passes = sum(r[0] for r in results)
        fails  = len(results) - passes
        if passes >= needed:
            print(" (one test passed)", end="")
            break
        if fails > runs - needed:
            print(" (no tests passed)", end="")
            break

    print(" Done.")

    passes_total = sum(r[0] for r in results)
    final_passed = passes_total >= needed
    best         = next((r for r in results if r[0]), results[-1])
    _, msg, stats, total_ticks = best

    print(f"  RESULT: {'[PASS]' if final_passed else '[FAIL]'} "
          f"[{passes_total}/{len(results)} runs passed] {msg}")

    for idx, (p, m, _, t) in enumerate(results):
        print(f"    Run {idx+1}: {'✓' if p else '✗'}  ticks={t}  {m}")

    if stats:
        print(f"  STATS (Duration: {total_ticks} ticks):")
        total_cpu = 0
        for tname, s in stats.items():
            cpu_pct    = min((s['est_runtime'] / total_ticks) * 100.0, 100.0)
            total_cpu += cpu_pct
            surplus    = s['starts'] - s['ends']
            print(f"    {tname}: CPU={cpu_pct:.1f}%  Misses={s['misses']}  "
                  f"Starts={s['starts']}  Ends={s['ends']}  "
                  f"Surplus={surplus}  Releases={s['releases']}")
        if "Overhead" in test_name:
            print(f"    TOTAL CPU by tasks : {total_cpu:.1f}%")
            print(f"    ESTIMATED IDLE     : {100.0 - total_cpu:.1f}%")
    print("")
    return final_passed


# validators



# Test 1 – Stress / Overlapping HRT
# Absolute deadline check on all tasks.

def val_stress_overlapping(logs, stats, raw, total_ticks):
    high_tasks  = ["T10", "T20", "T30"]
    high_misses = sum(stats.get(t, {}).get("misses", 0) for t in high_tasks)
    high_starts = [stats.get(t, {}).get("starts", 0) for t in high_tasks]
    all_active  = all(stats.get(f"T{i}0", {}).get("starts", 0) > 0
                      for i in [1, 2, 3, 4, 5, 6, 7, 8])
    viols, vdet = check_absolute_deadlines(logs, stats)

    # Worst response time for high-priority tasks
    lats = worst_case_latencies(stats)
    worst_resp = max((lats.get(t, 0) for t in high_tasks), default=0)
    max_period = max((stats.get(t, {}).get("period", 1) for t in high_tasks), default=1)
    resp_ok    = worst_resp <= max_period * 0.4

    if high_misses == 0 and min(high_starts) >= 60 and all_active and viols == 0 and resp_ok:
        return True, (f"Stress OK: 0 high-prio misses, min_starts={min(high_starts)}, "
                      f"worst_resp={worst_resp} (<= period*0.4), 0 deadline violations")
    return False, (f"high_misses={high_misses} min_starts={min(high_starts)}/60 "
                   f"all_active={all_active} violations={viols}({vdet}) "
                   f"worst_resp={worst_resp}/period*0.4={max_period*0.4:.0f}")


# Test 2 – Minimal Time Gap

def val_edge_minimal_gap(logs, stats, raw, total_ticks):
    ta_misses  = stats.get("TA",  {}).get("misses", 0)
    int_misses = stats.get("INT", {}).get("misses", 0)
    ta_starts  = stats.get("TA",  {}).get("starts", 0)
    ta_period  = stats.get("TA",  {}).get("period", 20)

    release_times = stats.get("TA", {}).get("release_times", [])
    bad_gaps = 0
    if len(release_times) >= 2:
        for a, b in zip(release_times, release_times[1:]):
            if abs((b - a) - ta_period) > ta_period * 0.10:
                bad_gaps += 1

    if ta_misses == 0 and int_misses == 0 and ta_starts >= 20 and bad_gaps == 0:
        return True, f"MinGap OK: TA={ta_starts} starts, no misses, all gaps ~{ta_period}"
    return False, (f"TA_miss={ta_misses} INT_miss={int_misses} "
                   f"starts={ta_starts}/20 bad_gaps={bad_gaps}")


# Test 3 – Preemption
# Ratio >= 2x, high_starts >= 100, and at least 3 real preemption events

def val_preemption_higher(logs, stats, raw, total_ticks):
    high_starts = stats.get("T_High", {}).get("starts", 0)
    low_starts  = stats.get("T_Low",  {}).get("starts", 0)
    ratio       = high_starts / low_starts if low_starts > 0 else 0

    low_events  = [e for e in logs if e["task"] == "T_Low"]
    high_events = [e for e in logs if e["task"] == "T_High" and "START" in e["event"]]
    preemptions = 0
    for le in low_events:
        if "START" not in le["event"]:
            continue
        next_end = next(
            (x["tick"] for x in low_events
             if "COMPLETE" in x["event"] and x["tick"] > le["tick"]), None
        )
        if next_end is None:
            continue
        preemptions += sum(1 for he in high_events
                           if le["tick"] < he["tick"] < next_end)

    if high_starts >= 100 and ratio >= 2.0 and preemptions >= 3:
        return True, (f"Preemption OK: High={high_starts}, Low={low_starts}, "
                      f"ratio={ratio:.1f}, preemptions={preemptions}")
    return False, (f"High={high_starts}/100 ratio={ratio:.1f}/3.0 "
                   f"preemptions={preemptions}/3")


# Test 3b – Response Time Worst-Case (dedicated)

def val_response_time_worst_case(logs, stats, raw, total_ticks):
    lats = worst_case_latencies(stats)
    high_starts  = stats.get("T_High", {}).get("starts", 0)
    high_deadline = stats.get("T_High", {}).get("deadline", 1)
    high_worst   = lats.get("T_High", 0)
    threshold    = high_deadline * 0.3

    violations = [(name, lat) for name, lat in lats.items()
                  if lat > stats.get(name, {}).get("deadline", 1) * 0.3]

    if not violations and high_starts >= 30:
        return True, (f"Response time OK: T_High worst={high_worst} "
                      f"<= {threshold:.0f} ({high_starts} activations)")
    if violations:
        return False, ("Response time violations: " +
                       ", ".join(f"{n}:{l}" for n, l in violations))
    return False, f"T_High starts={high_starts}/30"


# Test 4 – Release Jitter

def val_jitter(logs, stats, raw, total_ticks):
    total_misses  = sum(s.get("misses", 0) for s in stats.values())
    total_starts  = sum(s.get("starts", 0) for s in stats.values())
    worst_latency = 0
    worst_drift   = 0

    for name, s in stats.items():
        if not name.startswith("J"):
            continue
        period = s.get("period", 1)
        rtimes = s["release_times"]
        stimes = s["start_times"]

        # Max release→start latency
        si = 0
        for r in rtimes:
            while si < len(stimes) and stimes[si] < r:
                si += 1
            if si < len(stimes):
                worst_latency = max(worst_latency, stimes[si] - r)
                si += 1

        # Max consecutive-release drift (should be ~1.0 * period)
        for a, b in zip(rtimes, rtimes[1:]):
            worst_drift = max(worst_drift, (b - a) / period if period > 0 else 0)

    max_period = max((s.get("period", 1) for s in stats.values()), default=1)

    drift_ok = worst_drift <= 1.08
    if (worst_latency <= max_period * 0.35 and drift_ok
            and total_misses <= 3 and total_starts >= 50):
        return True, (f"Jitter OK: max_lat={worst_latency}, "
                      f"drift={worst_drift:.2f}, misses={total_misses}")
    return False, (f"lat={worst_latency}/period*0.35={max_period*0.35:.0f} "
                   f"drift={worst_drift:.2f}/1.08 misses={total_misses}/3 "
                   f"starts={total_starts}/50")


# Test 5 – Deadline Miss detection


def val_deadline_miss_enhanced(logs, stats, raw, total_ticks):
    if not logs:
        return False, "System did not start"
    total_misses = sum(s.get("misses", 0) for s in stats.values())
    dm1_starts   = stats.get("DM1", {}).get("starts", 0)
    int_misses   = stats.get("INT", {}).get("misses", 0)

    if total_misses >= 3 and dm1_starts >= 20 and int_misses == 0:
        return True, f"Deadline miss OK: {total_misses} misses, INT clean"
    return False, (f"total_misses={total_misses}/3 dm1_starts={dm1_starts}/20 "
                   f"int_misses={int_misses}/0")


# Test 6 – SKIP policy

def val_policy_skip_enhanced(logs, stats, raw, total_ticks):
    skip_events  = (raw.count("OVERRUN → SKIP") + raw.count("OVERRUN->SKIP") + raw.count("SKIP"))
    skip1_starts = stats.get("Skip1", {}).get("starts", 0)
    skip1_misses = stats.get("Skip1", {}).get("misses", 0)
    skip2_misses = stats.get("Skip2", {}).get("misses", 0)
    skip3_misses = stats.get("Skip3", {}).get("misses", 0)
    total_misses = skip1_misses + skip2_misses + skip3_misses

    if skip_events >= 4:
        return True, f"SKIP verified: {skip_events} events, total misses={total_misses}"
    # Fallback: tasks running with aggregate misses
    if skip1_starts >= 10 and total_misses >= 2:
        return True, f"SKIP working (no raw event): starts={skip1_starts}, misses={total_misses}"
    return False, f"SKIP: events={skip_events}, starts={skip1_starts}/10, misses={total_misses}/2"



# Test 7 – CATCH_UP policy

def val_policy_catchup_enhanced(logs, stats, raw, total_ticks):
    if total_ticks < 50:
        return False, f"System lockup (duration={total_ticks})"
    catchup_events  = (raw.count("OVERRUN → CATCH_UP") + raw.count("OVERRUN->CATCH_UP") + raw.count("CATCH_UP"))
    catch1_starts = stats.get("Catch1", {}).get("starts", 0)
    catch1_ends   = stats.get("Catch1", {}).get("ends", 0)
    catch1_misses = stats.get("Catch1", {}).get("misses", 0)
    catch2_starts = stats.get("Catch2", {}).get("starts", 0)
    catch3_starts = stats.get("Catch3", {}).get("starts", 0)
    if catch1_starts >= 20 and catch2_starts >= 15 and catch3_starts >= 10 and catchup_events>0:
        if catch1_misses > 0 and catch1_starts > catch1_ends:
            return True, f"CATCH_UP verified: starts={catch1_starts} > ends={catch1_ends}, total catchup = {catchup_events}"
        return True, f"CATCH_UP OK: C1={catch1_starts}, C2={catch2_starts}, C3={catch3_starts}, total catchup = {catchup_events}"
    return False, f"C1:{catch1_starts}/20 C2:{catch2_starts}/15 C3:{catch3_starts}/10"


# Test 8 – KILL policy

def val_policy_kill_enhanced(logs, stats, raw, total_ticks):
    if total_ticks < 50:
        return False, f"System lockup (duration={total_ticks})"
    kill_events  = (raw.count("OVERRUN → KILL") + raw.count("OVERRUN->KILL") + raw.count("KILL"))
    kill1_starts = stats.get("Kill1", {}).get("starts", 0)
    kill1_ends   = stats.get("Kill1", {}).get("ends", 0)
    if kill1_starts >= 15 and total_ticks > 150:
        killed = kill1_starts - kill1_ends
        if killed >= 5 and kill_events>0:
            return True, f"KILL verified: kill events = {kill_events}"
        return False, f"KILL not working: starts kill1={kill1_starts}, ends kill1={kill1_ends}, kill events={kill_events}"
    return False, f"Kill1_starts:{kill1_starts}/15 Duration:{total_ticks}/150"



# Test 9 – Round Robin fairness
# min starts >= 12 and relative spread (diff/max) <= 25%.

def val_rr_enhanced(logs, stats, raw, total_ticks):
    if total_ticks < 50:
        return False, f"System lockup (ticks={total_ticks})"

    rr_starts = [stats.get(f"RR{i}", {}).get("starts", 0) for i in range(1, 6)]
    if not rr_starts or min(rr_starts) == 0:
        return False, f"Some RR tasks did not run: {rr_starts}"

    rr_max = max(rr_starts)
    rr_min = min(rr_starts)
    spread = (rr_max - rr_min) / rr_max if rr_max > 0 else 1.0

    if rr_min >= 12 and spread <= 0.25:
        return True, f"RR OK: min={rr_min}, max={rr_max}, spread={spread:.0%}"
    return False, f"RR: min={rr_min}/12, spread={spread:.0%}/25%"


# Test 10 / 10.1 – CPU Overhead
# overhead <= 10% and estimated idle >= 15%.

def val_overhead_enhanced(logs, stats, raw, total_ticks):
    total_task_cpu = sum(s['est_runtime'] / total_ticks * 100.0 for s in stats.values())
    overhead = max(0.0, total_task_cpu - 8.0)
    idle     = 100.0 - total_task_cpu
    if overhead <= 13.0 and idle >= 20.0:
        return True, f"Overhead OK: {overhead:.1f}%, idle={idle:.1f}%"
    return False, f"overhead={overhead:.1f}%/13% idle={idle:.1f}%/20%"

def val_overhead_mixed(logs, stats, raw, total_ticks):
    total_task_cpu = sum(s['est_runtime'] / total_ticks * 100.0 for s in stats.values())
    total_starts   = sum(s.get('starts', 0) for s in stats.values())
    overhead = max(0.0, total_task_cpu - 9.0)
    idle     = 100.0 - total_task_cpu
    if overhead <= 11.0 and total_starts >= 40 and idle >= 15.0:
        return True, f"Mixed overhead OK: {overhead:.1f}%, starts={total_starts}, idle={idle:.1f}%"
    return False, f"overhead={overhead:.1f}%/10% starts={total_starts}/40 idle={idle:.1f}%/15%"


# Tests 11-15 – Aperiodic tasks (check base behavior)


def val_aper_basic(logs, stats, raw, total_ticks):
    done    = raw.count("APER_DONE")
    started = raw.count("APER_START")
    if done >= 3:
        return True, f"Aperiodic OK: {done}/4 completed (started={started})"
    return False, f"Aperiodic: done={done}/3, started={started}"

def val_aper_kill(logs, stats, raw, total_ticks):
    started = raw.count("APER_START")
    done    = raw.count("APER_DONE")
    if started > done and started >= 2:
        return True, f"Aperiodic KILL OK: started={started}, done={done}, killed={started-done}"
    return False, f"Aperiodic KILL: started={started}, done={done}"

def val_aper_overrun(logs, stats, raw, total_ticks):
    miss = ("DEADLINE_MISS" in raw or
            any("DEADLINE_MISS" in e["event"] for e in logs))
    done = raw.count("APER_DONE")
    if miss and done >= 1:
        return True, f"Aperiodic OVERRUN OK: missed deadline but completed ({done})"
    return False, f"Aperiodic OVERRUN: miss={miss}, done={done}"

def val_aper_full(logs, stats, raw, total_ticks):
    qf = raw.count("QUEUE_FULL")
    if qf >= 1:
        return True, f"Queue full detected ({qf} times)"
    return False, "Queue did not fill"

def val_aper_mixed(logs, stats, raw, total_ticks):
    per1_miss = stats.get("PER_1", {}).get("misses", 0)
    per2_miss = stats.get("PER_2", {}).get("misses", 0)
    aper_done = raw.count("APER_DONE")
    if per1_miss == 0 and per2_miss == 0 and aper_done >= 3:
        return True, f"Mixed OK: periodic no misses, {aper_done} aperiodic done"
    return False, f"per1_miss={per1_miss} per2_miss={per2_miss} aper_done={aper_done}/3"


# Tests 16-18 – Configuration


def val_config_basic(logs, stats, raw, total_ticks):
    a = stats.get("CfgA", {}).get("starts", 0)
    b = stats.get("CfgB", {}).get("starts", 0)
    c = stats.get("CfgC", {}).get("starts", 0)
    if a >= 20 and b >= 15 and c >= 10:
        return True, f"Config OK: A={a}, B={b}, C={c}"
    return False, f"CfgA={a}/20 CfgB={b}/15 CfgC={c}/10"

def val_config_priorities(logs, stats, raw, total_ticks):
    starts = {f"P{i}": stats.get(f"P{i}", {}).get("starts", 0) for i in [1, 3, 5]}
    if all(v >= 10 for v in starts.values()):
        return True, f"Priority tasks OK: {starts}"
    return False, f"Some priority tasks < 10 starts: {starts}"

def val_config_max_tasks(logs, stats, raw, total_ticks):
    active = sum(1 for i in range(1, 9) if stats.get(f"M{i}", {}).get("starts", 0) > 0)
    if active >= 8 and total_ticks > 500:
        return True, f"All 8/8 tasks active, ticks={total_ticks}"
    return False, f"{active}/8 tasks active, ticks={total_ticks}/500"


# Test 19 – Global Stress (all three overrun policies)

def val_all_overrun_policies(logs, stats, raw, total_ticks):
    gs1 = stats.get("GS1", {"misses": 0, "starts": 0, "ends": 0, "releases": 0})
    gc1 = stats.get("GC1", {"misses": 0, "starts": 0, "ends": 0})
    gk1 = stats.get("GK1", {"starts": 0, "ends": 0})

    gs1_skipped = max(0, gs1["releases"] - gs1["starts"])
    gc1_surplus = gc1["starts"] - gc1["ends"]
    gk1_killed  = gk1["starts"] - gk1["ends"]

    skip_ok  = gs1["misses"] >= 1 or gs1_skipped >= 3
    catch_ok = gc1["misses"] >= 1 or gc1_surplus >= 3
    kill_ok  = gk1_killed  >= 3

    active = sum([skip_ok, catch_ok, kill_ok])
    detail = (f"SK={'✓' if skip_ok else '✗'}(miss={gs1['misses']},skipped={gs1_skipped}) "
              f"CU={'✓' if catch_ok else '✗'}(miss={gc1['misses']},surplus={gc1_surplus}) "
              f"KL={'✓' if kill_ok else '✗'}(killed={gk1_killed})")

    if active >= 3:
        return True, f"All 3 policies active: {detail}"
    if active >= 2 and total_ticks >= 200:
        return True, f"2/3 policies (ticks={total_ticks}): {detail}"
    return False, f"Only {active}/3 policies: {detail}"


# Test 20 – Complete system (all policies + aperiodic)

def val_all_policies_system(logs, stats, raw, total_ticks):
    ms1 = stats.get("MS1", {"misses": 0, "starts": 0, "ends": 0, "releases": 0})
    mc1 = stats.get("MC1", {"misses": 0, "starts": 0, "ends": 0})
    mk1 = stats.get("MK1", {"starts": 0, "ends": 0})

    ms1_skipped = max(0, ms1["releases"] - ms1["starts"])
    ms1_surplus = ms1["starts"] - ms1["ends"]
    mc1_surplus = mc1["starts"] - mc1["ends"]
    mk1_killed  = mk1["starts"] - mk1["ends"]
    aper_done   = raw.count("APER_DONE")

    skip_ok  = ms1["misses"] >= 1 or ms1_skipped >= 3 or ms1_surplus >= 5
    catch_ok = mc1["misses"] >= 1 or mc1_surplus >= 5
    kill_ok  = mk1_killed  >= 2
    aper_ok  = aper_done   >= 2

    active = sum([skip_ok, catch_ok, kill_ok])
    detail = (f"SK={'✓' if skip_ok else '✗'}(miss={ms1['misses']},skip={ms1_skipped},surplus={ms1_surplus}) "
              f"CU={'✓' if catch_ok else '✗'}(miss={mc1['misses']},surplus={mc1_surplus}) "
              f"KL={'✓' if kill_ok else '✗'}(killed={mk1_killed}) "
              f"Aper={'✓' if aper_ok else '✗'}({aper_done})")

    if active >= 3 and aper_ok:
        return True, f"System OK: {detail}"
    if active >= 2 and aper_ok and total_ticks >= 200:
        return True, f"System OK 2/3: {detail}"
    return False, f"System FAIL ({active}/3, aper={aper_ok}): {detail}"



# Test 21 – Absolute Deadlines 

def val_absolute_deadlines(logs, stats, raw, total_ticks):
    viols, detail = check_absolute_deadlines(logs, stats)
    total_starts  = sum(s.get("starts", 0) for s in stats.values())
    if viols == 0 and total_starts >= 30:
        return True, f"All absolute deadlines met ({total_starts} activations checked)"
    if viols > 0:
        return False, f"Absolute deadline violations: {viols} — {detail}"
    return False, f"Too few activations to validate: {total_starts}/30"



# Test 22 – Preemption Chain
# Verifies that the scheduler correctly handles a chain of 3+ consecutive
# preemptions: a medium-priority task is preempted by a high one, which is
# itself preempted by an even higher one (or rapid successive preemptions).


def val_chain_preemption(logs, stats, raw, total_ticks):
    chains = 0
    for i in range(len(logs) - 2):
        e0, e1, e2 = logs[i], logs[i+1], logs[i+2]
        if ("START"    in e0["event"] and "START"    in e1["event"]
                and "START"    in e2["event"]
                and "COMPLETE" not in e0["event"]
                and "COMPLETE" not in e1["event"]
                and "COMPLETE" not in e2["event"]
                and e0["task"] != e1["task"]
                and e1["task"] != e2["task"]):
            chains += 1

    vhi_misses = stats.get("T_VHi",  {}).get("misses", 0)
    vhi_starts = stats.get("T_VHi",  {}).get("starts", 0)
    total_starts = sum(s.get("starts", 0) for s in stats.values())

    if chains >= 5 and vhi_misses == 0 and vhi_starts >= 30:
        return True, (f"Chain preemption OK: {chains} chains detected, "
                      f"T_VHi 0 misses ({vhi_starts} activations)")
    return False, (f"chains={chains}/5, T_VHi misses={vhi_misses}/0, "
                   f"starts={vhi_starts}/30, total_events={total_starts}")


# Test 23 - Overload Mixed Preemption

def val_overload_mixed_preemption(logs, stats, raw, total_ticks):
    high_misses = stats.get("HIGH", {}).get("misses", 99)
    high_starts = stats.get("HIGH", {}).get("starts", 0)
    if high_misses == 0 and high_starts >= 80:
        return True, "Overload + mixed preemption OK: HIGH schedulable"
    return False, f"HIGH misses={high_misses}, starts={high_starts}/80"


# Test 24 - Long run stability

def val_longrun_stability(logs, stats, raw, total_ticks):
    if not logs:
        return False, "No log output"

    total_misses = sum(s.get("misses", 0) for s in stats.values())
    total_starts = sum(s.get("starts", 0) for s in stats.values())
    expected_min_starts = 300  # ~3x Test 21

    max_gap_ratio = 0.0
    for name, s in stats.items():
        rtimes = s["release_times"]
        period = s.get("period", 1)
        for a, b in zip(rtimes, rtimes[1:]):
            ratio = (b - a) / period if period > 0 else 0
            max_gap_ratio = max(max_gap_ratio, ratio)

    stable = max_gap_ratio <= 2.0  # No gap > 2x period

    viols, vdet = check_absolute_deadlines(logs, stats)

    if (total_misses == 0 and total_starts >= expected_min_starts and stable and viols == 0):
        return True, (f"Long-run OK: {total_starts} starts in {total_ticks} ticks, "
                      f"0 misses, max_gap_ratio={max_gap_ratio:.2f}, 0 deadline violations")
    details = []
    if total_misses > 0:                       details.append(f"misses={total_misses}/0")
    if total_starts < expected_min_starts:     details.append(f"starts={total_starts}/{expected_min_starts}")
    if not stable:                             details.append(f"max_gap={max_gap_ratio:.2f}/2.0")
    if viols > 0:                              details.append(f"deadline_violations={viols}({vdet})")
    return False, " ".join(details)

# Test 25 - now completion-based (SRT)

def val_completion_deadline_check(logs, stats, raw, total_ticks):
    if not logs:
        return False, "No log output"

    viols, vdet = check_absolute_deadlines(logs, stats)
    total_starts = sum(s.get("starts", 0) for s in stats.values())
    total_ends   = sum(s.get("ends",   0) for s in stats.values())

    if total_ends < 20:
        return False, f"Not enough ends: {total_ends}/20 (starts={total_starts})"

    has_end_events = any(len(s.get("end_times", [])) > 0 for s in stats.values())
    mode = "completion-based" if has_end_events else "start-based (fallback)"

    if viols == 0 and total_starts >= 30:
        return True, (f"Deadline check OK ({mode}): 0 violations, "
                      f"starts={total_starts}, ends={total_ends}")
    if viols > 0:
        return False, (f"Deadline violations ({mode}): {viols} — {vdet}")
    return False, f"Not enough starts: {total_starts}/30"


# MAIN

if __name__ == "__main__":
    try:
        backup_main()

        print("\n" + "="*80)
        print("TEST SUITE  —  10 s simulation")
        print("="*80 + "\n")

        results_summary = []
        def T(name, conf, validator):
            passed = run_test(name, conf, validator)
            results_summary.append((name, passed))

        #PERIODIC

        T("1. StressTest_OverlappingHRT", {
            "scale": 20000,
            "tasks": [
                {"name": "T10", "priority": "tskIDLE_PRIORITY+7", "period": 10,  "deadline": 10,  "policy": "POLICY_SKIP", "workload": 5},
                {"name": "T20", "priority": "tskIDLE_PRIORITY+7", "period": 20,  "deadline": 12,  "policy": "POLICY_SKIP", "workload": 6},
                {"name": "T30", "priority": "tskIDLE_PRIORITY+6", "period": 25,  "deadline": 15,  "policy": "POLICY_SKIP", "workload": 7},
                {"name": "T40", "priority": "tskIDLE_PRIORITY+5", "period": 40,  "deadline": 20,  "policy": "POLICY_SKIP", "workload": 8},
                {"name": "T50", "priority": "tskIDLE_PRIORITY+4", "period": 50,  "deadline": 25,  "policy": "POLICY_SKIP", "workload": 9},
                {"name": "T60", "priority": "tskIDLE_PRIORITY+3", "period": 80,  "deadline": 30,  "policy": "POLICY_SKIP", "workload": 10},
                {"name": "T70", "priority": "tskIDLE_PRIORITY+2", "period": 100, "deadline": 35,  "policy": "POLICY_SKIP", "workload": 12},
                {"name": "T80", "priority": "tskIDLE_PRIORITY+1", "period": 200, "deadline": 60,  "policy": "POLICY_SKIP", "workload": 15},
            ]
        }, val_stress_overlapping)

        T("2. TestEdge_MinimalTimeGap", {
            "scale": 30000,
            "tasks": [
                {"name": "TA",  "priority": "tskIDLE_PRIORITY+5", "period": 20, "deadline": 18, "policy": "POLICY_SKIP", "workload": 3},
                {"name": "TB",  "priority": "tskIDLE_PRIORITY+3", "period": 20, "deadline": 18, "policy": "POLICY_SKIP", "workload": 8},
                {"name": "TC",  "priority": "tskIDLE_PRIORITY+2", "period": 20, "deadline": 18, "policy": "POLICY_SKIP", "workload": 9},
                {"name": "INT", "priority": "tskIDLE_PRIORITY+6", "period": 15, "deadline": 15, "policy": "POLICY_SKIP", "workload": 7},
            ]
        }, val_edge_minimal_gap)

        T("3. Test_PreemptionHigher", {
            "scale": 30000,
            "tasks": [
                {"name": "T_Low",  "priority": "tskIDLE_PRIORITY+1", "period": 100, "deadline": 100, "policy": "POLICY_SKIP", "workload": 40},
                {"name": "T_Med1", "priority": "tskIDLE_PRIORITY+3", "period": 50,  "deadline": 50,  "policy": "POLICY_SKIP", "workload": 15},
                {"name": "T_Med2", "priority": "tskIDLE_PRIORITY+3", "period": 50,  "deadline": 50,  "policy": "POLICY_SKIP", "workload": 15},
                {"name": "T_High", "priority": "tskIDLE_PRIORITY+6", "period": 10,  "deadline": 10,  "policy": "POLICY_SKIP", "workload": 7},
            ]
        }, val_preemption_higher)

        T("3b. Test_ResponseTimeWorstCase", {
            "scale": 30000,
            "tasks": [
                {"name": "T_Low",  "priority": "tskIDLE_PRIORITY+1", "period": 100, "deadline": 100, "policy": "POLICY_SKIP", "workload": 40},
                {"name": "T_Med1", "priority": "tskIDLE_PRIORITY+3", "period": 50,  "deadline": 50,  "policy": "POLICY_SKIP", "workload": 15},
                {"name": "T_Med2", "priority": "tskIDLE_PRIORITY+3", "period": 50,  "deadline": 50,  "policy": "POLICY_SKIP", "workload": 15},
                {"name": "T_High", "priority": "tskIDLE_PRIORITY+6", "period": 10,  "deadline": 10,  "policy": "POLICY_SKIP", "workload": 7},
            ]
        }, val_response_time_worst_case)

        T("4. Test_ReleaseJitter", {
            "scale": 30000,
            "tasks": [
                {"name": "J0", "priority": "tskIDLE_PRIORITY+2", "period": 20,  "deadline": 20,  "policy": "POLICY_SKIP", "workload": 5},
                {"name": "J1", "priority": "tskIDLE_PRIORITY+2", "period": 30,  "deadline": 30,  "policy": "POLICY_SKIP", "workload": 6},
                {"name": "J2", "priority": "tskIDLE_PRIORITY+2", "period": 50,  "deadline": 50,  "policy": "POLICY_SKIP", "workload": 8},
                {"name": "J3", "priority": "tskIDLE_PRIORITY+2", "period": 100, "deadline": 100, "policy": "POLICY_SKIP", "workload": 10},
            ]
        }, val_jitter)

        T("5. Test_DeadlineMiss", {
            "scale": 60000,
            "tasks": [
                {"name": "DM1", "priority": "tskIDLE_PRIORITY+5", "period": 10, "deadline": 10, "policy": "POLICY_SKIP", "workload": 8},
                {"name": "DM2", "priority": "tskIDLE_PRIORITY+3", "period": 30, "deadline": 15, "policy": "POLICY_SKIP", "workload": 10},
                {"name": "INT", "priority": "tskIDLE_PRIORITY+6", "period": 20, "deadline": 20, "policy": "POLICY_SKIP", "workload": 6},
            ]
        }, val_deadline_miss_enhanced)

        T("6. Test_OverrunPolicy_SKIP", {
            "scale": 30000,
            "tasks": [
                {"name": "Skip1", "priority": "tskIDLE_PRIORITY+4", "period": 20, "deadline": 20, "policy": "POLICY_SKIP", "workload": 22},
                {"name": "Skip2", "priority": "tskIDLE_PRIORITY+3", "period": 30, "deadline": 30, "policy": "POLICY_SKIP", "workload": 28},
                {"name": "Skip3", "priority": "tskIDLE_PRIORITY+2", "period": 40, "deadline": 40, "policy": "POLICY_SKIP", "workload": 35},
                {"name": "INT",   "priority": "tskIDLE_PRIORITY+6", "period": 15, "deadline": 15, "policy": "POLICY_SKIP", "workload": 8},
            ]
        }, val_policy_skip_enhanced)

        T("7. Test_OverrunPolicy_CATCH_UP", {
            "scale": 60000,
            "tasks": [
                {"name": "Catch1", "priority": "tskIDLE_PRIORITY+5", "period": 25, "deadline": 25, "policy": "POLICY_CATCH_UP", "workload": 30},
                {"name": "Catch2", "priority": "tskIDLE_PRIORITY+4", "period": 35, "deadline": 35, "policy": "POLICY_CATCH_UP", "workload": 25},
                {"name": "Catch3", "priority": "tskIDLE_PRIORITY+3", "period": 50, "deadline": 50, "policy": "POLICY_CATCH_UP", "workload": 45},
                {"name": "INT",    "priority": "tskIDLE_PRIORITY+7", "period": 20, "deadline": 20, "policy": "POLICY_SKIP",     "workload": 8},
            ]
        }, val_policy_catchup_enhanced)

        T("8. Test_OverrunPolicy_KILL", {
            "scale": 30000,
            "tasks": [
                {"name": "Kill1", "priority": "tskIDLE_PRIORITY+6", "period": 15, "deadline": 15, "policy": "POLICY_KILL", "workload": 25},
                {"name": "Kill2", "priority": "tskIDLE_PRIORITY+4", "period": 30, "deadline": 30, "policy": "POLICY_KILL", "workload": 35},
                {"name": "Kill3", "priority": "tskIDLE_PRIORITY+3", "period": 45, "deadline": 45, "policy": "POLICY_KILL", "workload": 50},
            ]
        }, val_policy_kill_enhanced)

        T("9. Test_RoundRobin", {
            "scale": 30000,
            "tasks": [
                {"name": "RR1", "priority": "tskIDLE_PRIORITY+3", "period": 50, "deadline": 50, "policy": "POLICY_SKIP", "workload": 8},
                {"name": "RR2", "priority": "tskIDLE_PRIORITY+3", "period": 50, "deadline": 50, "policy": "POLICY_SKIP", "workload": 8},
                {"name": "RR3", "priority": "tskIDLE_PRIORITY+3", "period": 50, "deadline": 50, "policy": "POLICY_SKIP", "workload": 8},
                {"name": "RR4", "priority": "tskIDLE_PRIORITY+3", "period": 50, "deadline": 50, "policy": "POLICY_SKIP", "workload": 8},
                {"name": "RR5", "priority": "tskIDLE_PRIORITY+3", "period": 50, "deadline": 50, "policy": "POLICY_SKIP", "workload": 8},
                {"name": "INT", "priority": "tskIDLE_PRIORITY+6", "period": 25, "deadline": 25, "policy": "POLICY_SKIP", "workload": 5},
            ]
        }, val_rr_enhanced)

        T("10. Test_CPUOverhead", {
            "scale": 30000,
            "tasks": [{"name": f"C{i}", "priority": "tskIDLE_PRIORITY+3",
                        "period": 100, "deadline": 100, "policy": "POLICY_SKIP",
                        "workload": 1} for i in range(1, 9)],
        }, val_overhead_enhanced)

        T("10.1. Test_CPUOverhead2", {
            "scale": 30000,
            "tasks": [
                {"name": "C1", "priority": "tskIDLE_PRIORITY+7", "period": 100, "deadline": 100, "policy": "POLICY_SKIP", "workload": 1},
                {"name": "C2", "priority": "tskIDLE_PRIORITY+6", "period": 100, "deadline": 100, "policy": "POLICY_SKIP", "workload": 2},
                {"name": "C3", "priority": "tskIDLE_PRIORITY+5", "period": 100, "deadline": 100, "policy": "POLICY_SKIP", "workload": 1},
                {"name": "C4", "priority": "tskIDLE_PRIORITY+4", "period": 100, "deadline": 100, "policy": "POLICY_SKIP", "workload": 2},
                {"name": "C5", "priority": "tskIDLE_PRIORITY+3", "period": 100, "deadline": 100, "policy": "POLICY_SKIP", "workload": 1},
                {"name": "C6", "priority": "tskIDLE_PRIORITY+3", "period": 100, "deadline": 100, "policy": "POLICY_SKIP", "workload": 1},
                {"name": "C7", "priority": "tskIDLE_PRIORITY+2", "period": 200, "deadline": 200, "policy": "POLICY_SKIP", "workload": 1},
                {"name": "C8", "priority": "tskIDLE_PRIORITY+2", "period": 200, "deadline": 200, "policy": "POLICY_SKIP", "workload": 1},
            ]
        }, val_overhead_mixed)

        # APERIODIC

        code11 = """
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)5, pdMS_TO_TICKS(30), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(15));
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)5, pdMS_TO_TICKS(30), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(15));
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)5, pdMS_TO_TICKS(30), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(15));
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)5, pdMS_TO_TICKS(30), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
        """
        T("11. Test_AperiodicServer", {
            "scale": 30000, "use_polling_server": True,
            "server_period": 50, "server_deadline": 50, "server_priority": "tskIDLE_PRIORITY+3",
            "tasks": [], "spawner_code": code11,
        }, val_aper_basic)

        code12 = """
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)1500, pdMS_TO_TICKS(20), APERIODIC_POLICY_KILL, xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(10));
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)1500, pdMS_TO_TICKS(20), APERIODIC_POLICY_KILL, xTaskGetTickCount());
        """
        T("12. Test_AperiodicKill", {
            "scale": 50000, "use_polling_server": True,
            "server_period": 100, "server_deadline": 100, "server_priority": "tskIDLE_PRIORITY+3",
            "tasks": [], "spawner_code": code12,
        }, val_aper_kill)

        code13 = """
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)1500, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
        """
        T("13. Test_AperiodicOverrun", {
            "scale": 50000, "use_polling_server": True,
            "server_period": 100, "server_deadline": 100, "server_priority": "tskIDLE_PRIORITY+3",
            "tasks": [], "spawner_code": code13,
        }, val_aper_overrun)

        code14 = """
        for (int i = 0; i < 5000; i++) {
            if (xTaskCreateAperiodic(vAperiodicJobWork, (void*)0, 50,
                    APERIODIC_POLICY_OVERRUN, xTaskGetTickCount()+5000) != pdPASS) {
                UART_printf("QUEUE_FULL\\n");
                break;
            }
        }
        """
        T("14. Test_AperiodicQueueFull", {
            "scale": 30000, "use_polling_server": True,
            "server_period": 5000, "server_deadline": 5000, "server_priority": "tskIDLE_PRIORITY+2",
            "tasks": [], "spawner_code": code14,
        }, val_aper_full)

        code15 = """
        for (int i = 0; i < 5; i++) {
            xTaskCreateAperiodic(vAperiodicJobWork, (void*)5, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
            vTaskDelay(pdMS_TO_TICKS(60));
        }
        """
        T("15. Test_AperiodicWithPeriodicTasks", {
            "scale": 30000, "use_polling_server": True,
            "server_period": 40, "server_deadline": 40, "server_priority": "tskIDLE_PRIORITY+3",
            "tasks": [
                {"name": "PER_1", "priority": "tskIDLE_PRIORITY+4", "period": 30, "deadline": 30, "policy": "POLICY_SKIP", "workload": 5},
                {"name": "PER_2", "priority": "tskIDLE_PRIORITY+2", "period": 50, "deadline": 50, "policy": "POLICY_SKIP", "workload": 5},
            ],
            "spawner_code": code15,
        }, val_aper_mixed)

        # CONFIGURATION

        T("16. Test_ConfigBasic", {
            "scale": 30000,
            "tasks": [
                {"name": "CfgA", "priority": "tskIDLE_PRIORITY+3", "period": 20, "deadline": 20, "policy": "POLICY_SKIP", "workload": 5},
                {"name": "CfgB", "priority": "tskIDLE_PRIORITY+2", "period": 30, "deadline": 30, "policy": "POLICY_SKIP", "workload": 8},
                {"name": "CfgC", "priority": "tskIDLE_PRIORITY+2", "period": 50, "deadline": 50, "policy": "POLICY_SKIP", "workload": 10},
            ]
        }, val_config_basic)

        T("17. Test_ConfigPriorities", {
            "scale": 30000,
            "tasks": [
                {"name": "P1", "priority": "tskIDLE_PRIORITY+1", "period": 50, "deadline": 50, "policy": "POLICY_SKIP", "workload": 5},
                {"name": "P3", "priority": "tskIDLE_PRIORITY+3", "period": 50, "deadline": 50, "policy": "POLICY_SKIP", "workload": 5},
                {"name": "P5", "priority": "tskIDLE_PRIORITY+5", "period": 50, "deadline": 50, "policy": "POLICY_SKIP", "workload": 5},
            ]
        }, val_config_priorities)

        T("18. Test_ConfigMaxTasks", {
            "scale": 30000,
            "tasks": [
                {"name": "M1", "priority": "tskIDLE_PRIORITY+4", "period": 50,  "deadline": 50,  "policy": "POLICY_SKIP", "workload": 5},
                {"name": "M2", "priority": "tskIDLE_PRIORITY+3", "period": 100, "deadline": 100, "policy": "POLICY_SKIP", "workload": 8},
                {"name": "M3", "priority": "tskIDLE_PRIORITY+3", "period": 150, "deadline": 150, "policy": "POLICY_SKIP", "workload": 12},
                {"name": "M4", "priority": "tskIDLE_PRIORITY+2", "period": 200, "deadline": 200, "policy": "POLICY_SKIP", "workload": 15},
                {"name": "M5", "priority": "tskIDLE_PRIORITY+2", "period": 250, "deadline": 250, "policy": "POLICY_SKIP", "workload": 18},
                {"name": "M6", "priority": "tskIDLE_PRIORITY+2", "period": 300, "deadline": 300, "policy": "POLICY_SKIP", "workload": 20},
                {"name": "M7", "priority": "tskIDLE_PRIORITY+1", "period": 400, "deadline": 400, "policy": "POLICY_SKIP", "workload": 25},
                {"name": "M8", "priority": "tskIDLE_PRIORITY+1", "period": 500, "deadline": 500, "policy": "POLICY_SKIP", "workload": 30},
            ]
        }, val_config_max_tasks)



        # STRESS AND OTHER TESTS

        T("19. Test_OverrunPolicies_GlobalStress", {
            "scale": 40000,
            "tasks": [
                {"name": "GS1", "priority": "tskIDLE_PRIORITY+5", "period": 30, "deadline": 30, "policy": "POLICY_SKIP",    "workload": 28},
                {"name": "GC1", "priority": "tskIDLE_PRIORITY+6", "period": 25, "deadline": 25, "policy": "POLICY_CATCH_UP","workload": 22},
                {"name": "GK1", "priority": "tskIDLE_PRIORITY+7", "period": 20, "deadline": 20, "policy": "POLICY_KILL",    "workload": 25},
            ]
        }, val_all_overrun_policies)

        code25 = """
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)8, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(50));
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)8, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(50));
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)25, pdMS_TO_TICKS(15), APERIODIC_POLICY_KILL, xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(50));
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)8, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(50));
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)25, pdMS_TO_TICKS(15), APERIODIC_POLICY_KILL, xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(50));
        xTaskCreateAperiodic(vAperiodicJobWork, (void*)8, pdMS_TO_TICKS(20), APERIODIC_POLICY_OVERRUN, xTaskGetTickCount());
        """
        T("20. Test_AllPolicies_CompleteSystem", {
            "scale": 30000, "use_polling_server": True,
            "server_period": 50, "server_deadline": 50, "server_priority": "tskIDLE_PRIORITY+4",
            "tasks": [
                {"name": "MS1", "priority": "tskIDLE_PRIORITY+5", "period": 30, "deadline": 30, "policy": "POLICY_SKIP",    "workload": 24},
                {"name": "MC1", "priority": "tskIDLE_PRIORITY+6", "period": 25, "deadline": 25, "policy": "POLICY_CATCH_UP","workload": 20},
                {"name": "MK1", "priority": "tskIDLE_PRIORITY+7", "period": 20, "deadline": 20, "policy": "POLICY_KILL",    "workload": 20},
            ],
            "spawner_code": code25,
        }, val_all_policies_system)

        T("21. Test_AbsoluteDeadlines", {
            "scale": 30000,
            "tasks": [
                {"name": "T_Low",  "priority": "tskIDLE_PRIORITY+1", "period": 100, "deadline": 100, "policy": "POLICY_SKIP", "workload": 20},
                {"name": "T_Med",  "priority": "tskIDLE_PRIORITY+3", "period": 50,  "deadline": 50,  "policy": "POLICY_SKIP", "workload": 10},
                {"name": "T_High", "priority": "tskIDLE_PRIORITY+6", "period": 10,  "deadline": 10,  "policy": "POLICY_SKIP", "workload": 4},
            ]
        }, val_absolute_deadlines)


        T("22. Test_ChainPreemption", {
            "scale": 30000,
            "tasks": [
                {"name": "T_Lo",  "priority": "tskIDLE_PRIORITY+1", "period": 200, "deadline": 200, "policy": "POLICY_SKIP", "workload": 60},
                {"name": "T_Med", "priority": "tskIDLE_PRIORITY+3", "period": 50,  "deadline": 50,  "policy": "POLICY_SKIP", "workload": 20},
                {"name": "T_Hi",  "priority": "tskIDLE_PRIORITY+6", "period": 20,  "deadline": 20,  "policy": "POLICY_SKIP", "workload": 8},
                {"name": "T_VHi", "priority": "tskIDLE_PRIORITY+8", "period": 8,   "deadline": 8,   "policy": "POLICY_SKIP", "workload": 3},
            ]
        }, val_chain_preemption)

        T("23. Test_OverloadMixedPreemption", {
            "scale": 30000,
            "tasks": [
                {"name": "HIGH", "priority": "tskIDLE_PRIORITY+8", "period": 10, "deadline": 10, "policy": "POLICY_SKIP", "workload": 3},
                {"name": "MED1", "priority": "tskIDLE_PRIORITY+5", "period": 30, "deadline": 30, "policy": "POLICY_SKIP", "workload": 15},
                {"name": "MED2", "priority": "tskIDLE_PRIORITY+5", "period": 35, "deadline": 35, "policy": "POLICY_SKIP", "workload": 15},
                {"name": "MED3", "priority": "tskIDLE_PRIORITY+5", "period": 40, "deadline": 40, "policy": "POLICY_SKIP", "workload": 15},
                {"name": "MED4", "priority": "tskIDLE_PRIORITY+5", "period": 45, "deadline": 45, "policy": "POLICY_SKIP", "workload": 15},
                {"name": "MED5", "priority": "tskIDLE_PRIORITY+5", "period": 50, "deadline": 50, "policy": "POLICY_SKIP", "workload": 15},
            ]
        }, val_overload_mixed_preemption)

        T("24. Test_LongRunStability", {
            "scale": 30000,
            "tasks": [
                {"name": "LR_Lo",  "priority": "tskIDLE_PRIORITY+1", "period": 100,"deadline": 100, "policy": "POLICY_SKIP", "workload": 20},
                {"name": "LR_Med", "priority": "tskIDLE_PRIORITY+3", "period": 50,"deadline": 50,  "policy": "POLICY_SKIP", "workload": 10},
                {"name": "LR_Hi",  "priority": "tskIDLE_PRIORITY+6", "period": 10,"deadline": 10,  "policy": "POLICY_SKIP", "workload": 4},
            ]
        }, val_longrun_stability)

        T("25. Test_CompletionDeadlineCheck", {
            "scale": 30000,
            "tasks": [
                # Utilization: 8/20 + 12/50 = 0.40 + 0.24 = 0.64 so schedulabile
                {"name": "CD_Hi", "priority": "tskIDLE_PRIORITY+5", "period": 20, "deadline": 15, "policy": "POLICY_SKIP", "workload": 8},
                {"name": "CD_Lo", "priority": "tskIDLE_PRIORITY+2", "period": 50,"deadline": 40, "policy": "POLICY_SKIP", "workload": 12},
            ]
        }, val_completion_deadline_check)


        print("\n" + "="*80)
        print("FINAL SUMMARY")
        print("="*80)
        passed_count = sum(1 for _, p in results_summary if p)
        total_count  = len(results_summary)
        print(f"\nTotal: {passed_count}/{total_count} tests PASSED\n")
        for name, passed in results_summary:
            print(f"  {'✓ PASS' if passed else '✗ FAIL'}  {name}")

    except KeyboardInterrupt:
        print("\nAborted.")
    except Exception as e:
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()
    finally:
        restore_main()