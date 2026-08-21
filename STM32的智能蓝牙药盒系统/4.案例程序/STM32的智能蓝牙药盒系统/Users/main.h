#ifndef __MAIN_H
#define __MAIN_H

/**
 * @file main.h
 * @brief 主程序公共头文件。
 */

#include "stm32f1xx_hal.h"

/* ==================== 系统基础组件 ==================== */
#include ".\SYSTEM\delay\delay.h"
#include ".\SYSTEM\sys\sys.h"

/* ==================== 外设驱动 ==================== */
#include ".\Hardware\OLED\OLED.h"
#include ".\Hardware\KEY\KEY.h"
#include ".\Hardware\LU6288\LU6288.h"
#include ".\Hardware\Bluetooth\Bluetooth.h"
#include ".\Hardware\DHT11\DHT11.h"
#include ".\Hardware\LED\LED.h"
#include ".\Hardware\Buzzer\Buzzer.h"
#include ".\Hardware\W25Q64\W25Q64.h"
#include ".\Hardware\ADC_Light_Battery\ADC_Light_Battery.h"
#include ".\Hardware\L9110H_Motor\L9110H_Motor.h"
#include ".\Hardware\DS1302_RTC\DS1302_RTC.h"
#include ".\Hardware\HX711_WeighingModule\HX711_WeighingModule.h"
#include ".\Hardware\Servo\Servo.h"

/* ==================== 应用层模块 ==================== */
#include ".\APPLICATION\Menu\Menu.h"

#endif /* __MAIN_H */
