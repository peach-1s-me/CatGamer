/* ============================================================
 * CatOS M0 演示程序
 *
 * 演示两件事：
 *   1) 固定优先级顺序：high(1) > mid(2) > low(3)。
 *      高优先级任务就绪时始终优先运行；yield 只在其同级任务之间轮转，
 *      因此 high 先运行完 10 次，随后 mid 运行 8 次，最后 low 运行 5 次。
 *   2) 抢占：slow(4) 运行中通过 resume 唤醒更高优先级的 preempt(2)，
 *      后者立即打断 slow，运行并退出后 slow 才继续。
 *
 * Sample output:
 *   [catos] M0 demo starting (fixed-priority + preemption)
 *   HHHHHHHHHH
 *   [high] completed 10 times
 *   MMMMMMMM
 *   [mid] completed 8 times
 *   LLLLL
 *   [low] completed 5 times
 *   [preempt] slow start
 *   [preempt] higher-priority task got CPU, preempting slow
 *   [preempt] slow finished (was preempted)
 *   [checker] all tasks done, exiting
 * ============================================================ */

#include "catos/catos.h"
#include <stdio.h>
#include <stdlib.h>

static void task_high(void *arg);
static void task_mid(void *arg);
static void task_low(void *arg);
static void task_preemp(void *arg);
static void task_slow(void *arg);
static void task_checker(void *arg);

static volatile int g_high_n, g_mid_n, g_low_n;
static volatile int g_slow_done;
static catos_task_t *g_preemp;

/* 模拟一段占用 CPU 的计算，用于观察调度效果 */
static void do_work(unsigned n)
{
    volatile unsigned x = 0;
    unsigned i;

    for (i = 0; i < n; i++)
        x += i;
    (void)x;
}

int main(void)
{
    catos_task_t *t;

    if (catos_kernel_init() != CATOS_OK) {
        printf("[demo] kernel init failed\n");
        return 1;
    }

    if (catos_task_create(&t, "high",   task_high,   NULL, 1, 0) != CATOS_OK ||
        catos_task_create(&t, "mid",    task_mid,    NULL, 2, 0) != CATOS_OK ||
        catos_task_create(&t, "low",    task_low,    NULL, 3, 0) != CATOS_OK ||
        catos_task_create(&g_preemp, "preemp", task_preemp, NULL, 2, 0) != CATOS_OK ||
        catos_task_create(&t, "slow",   task_slow,   NULL, 4, 0) != CATOS_OK ||
        catos_task_create(&t, "checker", task_checker, NULL, 5, 0) != CATOS_OK) {
        printf("[demo] task create failed\n");
        return 1;
    }

    /* preemp 先挂起，等 slow 运行后恢复它，以演示抢占 */
    catos_task_suspend(g_preemp);

    printf("[catos] M0 demo starting (fixed-priority + preemption)\n");
    fflush(stdout);
    catos_kernel_start();   /* 进入多任务模式，不再返回 */

    return 0;   /* 不可达 */
}

/* ---- 1) 固定优先级顺序 ---- */

static void task_high(void *arg)
{
    (void)arg;
    while (g_high_n < 10) {
        do_work(300000u);
        printf("H");
        fflush(stdout);
        g_high_n++;
        catos_sched_yield();   /* 同优先级无其他任务，yield 后仍是自己 */
    }
    printf("\n[high] completed %d times\n", g_high_n);
    fflush(stdout);
    catos_task_exit();
}

static void task_mid(void *arg)
{
    (void)arg;
    while (g_mid_n < 8) {
        do_work(300000u);
        printf("M");
        fflush(stdout);
        g_mid_n++;
        catos_sched_yield();
    }
    printf("\n[mid] completed %d times\n", g_mid_n);
    fflush(stdout);
    catos_task_exit();
}

static void task_low(void *arg)
{
    (void)arg;
    while (g_low_n < 5) {
        do_work(300000u);
        printf("L");
        fflush(stdout);
        g_low_n++;
        catos_sched_yield();
    }
    printf("\n[low] completed %d times\n", g_low_n);
    fflush(stdout);
    catos_task_exit();
}

/* ---- 2) 抢占演示 ---- */

static void task_preemp(void *arg)
{
    (void)arg;
    printf("[preempt] higher-priority task got CPU, preempting slow\n");
    fflush(stdout);
    catos_task_exit();
}

static void task_slow(void *arg)
{
    (void)arg;
    printf("[preempt] slow start\n");
    fflush(stdout);
    catos_task_resume(g_preemp);   /* 唤醒 prio2 任务 -> 立即抢占本任务 */
    do_work(1200000u);             /* 本段本应在抢占期间被搁置 */
    printf("[preempt] slow finished (was preempted)\n");
    fflush(stdout);
    g_slow_done = 1;
    catos_task_exit();
}

/* 最低优先级用户任务：所有更高优先级任务完成后才运行，负责退出 */
static void task_checker(void *arg)
{
    (void)arg;
    while (!(g_high_n >= 10 && g_mid_n >= 8 && g_low_n >= 5 && g_slow_done))
        catos_sched_yield();

    printf("[checker] all tasks done, exiting\n");
    fflush(stdout);
    exit(0);
}
