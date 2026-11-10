// main/app_ui_theme.h —— 界面设计约束（单一事实来源）。
//
// 设计意图（v2.0 起）：**圆角卡片 + 冷灰底 + 终端绿强调**。
//   旧版是"bit 极简"：硬边、无圆角、无阴影，像 80 年代掌机。
//   240×320 的小屏上硬边确实干净，但卡片之间无法形成层次，用户反馈
//   "分不清哪一块是状态、哪一块是提示"。v2.0 改用 16px 圆角卡片 +
//   1px 内描边来分区，颜色仍克制（见 docs/09 设计稿）。
//
// ★ 这一版（P2）**只换令牌与助手**，不改任何版面常数：
//   圆角化会改变圆角处的可用像素，版面预算要跟着重算，属于 P3 的事。
//   把两件事混在一起改，一旦布局红了就分不清是令牌还是坐标的问题。
//
// ★ 一条硬约束：**界面文本只用 ASCII**。
//   原因不是审美偏好，而是踩过的坑：LVGL 内置的 CJK 子集不覆盖本应用文案
//   （48 个中文字里缺 23 个 → 满屏方框），自带字体子集也仍然显示成方框，
//   两次都没能在真机上拿到可读的中文。ASCII 是点阵字体必然覆盖的，
//   而方框/空白是**无法在设备上自测**的那类故障（没有截图能力时只能靠用户拍照）。
//   因此这一版改为 ASCII + 图标化的状态指示（用色块画，不依赖任何字形）。
//   以后若要恢复中文，正确顺序是：先搞定"能在设备上看见屏幕"的手段，
//   再引入 CJK 字体，否则又是在盲改。
#pragma once

#include "lvgl.h"

#include <stdint.h>

// ── 调色板 v2.0（单一来源，取自 docs/09 设计稿的 :root）───────────────────
//
// ★ 先有语义名，再由旧名**指向**它们。
//   反向做（旧名是真值、新名再抄一遍）会让同一颜色存在两处真值，
//   改一处忘一处就会出现"卡片和描边不是同一套灰"的细微不一致 ——
//   这种问题在小屏上不刺眼，但整屏会显得脏，且极难定位。
#define UI_BG_v2 0x0B0F14       // 页面底
#define UI_CARD 0x121A26        // 卡片底（比页面亮一档，用于承载内容）
#define UI_CARD2 0x101823       // 次级卡片底
#define UI_STROKE 0x223042      // 卡片 1px 内描边
#define UI_HAIR 0x1B2735        // 细分隔线（比描边更弱）
#define UI_FG_HI 0xE8EEF5       // 主文本（比旧版更亮，圆角底上更清晰）
#define UI_SUB 0x8FA3B8         // 次文本
#define UI_DIM2 0x5A6B7D        // 弱文本
#define UI_ACC 0x3DDC84         // 终端绿（正常/在线）
#define UI_INFO_v2 0x4DB8FF     // 青蓝（信息/选中）
#define UI_WARN_v2 0xFFB454     // 琥珀（进行中/注意）
#define UI_ERR_v2 0xFF5D5D      // 红（错误/危险）

// 旧名 → 新值。保留旧名是为了让既有调用点一处不改就换上新配色；
// 以后新增代码请直接用上面的语义名。
#define UI_BG UI_BG_v2
#define UI_PANEL UI_CARD2
#define UI_LINE UI_STROKE
#define UI_FG UI_FG_HI
#define UI_DIM UI_DIM2
#define UI_ACCENT UI_ACC
#define UI_WARN UI_WARN_v2
#define UI_ERR UI_ERR_v2
#define UI_INFO UI_INFO_v2

// 列表行与层次
#define UI_FG_DIM UI_SUB      // 次级前景：未选中行的标题

// ── 预混色（12% 语义色叠在 UI_CARD 上的**实色**）──────────────────────────
//
// ★ 为什么预混成常量，而不是运行时用 opa 叠一层：
//   LVGL 在滚动/裁剪内容上叠半透明会把图层退化成离屏合成，整屏重绘；
//   240×320 上这就是肉眼可见的卡顿。徽章、选中行这类"淡色底"用量固定，
//   直接算出实色最省事，也保证与卡片底不会因为父对象透明度变化而偏色。
//
// 计算基准：目标色 over UI_CARD(#121A26) @12%，选中行 @10%。
// 换卡片底色就必须重算这几个值，否则徽章会浮在"另一套灰"上。
#define UI_ACC_BG 0x173131       // 绿：允许 / 完成
#define UI_INFO_BG 0x192D40      // 蓝：运行中
#define UI_WARN_BG 0x2E2C2C      // 琥珀：待审批
#define UI_ERR_BG 0x2E222D       // 红：拒绝 / 错误
#define UI_DIM_BG 0x1B2430       // 灰：空闲
#define UI_ROW_SEL 0x162D2F      // 选中行底（绿 10%）：明显亮于卡片，小屏才看得出

// ── 圆角阶（v2.0，与 docs/09 §1 一一对应）─────────────────────────────────
//
// ★ 只允许用这几档，不要就地写 7、11、13 这种数。
//   圆角在 240px 宽的屏上是"一眼层级"，随手取值会让相邻卡片看起来
//   不是同一套设计；统一阶之后，逐页改造也不必每页重新决定。
//   99 = 药丸（LVGL 里半径大于半高即自动收成药丸）。
#define UI_R_XS 4               // 键帽内 / 小方块
#define UI_R_SM 10              // 列表行
#define UI_R_CARD 14            // 卡片（底栏卡、次卡）
#define UI_R_LG 16              // 大卡 / 浮层（状态卡、Toast）
#define UI_R_PILL 99            // 药丸（徽章、主按钮）

// ── 字体 ──────────────────────────────────────────────────────────────────
//
// 界面是**中文**，字体分两层，由 LVGL 的 fallback 机制串起来：
//
//   app_ui_font_cjk   中文子集（思源黑体, 2bpp, 20px）← 主力，line_height 24
//   app_ui_font_8     点阵 ASCII（Press Start 2P, 1bpp, 8px）
//   app_ui_font_16    点阵 ASCII（Press Start 2P, 1bpp, 16px）← 也带 .fallback → 中文
//
// 这样 ASCII 保持"极客 bit"的点阵观感，中文由思源黑体渲染，
// 而**控件上只需指定一个字体**，不用在业务代码里判断字符属于哪种。
//
// ★ 中文字形踩过两次坑（内置 CJK 子集缺 23 字；自制子集仍显示方框），
//   因此字形集由 tools/make-fonts.py 从源码字面量**自动提取**，
//   并且脚本会解析生成产物里的真实字形表来核对覆盖率，
//   缺字直接报错。校验已接入 validate.sh --static。
#define UI_FONT_S (&app_ui_font_8)
#define UI_FONT_M (&app_ui_font_16)
// 中文标题用 16px 点阵位图的 fallback（思源黑体）。
// 单独暴露一个名字，便于"只想要中文观感"的地方显式使用。
#define UI_FONT_CJK (&app_ui_font_cjk)   // 20px, 2bpp（正文/标题/选项）
// 16px 中文：**提示行专用**。20px 一行约 10 个汉字，底栏/按键说明经常塞不下；
// 16px 每行约 13 个汉字，行高 20px，字库约 +30KB（见 tools/make-fonts.py）。
#define UI_FONT_CJK16 (&app_ui_font_cjk16)   // 16px, 2bpp
// 超大字号：用于"状态大字"。设备可能被放在桌角，状态必须隔一两米也能认出，
// 因此单独生成 40px 档（见 tools/make-fonts.py 的 UI_FONT_XL 说明）。
#define UI_FONT_XL (&app_ui_font_xl)
// 运行计时 28px：**只有数字和冒号**（TIMER_CHARS）。
// ★ 为什么不复用 UI_FONT_CJK16：16px 的 "10:23:45" 只有 61px，
//   放在桌角得凑近才读得到，而计时恰恰是"要不要现在走过去"的判断依据。
// ★ 为什么不复用 UI_FONT_XL：40px 档按状态词生成，字符集里根本没有数字，
//   拿它显示计时就是满屏方块。字号档与字符集是绑定的，不能只挑字号。
#define UI_FONT_TIMER (&app_ui_font_timer)

extern const lv_font_t app_ui_font_8;
extern const lv_font_t app_ui_font_16;
extern const lv_font_t app_ui_font_cjk;
extern const lv_font_t app_ui_font_cjk16;
extern const lv_font_t app_ui_font_xl;
extern const lv_font_t app_ui_font_timer;

// ── 布局栅格（面板可视区，全部硬边）──────────────────────────────────────
//
// ★ 面板不是整块 240×320：外壳有圆角，边缘约 10px 的内容会被遮挡
//   （用户反馈"屏幕有一点遮挡"）。因此**整个界面按安全区布局**，
//   四边各让出 UI_INSET，所有坐标从安全区算起，而不是从物理边缘算起。
//   这样做而不是整体缩放：字体会被缩放糊掉，而重新排版只是挪坐标。
#define UI_INSET 10
#define UI_VISUAL_W (240)               // 物理可视宽度（背景铺满它）
#define UI_VISUAL_H (320)               // 物理可视高度

#define UI_W (UI_VISUAL_W - UI_INSET * 2)   // 安全区宽度 = 220
#define UI_H (UI_VISUAL_H - UI_INSET * 2)   // 安全区高度 = 300

// 安全区内的相对坐标；渲染时统一加 UI_INSET 偏移（见 app_ui.c 的 POS 宏）
#define UI_PAD 6

// ── 字体度量（来自 main/fonts/ 的生成产物；改字体后必须同步核对）──────────
//
// ★ 版面的每个纵向尺寸都从这里推出，不写死像素偏移。踩过的坑：
//   给底栏第二行写死 y=19，而中文行高 24px > 19px，两行文字直接压在一起。
// ★ 行间留白同样要算：两个 24px 行框若首尾相接（间隙 0），20px 的中文
//   视觉上仍是一坨。行框之间至少留 4px，宁可少显示一行也不挤。
#define UI_LINE_H_CJK 24        // app_ui_font_cjk.line_height（20px 字号，字形高 22）
#define UI_GLYPH_H_CJK 22
#define UI_LINE_H_XL 38         // app_ui_font_xl.line_height
// 16px 中文的行高。用在底栏、键帽提示这类"要塞更多字"的地方：
// 20px 一行约 10 个汉字，16px 每行约 13 个，行高 20px。
#define UI_LINE_H_CJK16 20
// 运行计时 28px 的行高。★ 28px 字号的行高只有 21px —— 这不矛盾：
// lv_font_conv 的 line_height 取的是**实际字形盒**（digits 15×21），没有下伸部，
// 所以行框比字号小。不要按"字号 + 4"想当然写 32，那会在计时上方多出 11px 空档。
#define UI_LINE_H_TIMER 21
#define UI_TEXT_LINE_SPACE 4    // ui_theme_text 默认行间距
#define UI_TEXT_PITCH_CJK (UI_LINE_H_CJK + UI_TEXT_LINE_SPACE)   // 多行行距 = 28

// ── 版面规格（安全区坐标，由 tools/verify-layout.py 验证）────────────────
//
// v2.0 纵向预算（安全区 220×300）——**圆角卡片版**：
//     状态条      0 .. 22    高 22   信号格 + 应用名 + 电量（直接坐在页面底上）
//     间隙         22 .. 30   高 8
//     状态卡     30 .. 212    高 182  R16 + 1px 描边，卡内：徽章/计时/大字/提示
//     间隙        212 .. 220  高 8
//     底栏卡    220 .. 296    高 76  R14 + 1px 描边，两行提示 + 26×1 发丝线
//     下边距    296 .. 300    高 4
//
// ★ 旧版是「状态条 28 / 状态主体 211（分隔线夹住）/ 底栏 56」的硬边三段式。
//   换成卡片之后**中间的空隙本身承担了分隔作用**，不再需要 1px 分隔线；
//   状态卡与底栏卡之间的 8px 缝比任何线条都清楚地说明"这是两块东西"。
//
// ★ 状态卡高 182 是**算出来的**，不是拍的：见下面 UI_STATE_HINT_H 的推导。
#define UI_STATUSBAR_Y 0
#define UI_STATUSBAR_H 22
#define UI_CARD_GAP 8                                   // 卡片之间的缝
#define UI_FOOTER_H 76
#define UI_FOOTER_Y (UI_H - UI_FOOTER_H - 4)            // = 220（下边距 4）
#define UI_STATE_CARD_Y (UI_STATUSBAR_H + UI_CARD_GAP)  // = 30
#define UI_STATE_CARD_H (UI_FOOTER_Y - UI_CARD_GAP - UI_STATE_CARD_Y) // = 182

// ── 状态条横排布局（四个元素**统一 16px 思源黑体**）──────────────────────
//     信号格     0 .. 18     4 格递增，圆端
//     应用名    25 .. 91     16px "AI 通行证" 实宽 65.8（框宽 73）
//     当前时间 102 .. 146    16px "20:17" 实宽 39.6，右对齐
//     电量百分比 150 .. 194   16px "100%" 实宽 41.1，右对齐
//   （缝 6） 电池外壳 198 .. 218   20 宽 +（缝 4）2px 正极凸起贴右缘
//
// ★ 框宽比实宽各多留 2px（40→42、42→44）：adv_w 是 1/16 像素的定点数，
//   正好相等时四舍五入会差半个像素，宁可多给 2px 也不要在满电/整点时裁掉半个字形。
//
// ★ 为什么必须**同一款字体**（这条是踩坑记录）：
//   状态条原先应用名用 16px 思源、时间与电量用 8px 点阵，用户一眼看出
//   "大小不统一、没对齐"。看起来 8px 是被"220px 放不下 16px"逼的 ——
//   我据此算出 "全 16px 要 250px，必须缩文案" 并让用户选了缩文案的方案，
//   **这个结论是错的**：算错的地方是拿**点阵字体**的度量（Press Start 2P
//   在 16px 下每个字符前进 16px，5 字符 "20:17" 就是 80px，3 字符 "100"
//   是 48px）去估**比例字体**。
//   思源黑体 16px 是比例字体：数字 8.81、冒号 4.31、百分号 14.63，
//   "20:17" 只有 39.6，"100%" 只有 41.1 —— 四个元素全 16px 只占 186px，
//   220 的安全区还余下 11px 呼吸，**文案一个字都不用缩**。
//   教训：字宽必须从生成字体的真实 adv_w 取（tools/verify-layout.py 的
//   text_width 就是干这个的），不能按"字数 × 字号"心算等宽。
//
// ★ 时间为什么放电量左边：用户明确要求；且右侧没有余量（贴右缘的是电池
//   外壳与正极凸起，再往外就是圆角）。
// ★ 三者共用 UI_FONT_CJK16 的另一个好处：同一个字体里汉字与数字基线一致，
//   纵向不用各自调 y —— 全部 UI_CENTER_Y(22, 20) = 1。
#define UI_APPNAME_X 25                                     // 信号格 18 + 间距 7
#define UI_BATT_W 44                                        // "100%" 实宽 41.1 + 2 缓冲
#define UI_BATT_GAP 4                                       // 与电池外壳的缝
#define UI_BATT_X (UI_W - 22 - UI_BATT_GAP - UI_BATT_W)      // = 150（右缘 194）
#define UI_CLOCK_W 42                                       // "20:17" 实宽 39.6 + 2 缓冲
#define UI_CLOCK_GAP 6                                      // 与百分比的呼吸缝
#define UI_CLOCK_X (UI_BATT_X - UI_CLOCK_GAP - UI_CLOCK_W)   // = 102（右缘 144）

// 卡片左右内边距（设计稿 §3：徽章 left14、提示 right14）
#define UI_CARD_PAD_X 14

// 状态卡内部（相对卡片左上角）—— **信息整组居中**，纵向顺序：信息行 → 提示 → 计时
//     信息行   27 .. 65     灯 + XL 大字作为一组，**水平居中**（行框 38）
//     提示     73 .. 125    "N 个任务执行中"等主机标题，最多 2 行，居中
//     运行计时 133 .. 154   28px 行框 21，**在任务提示正下方居中**（只在「运行中」出现）
//
// ★ 10-31 用户要求：状态这组信息要居中（原来灯贴左、大字贴左、计时贴右，
//   整张卡片看起来"散在四个角"）。改法是：灯与大字放进一个横排容器，
//   由 flex 居中 —— 而不是给每个元素各算一个 x。
//   原因很实在：大字宽度随状态词变（空闲 2 字 80px、运行中 3 字 120px），
//   "整组居中"要求灯跟着一起挪；逐个写死 x 就得为每个状态词维护一套坐标，
//   改一个字就要重排一次，且必然有一处会忘。
//
// ★ 11-09 用户要求（两次）：先把计时从右上角移到状态下方，再要求它落到
//   「1 个任务正在进行」这行提示的**下面**。最终顺序固定为 信息行 → 提示 → 计时，
//   三者在卡片里整体垂直居中；计时与信息行同一条中线（TOP_MID 对齐）。
//
// ★ 纵向不是拍出来的数字，是算的：
//   块高 = 信息行 38 + 缝 8 + 提示 52 + 缝 8 + 计时 21 = 127
//   卡片内可用高 182 → 上边距 = (182 − 127) / 2 = 27
//   → 信息行 27..65、提示 73..125、计时 133..154，上下各留 27/28，视觉平衡。
//
// ★ 状态灯为什么是 22×22 而不是设计稿写的 18 高药丸：
//   ① 18px 是按 11px 西文算的，设备上中文最小 16px（行高 20），18px 的胶囊
//      装 20px 行框会裁字 —— 中文被裁就是方块（见文件头教训 1）；
//   ② 10-31 起灯里不再放文字（原先"圆点 + 状态词"与大字重复），
//      尺寸改为等高固定的正圆。留在 22 是为了在 38px 的信息行里不显小。
#define UI_STATE_CHIP_H 22
#define UI_STATE_CHIP_DOT 5         // 灯芯：5px 实色点
#define UI_STATE_CHIP_GAP 10        // 灯与大字之间（flex 列间距）

// ★ 提示只给 **2 行** —— 这是取舍，不是"放不下"。
//   3 行其实算得开：块高 = 38 + 8 + (3 × 28 − 4) + 8 + 21 = 155 < 182（卡高）。
//   不这么做的原因是**居中会把大字顶上去**：3 行时上边距只剩 13，
//   信息行被推到 13..51，大字离开视觉中心 29px，看起来像"整块内容偏上"。
//   主机标题通常一两行就说完了，超出部分用省略号比第 3 行更有信息量。
#define UI_STATE_HINT_LINES 2
#define UI_STATE_HINT_H (UI_STATE_HINT_LINES * UI_TEXT_PITCH_CJK - UI_TEXT_LINE_SPACE) // = 52
#define UI_STATE_HINT_W (UI_W - UI_CARD_PAD_X * 2)      // = 192

// 信息行（灯 + 大字）：行高取 XL 行框，灯在其内纵向居中
#define UI_STATE_ROW_H UI_LINE_H_XL                                 // = 38
#define UI_STATE_ROW_GAP 8                                          // 信息行与提示之间
#define UI_STATE_HINT_GAP 8                                         // 提示与计时之间
#define UI_STATE_BLOCK_H (UI_STATE_ROW_H + UI_STATE_ROW_GAP + UI_STATE_HINT_H + UI_STATE_HINT_GAP + UI_LINE_H_TIMER) // = 127
#define UI_STATE_ROW_Y ((UI_STATE_CARD_H - UI_STATE_BLOCK_H) / 2)      // = 27
#define UI_STATE_HINT_Y (UI_STATE_ROW_Y + UI_STATE_ROW_H + UI_STATE_ROW_GAP) // = 73
#define UI_STATE_TIMER_Y (UI_STATE_HINT_Y + UI_STATE_HINT_H + UI_STATE_HINT_GAP) // = 133
// 灯在信息行里纵向居中的偏移（flex 也会居中，这里给校验脚本一个可算的坐标）
#define UI_STATE_CHIP_INSET ((UI_STATE_ROW_H - UI_STATE_CHIP_H) / 2) // = 8

// 底栏卡内部（两行提示 + 中间 26×1 发丝线）
//   内容高 = 20 + 8 + 1 + 8 + 20 = 57，卡高 76 → 上下各留 9/10，接近居中
#define UI_FOOTER_SEP_W 26
#define UI_FOOTER_LINE1_Y 9
#define UI_FOOTER_SEP_Y (UI_FOOTER_LINE1_Y + UI_LINE_H_CJK16 + 8)          // = 37
#define UI_FOOTER_LINE2_Y (UI_FOOTER_SEP_Y + 1 + 8)                        // = 46

// 提示条（浮在状态主体上的短暂通知）
#define UI_TOAST_H 68
#define UI_TOAST_TEXT_Y 8
#define UI_TOAST_TEXT_LINES 2
#define UI_TOAST_TEXT_H (UI_TOAST_TEXT_LINES * UI_TEXT_PITCH_CJK - UI_TEXT_LINE_SPACE) // = 52

// ── 覆盖层规格（审批 / 余额，全屏覆盖安全区）──────────────────────────────
#define OV_TITLE_H 28
// ── 系统设置页（覆盖层）──
//     标题   0 .. 28
//     行 1   44 .. 74    高 30（行间 4，与追问页行同款样式）
//     行 2   78 .. 108
//     行 3  112 .. 142
//     行 4  146 .. 176
//     配对行 180 .. 232  高 52 —— 两行：状态行 + 主机名行。
//     ★ 配对行必须两行：单行只有约 9 个汉字宽，"已配对（主机名）"会被
//       省略号截断（真机反馈"已配对没显示全"）。主机名单独一行放得下
//       常见主机名，超长才落省略号。
//     底栏  276 .. 300
#define OV_SET_ROWS_Y (OV_TITLE_H + 16)   // = 44
#define OV_SET_PAIR_H (2 * UI_LINE_H_CJK + UI_TEXT_LINE_SPACE)   // = 52（两行中文）
#define OV_FOOT_H 24

// 审批页（覆盖层内坐标，2026-10-06 二改：**去掉倒计时**，只有一行摘要 + 按钮）：
//     标题     0 .. 28
//     摘要    38 .. 62    单行，超长省略号（完整信息在电脑卡片上；高风险=红、普通=黄）
//     选项 1   98 .. 128   高 30（行间 4）
//     选项 2  132 .. 162   高 30
//     底栏    276 .. 300   高 24
//   审批页停留到用户在电脑端或设备端作答为止，不再有倒计时条与倒计时文字。
#define OV_APPR_TOOL_Y (OV_TITLE_H + 10)                          // = 38（摘要行）
#define OV_APPR_OPT_H 30
#define OV_APPR_OPT_GAP 4
#define OV_APPR_OPTS_Y (OV_APPR_TOOL_Y + UI_LINE_H_CJK + 36)      // = 98

// 余额页（覆盖层内坐标）：
//     标题     0 .. 28
//     总额    40 .. 64
//     明细    80 .. 200   最多 4 行，行距 8（多行数字明细舒展不挤）
#define OV_BAL_TOTAL_Y (OV_TITLE_H + 12)                          // = 40
#define OV_BAL_LINE_SPACE 8
#define OV_BAL_LINES_Y (OV_BAL_TOTAL_Y + UI_LINE_H_CJK + 16)      // = 80
#define OV_BAL_LINES 4
#define OV_BAL_LINES_H (OV_BAL_LINES * (UI_LINE_H_CJK + OV_BAL_LINE_SPACE) - OV_BAL_LINE_SPACE) // = 120

// 识别结果卡（覆盖层内坐标，docs/06 §2）：
//     标题     0 .. 28
//     文本    38 .. 268   内部滚动（识别文本长短不定，固定行数会截断长句）
//     底栏   276 .. 300
#define OV_VOICE_TEXT_Y (OV_TITLE_H + 10)                                    // = 38
// 结果卡在正文与底栏之间留**三行**按键说明（用户指定）：
//     单击上键 删除/返回 / 单击确认 填入 / 长按确认 发送
// 提示行用 16px 中文（行高 20px）。
// ★ 高度必须按**行距**算，不能只按行高：ui_theme_text 默认带 UI_TEXT_LINE_SPACE(4) 行距，
//   三行需要 3*20 + 2*4 = 68px。早先用 60px 时第三行「长按确认 发送」被裁掉（真机反馈）。
//   写法与主题里其它多行控件一致：行数 × 行距 − 行距。
//   UI_LINE_H_CJK16 已在字体度量一节定义（底栏也要用它算行位置）。
#define UI_TEXT_PITCH_CJK16 (UI_LINE_H_CJK16 + UI_TEXT_LINE_SPACE)         // = 24
#define OV_VOICE_KEYS_LINES 3
#define OV_VOICE_KEYS_H (OV_VOICE_KEYS_LINES * UI_TEXT_PITCH_CJK16 - UI_TEXT_LINE_SPACE)  // = 68
// 结果卡不收底栏（共用底栏被隐藏），三行说明直接**贴底排**，省掉原来那一节空白。
#define OV_VOICE_KEYS_Y (UI_H - OV_VOICE_KEYS_H - 6)                       // = 234
#define OV_VOICE_TEXT_H (OV_VOICE_KEYS_Y - OV_VOICE_TEXT_Y - 6)            // = 190

// 追问 / 计划评审页（2026-10-06 改版：一行极短摘要 + 按钮行，正文在电脑卡片）：
//     标题     0 .. 28     「问题 i/N」或「计划评审」
//     摘要    34 .. 58     单行，超长省略号
//     行列表  62 .. 268    行高 30、行距 4；可见 6 行，多行滚动
//     底栏   276 .. 300
#define OV_Q_TEXT_Y (OV_TITLE_H + 6)                        // = 34（摘要行）
#define OV_Q_TEXT_H UI_LINE_H_CJK                           // = 24
#define OV_Q_LIST_Y (OV_Q_TEXT_Y + OV_Q_TEXT_H + 4)         // = 62
// 选择框下方新增一行「操作提示」（11-09 用户要求）：随光标所在行变化
// （选项=单击提交、其他=单击提交/长按录音、下一题/跳过各写各的）。
// 从列表高度里扣掉这一行，避免压到最后一行的行卡。
#define OV_Q_OP_HINT_H 20
#define OV_Q_LIST_H (UI_H - OV_FOOT_H - OV_Q_LIST_Y - OV_Q_OP_HINT_H - 10)  // = 184
#define OV_Q_OP_HINT_Y (OV_Q_LIST_Y + OV_Q_LIST_H + 4)                       // = 250

// 语音输入页（自定义答案 / 空闲 PTT 共用）：标题 + 圆环 + 居中状态/识别文字 + 底栏
// ★ 11-09 用户反馈"圆圈太靠上"：原先圆环 y=24，起点还在标题分隔线（28）之前，
//   看上去压在标题上。现在把 圆环 + 文字 作为一组在内容区（28..216）垂直居中：
//   组高 = 圆环 64 + 缝 12 + 文字 48 = 124 → 起点 28 + 32 = 60
//   → 圆环 60..124、文字 136..184，上下留白对称。
// ★ 11-09 用户反馈"识别框太小"：圆环从 64 缩到 56 并上移，识别框从 72 加高到 108。
//   节奏：标题 28 → 圆环 36..92（8px 缝）→ 识别框 100..208（8px 缝）→ 底栏 216。
#define OV_QV_RING_Y 36
#define OV_QV_RING_D 56
#define OV_QV_TEXT_Y 100
// 识别框（圆角卡）高度：100..208，给底栏（216）留 8px 间隙。
// ★ 与「识别结果卡」的正文框同款（ui_theme_card + 12/10 内边距 + 竖向滚动）。
#define OV_QV_TEXT_H 108
#define OV_Q_ROW_H 30
#define OV_Q_ROW_GAP 4

// 文本在容器内垂直居中（避免写死偏移）
#define UI_CENTER_Y(container_h, line_h) (((container_h) - (line_h)) / 2)

// ── 安全区 → 屏幕坐标 ─────────────────────────────────────────────────────
// 所有界面元素按安全区内的相对坐标排版，再统一加 INSET 偏移。
// 这样调整圆角遮挡只需改 UI_INSET 一个数字，不必重算每个坐标。
#define PX(x) ((x) + UI_INSET)
#define PY(y) ((y) + UI_INSET)

// ── 风格助手 ──────────────────────────────────────────────────────────────

// 硬边矩形（无圆角、无阴影），风格的基础图元。
static inline void ui_theme_rect(lv_obj_t *obj, uint32_t bg)
{
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// 带 1px 边框的面板。
static inline void ui_theme_box(lv_obj_t *obj, uint32_t bg, uint32_t border)
{
    ui_theme_rect(obj, bg);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
}

// ── 圆角助手（v2.0）───────────────────────────────────────────────────────
//
// ★ 为什么单独开助手而不是各处直接调 lv_obj_set_style_radius：
//   圆角控件在 LVGL 里还必须同时确认"内边距为 0"和"不裁剪自身"，
//   漏一个就会出现圆角外露出方角底色（小屏上非常明显）：
//     · pad_all 非 0 → 圆角内侧多一圈背景色，看着像描边没对齐；
//     · 仍需 border_width 明确设 0，否则继承主题默认宽度。
//   把这三件事绑在一个函数里，P3 逐页改造时才不会漏。

// 只给已有对象套圆角（不改底色/描边），用于改造既有的纯色块。
static inline void ui_theme_round(lv_obj_t *obj, int32_t radius)
{
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// 圆角卡片：底色 + 1px 内描边 + 指定圆角。radius 请用 UI_R_* 阶。
static inline void ui_theme_card(lv_obj_t *obj, uint32_t bg, uint32_t stroke, int32_t radius)
{
    ui_theme_round(obj, radius);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(stroke), 0);
}

// 药丸标签（状态徽标、"已配对"这类小胶囊）。
// 半径给 UI_R_PILL，LVGL 会自动收成半圆顶；无描边，由调用方按需再加。
static inline void ui_theme_pill(lv_obj_t *obj, uint32_t bg)
{
    ui_theme_round(obj, UI_R_PILL);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

// 文本标签（点阵/中文字体）。
static inline lv_obj_t *ui_theme_text(lv_obj_t *parent, const char *text,
                                      const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    // 默认给 4px 行间距缓冲，彻底规避回退字体行高不一致导致的行间挤压
    lv_obj_set_style_text_line_space(label, 4, 0);
    return label;
}
