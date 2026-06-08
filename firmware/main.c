#include "stm32f10x.h"
#include "stm32f10x_tim.h"
#include "ds18b20.h"
#include "oled.h"
#include "pid.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "pid_autotune.h"

// -------------------- 全局变量 --------------------
#define RX_BUF_SIZE 64
char rx_buffer[RX_BUF_SIZE]; // 串口接收缓冲区
uint8_t rx_index = 0;        // 接收缓冲区当前索引
uint8_t rx_has_data = 0;     // 接收完成标志
uint8_t timeout_counter = 0; // 超时计数器（用于命令补齐）
uint16_t heater_duty = 0;    // 加热器占空比（写入 TIM2 CCR2）
uint16_t pwm_value = 0;      // PWM 当前值（记录 heater_duty 输出）

// -------------------- 温度设定(默认27.00°) --------------------
float set_temperature = 27.0f; // 目标设定温度
PID_Controller pid_ctrl;       // PID控制器实例

// -------------------- 自整定 PID --------------------
AutoTuner g_autotuner;                 // 全局自整定器实例
volatile uint8_t flag_temp_update = 0; // 温度更新标志 (500ms)
volatile uint8_t flag_1s = 0;          // 蓝牙发送标志 (1s)

// -------------------- PWM 加热配置 (20kHz) --------------------
#define HEAT_TIM TIM2
#define HEAT_TIM_RCC RCC_APB1Periph_TIM2
#define HEAT_PORT GPIOA
#define HEAT_PIN GPIO_Pin_1
#define PWM_PERIOD 999   // 自动重装载值 (1MHz / 50 = 20kHz)
#define PWM_PRESCALER 71 // 预分频 (72MHz / 72 = 1MHz)
#define DUTY_HEAT 500    // 加热占空比 50%
#define DUTY_STOP 0      // 停止输出
#define DUTY_HOLD 2      // 过温保持占空比（极低，仅防断崖式降温）
#define MAX_SET_TEMP 50.0f // 安全上限：设定温度不得超过 50°C

// -------------------- 按键引脚与步进 --------------------
#define KEY_STEP_PIN GPIO_Pin_5
#define KEY_UP_PIN GPIO_Pin_6
#define KEY_DOWN_PIN GPIO_Pin_7
#define KEY_PORT GPIOA

// -------------------- 步进温度表 --------------------
#define DEBOUNCE_CNT 3
#define STEP_COUNT 3
const float step_table[STEP_COUNT] = {0.5f, 1.0f, 5.0f}; // 步进选项: 0.5°C, 1.0°C, 5.0°C
uint8_t step_index = 0;                                  // 当前步进索引
float temp_step = 0.5f;                                  // 当前步进值

// -------------------- 函数声明 --------------------
void RCC_Config(void);
void GPIO_Config(void);
void USART1_Config(void);
void NVIC_Config(void);
void SysTick_Init(void);
void TIM2_PWM_Init(void);
void USART_SendChar(uint8_t ch);
void USART_SendString(char *str);
void SendToBluetooth(void);
void ProcessReceivedData(void);
void KEY_Init(void);
void KEY_Scan(void);
void HeatingControl(void);

/**
 * @brief  主函数入口
 * @note   系统初始化后进入主循环，500ms更新温度并控制加热，1s发送蓝牙数据
 */
int main(void)
{
    RCC_Config();
    GPIO_Config();
    USART1_Config();
    NVIC_Config();
    SysTick_Init();
    TIM2_PWM_Init();
    DS18B20_Init(); // 初始化DS18B20，默认9位精度
    KEY_Init();     // 按键初始化 (PA5~PA7 上拉输入)
    OLED_Init();    // OLED 初始化 (I2C, SSD1306)

    // 显示固定标签(标题、单位等)
    OLED_DisplayLabels();
    PID_Init(&pid_ctrl, 6.0f, 0.6f, 0.7f);

    USART_SendString(" System Ready (Temp:500ms, BT:1s, PWM 20kHz)\r\n");

    while (1)
    {
        // ---- 温度更新与加热控制(500ms)----
        if (flag_temp_update)
        {
            flag_temp_update = 0;
            DS18B20_Update();
            HeatingControl();

            // 自整定模式 OLED (显示实时过程)
            if (g_autotuner.state != AT_IDLE)
            {
                char line[20];
                // 第 1 行始终显示当前温度 + 设定值
                sprintf(line, "T:%.1f  S:%.1f", latestTemp, set_temperature);
                OLED_ShowString(1, 0, line);

                if (g_autotuner.state == AT_BASE)
                {
                    OLED_ShowString(2, 0, "AT:Baseline");
                    sprintf(line, "Wait stable %d%%", AT_GetProgress(&g_autotuner));
                    OLED_ShowString(3, 0, line);
                }
                else if (g_autotuner.state == AT_TUNE_HIGH)
                {
                    OLED_ShowString(2, 0, "AT:Heat  ^");
                    sprintf(line, "%d/%d  max:%.1fC",
                            g_autotuner.current_halfcycle, AT_MIN_CYCLES,
                            g_autotuner.current_extreme);
                    OLED_ShowString(3, 0, line);
                    sprintf(line, "last:%.1fC  %d%%",
                            g_autotuner.last_crossing_temp,
                            AT_GetProgress(&g_autotuner));
                    OLED_ShowString(4, 0, line);
                }
                else if (g_autotuner.state == AT_TUNE_LOW)
                {
                    OLED_ShowString(2, 0, "AT:Cool  v");
                    sprintf(line, "%d/%d  min:%.1fC",
                            g_autotuner.current_halfcycle, AT_MIN_CYCLES,
                            g_autotuner.current_extreme);
                    OLED_ShowString(3, 0, line);
                    sprintf(line, "last:%.1fC  %d%%",
                            g_autotuner.last_crossing_temp,
                            AT_GetProgress(&g_autotuner));
                    OLED_ShowString(4, 0, line);
                }
                else if (g_autotuner.state == AT_CALC || g_autotuner.state == AT_CONFIRM)
                {
                    OLED_ShowString(2, 0, "AT:Complete");
                    sprintf(line, "Kp:%.1f Ki:%.2f", g_autotuner.Kp, g_autotuner.Ki);
                    OLED_ShowString(3, 0, line);
                    sprintf(line, "Kd:%.2f %d*", g_autotuner.Kd, g_autotuner.confidence);
                    OLED_ShowString(4, 0, line);
                }
                else if (g_autotuner.state == AT_FAIL)
                {
                    OLED_ShowString(2, 0, "AT:Failed");
                    OLED_ShowString(3, 0, "Overshoot/Noise");
                }
            }
            else
            {
                OLED_UpdateValues(latestTemp, set_temperature, temp_step, pwm_value);
            }
        }

        // ---- 蓝牙数据发送(1s)----
        if (flag_1s)
        {
            flag_1s = 0;
            SendToBluetooth();
        }
    }
}

// -------------------- 时钟配置 --------------------
/**
 * @brief  配置各外设时钟
 * @note   使能USART1、GPIOA/B、TIM2时钟
 */
void RCC_Config(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA |
                               RCC_APB2Periph_GPIOB,
                           ENABLE);
    RCC_APB1PeriphClockCmd(HEAT_TIM_RCC, ENABLE); // TIM2时钟
}

// -------------------- GPIO 配置 --------------------
/**
 * @brief  配置GPIO引脚
 * @note   PA9配置为USART1 TX(复用推挽), PA10配置为USART1 RX(浮空输入),
 *         PA1配置为TIM2_CH2 PWM输出(复用推挽)
 */
void GPIO_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    // PA9 (TX) 串口发送引脚
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // PA10 (RX) 串口接收引脚
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // PA1 (TIM2_CH2) PWM输出引脚
    GPIO_InitStructure.GPIO_Pin = HEAT_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(HEAT_PORT, &GPIO_InitStructure);
}

// -------------------- USART1 配置 (9600) --------------------
/**
 * @brief  USART1外设初始化配置
 * @note   波特率9600，8位数据位，1位停止位，无奇偶校验，启用接收中断
 */
void USART1_Config(void)
{
    USART_InitTypeDef USART_InitStructure;

    USART_InitStructure.USART_BaudRate = 9600;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART1, ENABLE);
}

// -------------------- 串口发送 --------------------
/**
 * @brief  发送单个字符
 * @param  ch: 要发送的字符
 */
void USART_SendChar(uint8_t ch)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
        ;
    USART_SendData(USART1, ch);
}

/**
 * @brief  发送字符串
 * @param  str: 要发送的字符串指针
 */
void USART_SendString(char *str)
{
    while (*str)
        USART_SendChar((uint8_t)*str++);
}

// -------------------- 蓝牙发送 --------------------
/**
 * @brief  通过蓝牙发送当前系统状态
 * @note   每秒调用一次，发送温度、设定值、PWM、自整定状态等信息
 */
void SendToBluetooth(void)
{
    char buf[120];
    float err = (latestTemp > -900.0f) ? (set_temperature - latestTemp) : 0;

    // 温度传感器异常
    if (latestTemp <= -900.0f)
    {
        sprintf(buf, "Temp:%.2f    Set:%.2f    Mode:Error\r\n", latestTemp, set_temperature);
        USART_SendString(buf);
        return;
    }

    // ===== 自整定相关状态 (带实时跟踪数据) =====
    if (g_autotuner.state == AT_BASE)
    {
        // 基线采集: 正常 PID 控温, 只报告进度
        sprintf(buf, "Temp:%.2f    Set:%.2f    PWM:%d    Mode:Baseline    Prog:%d%%\r\n",
                latestTemp, set_temperature, pwm_value,
                AT_GetProgress(&g_autotuner));
        USART_SendString(buf);
        return;
    }
    if (g_autotuner.state == AT_TUNE_HIGH)
    {
        // 加热半周期: 显示实时跟踪的最高温度
        sprintf(buf, "Temp:%.2f    Set:%.2f    PWM:%d    Mode:Heat    Dir:up    Half:%d/%d    TrackingMax:%.2f    LastPeak:%.2f\r\n",
                latestTemp, set_temperature, pwm_value,
                g_autotuner.current_halfcycle, AT_MIN_CYCLES,
                g_autotuner.current_extreme,
                g_autotuner.last_crossing_temp);
        USART_SendString(buf);
        return;
    }
    if (g_autotuner.state == AT_TUNE_LOW)
    {
        // 冷却半周期: 显示实时跟踪的最低温度
        sprintf(buf, "Temp:%.2f    Set:%.2f    PWM:%d    Mode:Cool    Dir:down    Half:%d/%d    TrackingMin:%.2f    LastValley:%.2f\r\n",
                latestTemp, set_temperature, pwm_value,
                g_autotuner.current_halfcycle, AT_MIN_CYCLES,
                g_autotuner.current_extreme,
                g_autotuner.last_crossing_temp);
        USART_SendString(buf);
        return;
    }
    if (g_autotuner.state == AT_CALC || g_autotuner.state == AT_CONFIRM)
    {
        // 整定完成: 发送推荐参数 + 峰谷分离结果 + 置信度
        sprintf(buf, "Temp:%.2f    Set:%.2f    PWM:%d    Mode:Result    Kp:%.2f    Ki:%.2f    Kd:%.2f    Conf:%d\r\n",
                latestTemp, set_temperature, pwm_value,
                g_autotuner.Kp, g_autotuner.Ki, g_autotuner.Kd,
                g_autotuner.confidence);
        USART_SendString(buf);
        return;
    }
    if (g_autotuner.state == AT_FAIL)
    {
        sprintf(buf, "Temp:%.2f    Set:%.2f    PWM:%d    Mode:Failed\r\n", latestTemp, set_temperature, pwm_value);
        USART_SendString(buf);
        return;
    }

    // ===== 正常 PID 控温 =====
    sprintf(buf, "Temp:%.2f    Set:%.2f    PWM:%d    Err:%.2f    Mode:PID\r\n",
            latestTemp, set_temperature, pwm_value, err);
    USART_SendString(buf);
}

// -------------------- 数据处理 --------------------
/**
 * @brief  处理接收到的串口命令
 * @note   支持自整定命令(ATUNE/ABORT/APPLY/IGNORE)和数字设定温度
 */
void ProcessReceivedData(void)
{
    if (rx_index == 0)
        return;

    rx_buffer[rx_index] = '\0';

    // ===== 自整定命令 =====
    if (strcmp(rx_buffer, "ATUNE") == 0)
    {
        if (g_autotuner.state == AT_IDLE)
        {
            AT_Start(&g_autotuner, set_temperature);
            USART_SendString("[AT] ATUNE started\r\n");
        }
        else
        {
            USART_SendString("[AT] Already active\r\n");
        }
        rx_index = 0;
        rx_has_data = 0;
        timeout_counter = 0;
        return;
    }
    if (strcmp(rx_buffer, "ABORT") == 0)
    {
        if (g_autotuner.state != AT_IDLE)
        {
            g_autotuner.state = AT_IDLE;
            USART_SendString("[AT] Aborted\r\n");
        }
        rx_index = 0;
        rx_has_data = 0;
        timeout_counter = 0;
        return;
    }
    if (strcmp(rx_buffer, "APPLY") == 0)
    {
        if (g_autotuner.state == AT_CALC || g_autotuner.state == AT_CONFIRM)
        {
            float Kp, Ki, Kd;
            AT_GetResult(&g_autotuner, &Kp, &Ki, &Kd);
            // 更新 PID 参数和增益调度基值
            pid_ctrl.Kp = Kp;
            pid_ctrl.Ki = Ki;
            pid_ctrl.Kd = Kd;
            pid_ctrl.Kp_base = Kp;
            pid_ctrl.Ki_base = Ki;
            pid_ctrl.Kd_base = Kd;
            pid_ctrl.tuning_point = set_temperature;
            PID_AdaptToSetpoint(&pid_ctrl, set_temperature);
            AT_Apply(&g_autotuner);
            USART_SendString("[AT] Applied! New PID active\r\n");
        }
        else
        {
            USART_SendString("[AT] No result to apply\r\n");
        }
        rx_index = 0;
        rx_has_data = 0;
        timeout_counter = 0;
        return;
    }
    if (strcmp(rx_buffer, "IGNORE") == 0)
    {
        AT_Reject(&g_autotuner);
        USART_SendString("[AT] Ignored, previous params kept\r\n");
        rx_index = 0;
        rx_has_data = 0;
        timeout_counter = 0;
        return;
    }
    if (strcmp(rx_buffer, "OLEDRESET") == 0)
    {
        OLED_Recover();
        USART_SendString("[DBG] OLED reinit done\r\n");
        rx_index = 0;
        rx_has_data = 0;
        timeout_counter = 0;
        return;
    }
    if (strcmp(rx_buffer, "STATUS") == 0)
    {
        char status_buf[100];
        const char *at_state = AT_GetStateText(&g_autotuner);
        sprintf(status_buf, "Temp:%.2f  Set:%.2f  PWM:%d  SysTick(cnt500:%d cnt1s:%d)  AT:%s\r\n",
                latestTemp, set_temperature, pwm_value,
                flag_temp_update ? 1 : 0, flag_1s ? 1 : 0, at_state);
        USART_SendString(status_buf);
        rx_index = 0; rx_has_data = 0; timeout_counter = 0;
        return;
    }
    if (strcmp(rx_buffer, "HELP") == 0)
    {
        USART_SendString("Cmds: ATUNE ABORT APPLY IGNORE OLEDRESET STATUS HELP\r\n");
        rx_index = 0; rx_has_data = 0; timeout_counter = 0;
        return;
    }

    // ===== 数字 → 设定温度（兼容旧格式） =====
    uint8_t valid = 1;
    uint8_t dot_count = 0;
    for (uint8_t i = 0; i < rx_index; i++)
    {
        char c = rx_buffer[i];
        if (c == '-')
        {
            if (i != 0)
            {
                valid = 0;
                break;
            }
        }
        else if (c == '.')
        {
            dot_count++;
            if (dot_count > 1)
            {
                valid = 0;
                break;
            }
        }
        else if (c < '0' || c > '9')
        {
            valid = 0;
            break;
        }
    }

    if (valid && dot_count <= 1)
    {
        set_temperature = (float)atof(rx_buffer);
        if (set_temperature > MAX_SET_TEMP) {
            set_temperature = MAX_SET_TEMP;
            USART_SendString(" Clamped to MAX 50.0\r\n");
        } else {
            USART_SendString(" Set ok\r\n");
        }
    }
    else
    {
        USART_SendString(" Invalid\r\n");
    }

    rx_index = 0;
    rx_has_data = 0;
    timeout_counter = 0;
}

// -------------------- USART1 中断处理 --------------------
/**
 * @brief  USART1接收中断处理
 * @note   接收字符存入缓冲区，支持退格、回车换行结束命令
 */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        uint8_t rec = USART_ReceiveData(USART1);

        // 回车或换行 → 结束一条命令
        if (rec == '\r' || rec == '\n')
        {
            if (rx_index > 0)
            {
                rx_has_data = 1; // 标记有完整命令可处理
                timeout_counter = 0;
            }
            return;
        }

        // 退格/删除 → 回退一个字符
        if ((rec == 0x08 || rec == 0x7F) && rx_index > 0)
        {
            rx_index--;
            return;
        }

        // 允许数字、负号、小数点、大写字母
        if ((rec >= '0' && rec <= '9') || rec == '-' || rec == '.' ||
            (rec >= 'A' && rec <= 'Z'))
        {
            if (rx_index < RX_BUF_SIZE - 1)
            {
                rx_buffer[rx_index++] = rec;
            }
            else
            {
                rx_index = 0;
            }
        }
        rx_has_data = 1;
        timeout_counter = 0;
    }
}

// -------------------- 按键初始化 (PA5~7 上拉输入) --------------------
/**
 * @brief  初始化按键GPIO
 * @note   PA5/6/7配置为上拉输入，用于调节步进、设定温度增减
 */
void KEY_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = KEY_STEP_PIN | KEY_UP_PIN | KEY_DOWN_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(KEY_PORT, &GPIO_InitStructure);
}

// -------------------- 按键扫描(SysTick中断,10ms) --------------------
/**
 * @brief  按键扫描与处理
 * @note   支持短按(步进/加减)和长按(STEP键触发自整定)，含消抖处理
 */
void KEY_Scan(void)
{
    static struct
    {
        uint8_t state;
        uint8_t cnt;
        uint8_t short_flag;
        uint16_t long_cnt; // 长按计数器
        uint8_t long_flag; // 长按标志
    } key[3] = {0};

    uint8_t cur[3];
    cur[0] = (GPIO_ReadInputDataBit(KEY_PORT, KEY_STEP_PIN) == Bit_RESET) ? 1 : 0;
    cur[1] = (GPIO_ReadInputDataBit(KEY_PORT, KEY_UP_PIN) == Bit_RESET) ? 1 : 0;
    cur[2] = (GPIO_ReadInputDataBit(KEY_PORT, KEY_DOWN_PIN) == Bit_RESET) ? 1 : 0;

    for (int i = 0; i < 3; i++)
    {
        switch (key[i].state)
        {
        case 0:
            if (cur[i])
            {
                key[i].state = 1;
                key[i].cnt = 1;
            }
            break;
        case 1:
            if (cur[i])
            {
                if (++key[i].cnt >= DEBOUNCE_CNT)
                {
                    key[i].state = 2;
                    key[i].short_flag = 1;
                    key[i].long_cnt = 0;
                }
            }
            else
                key[i].state = 0;
            break;
        case 2:
            if (cur[i])
            {
                // 长按检测：2 秒 = 200 次 * 10ms
                key[i].long_cnt++;
                if (key[i].long_cnt >= 200 && !key[i].long_flag)
                {
                    key[i].long_flag = 1;
                }
            }
            else
            {
                key[i].state = 0;
                key[i].long_cnt = 0;
                key[i].long_flag = 0;
            }
            break;
        }
    }

    if (key[0].short_flag)
    { // 短按STEP: 切换步进值
        key[0].short_flag = 0;
        step_index = (step_index + 1) % STEP_COUNT;
        temp_step = step_table[step_index];
    }
    if (key[0].long_flag)
    { // 长按 STEP → 触发自整定
        key[0].long_flag = 0;
        if (g_autotuner.state == AT_IDLE)
        {
            AT_Start(&g_autotuner, set_temperature);
        }
    }
    if (key[1].short_flag)
    { // 短按UP: 增加设定温度 (上限 50°C)
        key[1].short_flag = 0;
        set_temperature += temp_step;
        if (set_temperature > MAX_SET_TEMP) set_temperature = MAX_SET_TEMP;
    }
    if (key[2].short_flag)
    { // 短按DOWN: 减少设定温度 (下限 0°C)
        key[2].short_flag = 0;
        if (set_temperature - temp_step >= 0.0f)
            set_temperature -= temp_step;
    }
}

// -------------------- TIM2 PWM 初始化 (20kHz, PA1) --------------------
/**
 * @brief  初始化TIM2 PWM输出
 * @note   配置20kHz PWM频率，占空比由 HeatingControl 动态控制
 */
void TIM2_PWM_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef TIM_OCInitStructure;

    TIM_TimeBaseStructure.TIM_Period = PWM_PERIOD;
    TIM_TimeBaseStructure.TIM_Prescaler = PWM_PRESCALER;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(HEAT_TIM, &TIM_TimeBaseStructure);

    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = DUTY_STOP; // 初始占空比0
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC2Init(HEAT_TIM, &TIM_OCInitStructure);

    TIM_OC2PreloadConfig(HEAT_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(HEAT_TIM, ENABLE);
    TIM_Cmd(HEAT_TIM, ENABLE);
}

// -------------------- 加热控制 (PWM) --------------------
/**
 * @brief  加热器PWM控制
 * @note   根据温度与设定值偏差计算PID输出，支持自整定模式和回升抑制
 */
void HeatingControl(void)
{
    static float lowest_temp = 999.0f; // 温度低于设定值期间记录的最低温度
    static uint8_t boost_cut = 0;      // 回升抑制标志（1 = 本轮已介入过）

    if (latestTemp <= -900.0f)
    {
        heater_duty = DUTY_STOP;
        lowest_temp = 999.0f;
        boost_cut = 0;
        return;
    }

    // ===== 自整定模式：AT_Update 返回继电器输出或 AT_USE_PID =====
    if (g_autotuner.state != AT_IDLE)
    {
        uint16_t at_out = AT_Update(&g_autotuner, latestTemp);

        // 自整定完成 → 自动应用结果，无需用户确认
        if (g_autotuner.state == AT_CALC)
        {
            float Kp, Ki, Kd;
            AT_GetResult(&g_autotuner, &Kp, &Ki, &Kd);
            pid_ctrl.Kp = Kp; pid_ctrl.Ki = Ki; pid_ctrl.Kd = Kd;
            pid_ctrl.Kp_base = Kp; pid_ctrl.Ki_base = Ki; pid_ctrl.Kd_base = Kd;
            pid_ctrl.tuning_point = g_autotuner.setpoint;
            PID_AdaptToSetpoint(&pid_ctrl, g_autotuner.setpoint);

            char msg[60];
            sprintf(msg, "[AT] Auto-applied: Kp=%.2f Ki=%.2f Kd=%.2f Conf=%d\r\n",
                    Kp, Ki, Kd, AT_GetConfidence(&g_autotuner));
            USART_SendString(msg);

            AT_Apply(&g_autotuner);
        }

        if (at_out != AT_USE_PID)
        {
            heater_duty = at_out;
            TIM_SetCompare2(HEAT_TIM, heater_duty);
            pwm_value = heater_duty;
            return;
        }
        // AT_USE_PID: 基线采集/计算/确认期间，回退到正常 PID 控制
    }

    pid_ctrl.setpoint = set_temperature;

    // 设定温度变化 → 自动重算 PID 参数（增益调度）
    if (fabsf(set_temperature - pid_ctrl.last_sched_setpoint) > 0.01f)
    {
        PID_AdaptToSetpoint(&pid_ctrl, set_temperature);
    }

    // ===== 规则1：超过设定温度 → PWM 调至极低水平 =====
    if (latestTemp > set_temperature)
    {
        heater_duty = DUTY_HOLD; // 极低占空比保持，而非完全关闭
        pid_ctrl.integral = 0.0f;
        lowest_temp = 999.0f;
        boost_cut = 0;
    }
    // ===== 规则2：低于或等于设定温度 → 正常加热 + 回升抑制 =====
    else
    {
        // PID 计算基准输出
        heater_duty = (uint16_t)PID_Compute(&pid_ctrl, latestTemp);

        // 高温加热补偿: setpoint > 40℃ 时, 对 PID 输出微幅加力
        // comp = 0.2*(T-25), 45℃→+4 PWM, 50℃→+5 PWM, 防止谷底过低
        if (set_temperature > 40.0f) {
            uint16_t heat_boost = (uint16_t)(0.2f * (set_temperature - 25.0f));
            if (heat_boost > 8) heat_boost = 8;  // 上限 8 PWM
            heater_duty += heat_boost;
        }

        // 只要温度低于设定值，就持续更新最低温记录（捕捉底部）
        if (latestTemp < lowest_temp)
        {
            lowest_temp = latestTemp;
        }

        // 检测回升：当前温度比记录的最低点高出阈值以上，且本轮还未介入过
        if (boost_cut == 0 && latestTemp > lowest_temp + pid_ctrl.boost_cut_threshold)
        {
            heater_duty = (uint16_t)(heater_duty * pid_ctrl.boost_cut_factor); // 介入削功率
            PID_ResetIntegral(&pid_ctrl);                                      // 同步重置 PID 内部状态
            boost_cut = 1;                                                     // 标记本轮已介入，直到下次过温归零后重置
        }
    }

    TIM_SetCompare2(HEAT_TIM, heater_duty);
    pwm_value = heater_duty;
}

// -------------------- SysTick 初始化 (10ms) --------------------
/**
 * @brief  初始化SysTick定时器
 * @note   配置10ms中断周期，用于按键扫描、串口超时、标志位定时
 */
void SysTick_Init(void)
{
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8);
    if (SysTick_Config(SystemCoreClock / 8 / 100))
    {
        while (1)
            ;
    }
}

// -------------------- SysTick 中断 (10ms) --------------------
/**
 * @brief  SysTick定时中断处理
 * @note   每10ms扫描按键，每200ms检测串口超时，每500ms/1s设置标志位
 */
void SysTick_Handler(void)
{
    static uint16_t cnt_500ms = 0;
    static uint16_t cnt_1s = 0;

    KEY_Scan();

    // 串口命令超时检测 (200ms)
    if (rx_has_data && rx_index > 0)
    {
        timeout_counter++;
        if (timeout_counter >= 20)
        {
            ProcessReceivedData();
            rx_has_data = 0;
            timeout_counter = 0;
        }
    }

    // 温度更新标志位 (500ms)
    cnt_500ms++;
    if (cnt_500ms >= 50)
    {
        cnt_500ms = 0;
        flag_temp_update = 1;
    }

    // 蓝牙发送标志位 (1s) — SysTick 10ms, 需计数 100 次
    cnt_1s++;
    if (cnt_1s >= 200)  /* 2s = 200 * 10ms */
    {
        cnt_1s = 0;
        flag_1s = 1;
    }
}

// -------------------- NVIC 配置 --------------------
/**
 * @brief  配置NVIC中断优先级
 * @note   配置USART1中断为优先级1，用于串口接收
 */
void NVIC_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/* printf 重定向(串口输出) */
int fputc(int ch, FILE *f)
{
    USART_SendChar((uint8_t)ch);
    return ch;
}
