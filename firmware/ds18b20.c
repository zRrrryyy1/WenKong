#include "ds18b20.h"

float latestTemp = 0.0f;   // ??????

/* 前向声明 */
static uint8_t DS18B20_Reset(void);
static void DS18B20_WriteByte(uint8_t dat);
static uint8_t DS18B20_ReadByte(void);

/* 软件微秒延迟 (72MHz 主频, 实测校准)
   每循环约 5~6 个时钟周期 ≈ 0.075μs, ×14 ≈ 1.05μs/us */
static void delay_us(uint32_t us) {
    volatile uint32_t i;
    for (i = 0; i < us * 14; i++) {
        __NOP();
    }
}

/* 软件毫秒延迟 */
static void delay_ms(uint32_t ms) {
    volatile uint32_t i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 16000; j++)
            __NOP();
}

/* ??DQ????? */
static void DQ_Out(void) {
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin   = DS18B20_DQ_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DS18B20_DQ_PORT, &GPIO_InitStruct);
}

/* ??DQ????? */
static void DQ_In(void) {
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin   = DS18B20_DQ_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(DS18B20_DQ_PORT, &GPIO_InitStruct);
}

/* 初始化引脚, 并设为 12 位分辨率 (0.0625℃ 精度, 750ms 转换) */
void DS18B20_Init(void) {
    RCC_APB2PeriphClockCmd(DS18B20_DQ_RCC, ENABLE);
    DQ_Out();
    DQ_HIGH();

    DS18B20_Reset();
    DS18B20_WriteByte(0xCC);     // Skip ROM
    DS18B20_WriteByte(0x4E);     // Write Scratchpad
    DS18B20_WriteByte(0x00);     // TH (unused)
    DS18B20_WriteByte(0x00);     // TL (unused)
    DS18B20_WriteByte(0x7F);     // Config: 12-bit (R1=1,R0=1 → bits 6-5)
                                 // 0x7F = 0b01111111
    DS18B20_Reset();             // 结束写入
}

/* ????,??0??????? */
static uint8_t DS18B20_Reset(void) {
    uint8_t ack;
    DQ_Out();
    DQ_LOW();
    delay_us(500);
    DQ_HIGH();
    DQ_In();
    delay_us(60);
    ack = DQ_READ();
    delay_us(240);
    DQ_Out();
    DQ_HIGH();
    return ack;
}

/* ?????,???? */
static void DS18B20_WriteByte(uint8_t dat) {
    uint8_t i;
    DQ_Out();
    for (i = 0; i < 8; i++) {
        if (dat & 0x01) {
            DQ_LOW();
            delay_us(2);
            DQ_HIGH();
            delay_us(60);
        } else {
            DQ_LOW();
            delay_us(60);
            DQ_HIGH();
            delay_us(2);
        }
        dat >>= 1;
    }
}

/* ?????,???? */
static uint8_t DS18B20_ReadByte(void) {
    uint8_t i, dat = 0;
    for (i = 0; i < 8; i++) {
        dat >>= 1;
        DQ_Out();
        DQ_LOW();
        delay_us(2);
        DQ_HIGH();
        DQ_In();
        delay_us(5);
        if (DQ_READ()) dat |= 0x80;
        delay_us(60);
    }
    DQ_Out();
    return dat;
}

/* ??????,??latestTemp */
void DS18B20_Update(void) {
    uint8_t tempL, tempH;
    int16_t raw;

    if (DS18B20_Reset() != 0) {
        latestTemp = -999.0f;    // ????
        return;
    }

    __disable_irq();
    DS18B20_WriteByte(0xCC);     // Skip ROM
    DS18B20_WriteByte(0x44);     // Convert T
    __enable_irq();

    delay_ms(800);               // 等待转换 (12bit 750ms, 留余量)

    if (DS18B20_Reset() != 0) {
        latestTemp = -999.0f;
        return;
    }

    __disable_irq();
    DS18B20_WriteByte(0xCC);
    DS18B20_WriteByte(0xBE);     // Read Scratchpad
    tempL = DS18B20_ReadByte();
    tempH = DS18B20_ReadByte();
    __enable_irq();

    raw = (tempH << 8) | tempL;
    latestTemp = raw * 0.0625f;
}



