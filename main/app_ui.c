// main/app_ui.c —— 设备主界面（精简版：只显示当前任务状态）
//
// ══════════════════════════════════════════════════════════════════════════
//  为什么只剩一屏
// ══════════════════════════════════════════════════════════════════════════
//  上一版有任务列表、详情页、余额页。实际使用后确认：**任务列表在 240×320 上
//  既看不全也读不快**，用户真正需要的是"现在要不要我管一下"。因此精简成
//  一个状态词 + 一个大色块：
//
//      空闲     灰    没有任务在跑
//      运行中   青    任务正在执行，不用管
//      待审批   琥珀  ★ 需要你按键，会响提示音
//      已完成   绿    刚跑完，会响提示音
//
//  设计的核心是**状态一眼可辨**：颜色 + 大字 + 全屏占比。
//  设备可能被放在桌角，扫一眼就知道要不要伸手。
//
// ══════════════════════════════════════════════════════════════════════════
//  交互只有两件事
// ══════════════════════════════════════════════════════════════════════════
//  · 待审批时：上/下选「运行一次 / 拒绝」，确定提交（覆盖层）
//  · 其余时候：下键长按看余额；按住确定说话（PTT，松开发送）
//
// ══════════════════════════════════════════════════════════════════════════
//  三条来自真机返工的教训（改这个文件前务必读完）
// ══════════════════════════════════════════════════════════════════════════
//  1. **含中文的文本不能用 8px 字体**。app_ui_font_8 行高只有 8，而中文字形
//     12~13px 高，LVGL 按行高裁剪，剩下的就是一个方块。中文一律 ≥16px。
//
//  2. **行位置必须由字体行高算出，不能写死像素**。曾给底栏第二行写死 y=19，
//     而中文行高 24px > 19px，两行文字直接压在一起。
//
//  3. **元素坐标要么来自 UI_* 宏，要么来自行高计算**，不要出现魔法数字。
//     版面由 tools/verify-layout.py 预先验证，不靠"改完拍照看看"迭代 ——
//     设备没有截图能力，拍照一轮要十几分钟。
//
// ══════════════════════════════════════════════════════════════════════════
//  线程模型
// ══════════════════════════════════════════════════════════════════════════
//  其他任务只写 volatile 的"待处理"结构，LVGL 定时器统一消费并更新控件。
//  **绝不跨任务直接操作 LVGL 对象**（LVGL 非线程安全）。
#include "app_ui.h"

#include "app_clock_logic.h"
#include "app_proto.h"
#include "app_conn_logic.h"
#include "app_link.h"
#include "app_screens.h"
#include "app_settings.h"
#include "app_ui_theme.h"
#include "app_voice.h"
#include "utf8_clamp.h"

#include <stdio.h>
#include <string.h>

#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "app_ui";

// 熄屏：无操作 5 分钟后关背光。任何状态变化都会重新亮屏 ——
// 熄屏只是省电，不该让用户错过"待审批"这种需要立刻响应的状态。
// 熄屏时长不再写死：由设置页决定（0 = 常亮），见 app_ui_tick 里的读取
// 背光百分比改由设置页决定（默认 75%，见 app_settings_logic 的档位表）

// ── 界面对象 ──────────────────────────────────────────────────────────────

static lv_obj_t *s_scr;

// 状态条（顶部细条：链路状态 + 应用名）
// 链路信号格（替代旧的"已连接/未连接"文字与色块，用户要求）
#define LINK_BARS 4
static lv_obj_t *s_link_bars[LINK_BARS];

// 电量显示（右上角，带百分比）。值由 app_ui_set_device_info 喂入（app 任务、
// 10 秒一次），LVGL 更新在 ui_tick 里做 —— 与其它跨任务投递同一套脏标志模式。
#define BATT_UNKNOWN (-2)
static lv_obj_t *s_batt_shell;
static lv_obj_t *s_batt_fill;
static lv_obj_t *s_batt_tip;
static lv_obj_t *s_batt_text;
static volatile int s_pending_battery = BATT_UNKNOWN;
static volatile bool s_batt_dirty = false;
static lv_obj_t *s_app_name;

// 状态卡（v2.0：圆角卡片 + 状态灯，取代旧版"整块铺状态色"）
static lv_obj_t *s_state_block;   // 卡片本体（底色固定 UI_CARD）
static lv_obj_t *s_state_chip;    // 状态灯：22×22 圆，淡底
static lv_obj_t *s_state_chip_dot; // 状态灯芯：5px 实色点
static lv_obj_t *s_state_text;    // 状态大字（状态词**只在这里出现一次**）
static lv_obj_t *s_state_hint;    // 一行补充说明（任务标题 / 提示）

// ── 运行计时（仅「运行中」显示）──
//
// ★ 设备端只负责**走秒**，不负责判断时长：锚点与基准都来自主机的 elapsedMs。
//   用单调时钟（esp_timer）而不是墙钟：设备没有 RTC，墙钟不可信。
//   每次刷新都用「基准 + 经过时间」重算，而不是 +1 累加 —— 掉帧、熄屏、
//   渲染卡顿都不会让计时漂移。
static lv_obj_t *s_timer_text;
static long s_run_base_ms = 0;     // 主机给的已运行毫秒（种子）
static uint32_t s_run_anchor_ms = 0; // 收到种子时的本地单调时钟
static bool s_run_known = false;   // false = 主机未给种子，不显示
static int32_t s_run_last_sec = -1; // 上次渲染的秒数，避免每帧重设文本

static lv_obj_t *s_footer_l1;     // 底栏提示：第一行
static lv_obj_t *s_footer_l2;     // 底栏提示：第二行
static lv_obj_t *s_footer_sep;    // 底栏两行之间的 26×1 发丝线

// ── 状态条第 4 项：当前时间（主机播种）──
//
// 设备没有 RTC，也不该有：墙钟是主机随 task.state 下发的「UTC 秒 + 时区偏移」，
// 设备收到后记锚点、再用单调时钟本地走秒，每分钟重算一次。
// 这与运行计时是同一套模型（种子 + 单调时钟），区别只是粒度到分钟、且**始终显示**：
// 空闲时用户更需要知道"现在几点"。
static lv_obj_t *s_clock_text;
static int32_t s_clock_seed_sec = 0;      // 主机给的 UTC 秒
static int16_t s_clock_tz_min = 0;        // 东正西负（北京 = +480）
static uint32_t s_clock_anchor_ms = 0;    // 收到种子时的本地单调时钟
static bool s_clock_known = false;        // false = 还没收到种子，显示 "--:--"
static int32_t s_clock_last_key = -1;     // 上次渲染的 时*60+分，避免每帧重设
// 跨任务投递（app 任务写，ui_tick 读）
static volatile long s_pending_clock_sec = -1;
static volatile long s_pending_clock_tz = 0;
static volatile bool s_clock_dirty = false;

static lv_obj_t *s_toast;         // 短暂通知
static lv_obj_t *s_toast_text;

static lv_timer_t *s_timer;
static lv_timer_t *s_toast_timer;

// ── 状态 ──────────────────────────────────────────────────────────────────

static ap_task_state_t s_state = AP_TASK_STATE_IDLE;
/** 主机随状态一起下发的短标题，可为空。 */
static char s_state_title[64];

static bool s_screen_on = true;
static uint32_t s_last_activity_ms = 0;

/**
 * 「待审批」状态等待审批请求的兜底超时。
 *
 * 主机先推状态、再发审批请求。正常情况下两者相隔几十毫秒，用户察觉不到。
 * 但如果**请求那一条丢了**（BLE 通知不保证送达），设备就会永远显示
 * 「等待授权请求」而没有任何可操作的东西 —— 提示是真的，但状态卡死了。
 *
 * 主机侧的审批超时（默认 30 秒）会走"按拒绝上报"并把状态收回，
 * 所以这里只需给一个略长的上限，把"主机也没能收回"这种极端情况兜住。
 */
#define APPROVAL_WAIT_TIMEOUT_MS 40000
static uint32_t s_waiting_since_ms = 0;

// 待处理更新（其他任务写，LVGL 定时器读）
static volatile bool s_state_dirty = true;
static volatile ap_task_state_t s_pending_state = AP_TASK_STATE_IDLE;
static char s_pending_title[64];
// 运行计时的种子（-1 = 主机未给，设备不显示计时）
static volatile long s_pending_elapsed_ms = -1;
static volatile bool s_link_dirty = true;
static volatile ap_link_state_t s_pending_link = AP_LINK_OFF;
// 上一次消费到的链路状态：用于判断"是否从已连接掉到断开"（掉线就回主页）。
static ap_link_state_t s_last_link = AP_LINK_OFF;
// 录音期间被缓存的状态变化（见 app_ui_set_task_state / app_ui_set_recording）。
static ap_task_state_t s_deferred_state = AP_TASK_STATE_IDLE;
static char s_deferred_title[64];
static long s_deferred_elapsed_ms = -1;
static volatile bool s_deferred_state_pending = false;

static volatile bool s_toast_dirty = false;
// 语音任务等非 LVGL 任务请求隐藏 toast（只置标志，由 ui_tick 落地）。
static volatile bool s_toast_hide_dirty = false;
// ★ 512 字节而不是 80：识别结果是动态文本（一句话可几十个汉字），80 字节
//   （约 26 个汉字）会把长句硬截成半截，配合下面的 UTF-8 安全截断才不会出方块。
static char s_pending_toast[512];
static volatile bool s_pending_toast_urgent = false;
// ★ 粘性 toast 标志：true 时 toast_show_sticky 不自动消失（按住说话期间一直可见）。
static volatile bool s_pending_toast_sticky = false;

// ── 状态 → 视觉 ───────────────────────────────────────────────────────────

/**
 * 任务状态 → 显示文案与配色。
 *
 * 「待审批」与「已完成」用最醒目的两种色（琥珀 / 绿），因为它们是需要
 * 用户注意的状态；「运行中」用青色（说明在动，但不用管）；「空闲」用灰。
 */
/**
 * 任务状态 → 徽章底色（预混实色：12% 语义色 over UI_CARD）。
 *
 * ★ 单独一个函数而不是在 refresh_state 里写第二份 switch：
 *   徽章底色与文字色必须**成对**变化。手写两处 switch 迟早出现
 *   "蓝底配琥珀字"这种错配，而它在小屏上不像 bug、像"设计如此"。
 */
static uint32_t state_chip_bg(ap_task_state_t st)
{
    switch (st) {
    case AP_TASK_STATE_RUNNING: return UI_INFO_BG;
    case AP_TASK_STATE_WAITING_APPROVAL: return UI_WARN_BG;
    case AP_TASK_STATE_COMPLETED: return UI_ACC_BG;
    case AP_TASK_STATE_IDLE:
    default: return UI_DIM_BG;
    }
}

/**
 * 状态的三个呈现面：大字文案、提示行文案、主色。
 *
 * ★ `hint` 现在**四个状态全为空**。它原本写的是状态词的另一种说法
 *   （"运行中" / "任务正在执行"、"空闲" / "没有正在执行的任务"……），
 *   在大字已经明说的前提下纯属重复。提示行从此专职显示**主机下发的任务
 *   标题**（见 refresh_state），没有标题就空着 —— 空白比复述更安静。
 *   这也是"状态显示有重复"这条反馈的根：同一句话在徽章、大字、提示行
 *   出现了两到三次。
 */
static void state_style(ap_task_state_t st, const char **text, const char **hint, uint32_t *color)
{
    *hint = "";
    switch (st) {
    case AP_TASK_STATE_RUNNING:
        *text = "运行中";
        *color = UI_INFO;
        break;
    case AP_TASK_STATE_WAITING_APPROVAL:
        *text = "待审批";
        *color = UI_WARN;
        break;
    case AP_TASK_STATE_COMPLETED:
        *text = "已完成";
        *color = UI_ACCENT;
        break;
    case AP_TASK_STATE_IDLE:
    default:
        *text = "空闲";
        *color = UI_DIM;
        break;
    }
}

/**
 * 清洗来自主机的文本：换行折成空格，丢弃其它控制字符。
 *
 * 保留中文 —— 字体子集已覆盖界面文案与常见汉字。字体没覆盖到的字**不做降级处理**：
 * 那说明字体子集没跟上文案，由 tools/make-fonts.py 的覆盖率校验在构建期拦住，
 * 而不是运行期悄悄改成 '?'（那会让"缺字"这个真问题被掩盖）。
 */
static void clean_text(const char *src, char *dst, size_t dst_size)
{
    size_t out = 0;
    if (dst_size == 0) return;
    for (const unsigned char *p = (const unsigned char *)(src ? src : ""); *p && out + 1 < dst_size; p++) {
        if (*p == '\n' || *p == '\r' || *p == '\t') {
            dst[out++] = ' ';
        } else if (*p >= 0x20) {
            dst[out++] = (char)*p;
        }
    }
    dst[out] = '\0';
}



// ── 熄屏 ──────────────────────────────────────────────────────────────────

/** 记录一次"有意义的动静"，用于推迟熄屏。 */
static void note_activity(void)
{
    s_last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

static void screen_set(bool on)
{
    if (s_screen_on == on) return;
    s_screen_on = on;
    // 只关背光、不关屏：重新点亮时无需重建界面，也没有闪烁。
    bsp_display_backlight(on ? (uint8_t)ap_settings_get()->brightness : 0);
    if (on) note_activity();
    ESP_LOGI(TAG, "屏幕%s", on ? "点亮" : "熄灭（无操作到设置时长）");
}

/** 亮屏（若已亮则只刷新活动时间）。状态变化时应调用。 */
static void screen_wake(void)
{
    note_activity();
    screen_set(true);
}

// ── 刷新 ──────────────────────────────────────────────────────────────────

/**
 * 格式化已运行时长。
 *
 * < 1 小时：MM:SS；≥ 1 小时：H:MM:SS（右侧对齐，变宽时向左生长，不挤压状态大字）。
 *
 * ★ 严格按设计稿的格式输出，**不加任何前缀符号**（早期版本在起点不可靠时
 *   会印一个 `~`，按设计稿要求已去掉）。因此本函数的输出可以直接当"精确值"读，
 *   精度问题只在主机侧处理：主机给不出可信基准时就不下发 elapsedMs（设备不显示计时），
 *   而不是下发一个含糊的数再让设备打补丁。
 */
static void format_elapsed(uint32_t total_s, char *out, size_t out_size)
{
    const uint32_t h = total_s / 3600u;
    const uint32_t m = (total_s % 3600u) / 60u;
    const uint32_t s = total_s % 60u;
    if (h > 0) {
        snprintf(out, out_size, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    } else {
        snprintf(out, out_size, "%02u:%02u", (unsigned)m, (unsigned)s);
    }
}

/**
 * 刷新计时标签。
 *
 * force=false 时只在**秒数变化**时才 set_text：ui_tick 是 20ms 一次，
 * 每帧重设标签会白白产生重绘。
 */
static void refresh_timer(bool force)
{
    if (!s_timer_text) return;

    // 非「运行中」一律清空 —— 空闲/待审批/已完成都不该留一个还在走的数字。
    //
    // ★ 这里**每次都读一遍标签实际内容**，而不是靠 `s_run_last_sec == -1`
    //   当"已清空"的标记：标记和标签是两处状态，任何一处漏更新就会在空闲
    //   界面上留一个过期数字（用户明确要求"空闲时不用显示时间"）。
    //   多读一次 `lv_label_get_text` 的代价远小于一个会撒谎的界面。
    if (s_state != AP_TASK_STATE_RUNNING || !s_run_known) {
        s_run_last_sec = -1;
        if (lv_label_get_text(s_timer_text)[0] != '\0') {
            lv_label_set_text(s_timer_text, "");
        }
        return;
    }

    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    const uint32_t elapsed_s = (uint32_t)((s_run_base_ms + (long)(uint32_t)(now - s_run_anchor_ms)) / 1000);
    if (!force && (int32_t)elapsed_s == s_run_last_sec) return;
    s_run_last_sec = (int32_t)elapsed_s;

    // ★ 字号**不再随运行时长变化**。
    //   旧逻辑是超过 1 小时改用 8px 点阵：那时用的是 16px 点阵（等宽，
    //   每字符 16px），跨小时串 "10:23:45" 要 128px，左端会伸进徽章药丸。
    //   换成 16px 思源黑体后同一个串只有 61.5px（数字 8.81 / 冒号 4.31），
    //   右侧 192px 的可用宽度里绰绰有余 —— 于是"跨小时字变小"这个
    //   纯观感副作用消失，整块卡片里也只有一种字号了。
    //   （判据改了字体就必须重算，别再凭"字数 × 字号"心算，见 app_ui_theme.h。）
    char buf[16];
    format_elapsed(elapsed_s, buf, sizeof(buf));
    lv_label_set_text(s_timer_text, buf);
}

/**
 * 刷新状态条上的当前时间。
 *
 * 每分钟才真正重设一次文本（force=true 时立即重设）。
 * ★ 与运行计时相反，这个标签**在空闲时照常显示** —— 用户要的就是"现在几点"，
 *   而不是"任务跑了多久"。两者互不干扰：计时只在 RUNNING 出现，时间常驻。
 */
static void refresh_clock(bool force)
{
    if (!s_clock_text || !s_clock_known) return;

    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    const int32_t epoch_sec = s_clock_seed_sec + (int32_t)((now - s_clock_anchor_ms) / 1000u);
    int hh = 0, mm = 0;
    ap_clock_hhmm(epoch_sec, s_clock_tz_min, &hh, &mm);

    const int32_t key = hh * 60 + mm;
    if (!force && key == s_clock_last_key) return;
    s_clock_last_key = key;

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
    lv_label_set_text(s_clock_text, buf);
}

// ── 底栏卡：单行 / 双行两种排版 ───────────────────────────────────────────
//
// ★ 单行时**必须隐藏发丝线**：一条 26px 的线悬在唯一一行字下面，
//   看起来像"下面还有内容被截掉了"。行位置也要整体居中，
//   否则底部会空出一大块（旧的静默 bug：只隐藏第二行、不重新居中）。
static void footer_set_single(const char *line1, uint32_t color)
{
    if (!s_footer_l1) return;
    lv_label_set_text(s_footer_l1, line1);
    lv_obj_set_style_text_color(s_footer_l1, lv_color_hex(color), 0);
    lv_obj_set_pos(s_footer_l1, 0, UI_CENTER_Y(UI_FOOTER_H, UI_LINE_H_CJK16));
    lv_label_set_text(s_footer_l2, "");
    if (s_footer_sep) lv_obj_add_flag(s_footer_sep, LV_OBJ_FLAG_HIDDEN);
}

static void footer_set_two(const char *line1, const char *line2, uint32_t color)
{
    if (!s_footer_l1) return;
    lv_label_set_text(s_footer_l1, line1);
    lv_obj_set_style_text_color(s_footer_l1, lv_color_hex(color), 0);
    lv_obj_set_pos(s_footer_l1, 0, UI_FOOTER_LINE1_Y);
    lv_label_set_text(s_footer_l2, line2);
    lv_obj_set_style_text_color(s_footer_l2, lv_color_hex(color), 0);
    lv_obj_set_pos(s_footer_l2, 0, UI_FOOTER_LINE2_Y);
    if (s_footer_sep) lv_obj_remove_flag(s_footer_sep, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_state(void)
{
    if (!s_scr) return;

    const char *text = "空闲";
    const char *hint = "";
    uint32_t color = UI_DIM;
    state_style(s_state, &text, &hint, &color);

    // 大字 + 状态灯：文字**只写一次**（大字），灯只用颜色表达。
    //
    // ★ 这里原先还有个「徽章药丸」：圆点 + 状态词，和大字取同一个字符串，
    //   于是屏幕上「运行中」出现两遍（用户反馈"状态显示有重复的"）。
    //   状态这种一眼信息，重复两遍不是强调、是噪音；现在灯退化成 22×22 的
    //   圆形指示灯，文字只留大字一处。灯的淡底（12% 状态色）+ 实色芯
    //   保留了原来的层次，也仍然能靠颜色在余光里分辨状态。
    lv_label_set_text(s_state_text, text);
    lv_obj_set_style_text_color(s_state_text, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_state_chip, lv_color_hex(state_chip_bg(s_state)), 0);
    lv_obj_set_style_bg_color(s_state_chip_dot, lv_color_hex(color), 0);
    // ★ 卡片底色**不随状态变**：颜色只出现在徽章与大字上。
    //   旧版把 182px 高的整块铺 20% 状态色，待审批时半屏发黄、字压在上面难读；
    //   新版把颜色面积收小，反而更容易一眼分辨。

    // 补充行：优先显示主机下发的任务标题，没有就显示状态说明
    lv_label_set_text(s_state_hint, s_state_title[0] ? s_state_title : hint);
    lv_obj_set_style_text_color(s_state_hint, lv_color_hex(UI_SUB), 0);

    // ── 底栏提示 ──────────────────────────────────────────────────────────
    //
    // ★ 这里的一条规矩：**提示只能写"按了确实会发生的事"**。
    //
    //   原先在「待审批」时写的是"确定键处理审批"，而主页的确定键**什么都不做**
    //   （审批页是收到主机请求时自动弹出的，主页按键并不参与）。
    //   一句做不到的指引比没有指引更糟 —— 用户会反复按、以为设备坏了。
    //
    //   因此按"此刻真正可用的操作"来写：
    //     · 审批挂起但审批页尚未出现 → 明确说"等待授权请求"，让用户知道该等
    //     · 其余情况 → 两行 + 中间发丝线（行框 20 + 缝 8 + 线 1 + 缝 8 + 行框 20）
    switch (s_state) {
    case AP_TASK_STATE_WAITING_APPROVAL:
        // 审批页会由主机请求自动弹出；在它出现之前主页没有可执行动作。
        footer_set_single("等待授权请求", UI_WARN);
        break;
    default:
        // 底栏用 16px 中文（行高 20）：一行约 13 个字，才装得下三条手势提示
        footer_set_two("长按上键设置 下键看余额", "按住确定说话", UI_SUB);
        break;
    }

    // 计时标签跟着状态走：进入运行中立刻显示（不等下一次 tick），离开就清空。
    refresh_timer(true);
}

static void refresh_signal_bars(void);

static void refresh_link(void)
{
    if (!s_scr) return;
    refresh_signal_bars();

    // 顶栏只留应用名：连接状态改由**信号格**表达（用户要求），
    // 文字状态（已连接/未连接…）不再占位置。
    lv_label_set_text(s_app_name, "AI 通行证");

    if (s_pending_link == AP_LINK_CONNECTED || s_pending_link == AP_LINK_READY) {
        refresh_state();   // 恢复正常的任务提示
    } else {
        // 未连接在主界面上给明确提示（用户要求）。此时按住说话/看余额都无效，
        // 底栏也只写做得到的事。
        lv_label_set_text(s_state_hint, "蓝牙未连接");
        lv_obj_set_style_text_color(s_state_hint, lv_color_hex(UI_ERR), 0);
        footer_set_single("等待蓝牙连接", UI_WARN);
    }
}

/**
 * 信号格：4 格递增方块，格数 = RSSI 强弱（ap_rssi_level）。
 * 颜色随链路阶段变：READY 绿 / 握手中琥珀 / 未连接全部熄灭。
 * 由 ui_tick 每秒刷一次（RSSI 缓存本身 2 秒一采，见 app_link）。
 */
static void refresh_signal_bars(void)
{
    const bool linked = (s_pending_link == AP_LINK_CONNECTED || s_pending_link == AP_LINK_READY);
    const int level = linked ? ap_rssi_level(ap_link_rssi()) : 0;
    const uint32_t color = (s_pending_link == AP_LINK_READY) ? UI_ACCENT
                         : linked ? UI_WARN : UI_LINE;
    for (int i = 0; i < LINK_BARS; i++) {
        lv_obj_set_style_bg_color(s_link_bars[i], lv_color_hex((i < level) ? color : UI_LINE), 0);
    }
}

/** 电量：图标内条宽 + 百分比文字 + 颜色分档（≥30 绿 / 15-29 琥珀 / <15 红）。 */
static void refresh_battery(void)
{
    if (!s_batt_text) return;
    const int pct = s_pending_battery;
    if (pct < 0) {
        // 未知（首次采样前 / 电池芯片读失败）：显示 "--"，不显示假的 0%
        lv_label_set_text(s_batt_text, "--");
        lv_obj_set_style_text_color(s_batt_text, lv_color_hex(UI_DIM), 0);
        lv_obj_set_width(s_batt_fill, 0);
        return;
    }
    const int p = pct > 100 ? 100 : pct;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", p);
    lv_label_set_text(s_batt_text, buf);
    const uint32_t color = (p >= 30) ? UI_ACCENT : (p >= 15) ? UI_WARN : UI_ERR;
    lv_obj_set_style_text_color(s_batt_text, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_batt_fill, lv_color_hex(color), 0);
    lv_obj_set_width(s_batt_fill, (p * 16 + 50) / 100);   // 外壳内宽 16px
}

// ── 提示条 ────────────────────────────────────────────────────────────────

static void toast_timeout(lv_timer_t *timer)
{
    (void)timer;
    if (s_toast) lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
}

// ★ 粘性 toast：不自动消失，直到下一个 toast 或 app_ui_hide_toast() 显式隐藏。
//   用于"开始说话"这类需要持续显示的状态提示（按住说话期间一直可见）。
static void toast_show_sticky(const char *text, bool urgent)
{
    if (!s_toast) return;
    char clamped[512];
    utf8_copy_clamped(text, clamped, sizeof(clamped));
    char clean[512];
    clean_text(clamped, clean, sizeof(clean));
    lv_label_set_text(s_toast_text, clean);
    lv_obj_set_style_border_color(s_toast, lv_color_hex(urgent ? UI_ERR : UI_ACCENT), 0);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);
    // 不启动自动隐藏定时器 —— 粘性 toast 一直显示到被显式隐藏或新 toast 替换。
    if (s_toast_timer) { lv_timer_delete(s_toast_timer); s_toast_timer = NULL; }
}

static void toast_show(const char *text, bool urgent)
{
    if (!s_toast) return;
    // ★ 先按字符边界做 UTF-8 安全截断（而非按字节硬切），再清洗控制字符。
    //   顺序不能反：clean_text 逐字节搬运，若源串末尾本来就是半个汉字，
    //   搬运后仍然是半截，LVGL 渲染时就成了方块。见 utf8_clamp.c 的说明。
    char clamped[512];
    utf8_copy_clamped(text, clamped, sizeof(clamped));
    char clean[512];
    clean_text(clamped, clean, sizeof(clean));
    lv_label_set_text(s_toast_text, clean);
    // 用左边框颜色区分紧急程度：比改背景色更醒目，也不刺眼
    lv_obj_set_style_border_color(s_toast, lv_color_hex(urgent ? UI_ERR : UI_ACCENT), 0);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);
    if (s_toast_timer) lv_timer_delete(s_toast_timer);
    s_toast_timer = lv_timer_create(toast_timeout, urgent ? 4000 : 2500, NULL);
}

/** 显式隐藏提示条（用于清除"开始说话"这类粘性提示）。 */
void app_ui_hide_toast(void)
{
    // ★ 本函数会被**语音任务**调用（录音收尾 app_voice.c），因此绝不能在这里碰 LVGL。
    //   真机事故（2026-10-06）：这里直接 lv_obj_add_flag + lv_timer_delete，与 LVGL 任务
    //   并发访问内部链表 → 损坏 → lv_inv_area 死循环 → 整机冻死。
    //   现象："按住说话说完，一松手就死机"（收尾正好是 VAD 判定那一刻）。
    //   现在只置标志，真正的隐藏在 LVGL 任务里的 ui_tick 消费。
    s_toast_hide_dirty = true;
}

/** 真正隐藏 toast（**只在 LVGL 任务里调用**）。 */
static void toast_hide_now(void)
{
    if (s_toast) lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    if (s_toast_timer) { lv_timer_delete(s_toast_timer); s_toast_timer = NULL; }
}

// ── LVGL 定时器 ───────────────────────────────────────────────────────────

void app_screens_consume_pending(void);

static void ui_tick(lv_timer_t *timer)
{
    (void)timer;

    // 覆盖页的跨任务投递与结果卡计时都挂在这一个定时器上：它们访问同一批 LVGL 对象，
    // 本来就必须串行；多开定时器只是徒增调度开销。
    // （审批页的倒计时已按用户要求删除，这里不再需要它的 tick。）
    app_screens_consume_pending();
    app_screens_tick_voice((uint32_t)(esp_timer_get_time() / 1000));
    app_screens_tick_question((uint32_t)(esp_timer_get_time() / 1000));

    // 电量格每秒刷一次：RSSI 缓存 2 秒一采，太快刷也只是空转。
    {
        static uint32_t s_signal_next_ms = 0;
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if ((int32_t)(now - s_signal_next_ms) >= 0) {
            s_signal_next_ms = now + 1000;
            refresh_signal_bars();
        }
    }

    // 运行计时：内部只在秒数变化时才重设文本，20ms 调一次也不会产生多余重绘。
    refresh_timer(false);

    // 主机时间种子：到了就重新锚定并立刻重画一次。
    if (s_clock_dirty) {
        s_clock_dirty = false;
        s_clock_seed_sec = (int32_t)s_pending_clock_sec;
        s_clock_tz_min = (int16_t)s_pending_clock_tz;
        s_clock_anchor_ms = (uint32_t)(esp_timer_get_time() / 1000);
        s_clock_known = true;
        if (s_clock_text) lv_obj_set_style_text_color(s_clock_text, lv_color_hex(UI_SUB), 0);
        refresh_clock(true);
    } else {
        refresh_clock(false);
    }

    if (s_batt_dirty) {
        s_batt_dirty = false;
        refresh_battery();
    }

    if (s_link_dirty) {
        s_link_dirty = false;
        // 蓝牙断开 → 默认返回主页：收起余额/审批等覆盖层。
        // 链路一旦离开 CONNECTED/READY（掉到 OFF/ADVERTISING/FAILED 就是断了），
        // 覆盖层留着也没意义（审批发不出去、余额拉不到），不如回到主页。
        const ap_link_state_t prev_link = s_last_link;
        s_last_link = s_pending_link;
        const bool was_up = (prev_link == AP_LINK_CONNECTED || prev_link == AP_LINK_READY);
        const bool now_up = (s_pending_link == AP_LINK_CONNECTED || s_pending_link == AP_LINK_READY);
        if (was_up && !now_up) {
            // ★ 配对码页例外：未配对时链路本来就不会进 READY，一旦按"断开"收掉，
            //   用户就看不到码、也就没法配对（真机反馈"设备上没有确认配对码的地方"）。
            //   它只在用户主动关闭或配对成功时收起。
            if (app_screens_current() != AP_SCREEN_NONE &&
                app_screens_current() != AP_SCREEN_PAIR) {
                ESP_LOGI(TAG, "蓝牙断开，覆盖层返回主页");
                // ★ 必须 hide(当前页)：之前写的是 hide(AP_SCREEN_NONE)，
                //   那是空操作（不匹配任何覆盖层对象），断开后覆盖层一直留在屏幕上。
                app_screens_hide(app_screens_current());
            }
        }
        refresh_link();
    }
    if (s_state_dirty) {
        s_state_dirty = false;
        const ap_task_state_t prev = s_state;
        s_state = s_pending_state;
        snprintf(s_state_title, sizeof(s_state_title), "%s", s_pending_title);
        // ── 计时种子 ──
        // 只在「运行中」播种；主机没给（-1）就不显示计时（宁可不显示，也不猜）。
        // 每次收到新种子都重新锚定：重连、多会话基准前移、插件重启都走这条路径。
        if (s_state == AP_TASK_STATE_RUNNING && s_pending_elapsed_ms >= 0) {
            s_run_known = true;
            s_run_base_ms = s_pending_elapsed_ms;
            s_run_anchor_ms = (uint32_t)(esp_timer_get_time() / 1000);
            s_run_last_sec = -1;   // 强制下一帧渲染
        } else if (s_state != AP_TASK_STATE_RUNNING) {
            // 离开运行中就丢掉锚点：已完成/待审批不应带着一个还在走的数字。
            s_run_known = false;
            s_run_last_sec = -1;
        }
        // 记录进入「待审批」的时刻，用于下面的兜底超时
        if (s_state == AP_TASK_STATE_WAITING_APPROVAL && prev != AP_TASK_STATE_WAITING_APPROVAL) {
            s_waiting_since_ms = (uint32_t)(esp_timer_get_time() / 1000);
        }
        refresh_state();
        // ★ 状态变化就亮屏。熄屏是省电，不是"屏蔽通知"——
        //   待审批与已完成必须让用户看得见，否则熄屏就成了漏掉审批的原因。
        if (s_state != prev) {
            screen_wake();
            // ★ 状态变化 → 回主页（真机返工记录）：收起余额覆盖层。
            //   余额是"随时可查"的参考信息，不该压住"正在执行/待审批/已完成"
            //   这些需要用户关注的状态。审批页**不动** —— 它需要用户按键决定，
            //   而且它本身就伴随"待审批"状态出现，收掉就没人能批了。
            if (app_screens_current() == AP_SCREEN_BALANCE) {
                ESP_LOGI(TAG, "任务状态变化，收起余额页");
                app_screens_hide(AP_SCREEN_BALANCE);
            }
        }
    }
    if (s_toast_hide_dirty) {
        s_toast_hide_dirty = false;
        // 同一轮里若还有新 toast 要显示，就以新 toast 为准（后到者胜）
        if (!s_toast_dirty) toast_hide_now();
    }
    if (s_toast_dirty) {
        s_toast_dirty = false;
        if (s_pending_toast_sticky) {
            toast_show_sticky(s_pending_toast, s_pending_toast_urgent);
        } else {
            toast_show(s_pending_toast, s_pending_toast_urgent);
        }
        screen_wake();
    }

    // 兜底：待审批等太久却没有审批页可操作 → 自行回落到空闲。
    // 宁可显示"空闲"（用户不会去操作），也不要停在一个做不到的指引上。
    if (s_state == AP_TASK_STATE_WAITING_APPROVAL && !app_screens_approval_active() &&
        s_waiting_since_ms != 0) {
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if ((uint32_t)(now - s_waiting_since_ms) >= APPROVAL_WAIT_TIMEOUT_MS) {
            ESP_LOGW(TAG, "等待授权请求超时，回落到空闲（避免卡在无效提示上）");
            s_waiting_since_ms = 0;
            app_ui_set_task_state(AP_TASK_STATE_IDLE, NULL, -1);
        }
    }
    // 审批页一旦打开，清掉等待计时
    if (app_screens_approval_active()) s_waiting_since_ms = 0;

    // 熄屏检查。审批页打开时不熄屏 —— 那正是最需要用户操作的时候。
    // 超时来自设置页（默认**常亮** = 0，见 app_settings_logic 的档位表）。
    const int off_min = ap_settings_get()->screen_off_min;
    if (off_min > 0 && s_screen_on && !app_screens_approval_active()) {
        const uint32_t timeout_ms = (uint32_t)off_min * 60u * 1000u;
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if ((uint32_t)(now - s_last_activity_ms) >= timeout_ms) {
            screen_set(false);
        }
    }
}

// ── 构建 ──────────────────────────────────────────────────────────────────

static void build_statusbar(void)
{
    // v2.0：状态条不再有底色和分隔线。它直接坐在页面底上，
    // 靠"下面 8px 缝 + 一张卡片"表达层级 —— 硬边时代那层背景 + 1px 线，
    // 换成圆角卡片之后反而成了第二套边界，整屏看着脏。
    //
    // 子对象挂在 s_scr 上，坐标即安全区坐标（PX/PY 负责加 INSET 偏移）。
    const int bar_bottom = UI_STATUSBAR_Y + UI_STATUSBAR_H;   // = 22

    // 链路信号格：4 格递增方块（宽 3 / 间距 2 / 底边对齐 / 圆端）。
    // 圆角在 3px 宽的方块上不能用 UI_R_* 档：半径大于半宽会被 LVGL 收成胶囊，
    // 这里要的正是"胶囊短棒"，所以固定写 2。
    for (int i = 0; i < LINK_BARS; i++) {
        const int h = 4 + i * 3;
        lv_obj_t *bar_i = lv_obj_create(s_scr);
        ui_theme_rect(bar_i, UI_LINE);
        lv_obj_set_style_radius(bar_i, 2, 0);
        lv_obj_set_size(bar_i, 3, h);
        lv_obj_set_pos(bar_i, PX(i * 5), PY(bar_bottom - h));
        s_link_bars[i] = bar_i;
    }

    // 应用名：16px 中文（行高 20），在 22px 状态条里居中（y=1）。
    // ★ 不能用 20px 中文（行高 24 > 22）——会被裁掉一截，中文裁切就是方块。
    // ★ 与右侧的时间/电量**同一款字体**（UI_FONT_CJK16），否则字号一样但
    //   一款点阵一款比例，看上去仍是"没对齐"（见 app_ui_theme.h 状态条一节）。
    s_app_name = ui_theme_text(s_scr, "AI 通行证", UI_FONT_CJK16, UI_SUB);
    lv_obj_set_pos(s_app_name, PX(UI_APPNAME_X), PY(UI_CENTER_Y(UI_STATUSBAR_H, UI_LINE_H_CJK16)));

    // ── 电量（右上角：百分比 + 图标）──
    // ★ 顺序是"百分比在图标左侧"，与设计稿相反。原因：图标右侧还有 2px 正极凸起，
    //   把百分比放它右边至少需要 32px（"100%"），220 宽的安全区里会溢出到圆角外。
    //   小屏上字号不能缩，只能换顺序。
    const int batt_y = UI_CENTER_Y(UI_STATUSBAR_H, 10);   // = 6
    const int shell_x = UI_W - 22;                        // 20 外壳 + 2 凸起，贴右缘

    s_batt_shell = lv_obj_create(s_scr);
    ui_theme_rect(s_batt_shell, UI_LINE);
    lv_obj_set_style_radius(s_batt_shell, 3, 0);
    lv_obj_set_pos(s_batt_shell, PX(shell_x), PY(batt_y));
    lv_obj_set_size(s_batt_shell, 20, 10);

    s_batt_tip = lv_obj_create(s_scr);
    ui_theme_rect(s_batt_tip, UI_LINE);
    lv_obj_set_style_radius(s_batt_tip, 1, 0);
    lv_obj_set_pos(s_batt_tip, PX(shell_x + 20), PY(batt_y + 3));
    lv_obj_set_size(s_batt_tip, 2, 4);

    s_batt_fill = lv_obj_create(s_scr);
    ui_theme_rect(s_batt_fill, UI_DIM);
    lv_obj_set_style_radius(s_batt_fill, 2, 0);
    lv_obj_set_pos(s_batt_fill, PX(shell_x + 2), PY(batt_y + 2));
    lv_obj_set_size(s_batt_fill, 0, 6);

    // 百分比：16px 思源（"100%" 实宽 41.1）。宽度按 "100%" 留足，
    // 否则满电时最后那个 % 会被裁掉。
    // ★ 用 UI_FONT_CJK16 而不是 8px 点阵：状态条四个元素要同一款字体同一字号，
    //   点阵在 16px 下是等宽（每字符 16px），"100%" 要 64px，根本放不下；
    //   思源是比例字体，只要 41.1px（见 app_ui_theme.h 状态条一节的踩坑记录）。
    s_batt_text = ui_theme_text(s_scr, "--", UI_FONT_CJK16, UI_DIM);
    lv_obj_set_pos(s_batt_text, PX(UI_BATT_X), PY(UI_CENTER_Y(UI_STATUSBAR_H, UI_LINE_H_CJK16)));
    lv_obj_set_width(s_batt_text, UI_BATT_W);
    lv_obj_set_style_text_align(s_batt_text, LV_TEXT_ALIGN_RIGHT, 0);

    // ── 当前时间（电量左边，与电量同款 16px）──
    // 未收到主机种子时显示 "--:--"：与电量的 "--" 是同一种"我还不知道"的表达；
    // 右对齐且框宽固定，所以知道时间后只是左边缘生长，不会牵动别的元素重排。
    s_clock_text = ui_theme_text(s_scr, "--:--", UI_FONT_CJK16, UI_SUB);
    lv_obj_set_pos(s_clock_text, PX(UI_CLOCK_X), PY(UI_CENTER_Y(UI_STATUSBAR_H, UI_LINE_H_CJK16)));
    lv_obj_set_width(s_clock_text, UI_CLOCK_W);
    lv_obj_set_style_text_align(s_clock_text, LV_TEXT_ALIGN_RIGHT, 0);
}

static void build_state_card(void)
{
    // 状态卡：R16 + 1px 淡描边，底色固定 UI_CARD（**不随状态变**，见 refresh_state）
    s_state_block = lv_obj_create(s_scr);
    ui_theme_card(s_state_block, UI_CARD, UI_STROKE, UI_R_LG);
    lv_obj_set_pos(s_state_block, PX(0), PY(UI_STATE_CARD_Y));
    lv_obj_set_size(s_state_block, UI_W, UI_STATE_CARD_H);

    // 状态灯：22×22 正圆（UI_R_PILL 在等宽等高时就是圆），内芯 5px 实色点。
    // ★ 固定尺寸，不再 LV_SIZE_CONTENT 随文字伸缩 —— 里面已经没有文字了。
    s_state_chip = lv_obj_create(s_state_block);
    ui_theme_pill(s_state_chip, UI_DIM_BG);
    lv_obj_set_size(s_state_chip, UI_STATE_CHIP_H, UI_STATE_CHIP_H);
    lv_obj_set_pos(s_state_chip, UI_CARD_PAD_X, UI_STATE_CHIP_Y);
    lv_obj_set_flex_flow(s_state_chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_state_chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_state_chip_dot = lv_obj_create(s_state_chip);
    ui_theme_pill(s_state_chip_dot, UI_DIM);
    lv_obj_set_size(s_state_chip_dot, UI_STATE_CHIP_DOT, UI_STATE_CHIP_DOT);

    // XL 大字：**左对齐**（设计稿改版）。居中时"运行中"三个字会被两侧留白
    // 推得比徽章还轻，视线抓不住；左对齐让徽章、大字、提示落在同一条竖向基线上。
    s_state_text = ui_theme_text(s_state_block, "空闲", UI_FONT_XL, UI_DIM);
    lv_obj_set_pos(s_state_text, UI_CARD_PAD_X, UI_STATE_TEXT_Y);
    lv_obj_set_width(s_state_text, UI_STATE_HINT_W);
    lv_obj_set_style_text_align(s_state_text, LV_TEXT_ALIGN_LEFT, 0);

    // 提示行：2 行 + 省略号，左对齐（高度由 UI_STATE_HINT_H 钉死，见主题头说明）
    s_state_hint = ui_theme_text(s_state_block, "", UI_FONT_CJK, UI_SUB);
    lv_obj_set_pos(s_state_hint, UI_CARD_PAD_X, UI_STATE_HINT_Y);
    lv_obj_set_width(s_state_hint, UI_STATE_HINT_W);
    lv_obj_set_height(s_state_hint, UI_STATE_HINT_H);
    lv_obj_set_style_text_align(s_state_hint, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(s_state_hint, UI_TEXT_LINE_SPACE, 0);
    lv_label_set_long_mode(s_state_hint, LV_LABEL_LONG_DOT);

    // 运行计时：贴卡片右上角，与徽章同一行。
    // ★ 16px 思源（与状态条同一款）：等宽点阵在 16px 下 6 位数字要 96px，
    //   思源只要 52.9px，且数字是 tabular（每字等宽），秒数跳动时不会左右抽。
    //   右对齐 = 标签宽度自适应、右边缘钉死在卡片内边距上，数字变长只向左长。
    s_timer_text = ui_theme_text(s_state_block, "", UI_FONT_CJK16, UI_INFO);
    lv_obj_align(s_timer_text, LV_ALIGN_TOP_RIGHT, -UI_CARD_PAD_X, UI_STATE_TIMER_Y);
}

static void build_footer(void)
{
    // 底栏卡：R14 + 1px 描边，底色 UI_CARD2（比状态卡暗一档，明确是"附属信息"）
    lv_obj_t *footer = lv_obj_create(s_scr);
    ui_theme_card(footer, UI_CARD2, UI_STROKE, UI_R_CARD);
    lv_obj_set_pos(footer, PX(0), PY(UI_FOOTER_Y));
    lv_obj_set_size(footer, UI_W, UI_FOOTER_H);

    // 两行 16px 中文（行高 20）+ 中间 26×1 发丝线。位置全部由主题宏算出，
    // 不写死 y（教训 2：写死 y=19 时中文行高 24 直接压字）。
    s_footer_l1 = ui_theme_text(footer, "", UI_FONT_CJK16, UI_SUB);
    lv_obj_set_pos(s_footer_l1, 0, UI_FOOTER_LINE1_Y);
    lv_obj_set_width(s_footer_l1, UI_W);
    lv_obj_set_style_text_align(s_footer_l1, LV_TEXT_ALIGN_CENTER, 0);

    s_footer_sep = lv_obj_create(footer);
    ui_theme_rect(s_footer_sep, UI_HAIR);
    lv_obj_set_size(s_footer_sep, UI_FOOTER_SEP_W, 1);
    lv_obj_set_pos(s_footer_sep, (UI_W - UI_FOOTER_SEP_W) / 2, UI_FOOTER_SEP_Y);

    s_footer_l2 = ui_theme_text(footer, "", UI_FONT_CJK16, UI_SUB);
    lv_obj_set_pos(s_footer_l2, 0, UI_FOOTER_LINE2_Y);
    lv_obj_set_width(s_footer_l2, UI_W);
    lv_obj_set_style_text_align(s_footer_l2, LV_TEXT_ALIGN_CENTER, 0);
}

static void build_toast(void)
{
    // 提示条浮在状态卡之上：R16 + 1px 语义色描边（颜色由 toast_show 改）。
    //
    // ★ 不做设计稿里的"全屏半透明遮罩"：
    //   ① 半透明全屏对象在 LVGL 里会触发整屏重绘，这台机器上肉眼可见掉帧；
    //   ② 更重要的原因是语义 —— Toast 只是"顺带告诉你一句"，
    //      盖一层遮罩会让人以为需要先处理它。真正需要立刻响应的是「待审批」，
    //      那个走的是全屏覆盖层，本来就该挡住主页。
    s_toast = lv_obj_create(s_scr);
    ui_theme_card(s_toast, UI_CARD, UI_ACCENT, UI_R_LG);
    lv_obj_set_pos(s_toast, PX(UI_CARD_PAD_X), PY(UI_STATE_CARD_Y + 20));
    lv_obj_set_size(s_toast, UI_W - UI_CARD_PAD_X * 2, UI_TOAST_H);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    // 文本限 2 行、超出省略号：提示文案最长 512 字节，不限行会撑破提示条底边
    s_toast_text = ui_theme_text(s_toast, "", UI_FONT_CJK, UI_FG);
    lv_obj_set_pos(s_toast_text, 12, UI_TOAST_TEXT_Y);
    lv_obj_set_width(s_toast_text, UI_W - UI_CARD_PAD_X * 2 - 24);
    lv_obj_set_height(s_toast_text, UI_TOAST_TEXT_H);
    lv_obj_set_style_text_line_space(s_toast_text, UI_TEXT_LINE_SPACE, 0);
    lv_label_set_long_mode(s_toast_text, LV_LABEL_LONG_DOT);
}

void app_ui_create(void)
{
    if (s_scr) return;

    s_scr = lv_obj_create(NULL);
    ui_theme_rect(s_scr, UI_BG);

    build_statusbar();
    build_state_card();
    build_footer();
    build_toast();

    lv_screen_load(s_scr);

    // 覆盖层（审批/余额）在主页之后创建，靠 HIDDEN 标志盖在主页上
    app_screens_create();

    s_timer = lv_timer_create(ui_tick, 20, NULL);

    s_pending_link = AP_LINK_OFF;
    refresh_link();
    refresh_state();
    bsp_display_backlight((uint8_t)ap_settings_get()->brightness);
    note_activity();

    ESP_LOGI(TAG, "界面已创建（精简状态版）");
}

void app_ui_destroy(void)
{
    // 顺序要紧：先覆盖层与定时器，再主页。定时器回调会访问覆盖层对象，
    // 反过来销毁会留下悬空指针。
    app_screens_destroy();
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_toast_timer) { lv_timer_delete(s_toast_timer); s_toast_timer = NULL; }
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }

    s_link_bars[0] = NULL; s_link_bars[1] = NULL;
    s_link_bars[2] = NULL; s_link_bars[3] = NULL;
    s_batt_shell = NULL; s_batt_fill = NULL; s_batt_tip = NULL; s_batt_text = NULL;
    s_app_name = NULL;
    s_state_block = NULL; s_state_chip = NULL; s_state_chip_dot = NULL;
    s_state_text = NULL; s_state_hint = NULL; s_footer_l1 = NULL; s_footer_l2 = NULL;
    s_footer_sep = NULL;
    s_toast = NULL; s_toast_text = NULL;
}

// ── 跨任务更新入口 ────────────────────────────────────────────────────────

void app_ui_on_link_state(ap_link_state_t state)
{
    s_pending_link = state;
    s_link_dirty = true;
}

void app_ui_apply_brightness(void)
{
    // 屏亮着才调背光：熄屏状态下改亮度不该把屏点亮
    bsp_display_backlight(s_screen_on ? (uint8_t)ap_settings_get()->brightness : 0);
}

void app_ui_set_host_clock(long epoch_sec, int tz_offset_min)
{
    // 主机时间随 task.state 一起到（见 app.c）。和状态一样走"待处理 + 脏标志"，
    // 因为这里可能是 BLE 任务在调，而 LVGL 不能跨任务碰。
    if (epoch_sec <= 0) return;
    s_pending_clock_tz = tz_offset_min;
    s_pending_clock_sec = epoch_sec;
    s_clock_dirty = true;
}

void app_ui_set_task_state(ap_task_state_t state, const char *title, long elapsed_ms)
{
    // ★ 录音期间**缓存而不应用**：主机的 task.state 推送可能因为链路重连、
    //   定时器等任何原因在录音中途到达，覆盖"识别中…"的显示 ——
    //   表现为"按住说话到一半界面跳到空闲"（真机返工记录）。
    //   这里把状态暂存起来，等 app_ui_set_recording(false) 时一次性应用。
    //
    //   计时种子一并缓存：录音结束后补推的那条会带上最新 elapsedMs，
    //   如果丢掉它，松手后计时会从旧锚点继续，看起来"少走了一段"。
    if (app_voice_recording()) {
        s_deferred_state = state;
        clean_text(title, s_deferred_title, sizeof(s_deferred_title));
        s_deferred_elapsed_ms = elapsed_ms;
        s_deferred_state_pending = true;
        return;
    }
    s_pending_state = state;
    clean_text(title, s_pending_title, sizeof(s_pending_title));
    s_pending_elapsed_ms = elapsed_ms;
    s_state_dirty = true;
}

/**
 * 录音状态变化。由 app_voice.c 在开始/结束录音时调用。
 *
 * 结束录音时把缓存的状态变化一次性应用（如果有）。
 */
void app_ui_set_recording(bool recording)
{
    if (recording) return;   // 开始录音不需要额外动作
    if (!s_deferred_state_pending) return;
    s_deferred_state_pending = false;
    s_pending_state = s_deferred_state;
    snprintf(s_pending_title, sizeof(s_pending_title), "%s", s_deferred_title);
    s_pending_elapsed_ms = s_deferred_elapsed_ms;
    s_state_dirty = true;
}

void app_ui_set_device_info(int battery_percent, int8_t rssi, uint16_t mtu)
{
    // 电量放右上角显示（用户要求）；RSSI 由信号格表达（refresh_signal_bars），
    // MTU 是链路细节，只进控制面板。
    (void)rssi;
    (void)mtu;
    s_pending_battery = battery_percent;   // -1 = 未知
    s_batt_dirty = true;
}

void app_ui_post_toast(const char *text, bool urgent)
{
    if (!text) return;
    // ★ UTF-8 安全截断（而不是 snprintf 按字节硬切）：识别结果是动态长句，
    //   按字节切会切在汉字中间，LVGL 渲染出方块。见 utf8_clamp.c 的说明。
    utf8_copy_clamped(text, s_pending_toast, sizeof(s_pending_toast));
    s_pending_toast_urgent = urgent;
    s_pending_toast_sticky = false;
    s_toast_dirty = true;
}

/** 粘性 toast：不自动消失，直到下一个 toast 或 app_ui_hide_toast()。 */
void app_ui_post_toast_sticky(const char *text, bool urgent)
{
    if (!text) return;
    utf8_copy_clamped(text, s_pending_toast, sizeof(s_pending_toast));
    s_pending_toast_urgent = urgent;
    s_pending_toast_sticky = true;
    s_toast_dirty = true;
}

// ── 按键 ──────────────────────────────────────────────────────────────────

bool app_ui_handle_key(int btn, int event)
{
    if (!s_scr) return false;

    // 任何按键都算"有动静"，推迟熄屏并确保屏幕可见
    screen_wake();

    // 覆盖层优先消费按键（审批页尤其不能漏给主页）
    if (app_screens_handle_key(btn, event)) return true;

    // PTT 录音：确定键的按下/松开交给 app_main（按住说话）。
    // ★ 这两个事件必须放行 —— 曾被下面的兜底消费拦住，app_main 的录音分支
    //   永远收不到，表现就是"按了没反应"（真实反馈过的缺陷）。
    if (btn == APP_UI_BTN_OK && (event == APP_UI_EV_PRESS || event == APP_UI_EV_RELEASE)) return false;

    // 下键长按交给 app_main（请求余额）
    if (btn == APP_UI_BTN_DOWN && event == APP_UI_EV_LONG) return false;

    // 其余按键在精简版里没有动作，但**消费掉**，避免漏出去触发别的行为
    return true;
}
