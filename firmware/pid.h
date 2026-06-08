#ifndef __PID_H
#define __PID_H

typedef struct {
    // ---------- 活动参数（增益调度或自整定更新） ----------
    float Kp, Ki, Kd;
    float setpoint;
    float integral;
    float prev_input;             // 上次测量温度（微分先行用）
    float output_min, output_max;
    float last_output;            // 上次输出（增量限幅用）

    // ---------- 可调阈值（原硬编码值） ----------
    float integral_sep_threshold; // 积分分离阈值（℃），原 0.6
    float delta_max;              // 输出增量限幅（/周期），原 50
    float boost_cut_factor;       // 回升抑制削功率系数，原 0.6
    float boost_cut_threshold;    // 回升抑制触发阈值（℃），原 0.1

    // ---------- 增益调度基准（35℃ 整定点） ----------
    float Kp_base, Ki_base, Kd_base;  // 基准增益
    float tuning_point;               // 基准温度（℃），默认 35.0
    float Kp_slope, Ki_slope, Kd_slope; // 增益随温度的变化率
    float sep_slope, delta_slope;     // 阈值随温度的变化率
    float last_sched_setpoint;        // 上次调度的设定温度
} PID_Controller;

void PID_Init(PID_Controller *pid, float kp, float ki, float kd);
float PID_Compute(PID_Controller *pid, float current_temp);
void PID_ResetIntegral(PID_Controller *pid);

/* 增益调度：根据设定温度重算 Kp/Ki/Kd 及阈值 */
void PID_AdaptToSetpoint(PID_Controller *pid, float setpoint);

#endif
