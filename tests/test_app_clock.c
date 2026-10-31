// tests/test_app_clock.c —— ap_clock_hhmm 的宿主测试。
//
// 这个函数只有几行，但它是**唯一**会把「主机给的时间」变成用户看到的数字的地方。
// 时区/跨日/整点边界算错，用户看到的就不是"差几分钟"，而是差 8 小时或 12 小时 ——
// 那种错误在真机上要盯一整天才能发现，在这里一行断言就能钉住。
#include <stdio.h>

#include "app_clock_logic.h"

static int g_fail = 0;
static int g_total = 0;

static void check(int cond, const char *what)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL %s\n", what);
    }
}

static void expect(int32_t epoch_sec, int tz_min, int want_hh, int want_mm, const char *what)
{
    int hh = -1, mm = -1;
    ap_clock_hhmm(epoch_sec, tz_min, &hh, &mm);
    if (hh != want_hh || mm != want_mm) {
        g_fail++;
        g_total++;
        printf("FAIL %s：得到 %02d:%02d，期望 %02d:%02d\n", what, hh, mm, want_hh, want_mm);
        return;
    }
    g_total++;
}

int main(void)
{
    // 2026-10-31 12:00:00 UTC
    const int32_t noon_utc = 1793448000;

    expect(noon_utc, 480, 20, 0, "北京 UTC+8：正午 UTC → 20:00");
    expect(noon_utc, 0, 12, 0, "UTC±0 → 12:00");
    expect(noon_utc, -300, 7, 0, "纽约 UTC-5 → 07:00");
    expect(noon_utc, 330, 17, 30, "印度 UTC+5:30 → 17:30（半小时时区）");

    // 跨日：UTC 20:00 + 8h = 次日 04:00
    expect(noon_utc + 8 * 3600, 480, 4, 0, "跨日：UTC 20:00 +8 → 次日 04:00");
    // 跨日反向：UTC 02:00 - 5h = 前一日 21:00
    expect(noon_utc - 10 * 3600, -300, 21, 0, "跨日反向：UTC 02:00 -5 → 前一日 21:00");

    // 日界边界：本地 23:59 与次日 00:00 必须干净切换
    expect(noon_utc + 8 * 3600 - 60, 480, 3, 59, "日界前 1 分钟");
    expect(noon_utc + 8 * 3600, 480, 4, 0, "日界当刻");

    // 秒不参与进位：59 秒仍是同一分钟
    expect(noon_utc + 59, 480, 20, 0, "同一分钟内的 59 秒不跳分");
    expect(noon_utc + 60, 480, 20, 1, "满 60 秒才跳下一分");

    // epoch 为 0（1970-01-01 00:00 UTC）
    expect(0, 0, 0, 0, "epoch 0 + UTC → 00:00");
    expect(0, 480, 8, 0, "epoch 0 + 北京 → 08:00");

    // 负时区跨到前一天：epoch 3 小时，纽约 -5 → 前一日 22:00
    expect(3 * 3600, -300, 22, 0, "负偏移回绕到前一天");

    // 空指针也要能调用（实现里已判空）
    ap_clock_hhmm(noon_utc, 480, NULL, NULL);
    check(1, "空指针不崩");

    printf("%s: %d 项\n", g_fail ? "FAILED" : "OK", g_total);
    return g_fail ? 1 : 0;
}
