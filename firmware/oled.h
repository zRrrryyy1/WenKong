#ifndef __OLED_H
#define __OLED_H

#include "stm32f10x.h"

// IIC ????(????????)
#define OLED_SCL_PORT   GPIOB
#define OLED_SCL_PIN    GPIO_Pin_6
#define OLED_SDA_PORT   GPIOB
#define OLED_SDA_PIN    GPIO_Pin_7

#define OLED_SCL_Clr()  GPIO_ResetBits(OLED_SCL_PORT, OLED_SCL_PIN)
#define OLED_SCL_Set()  GPIO_SetBits(OLED_SCL_PORT, OLED_SCL_PIN)
#define OLED_SDA_Clr()  GPIO_ResetBits(OLED_SDA_PORT, OLED_SDA_PIN)
#define OLED_SDA_Set()  GPIO_SetBits(OLED_SDA_PORT, OLED_SDA_PIN)
#define OLED_SDA_Read() GPIO_ReadInputDataBit(OLED_SDA_PORT, OLED_SDA_PIN)

// ?????(???????)
struct OLED_CHINESE {
    unsigned char Char[2];
    unsigned char Hex[32];
};

// ??????
extern const unsigned char OLED_F8X16[];
extern const struct OLED_CHINESE OLED_HZK[];

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr);
void OLED_ShowString(uint8_t x, uint8_t y, const char *str);
void OLED_ShowChinese(uint8_t x, uint8_t y, const char *p);
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t decimal);
void OLED_DisplayTemp(float current, float set);
void OLED_UpdateValues(float current, float set, float step, uint16_t pwm);
void OLED_DisplayLabels(void);

/* I2C 总线恢复 + OLED 重初始化（用于 OLED 掉线后抢救） */
void OLED_Recover(void);


#endif
