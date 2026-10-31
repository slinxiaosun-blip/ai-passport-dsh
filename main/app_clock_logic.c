// main/app_clock_logic.c —— 见头文件说明。
#include "app_clock_logic.h"

#define MINUTES_PER_DAY 1440

void ap_clock_hhmm(int32_t epoch_sec, int tz_offset_min, int *hh, int *mm)
{
    // 先把「UTC 秒」折成「本地分钟」：加时区偏移（分钟×60）后除以 60。
    // ★ 中间用 int64：int32 的 epoch_sec 在 2038 之后会溢出，先扩宽，
    //   免得将来某天这个函数在 2038 突然给出负数时间。
    const int64_t local_min =
        ((int64_t)epoch_sec + (int64_t)tz_offset_min * 60) / 60;

    // 取模到「一天之内」。C 的 / 与 % 对负数向零截断，所以负 epoch
    // 或极西时区会得到负余数，需要补一天再回绕。
    int rem = (int)(local_min % MINUTES_PER_DAY);
    if (rem < 0) rem += MINUTES_PER_DAY;

    if (hh) *hh = rem / 60;
    if (mm) *mm = rem % 60;
}
