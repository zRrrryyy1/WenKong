/**
 * @file    pid_autotune.h
 * @brief   继电器反馈自整定（Astrom-Hagglund Relay Auto-Tuning）
 *
 * 原理: 用继电器（Bang-Bang）控制产生等幅振荡极限环,
 *       测量临界增益 Ku 和临界周期 Tu, 再用保守 ZN 公式
 *       计算 PID 参数, 附带置信度评级供用户参考.
 *
 * 振荡非对称性: 加热靠 PWM、降温靠自然散热, 因此波峰幅值
 *              通常大于波谷幅值, 代码将二者分离存储和评估.
 *
 * 安全措施: 过冲终止、30min 超时、幅值异常检测、参数限幅.
 */

#ifndef __PID_AUTOTUNE_H
#define __PID_AUTOTUNE_H

#include "stm32f10x.h"
#include "pid.h"
#include <math.h>

/* ========== 可调参数 ========== */

/* 继电器输出 */
#define AT_USE_PID           0xFFFF  /* 标记: 由外部 PID 控制, 本函数不接管 */
#define AT_OUTPUT_HIGH       80      /* 继电器加热 PWM (8%, 无主动散热不宜太高) */
#define AT_OUTPUT_LOW        0       /* 继电器保持 PWM (0% = 自然冷却) */

/* 继电器切换 */
#define AT_HYSTERESIS        0.2f    /* 回滞 (℃), 防止继电器抖动 */
#define AT_ABORT_OVERSHOOT   6.0f    /* 过冲超过此值立即中止整定 (℃) */

/* 半周期参数 */
#define AT_MIN_CYCLES        10      /* 需要采集的半周期数 (即 5 完整周期) */
#define AT_SKIP_CYCLES       2       /* 跳过前 N 个半周期 (瞬态数据不可靠) */
#define AT_MAX_PEAKS         6       /* 峰/谷数组容量 = AT_MIN_CYCLES/2 + 1 */

/* 超时 */
#define AT_TIMEOUT_CYCLES    3600    /* 30min (500ms * 3600) */
#define AT_CONFIRM_TIMEOUT   120     /* 60s 确认超时 (120 * 500ms) */
#define AT_BASELINE_CYCLES   20      /* 基线采集周期 (20 * 500ms = 10s) */

/* ========== 置信度评估阈值 ========== */

/* 幅值变异系数 CV = stddev / mean */
#define AT_CV_AMP_EXCELLENT  0.10f   /* CV < 10% → ★★★★★ */
#define AT_CV_AMP_GOOD       0.20f   /* CV < 20% → ★★★★☆ */
#define AT_CV_AMP_FAIR       0.30f   /* CV < 30% → ★★★☆☆ */
/* > 30% → ★★☆☆☆ (勉强可用) */

/* 周期变异系数 (比幅值更苛刻, 因为半周期少) */
#define AT_CV_PERIOD_GOOD    0.15f
#define AT_CV_PERIOD_FAIR    0.25f

/* 非对称容差: 波峰/波谷比值超出此范围扣一星 */
#define AT_SYM_LIMIT_LO      0.4f    /* 降温幅值不到升温幅值 40% */
#define AT_SYM_LIMIT_HI      2.5f    /* 升温幅值超过降温幅值 2.5 倍 */

/* ========== 状态机 ========== */
typedef enum {
    AT_IDLE = 0,
    AT_BASE,         /* 采集当前控温误差基线 */
    AT_TUNE_HIGH,    /* 继电器输出高（升温） */
    AT_TUNE_LOW,     /* 继电器输出低（降温/自然冷却） */
    AT_CALC,         /* 计算完成, 等待确认 */
    AT_CONFIRM,      /* 用户已看到结果, 等待 APPLY/IGNORE */
    AT_FAIL          /* 整定失败 */
} AT_State;

/* ========== 自整定器控制块 ========== */
typedef struct {
    AT_State state;
    float    setpoint;              /* 本次整定的目标温度 */

    /* ---- 基线采集 ---- */
    float    baseline_sum;          /* 误差绝对值累加 */
    float    baseline_max;          /* 最大单次误差 */
    uint16_t baseline_count;        /* 已采集样本数 */
    uint8_t  baseline_extend;       /* 基线延长次数 (等待升温用) */

    /* ---- 继电器控制 / 峰谷分离 ---- */
    float    current_extreme;       /* 当前半周期正在跟踪的极值 (℃) */
    float    peak_devs[AT_MAX_PEAKS];   /* 波峰: 高于设定值的幅值 (正) */
    float    valley_devs[AT_MAX_PEAKS]; /* 波谷: 低于设定值的幅值 (正) */
    uint32_t cross_cycles[AT_MIN_CYCLES]; /* 每次过零时的 cycle_counter */
    uint8_t  peak_count;            /* 已记录的波峰数 */
    uint8_t  valley_count;          /* 已记录的波谷数 */

    /* ---- 定时 ---- */
    uint32_t cycle_counter;         /* 500ms 控制周期计数 */

    /* ---- 测量结果 (计算阶段填入) ---- */
    float    peak_amplitude;        /* 平均波峰幅值 (℃) */
    float    valley_amplitude;      /* 平均波谷幅值 (℃) */
    float    amplitude;             /* 综合幅值 = (peak+valley)/2 */
    float    asymmetry_ratio;       /* 非对称比 = peak/valley */
    float    Tu;                    /* 临界周期 (秒) */
    float    Ku;                    /* 临界增益 */

    /* ---- PID 推荐参数 ---- */
    float    Kp, Ki, Kd;
    uint8_t  confidence;            /* 1-5 星 */

    /* ---- 实时显示数据 ---- */
    uint8_t  current_halfcycle;     /* 当前是第几个半周期 (1-based) */
    float    last_crossing_temp;    /* 最近一次过零时的极值温度 (刷 OLED/蓝牙用) */
} AutoTuner;

/* ========== API ========== */

void     AT_Start(AutoTuner *at, float setpoint);
uint16_t AT_Update(AutoTuner *at, float current_temp);

/* 状态查询 */
uint8_t  AT_IsActive(AutoTuner *at);
uint8_t  AT_IsComplete(AutoTuner *at);
uint8_t  AT_IsFailed(AutoTuner *at);
uint8_t  AT_IsConfirming(AutoTuner *at);
uint8_t  AT_GetProgress(AutoTuner *at);       /* 0-100 */
uint8_t  AT_GetConfidence(AutoTuner *at);     /* 1-5 */
const char *AT_GetStateText(AutoTuner *at);

/* 获取结果 */
void     AT_GetResult(AutoTuner *at, float *Kp, float *Ki, float *Kd);

/* 用户确认/拒绝 */
void     AT_Apply(AutoTuner *at);
void     AT_Reject(AutoTuner *at);

#endif /* __PID_AUTOTUNE_H */
