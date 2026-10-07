// main/app_alert.c —— 提示音实现（正弦音调，运行时合成，不占 Flash）
//
// 做法：**运行时合成正弦波**，而不是把音频样本烧进固件。
//
// 对比过的三种方案：
//   1. 内置语音片段  → 有内容但几百 KB，且嘈杂环境下听不清（见 app_alert.h）
//   2. 内置音调样本  → 几十 KB，但样本本质是"几个正弦波的叠加"，存样本不如存参数
//   3. 运行时合成    → **零 Flash 占用**，只有一段几十行的合成代码
// 选 3。音调是可完全参数化的（频率 + 时长 + 包络），没有任何存样本的必要。
//
// ★ 包络很重要：直接截断正弦波会在起止处产生"咔哒"声（波形不连续）。
//   因此每段音调都做 5ms 的淡入淡出。
#include "app_alert.h"

#include <math.h>
#include <string.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "app_alert";

#define SAMPLE_RATE 16000        // 与 BSP 的默认音频格式一致
#define TONE_MS_MAX 400          // 单段音调最长时长
#define FADE_MS 5                // 淡入淡出，消除咔哒声
// 默认音量（中档）；运行期由设置页/主机配置改写（见 alert_set_volume）
#define ALERT_VOLUME_DEFAULT 70

// 音调描述
typedef struct {
    uint16_t freq_hz;   // 0 = 静音（用于在双响之间插一段间隔）
    uint16_t ms;
} tone_t;

// ── 音型定义 ──────────────────────────────────────────────────────────────
//
// 两种状态刻意用**截然不同的节奏**，而不是只差一个音高：
// 节奏差异在嘈杂环境或注意力不集中时仍然可辨，纯音高差异则不行。
//
//   待审批：高-低 双响，急促 → "有事要你处理"
//   已完成：低-高 单次上滑，舒缓 → "结束了，知道就好"
static const tone_t TONES_APPROVAL[] = {
    { 1200, 110 }, { 0, 60 }, { 1600, 110 }, { 0, 60 }, { 1200, 110 },
};
// 审批/计划"已选择"：上行双音（1200→1500），短促、明确是"收到了"
static const tone_t TONES_DECIDED[] = {
    { 1200, 70 }, { 30, 30 }, { 1500, 90 },
};
static const tone_t TONES_COMPLETED[] = {
    { 900, 130 }, { 0, 40 }, { 1200, 220 },
};

static bool s_initialized = false;
// 提示音量（百分比）：用户可在设备设置页调档，见 app_settings_logic.c 的档位表
static uint8_t s_volume = ALERT_VOLUME_DEFAULT;
// 与音量保持一致的开关位（0 = 静音）；保留它是为了兼容主机下发的布尔配置
static bool s_enabled = true;
static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static int16_t s_buf[SAMPLE_RATE * TONE_MS_MAX / 1000];

/** 合成一段正弦音调进 s_buf，返回样本数。 */
static size_t synth_tone(uint16_t freq_hz, uint16_t ms)
{
    const size_t n = (size_t)SAMPLE_RATE * ms / 1000;
    const size_t fade = (size_t)SAMPLE_RATE * FADE_MS / 1000;
    const float two_pi_f = 2.0f * (float)M_PI * (float)freq_hz / (float)SAMPLE_RATE;

    for (size_t i = 0; i < n && i < sizeof(s_buf) / sizeof(s_buf[0]); i++) {
        if (freq_hz == 0) {
            s_buf[i] = 0;                       // 间隔：静音
            continue;
        }
        float v = sinf(two_pi_f * (float)i);
        // 淡入淡出包络：不做的话起止处会有明显"咔哒"声
        if (i < fade) {
            v *= (float)i / (float)fade;
        } else if (i + fade > n) {
            v *= (float)(n - i) / (float)fade;
        }
        s_buf[i] = (int16_t)(v * 12000.0f);     // 幅度留足余量，避免削顶
    }
    return n;
}

static void play_kind(alert_kind_t kind)
{
    const tone_t *tones = NULL;
    size_t count = 0;
    switch (kind) {
    case ALERT_APPROVAL:
        tones = TONES_APPROVAL;
        count = sizeof(TONES_APPROVAL) / sizeof(TONES_APPROVAL[0]);
        break;
    case ALERT_COMPLETED:
        tones = TONES_COMPLETED;
        count = sizeof(TONES_COMPLETED) / sizeof(TONES_COMPLETED[0]);
        break;
    case ALERT_DECIDED:
        tones = TONES_DECIDED;
        count = sizeof(TONES_DECIDED) / sizeof(TONES_DECIDED[0]);
        break;
    default:
        return;
    }

    // 音频格式按需设置：BSP 的格式是全局的，别的模块可能改过它
    if (bsp_audio_set_format(SAMPLE_RATE, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "设置音频格式失败，跳过提示音");
        return;
    }
    bsp_audio_set_volume(s_volume);

    for (size_t i = 0; i < count; i++) {
        const size_t n = synth_tone(tones[i].freq_hz, tones[i].ms);
        if (n == 0) continue;
        // 写入失败（如音频未初始化）就放弃这一次，不要让任务卡住
        if (bsp_audio_write(s_buf, n * sizeof(int16_t)) != ESP_OK) {
            ESP_LOGW(TAG, "音频写入失败，提示音中断");
            return;
        }
    }
}

static void alert_task(void *arg)
{
    (void)arg;
    alert_kind_t kind;
    for (;;) {
        if (xQueueReceive(s_queue, &kind, portMAX_DELAY) == pdTRUE) {
            play_kind(kind);
        }
    }
}

esp_err_t alert_init(void)
{
    if (s_initialized) return ESP_OK;

    // 队列深度 1：提示音只关心"最新要响什么"。
    // 深度大了会在状态快速抖动时积压成一串噪音，而提示音过时就没有意义了。
    s_queue = xQueueCreate(1, sizeof(alert_kind_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    if (xTaskCreate(alert_task, "app_alert", 4096, NULL, 4, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "提示音就绪（运行时合成，不占 Flash）");
    return ESP_OK;
}

void alert_play(alert_kind_t kind, bool enabled)
{
    if (!s_initialized || !enabled || kind == ALERT_NONE) return;

    // 队列满时覆盖旧请求：新的状态比旧的更值得播报。
    // 不阻塞 —— 调用方可能在 LVGL 定时器里（见 app_alert.h）。
    if (xQueueSend(s_queue, &kind, 0) != pdTRUE) {
        alert_kind_t dropped;
        (void)xQueueReceive(s_queue, &dropped, 0);
        (void)xQueueSend(s_queue, &kind, 0);
    }
}

uint8_t alert_get_volume(void) { return s_volume; }

void alert_set_volume(uint8_t percent)
{
    const uint8_t next = percent > 100 ? 100 : percent;
    if (next != s_volume) {
        ESP_LOGI(TAG, "提示音量 %u%% → %u%%", (unsigned)s_volume, (unsigned)next);
    }
    s_volume = next;
    // 0 = 静音，等价于旧的"关闭提示音"；非 0 时同步打开开关位。
    s_enabled = next > 0;
}

bool alert_get_setting(void) { return s_volume > 0; }

void alert_set_setting(bool enabled)
{
    alert_set_volume(enabled ? ALERT_VOLUME_DEFAULT : 0);
}
