#ifndef __DS18B20_H
#define __DS18B20_H

#include "stm32f10x.h"

/* ?????????? */
#define DS18B20_DQ_PORT     GPIOA
#define DS18B20_DQ_PIN      GPIO_Pin_0
#define DS18B20_DQ_RCC      RCC_APB2Periph_GPIOA

#define DQ_LOW()    GPIO_ResetBits(DS18B20_DQ_PORT, DS18B20_DQ_PIN)
#define DQ_HIGH()   GPIO_SetBits(DS18B20_DQ_PORT, DS18B20_DQ_PIN)
#define DQ_READ()   GPIO_ReadInputDataBit(DS18B20_DQ_PORT, DS18B20_DQ_PIN)

extern float latestTemp;   // ????,???????(??:°C)

void DS18B20_Init(void);
void DS18B20_Update(void); // ??????? latestTemp

#endif
