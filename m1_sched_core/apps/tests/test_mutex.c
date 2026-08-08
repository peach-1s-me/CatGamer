/* ============================================================
 * CatOS M1 互斥量 / 优先级继承测试（FR-SYNC-001、FR-PRIO）
 *
 * 覆盖：
 *   t1 互斥量基本操作：lock / trylock / 嵌套获取 / unlock / 非持有者释放
 *   t2 经典优先级反转（FR-TEST-002 / FR-PRIO-001/004）：
 *      Low 持锁、Medium 长跑、High 阻塞 -> 断言 Low 有效优先级被提升到 1，
 *      且 High 在 Medium 完成前获得互斥量（PIP 生效，顺序 'H' 先于 'm'）。
 *   t3 链式继承（FR-PRIO-002）：High 阻塞 M1（Low 持）-> Low 阻塞 M2（Base 持）
 *      -> 沿链提升 Base 到 1。
 *
 * 期望完整日志：t2="LMhHmE" + t3="BLhlHxb"
 * 退出码 0 = 全部通过。
 * ============================================================ */

#include "catos/catos.h"
#include "test_common.h"
#include <stdlib.h>

static catos_mutex_t g_m2;                       /* t2 互斥量 */
static catos_mutex_t g_m3_a, g_m3_b;             /* t3 两个互斥量 */

static catos_task_t *g_t2_low, *g_t2_med, *g_t2_high;
static catos_task_t *g_t3_base, *g_t3_low, *g_t3_high;

static volatile LONG g_t2_done;                  /* t2 任务退出计数 */
static volatile LONG g_t3_done;                  /* t3 任务退出计数 */
static volatile int  g_pip_ok;                   /* t2：Low 被提升到 1 */
static volatile int  g_chain_ok;                 /* t3：Base 被链式提升到 1 */

/* 一段长计算：无 PIP 时 Medium 会先跑完它，饿死 Low */
static void long_work(void)
{
    volatile unsigned x = 0;
    unsigned i;

    for (i = 0; i < 2000000u; i++)
        x += i;
    (void)x;
}

/* ================= t2：经典优先级反转 ================= */

static void t2_low(void *arg)
{
    (void)arg;
    catos_mutex_lock(&g_m2);              /* 获得互斥量 */
    logc('L');
    catos_task_resume(g_t2_med);          /* Medium(2) 抢占 Low(3) */

    /* Low 被提升后恢复：此刻 High 阻塞在 g_m2 上，有效优先级应为 1 */
    g_pip_ok = (catos_task_eff_prio(catos_task_self()) == 1);

    catos_mutex_unlock(&g_m2);            /* 释放 -> 所有权转移给 High */
    logc('E');
    InterlockedIncrement(&g_t2_done);
    catos_task_exit();
}

static void t2_med(void *arg)
{
    (void)arg;
    logc('M');
    expect(catos_mutex_trylock(&g_m2) == CATOS_E_BUSY, "t2: trylock while held -> BUSY");
    catos_task_resume(g_t2_high);         /* High(1) 抢占 Medium(2) */

    long_work();                          /* 长计算：无 PIP 时在此饿死 Low */
    logc('m');
    InterlockedIncrement(&g_t2_done);
    catos_task_exit();
}

static void t2_high(void *arg)
{
    (void)arg;
    logc('h');
    catos_mutex_lock(&g_m2);              /* 阻塞；Low 被提升到 1 */
    logc('H');                            /* 获得互斥量 */
    catos_mutex_unlock(&g_m2);
    InterlockedIncrement(&g_t2_done);
    catos_task_exit();
}

/* ================= t3：链式继承 ================= */

static void t3_base(void *arg)
{
    (void)arg;
    catos_mutex_lock(&g_m3_b);            /* Base 持有 M2 */
    logc('B');
    catos_task_resume(g_t3_low);          /* Low(2) 抢占 Base(3) */

    /* Base 被沿链提升到 1（High->Low->Base）后恢复 */
    g_chain_ok = (catos_task_eff_prio(catos_task_self()) == 1);

    catos_mutex_unlock(&g_m3_b);          /* 转移给 Low */
    logc('b');
    InterlockedIncrement(&g_t3_done);
    catos_task_exit();
}

static void t3_low(void *arg)
{
    (void)arg;
    catos_mutex_lock(&g_m3_a);            /* Low 持有 M1 */
    logc('L');
    catos_task_resume(g_t3_high);         /* High(1) 抢占 Low(2) */

    catos_mutex_lock(&g_m3_b);            /* M2 被 Base 持有 -> Low 阻塞 */
    logc('l');                            /* 获得 M2 */
    catos_mutex_unlock(&g_m3_a);          /* 转移 M1 给 High */
    logc('x');
    catos_mutex_unlock(&g_m3_b);
    InterlockedIncrement(&g_t3_done);
    catos_task_exit();
}

static void t3_high(void *arg)
{
    (void)arg;
    logc('h');
    catos_mutex_lock(&g_m3_a);            /* M1 被 Low 持有 -> High 阻塞；链式提升 Base 到 1 */
    logc('H');                            /* 获得 M1 */
    catos_mutex_unlock(&g_m3_a);
    InterlockedIncrement(&g_t3_done);
    catos_task_exit();
}

/* ================= 协调者 ================= */

static void coordinator(void *arg)
{
    catos_mutex_t m;
    (void)arg;

    /* ---- t1：互斥量基本操作 ---- */
    catos_mutex_init(&m, "m1");
    expect(catos_mutex_trylock(&m) == CATOS_OK,        "t1: trylock on free mutex");
    expect(catos_mutex_lock(&m) == CATOS_OK,           "t1: nested lock");
    expect(catos_mutex_unlock(&m) == CATOS_OK,         "t1: unlock #1");
    expect(catos_mutex_unlock(&m) == CATOS_OK,         "t1: unlock #2");
    expect(catos_mutex_trylock(&m) == CATOS_OK,        "t1: trylock after release");
    expect(catos_mutex_unlock(&m) == CATOS_OK,         "t1: unlock #3");
    expect(catos_mutex_unlock(&m) == CATOS_E_INVAL,    "t1: unlock by non-owner");

    /* ---- t2：经典优先级反转（PIP） ---- */
    catos_task_resume(g_t2_low);
    while (g_t2_done < 3)
        catos_sched_yield();
    expect(g_pip_ok, "t2: Low saw eff_prio==1 while High blocked on its mutex (PIP)");
    expect(g_log_len == 6 && memcmp(g_log, "LMhHmE", 6) == 0,
           "t2: order LMhHmE ('H' before 'm' proves Low preempted Medium via PIP)");

    /* ---- t3：链式继承 ---- */
    catos_task_resume(g_t3_base);
    while (g_t3_done < 3)
        catos_sched_yield();
    expect(g_chain_ok, "t3: Base saw eff_prio==1 (chain High->Low->Base)");
    expect(g_log_len == 13 && memcmp(g_log + 6, "BLhlHxb", 7) == 0,
           "t3: order BLhlHxb (Low passed M2 before High got M1)");

    printf("  [%s] log = %.*s\n", g_fail ? "FAIL" : "PASS", (int)g_log_len, g_log);
    fflush(stdout);
    exit(g_fail ? 1 : 0);
}

int main(void)
{
    catos_task_t *c;

    if (catos_kernel_init() != CATOS_OK) {
        printf("kernel init failed\n");
        return 1;
    }
    catos_mutex_init(&g_m2,   "m2");
    catos_mutex_init(&g_m3_a, "m3a");
    catos_mutex_init(&g_m3_b, "m3b");

    /* 协调者：最低优先级用户任务 */
    catos_task_create(&c, "coord", coordinator, NULL, 5, 0);

    /* t2 辅助任务：创建后挂起，由协调者控制启动 */
    catos_task_create(&g_t2_low,  "t2low",  t2_low,  NULL, 3, 0); catos_task_suspend(g_t2_low);
    catos_task_create(&g_t2_med,  "t2med",  t2_med,  NULL, 2, 0); catos_task_suspend(g_t2_med);
    catos_task_create(&g_t2_high, "t2high", t2_high, NULL, 1, 0); catos_task_suspend(g_t2_high);

    /* t3 辅助任务 */
    catos_task_create(&g_t3_base, "t3base", t3_base, NULL, 3, 0); catos_task_suspend(g_t3_base);
    catos_task_create(&g_t3_low,  "t3low",  t3_low,  NULL, 2, 0); catos_task_suspend(g_t3_low);
    catos_task_create(&g_t3_high, "t3high", t3_high, NULL, 1, 0); catos_task_suspend(g_t3_high);

    catos_kernel_start();   /* 不再返回 */
    return 1;
}
