#include "oled.h"
#include <stdio.h>

/* ================================================================
 *  I2C 总线恢复: OLED 掉线时尝试抢救
 *
 *  原理: 如果从机卡住了 SDA 线, 主机连续 toggle SCL 9~15 次
 *        可以让从机完成当前传输、释放 SDA, 恢复正常通信.
 * ================================================================ */
void OLED_Recover(void)
{
    uint8_t i;

    /* 先把两个脚配成推挽输出(软件 GPIO 模式) */
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = OLED_SCL_PIN | OLED_SDA_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(OLED_SCL_PORT, &gpio);

    /* SDA 拉高, 然后在 SCL 上打 15 个脉冲 */
    OLED_SDA_Set();
    for (i = 0; i < 15; i++) {
        OLED_SCL_Set();
        { volatile uint32_t d = 0; while (d++ < 200); }
        OLED_SCL_Clr();
        { volatile uint32_t d = 0; while (d++ < 200); }
    }

    /* 产生一个 STOP 条件 */
    OLED_SDA_Clr();
    { volatile uint32_t d = 0; while (d++ < 200); }
    OLED_SCL_Set();
    { volatile uint32_t d = 0; while (d++ < 200); }
    OLED_SDA_Set();

    /* 等 50ms 让 OLED 内部稳压稳定 */
    { volatile uint32_t w = 0; while (w++ < 50000); }

    /* 重新初始化 OLED (两次, 确保所有寄存器写进去) */
    OLED_Init();
    OLED_Init();
    OLED_Clear();
    OLED_DisplayLabels();
}

static void IIC_Delay(void) {
    uint8_t t = 10;
    while (t--);
}

static void IIC_Start(void) {
    OLED_SDA_Set();
    OLED_SCL_Set();
    IIC_Delay();
    OLED_SDA_Clr();
    IIC_Delay();
    OLED_SCL_Clr();
}

static void IIC_Stop(void) {
    OLED_SDA_Clr();
    OLED_SCL_Set();
    IIC_Delay();
    OLED_SDA_Set();
    IIC_Delay();
}

static void IIC_WriteByte(uint8_t dat) {
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (dat & 0x80) OLED_SDA_Set();
        else            OLED_SDA_Clr();
        dat <<= 1;
        OLED_SCL_Set();
        IIC_Delay();
        OLED_SCL_Clr();
        IIC_Delay();
    }
    // ACK (??)
    OLED_SDA_Set();
    OLED_SCL_Set();
    IIC_Delay();
    OLED_SCL_Clr();
}

static void OLED_WriteCmd(uint8_t cmd) {
    IIC_Start();
    IIC_WriteByte(0x78);
    IIC_WriteByte(0x00);   // 0x00: ??
    IIC_WriteByte(cmd);
    IIC_Stop();
}

static void OLED_WriteData(uint8_t dat) {
    IIC_Start();
    IIC_WriteByte(0x78);
    IIC_WriteByte(0x40);   // 0x40: ??
    IIC_WriteByte(dat);
    IIC_Stop();
}

static void OLED_SetPos(uint8_t x, uint8_t y) {
    OLED_WriteCmd(0xB0 + x);
    OLED_WriteCmd((y  & 0xF0) >> 4 | 0x10);
    OLED_WriteCmd((y & 0x0F) | 0x01);
}

void OLED_Init(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = OLED_SCL_PIN | OLED_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_SetBits(OLED_SCL_PORT, OLED_SCL_PIN | OLED_SDA_PIN);

    OLED_WriteCmd(0xAE);
    OLED_WriteCmd(0xD5); OLED_WriteCmd(0x80);
    OLED_WriteCmd(0xA8); OLED_WriteCmd(0x3F);
    OLED_WriteCmd(0xD3); OLED_WriteCmd(0x00);
    OLED_WriteCmd(0x40);
    OLED_WriteCmd(0x8D); OLED_WriteCmd(0x14);
    OLED_WriteCmd(0x20); OLED_WriteCmd(0x00);
    OLED_WriteCmd(0xA1);
    OLED_WriteCmd(0xC8);
    OLED_WriteCmd(0xDA); OLED_WriteCmd(0x12);
    OLED_WriteCmd(0x81); OLED_WriteCmd(0xEF);
    OLED_WriteCmd(0xD9); OLED_WriteCmd(0xF1);
    OLED_WriteCmd(0xDB); OLED_WriteCmd(0x30);
    OLED_WriteCmd(0xA4);
    OLED_WriteCmd(0xA6);
    OLED_WriteCmd(0xAF);
}

void OLED_Clear(void) {
    uint8_t i, n;
    for (i = 0; i < 8; i++) {
        OLED_WriteCmd(0xB0 + i);
        OLED_WriteCmd(0x00);
        OLED_WriteCmd(0x10);
        for (n = 0; n < 128; n++)
            OLED_WriteData(0);
    }
}

void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr) {
    uint8_t c = chr - ' ';
    OLED_SetPos(x, y);
    for (uint8_t i = 0; i < 8; i++)
        OLED_WriteData(OLED_F8X16[c * 16 + i]);
    OLED_SetPos(x + 1, y);
    for (uint8_t i = 0; i < 8; i++)
        OLED_WriteData(OLED_F8X16[c * 16 + i + 8]);
}

void OLED_ShowString(uint8_t x, uint8_t y, const char *str) {
    uint8_t real_x = (x - 1) * 2;
    uint8_t col = y * 8;
    while (*str) {
        OLED_ShowChar(real_x, col, *str);
        col += 8;
        if (col > 120) { col = 0; real_x += 2; }
        str++;
    }
}

void OLED_ShowChinese(uint8_t x, uint8_t y, const char *p) {
    uint8_t real_x = (x - 1) * 2;
    uint8_t col = y * 16;
    while (*p) {
        for (uint8_t idx = 0; idx < 22; idx++) {
            if (OLED_HZK[idx].Char[0] == *p && OLED_HZK[idx].Char[1] == *(p + 1)) {
                OLED_SetPos(real_x, col);
                for (uint8_t t = 0; t < 16; t++)
                    OLED_WriteData(OLED_HZK[idx].Hex[t]);
                OLED_SetPos(real_x + 1, col);
                for (uint8_t t = 0; t < 16; t++)
                    OLED_WriteData(OLED_HZK[idx].Hex[t + 16]);
                break;
            }
        }
        p += 2;
        col += 16;
    }
}

void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t decimal) {
    char buf[10];
    if (decimal == 1) sprintf(buf, "%.1f", num);
    else              sprintf(buf, "%.2f", num);
    OLED_ShowString(x, y, buf);
}

void OLED_DisplayTemp(float current, float set) {
    OLED_ShowChinese(1, 0, "��ǰ�¶�");
    OLED_ShowString(1, 8, ":");
    if (current > -900.0f)
        OLED_ShowFloat(1, 9, current, 2);  // ?? 2
    else
        OLED_ShowString(1, 9, "Error");

    OLED_ShowChinese(2, 0, "�����¶�");
    OLED_ShowString(2, 8, ":");
    OLED_ShowFloat(2, 9, set, 2);          // ?? 2
}

/**
 * ??????????
 * current: ????
 * set: ????
 * step: ?????
 * pwm: ?? PWM ? (0 ? 999)
 */


// 全英文标签 (避免中文字库问题)
void OLED_DisplayLabels(void) {
    OLED_Clear();
    OLED_ShowString(1, 0, "CUR:");      // Current temperature
    OLED_ShowString(2, 0, "SET:");      // Target temperature
    OLED_ShowString(3, 0, "ERR:");      // Error
    OLED_ShowString(4, 0, "PWM:");      // PWM duty
}

// 更新测量值 (不覆盖标签区域)
void OLED_UpdateValues(float current, float set, float step, uint16_t pwm)
{
    char buf[16];

    // 当前温度 (固定 6 字符宽度, 避免分步清空导致闪烁)
    if (current > -900.0f)
        sprintf(buf, "%6.2f", current);
    else
        sprintf(buf, "Error");
    OLED_ShowString(1, 4, buf);

    // 目标温度
    sprintf(buf, "%6.2f", set);
    OLED_ShowString(2, 4, buf);

    // 误差
    if (current > -900.0f)
        sprintf(buf, "%6.2f", set - current);
    else
        sprintf(buf, "  ---");
    OLED_ShowString(3, 4, buf);

    // PWM
    sprintf(buf, "%d/999", pwm);
    OLED_ShowString(4, 4, buf);
}
