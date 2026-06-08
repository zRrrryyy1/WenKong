#include "pid.h"
#include <math.h>

void PID_Init(PID_Controller *pid, float kp, float ki, float kd)
{
    // 活动增益
    pid->Kp = kp;
    pid->Ki = ki;
    pid->Kd = kd;
    pid->setpoint = 27.0f;
    pid->integral = 0.0f;
    pid->prev_input = 0.0f;
    pid->output_min = 0.0f;
    pid->output_max = 999.0f;
    pid->last_output = 0.0f;

    // 可调阈值（默认值 = 原硬编码值）
    pid->integral_sep_threshold = 0.6f;
    pid->delta_max = 50.0f;
    pid->boost_cut_factor = 0.6f;
    pid->boost_cut_threshold = 0.1f;

    // 增益调度基准（35℃ 整定点）
    pid->Kp_base = kp;
    pid->Ki_base = ki;
    pid->Kd_base = kd;
    pid->tuning_point = 35.0f;
    pid->Kp_slope = 0.05f;      // Kp 变化率 /℃（原 0.04→0.05 增强高温回温）
    pid->Ki_slope = 0.03f;      // Ki 变化率 /℃
    pid->Kd_slope = 0.05f;      // Kd 变化率 /℃
    pid->sep_slope  = 0.02f;    // 积分分离阈值变化率 /℃
    pid->delta_slope = 0.0f;    // 增量限幅暂不随温度变化
    pid->last_sched_setpoint = -999.0f;
}

float PID_Compute(PID_Controller *pid, float current_temp)
{
    float error = pid->setpoint - current_temp;
    float P_out, I_out, D_out, output;

    // 1. 比例项
    P_out = pid->Kp * error;

    // 2. 积分分离：误差绝对值在阈值内才累加积分
    if (fabs(error) < pid->integral_sep_threshold) {
        pid->integral += error;
    } else {
        pid->integral = 0.0f;
    }

    // 积分限幅（输出范围的 20%）
    float integral_max = pid->output_max * 0.2f;
    float integral_min = pid->output_min * 0.2f;
    if (pid->integral > integral_max) pid->integral = integral_max;
    if (pid->integral < integral_min) pid->integral = integral_min;
    I_out = pid->Ki * pid->integral;

    // 3. 微分先行（对测量值微分，避免微分突变）
    D_out = pid->Kd * (current_temp - pid->prev_input);
    pid->prev_input = current_temp;

    // 4. 总输出
    output = P_out + I_out - D_out;

    // 5. 输出增量限幅（抑制突变）
    float delta = output - pid->last_output;
    if (delta > pid->delta_max) {
        output = pid->last_output + pid->delta_max;
    } else if (delta < -pid->delta_max) {
        output = pid->last_output - pid->delta_max;
    }
    pid->last_output = output;

    // 6. 输出总限幅
    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;

    return output;
}

void PID_ResetIntegral(PID_Controller *pid)
{
    pid->integral = 0.0f;
    pid->prev_input = 0.0f;
}

/* 增益调度：以 tuning_point 为基准，根据 setpoint 线性缩放 */
void PID_AdaptToSetpoint(PID_Controller *pid, float setpoint)
{
    float delta_t = setpoint - pid->tuning_point;

    // 线性缩放三个增益
    pid->Kp = pid->Kp_base * (1.0f + pid->Kp_slope * delta_t);
    pid->Ki = pid->Ki_base * (1.0f + pid->Ki_slope * delta_t);
    pid->Kd = pid->Kd_base * (1.0f + pid->Kd_slope * delta_t);

    // 限幅保护：防止增益为负或过大
    if (pid->Kp < 0.5f) pid->Kp = 0.5f;
    if (pid->Ki < 0.05f) pid->Ki = 0.05f;
    if (pid->Kd < 0.05f) pid->Kd = 0.05f;
    if (pid->Kp > 15.0f) pid->Kp = 15.0f;
    if (pid->Ki > 3.0f)  pid->Ki = 3.0f;
    if (pid->Kd > 5.0f)  pid->Kd = 5.0f;

    // 积分分离阈值随温度变化
    pid->integral_sep_threshold = 0.6f * (1.0f + pid->sep_slope * delta_t);
    if (pid->integral_sep_threshold < 0.2f) pid->integral_sep_threshold = 0.2f;
    if (pid->integral_sep_threshold > 1.5f) pid->integral_sep_threshold = 1.5f;

    // 增量限幅：低温收窄防超调，高温放宽助回温
    pid->delta_max = 50.0f;
    if (setpoint < 25.0f)       pid->delta_max = 30.0f;
    else if (setpoint <= 40.0f) pid->delta_max = 50.0f;
    else if (setpoint <= 45.0f) pid->delta_max = 80.0f;
    else                        pid->delta_max = 120.0f;

    // 回升抑制：高温区削功率更温和、触发更迟钝，让回温更有力
    if (setpoint > 40.0f) {
        pid->boost_cut_factor    = 0.6f + (setpoint - 40.0f) * 0.02f; // 40℃→0.6, 45℃→0.7, 50℃→0.8
        pid->boost_cut_threshold = 0.1f + (setpoint - 40.0f) * 0.01f; // 40℃→0.1, 45℃→0.15, 50℃→0.2
    } else {
        pid->boost_cut_factor    = 0.6f;
        pid->boost_cut_threshold = 0.1f;
    }

    pid->last_sched_setpoint = setpoint;
}
