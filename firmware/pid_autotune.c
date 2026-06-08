/**
 * @file    pid_autotune.c
 * @brief   继电器自整定核心实现
 *
 * 每 500ms 被 HeatingControl 调用一次.
 * 返回 AT_OUTPUT_HIGH/LOW 接管 PWM, 或 AT_USE_PID 回退给 PID.
 */

#include "pid_autotune.h"

/* ================================================================
 *  静态辅助: 置信度评级
 *
 *  考察四个维度:
 *    1) 波峰幅值一致性  (CV of peak deviations)
 *    2) 波谷幅值一致性  (CV of valley deviations)
 *    3) 周期一致性      (CV of full-period times)
 *    4) 峰/谷对称性     (peak/valley ratio)
 *
 *  取最差维度作为最终星级, 因为薄弱环节决定整体可信度.
 * ================================================================ */
static uint8_t calc_confidence(AutoTuner *at)
{
    uint8_t skip = AT_SKIP_CYCLES / 2;  /* 跳过的完整周期数 */
    uint8_t i, n;
    float sum, mean, var;

    /* ---- 维度 1: 波峰幅值一致性 ---- */
    float peak_cv = 0;
    n = at->peak_count - skip;
    if (n >= 2) {
        sum = 0;
        for (i = skip; i < at->peak_count; i++) sum += at->peak_devs[i];
        mean = sum / n;
        if (mean > 0.05f) {
            var = 0;
            for (i = skip; i < at->peak_count; i++) {
                float d = at->peak_devs[i] - mean;
                var += d * d;
            }
            peak_cv = (float)sqrt(var / n) / mean;
        }
    }

    /* ---- 维度 2: 波谷幅值一致性 ---- */
    float valley_cv = 0;
    n = at->valley_count - skip;
    if (n >= 2) {
        sum = 0;
        for (i = skip; i < at->valley_count; i++) sum += at->valley_devs[i];
        mean = sum / n;
        if (mean > 0.05f) {
            var = 0;
            for (i = skip; i < at->valley_count; i++) {
                float d = at->valley_devs[i] - mean;
                var += d * d;
            }
            valley_cv = (float)sqrt(var / n) / mean;
        }
    }

    /* 合并峰谷幅值 CV (取较大者) */
    float amp_cv = (peak_cv > valley_cv) ? peak_cv : valley_cv;

    /* ---- 维度 3: 周期一致性 ---- */
    float period_cv = 1.0f;  /* 默认很差 */
    uint8_t total_crossings = at->peak_count + at->valley_count;
    n = 0;
    sum = 0;
    /* 同向过零间隔 = 完整周期, 用 i+2 步进 */
    for (i = AT_SKIP_CYCLES + 2; i < total_crossings; i++) {
        /* 跳过间距太小的 (噪声触发) */
        if (at->cross_cycles[i] > at->cross_cycles[i - 2] + 2) {
            float p = (float)(at->cross_cycles[i] - at->cross_cycles[i - 2]);
            sum += p;
            n++;
        }
    }
    if (n >= 2) {
        mean = sum / n;
        var = 0;
        uint8_t m = 0;
        for (i = AT_SKIP_CYCLES + 2; i < total_crossings; i++) {
            if (at->cross_cycles[i] > at->cross_cycles[i - 2] + 2) {
                float d = (float)(at->cross_cycles[i] - at->cross_cycles[i - 2]) - mean;
                var += d * d;
                m++;
            }
        }
        period_cv = (float)sqrt(var / m) / (mean + 1.0f);
    }

    /* ---- 维度 4: 非对称性 ---- */
    float sym_penalty = 0;
    if (at->peak_amplitude > 0.05f && at->valley_amplitude > 0.05f) {
        float ratio = at->peak_amplitude / at->valley_amplitude;
        if (ratio < AT_SYM_LIMIT_LO || ratio > AT_SYM_LIMIT_HI)
            sym_penalty = 1;  /* 扣一星 */
    }

    /* ---- 综合评级: 取最差维度 ---- */
    uint8_t amp_stars, period_stars;

    if      (amp_cv < AT_CV_AMP_EXCELLENT) amp_stars = 5;
    else if (amp_cv < AT_CV_AMP_GOOD)      amp_stars = 4;
    else if (amp_cv < AT_CV_AMP_FAIR)      amp_stars = 3;
    else                                   amp_stars = 2;

    if      (period_cv < AT_CV_PERIOD_GOOD) period_stars = 4;  /* 周期样本少, 上限 4 */
    else if (period_cv < AT_CV_PERIOD_FAIR) period_stars = 3;
    else                                    period_stars = 2;

    uint8_t base = (amp_stars < period_stars) ? amp_stars : period_stars;
    if (sym_penalty > 0 && base > 1) base--;

    return base;
}

/* ================================================================
 *  AT_Start: 初始化整定器
 * ================================================================ */
void AT_Start(AutoTuner *at, float setpoint)
{
    at->state    = AT_BASE;
    at->setpoint = setpoint;

    /* 基线 */
    at->baseline_sum   = 0;
    at->baseline_max   = 0;
    at->baseline_count = 0;
    at->baseline_extend = 0;

    /* 峰谷 */
    at->current_extreme   = setpoint;
    at->peak_count  = 0;
    at->valley_count = 0;
    at->cycle_counter = 0;
    at->current_halfcycle = 0;
    at->last_crossing_temp = setpoint;

    /* 结果 */
    at->peak_amplitude   = 0;
    at->valley_amplitude = 0;
    at->amplitude  = 0;
    at->asymmetry_ratio = 1.0f;
    at->Tu = 0;
    at->Ku = 0;
    at->Kp = 0;  at->Ki = 0;  at->Kd = 0;
    at->confidence = 0;
}

/* ================================================================
 *  AT_Update: 主状态机, 每个 500ms 控制周期调用一次
 *
 *  返回值: AT_USE_PID     → 使用正常 PID 控温
 *          AT_OUTPUT_HIGH → 继电器强制加热
 *          AT_OUTPUT_LOW  → 继电器强制冷却(关加热)
 * ================================================================ */
uint16_t AT_Update(AutoTuner *at, float current_temp)
{
    /* ==================== 安全过滤 ==================== */
    /* 传感器故障 → 什么都不做 */
    if (current_temp <= -900.0f)
        return AT_USE_PID;

    /* ==================== 超时 ==================== */
    at->cycle_counter++;
    if (at->cycle_counter > AT_TIMEOUT_CYCLES) {
        at->state = AT_FAIL;
        return AT_USE_PID;
    }

    /* ==================== 状态: 基线采集 ==================== */
    if (at->state == AT_BASE) {
        float err = (float)fabs(current_temp - at->setpoint);
        at->baseline_sum += err;
        if (err > at->baseline_max) at->baseline_max = err;
        at->baseline_count++;

        if (at->baseline_count >= AT_BASELINE_CYCLES) {
            at->cycle_counter  = 0;
            at->peak_count     = 0;
            at->valley_count   = 0;
            at->current_halfcycle = 1;

            /* --- 根据当前温度距设定值的远近, 决定进入策略 --- */

            if (current_temp > at->setpoint + 1.0f) {
                /* 温度远高于设定值 → 直接进入自然降温模式 */
                at->state = AT_TUNE_LOW;
                at->current_extreme = current_temp;
            }
            else if (current_temp < at->setpoint - 1.0f) {
                /* 温度远低于设定值 → 延长基线等 PID 升温, 最多 ~15s */
                if (at->baseline_extend < 10) {
                    at->baseline_extend++;
                    at->baseline_count = AT_BASELINE_CYCLES - 3;
                    return AT_USE_PID;
                }
                /* 等够了 → 强制以低功率开始加热振荡 */
                at->state = AT_TUNE_HIGH;
                at->current_extreme = current_temp;
            }
            else {
                /* 温度已在设定值 ±1℃ 内 → 按当前方向决定起振极 */
                at->current_extreme = current_temp;
                at->state = (current_temp <= at->setpoint)
                            ? AT_TUNE_HIGH
                            : AT_TUNE_LOW;
            }
        }
        return AT_USE_PID;  /* 基线期间仍用 PID 控温 */
    }

    /* ================================================================
     *  状态: 继电器加热 (输出 PWM = AT_OUTPUT_HIGH, 即 80)
     *
     *  持续跟踪最高温度, 当温度上升到 ≥ setpoint + hysteresis 时:
     *    → 记录波峰, 切换到降温模式
     * ================================================================ */
    if (at->state == AT_TUNE_HIGH) {
        /* 跟踪本半周期内的最高温度 */
        if (current_temp > at->current_extreme)
            at->current_extreme = current_temp;

        /* 过冲安全中止 */
        if (current_temp > at->setpoint + AT_ABORT_OVERSHOOT) {
            at->state = AT_FAIL;
            return AT_USE_PID;
        }

        /* 检测过零: 上升到设定值+回滞 */
        if (current_temp >= at->setpoint + AT_HYSTERESIS) {
            /* 记录波峰 (正值, 偏离设定值的量) */
            if (at->peak_count < AT_MAX_PEAKS) {
                at->peak_devs[at->peak_count] = at->current_extreme - at->setpoint;
                at->cross_cycles[at->peak_count + at->valley_count] = at->cycle_counter;
                at->last_crossing_temp = at->current_extreme;
                at->peak_count++;
                at->current_halfcycle = at->peak_count + at->valley_count;
            }

            /* 切换到降温, 开始跟踪最低温度 */
            at->current_extreme = current_temp;
            at->state = AT_TUNE_LOW;

            /* 采集够了? */
            if (at->peak_count + at->valley_count >= AT_MIN_CYCLES)
                goto calculate;

            return AT_OUTPUT_LOW;  /* 立即切到低输出, 不等下一周期 */
        }
        return AT_OUTPUT_HIGH;
    }

    /* ================================================================
     *  状态: 继电器冷却 (输出 PWM = AT_OUTPUT_LOW, 即 0)
     *
     *  持续跟踪最低温度, 当温度下降到 ≤ setpoint - hysteresis 时:
     *    → 记录波谷, 切换到加热模式
     * ================================================================ */
    if (at->state == AT_TUNE_LOW) {
        /* 跟踪本半周期内的最低温度 */
        if (current_temp < at->current_extreme)
            at->current_extreme = current_temp;

        /* 检测过零: 下降到设定值-回滞 */
        if (current_temp <= at->setpoint - AT_HYSTERESIS) {
            /* 记录波谷 (正值, 偏离设定值的量) */
            if (at->valley_count < AT_MAX_PEAKS) {
                at->valley_devs[at->valley_count] = at->setpoint - at->current_extreme;
                at->cross_cycles[at->peak_count + at->valley_count] = at->cycle_counter;
                at->last_crossing_temp = at->current_extreme;
                at->valley_count++;
                at->current_halfcycle = at->peak_count + at->valley_count;
            }

            /* 切换到加热, 开始跟踪最高温度 */
            at->current_extreme = current_temp;
            at->state = AT_TUNE_HIGH;

            /* 采集够了? */
            if (at->peak_count + at->valley_count >= AT_MIN_CYCLES)
                goto calculate;

            return AT_OUTPUT_HIGH;  /* 立即切到高输出 */
        }
        return AT_OUTPUT_LOW;
    }

    /* ================================================================
     *  状态: 等待用户确认 (有超时自动放弃)
     * ================================================================ */
    if (at->state == AT_CALC || at->state == AT_CONFIRM) {
        at->cycle_counter++;
        if (at->cycle_counter > AT_CONFIRM_TIMEOUT) {
            at->state = AT_IDLE;   /* 60s 无人理会 → 静默放弃 */
        }
        return AT_USE_PID;         /* 确认期间用 PID 控温 */
    }

    /* 空闲/失败态 */
    return AT_USE_PID;

    /* ================================================================
     *  计算阶段: 根据采集到的峰/谷数据计算 PID 参数
     * ================================================================ */
calculate:
    {
        uint8_t skip_p = AT_SKIP_CYCLES / 2;  /* 跳过的峰数 */
        uint8_t skip_v = AT_SKIP_CYCLES / 2;  /* 跳过的谷数 */
        uint8_t i, n;
        float sum;

        /* ----- 1) 计算平均波峰幅值 ----- */
        n = at->peak_count - skip_p;
        if (n < 2) { at->state = AT_FAIL; return AT_USE_PID; }
        sum = 0;
        for (i = skip_p; i < at->peak_count; i++) sum += at->peak_devs[i];
        at->peak_amplitude = sum / n;

        /* ----- 2) 计算平均波谷幅值 ----- */
        n = at->valley_count - skip_v;
        if (n < 2) { at->state = AT_FAIL; return AT_USE_PID; }
        sum = 0;
        for (i = skip_v; i < at->valley_count; i++) sum += at->valley_devs[i];
        at->valley_amplitude = sum / n;

        /* ----- 3) 综合幅值 & 非对称比 ----- */
        at->amplitude = (at->peak_amplitude + at->valley_amplitude) / 2.0f;
        if (at->valley_amplitude > 0.05f)
            at->asymmetry_ratio = at->peak_amplitude / at->valley_amplitude;
        else
            at->asymmetry_ratio = 99.0f;

        /* 幅值异常检查 */
        if (at->amplitude < 0.15f || at->amplitude > 15.0f) {
            at->state = AT_FAIL;
            return AT_USE_PID;
        }

        /* ----- 4) 计算平均完整周期 (同向过零间隔) ----- */
        uint8_t total = at->peak_count + at->valley_count;
        float period_sum = 0;
        uint8_t period_n = 0;
        for (i = AT_SKIP_CYCLES + 2; i < total; i++) {
            if (at->cross_cycles[i] > at->cross_cycles[i - 2] + 2) {
                period_sum += (float)(at->cross_cycles[i] - at->cross_cycles[i - 2]);
                period_n++;
            }
        }
        if (period_n < 2) { at->state = AT_FAIL; return AT_USE_PID; }

        float tu_cycles = period_sum / period_n;
        at->Tu = tu_cycles * 0.5f;  /* 每控制周期 500ms */

        /* ----- 5) 临界增益 Ku = 4·h / (π·a) ----- */
        /* h = 继电器输出的"等效幅度", 即高低差的一半 (PWM 单位) */
        float h = (float)(AT_OUTPUT_HIGH - AT_OUTPUT_LOW) / 2.0f;
        at->Ku = 4.0f * h / (3.14159f * at->amplitude);

        /* ----- 6) 保守 Ziegler-Nichols ----- */
        at->Kp = 0.35f * at->Ku;
        at->Ki = 1.5f  * at->Kp / at->Tu;
        at->Kd = at->Kp * at->Tu / 12.0f;

        /* 限幅保护 */
        if (at->Kp < 0.5f)  at->Kp = 0.5f;
        if (at->Kp > 20.0f) at->Kp = 20.0f;
        if (at->Ki < 0.05f) at->Ki = 0.05f;
        if (at->Ki > 5.0f)  at->Ki = 5.0f;
        if (at->Kd < 0.05f) at->Kd = 0.05f;
        if (at->Kd > 10.0f) at->Kd = 10.0f;

        /* ----- 7) 置信度评级 ----- */
        at->confidence = calc_confidence(at);

        at->cycle_counter = 0;
        at->state = AT_CALC;
        return AT_USE_PID;
    }
}

/* ================================================================
 *  查询接口
 * ================================================================ */

uint8_t AT_IsActive(AutoTuner *at)
{
    return (at->state != AT_IDLE && at->state != AT_CALC &&
            at->state != AT_CONFIRM && at->state != AT_FAIL);
}

uint8_t AT_IsComplete(AutoTuner *at)
{
    return (at->state == AT_CALC || at->state == AT_CONFIRM);
}

uint8_t AT_IsFailed(AutoTuner *at)
{
    return (at->state == AT_FAIL);
}

uint8_t AT_IsConfirming(AutoTuner *at)
{
    return (at->state == AT_CONFIRM);
}

uint8_t AT_GetProgress(AutoTuner *at)
{
    if (at->state == AT_IDLE)  return 0;
    if (at->state == AT_BASE)  return (uint8_t)((uint32_t)at->baseline_count * 100 / AT_BASELINE_CYCLES);
    if (at->state == AT_TUNE_HIGH || at->state == AT_TUNE_LOW) {
        uint8_t total = at->peak_count + at->valley_count;
        return (uint8_t)((uint32_t)total * 100 / AT_MIN_CYCLES);
    }
    if (at->state == AT_CALC || at->state == AT_CONFIRM) return 100;
    return 0;
}

void AT_GetResult(AutoTuner *at, float *Kp, float *Ki, float *Kd)
{
    *Kp = at->Kp;  *Ki = at->Ki;  *Kd = at->Kd;
}

void AT_Apply(AutoTuner *at)  { at->state = AT_IDLE; }
void AT_Reject(AutoTuner *at) { at->state = AT_IDLE; }

const char *AT_GetStateText(AutoTuner *at)
{
    switch (at->state) {
        case AT_IDLE:      return "IDLE";
        case AT_BASE:      return "Base";
        case AT_TUNE_HIGH: return "Heat";
        case AT_TUNE_LOW:  return "Cool";
        case AT_CALC:      return "Done";
        case AT_CONFIRM:   return "Confirm";
        case AT_FAIL:      return "Failed";
        default:           return "?";
    }
}

uint8_t AT_GetConfidence(AutoTuner *at)
{
    return at->confidence;
}
