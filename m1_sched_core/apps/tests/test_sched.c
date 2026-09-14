/* ============================================================
 * CatOS M0 调度冒烟测试
 *
 * 覆盖：
 *   - 参数校验（非法优先级/入口返回错误，测试 C）；
 *   - 优先级抢占 + suspend/resume（测试 A）；
 *   - 同优先级 yield 轮转（测试 B）；
 *   - 调度启动后的动态创建/删除任务。
 *
 * 方法：所有辅助任务在启动前创建（挂起或就绪），由一个最低优先级的
 * 协调者任务按阶段驱动，保证日志顺序确定。
 *
 * 退出码 0 = 全部通过；非 0 = 失败。
 * ============================================================ */

#include "catos/catos.h"
#include "test_common.h"

/* ---- 测试 B：同优先级 yield 轮转 ----
 * 期望日志 "ABab"：两个同优先级任务通过 yield 交替运行。 */
static void tB_y1(void *arg)
{
    (void)arg;
    expect(catos_task_self() != NULL &&
           catos_task_self()->state == CATOS_TASK_RUNNING,
           "tB: catos_task_self()/state in running task");
    logc('A');
    catos_sched_yield();
    logc('a');
    catos_task_suspend(catos_task_self());
}

static void tB_y2(void *arg)
{
    (void)arg;
    logc('B');
    catos_sched_yield();
    logc('b');
    catos_task_suspend(catos_task_self());
}

/* ---- 测试 A：优先级 + suspend/resume ----
 * 协调者依次 resume 高/中/低任务，期望日志 "HML"（优先级顺序）。
 * 每个任务记录一次后挂起自己，把 CPU 交还协调者。 */
static void tA_high(void *arg) { (void)arg; logc('H'); catos_task_suspend(catos_task_self()); }
static void tA_mid(void *arg)  { (void)arg; logc('M'); catos_task_suspend(catos_task_self()); }
static void tA_low(void *arg)  { (void)arg; logc('L'); catos_task_suspend(catos_task_self()); }

/* 动态创建的任务：运行一次后结束 */
static void t_dyn(void *arg)
{
    (void)arg;
    logc('D');
    catos_task_exit();
}

static catos_task_t *g_h, *g_m, *g_l;

/* ---- 协调者：优先级低于所有测试任务，仅在它们让出 CPU 后运行 ---- */
static void coordinator(void *arg)
{
    catos_task_t *dyn;
    catos_err_t err;
    (void)arg;

    /* 阶段 1：验证 yield 轮转（Y1/Y2 已自行完成并挂起） */
    while (g_log_len < 4)
        catos_sched_yield();
    expect(g_log_len == 4 && catos_memcmp(g_log, "ABab", 4) == 0,
           "tB: same-priority yield round-robin 'ABab'");

    /* 阶段 2：依次恢复高/中/低任务，验证优先级顺序 "HML" */
    catos_task_resume(g_h);
    catos_task_resume(g_m);
    catos_task_resume(g_l);
    while (g_log_len < 7)
        catos_sched_yield();
    expect(g_log_len == 7 && catos_memcmp(g_log + 4, "HML", 3) == 0,
           "tA: priority order 'HML' via suspend/resume");

    /* 阶段 3：删除一个已挂起任务 + 拒绝删除自己 */
    expect(catos_task_delete(catos_task_self()) == CATOS_E_INVAL,
           "tC: deleting self is rejected");
    expect(catos_task_delete(g_m) == CATOS_OK,
           "tC: deleting a suspended task succeeds");

    /* 阶段 4：调度启动后动态创建任务，应立即抢占（日志 'D'） */
    err = catos_task_create(&dyn, "dyn", t_dyn, NULL, 4, 0);
    expect(err == CATOS_OK, "tC: dynamic create after start");
    while (g_log_len < 8)
        catos_sched_yield();
    expect(g_log[7] == 'D', "tC: dynamically created task ran and logged 'D'");
    expect(catos_task_delete(dyn) == CATOS_OK, "tC: deleting terminated task");

    report_and_exit();
}

int main(void)
{
    catos_task_t *y1, *y2, *c;
    catos_err_t err;

    if (catos_kernel_init() != CATOS_OK) {
        catos_printf("kernel init failed\n");
        return 1;
    }

    /* 测试 C：参数校验（不创建任务） */
    err = catos_task_create(&y1, "bad", tB_y1, NULL, -1, 0);
    expect(err == CATOS_E_INVAL, "tC: negative priority rejected");
    err = catos_task_create(&y1, "bad", tB_y1, NULL, CATOS_CFG_NUM_PRIO, 0);
    expect(err == CATOS_E_INVAL, "tC: priority out of range rejected");
    err = catos_task_create(&y1, "bad", NULL, NULL, 1, 0);
    expect(err == CATOS_E_INVAL, "tC: NULL entry rejected");

    /* 测试 B：两个同优先级任务就绪运行 */
    catos_task_create(&y1, "y1", tB_y1, NULL, 2, 0);
    catos_task_create(&y2, "y2", tB_y2, NULL, 2, 0);

    /* 测试 A：高/中/低任务创建后立即挂起，由协调者控制 */
    catos_task_create(&g_h, "high", tA_high, NULL, 1, 0);
    catos_task_suspend(g_h);
    catos_task_create(&g_m, "mid",  tA_mid,  NULL, 2, 0);
    catos_task_suspend(g_m);
    catos_task_create(&g_l, "low",  tA_low,  NULL, 3, 0);
    catos_task_suspend(g_l);

    /* 协调者（最低优先级用户任务） */
    catos_task_create(&c, "coord", coordinator, NULL, 5, 0);

    catos_kernel_start();   /* 进入多任务模式，不再返回 */
    return 1;               /* 不可达 */
}
