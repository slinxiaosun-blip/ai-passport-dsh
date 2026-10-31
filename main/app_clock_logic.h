// main/app_clock_logic.h —— 状态条「当前时间」的纯逻辑（可在宿主上直接测）。
//
// 设备没有 RTC，也不该有：墙钟来自主机下发的种子（UTC 秒 + 时区偏移分钟），
// 之后由设备用单调时钟本地走秒。这里只做「UTC 秒 + 偏移 → 本地时:分」这一步换算，
// 不碰 LVGL、不碰协议，好处是能在宿主机上把跨日/跨时区的边界钉死。
#pragma once

#include <stdint.h>

/**
 * 把 UTC 秒换算成本地时、分。
 *
 * @param epoch_sec      自 1970-01-01 UTC 起的秒数（主机下发）
 * @param tz_offset_min  本地时区相对 UTC 的偏移，分钟，东正西负（北京 = +480）
 * @param hh             输出：0..23
 * @param mm             输出：0..59
 *
 * 说明：只处理「时:分」，不处理日期 —— 状态条那 40px 只放得下 HH:MM，
 * 而用户看时间时关心的是「现在几点」，不是「今天几号」。
 * 跨日由取模自动完成（20:00 UTC + 8h → 次日 04:00）。
 */
void ap_clock_hhmm(int32_t epoch_sec, int tz_offset_min, int *hh, int *mm);
