#ifndef __LCD_INIT_H
#define __LCD_INIT_H

#include "sys.h"

#define USE_HORIZONTAL 1  //设置横屏或者竖屏显示 0或1为竖屏 2或3为横屏


#if USE_HORIZONTAL==0||USE_HORIZONTAL==1
#define LCD_W 128
#define LCD_H 160

#else
#define LCD_W 160
#define LCD_H 128
#endif



//-----------------LCD引脚定义---------------- 
// 修改引脚时只需修改以下端口/引脚/时钟宏
#define LCD_SCLK_GPIO_PORT GPIOB
#define LCD_SCLK_GPIO_PIN  GPIO_Pin_13
#define LCD_SCLK_GPIO_CLK  RCC_APB2Periph_GPIOB

#define LCD_MOSI_GPIO_PORT GPIOB
#define LCD_MOSI_GPIO_PIN  GPIO_Pin_15
#define LCD_MOSI_GPIO_CLK  RCC_APB2Periph_GPIOB

#define LCD_RES_GPIO_PORT  GPIOA
#define LCD_RES_GPIO_PIN   GPIO_Pin_8
#define LCD_RES_GPIO_CLK   RCC_APB2Periph_GPIOA

#define LCD_DC_GPIO_PORT   GPIOA
#define LCD_DC_GPIO_PIN    GPIO_Pin_9
#define LCD_DC_GPIO_CLK    RCC_APB2Periph_GPIOA

#define LCD_CS_GPIO_PORT   GPIOA
#define LCD_CS_GPIO_PIN    GPIO_Pin_10
#define LCD_CS_GPIO_CLK    RCC_APB2Periph_GPIOA

#define LCD_BLK_GPIO_PORT  GPIOA
#define LCD_BLK_GPIO_PIN   GPIO_Pin_11
#define LCD_BLK_GPIO_CLK   RCC_APB2Periph_GPIOA

#define LCD_GPIO_ALL_CLK (LCD_SCLK_GPIO_CLK|LCD_MOSI_GPIO_CLK|LCD_RES_GPIO_CLK|LCD_DC_GPIO_CLK|LCD_CS_GPIO_CLK|LCD_BLK_GPIO_CLK)

#define LCD_SCLK_Clr() GPIO_ResetBits(LCD_SCLK_GPIO_PORT,LCD_SCLK_GPIO_PIN)//SCL=SCLK
#define LCD_SCLK_Set() GPIO_SetBits(LCD_SCLK_GPIO_PORT,LCD_SCLK_GPIO_PIN)

#define LCD_MOSI_Clr() GPIO_ResetBits(LCD_MOSI_GPIO_PORT,LCD_MOSI_GPIO_PIN)//SDA=MOSI
#define LCD_MOSI_Set() GPIO_SetBits(LCD_MOSI_GPIO_PORT,LCD_MOSI_GPIO_PIN)

#define LCD_RES_Clr()  GPIO_ResetBits(LCD_RES_GPIO_PORT,LCD_RES_GPIO_PIN)//RES
#define LCD_RES_Set()  GPIO_SetBits(LCD_RES_GPIO_PORT,LCD_RES_GPIO_PIN)

#define LCD_DC_Clr()   GPIO_ResetBits(LCD_DC_GPIO_PORT,LCD_DC_GPIO_PIN)//DC
#define LCD_DC_Set()   GPIO_SetBits(LCD_DC_GPIO_PORT,LCD_DC_GPIO_PIN)
		     
#define LCD_CS_Clr()   GPIO_ResetBits(LCD_CS_GPIO_PORT,LCD_CS_GPIO_PIN)//CS
#define LCD_CS_Set()   GPIO_SetBits(LCD_CS_GPIO_PORT,LCD_CS_GPIO_PIN)

#define LCD_BLK_Clr()  GPIO_ResetBits(LCD_BLK_GPIO_PORT,LCD_BLK_GPIO_PIN)//BLK
#define LCD_BLK_Set()  GPIO_SetBits(LCD_BLK_GPIO_PORT,LCD_BLK_GPIO_PIN)




void LCD_GPIO_Init(void);//初始化GPIO
void LCD_Writ_Bus(u8 dat);//模拟SPI时序
void LCD_WR_DATA8(u8 dat);//写入一个字节
void LCD_WR_DATA(u16 dat);//写入两个字节
void LCD_WR_REG(u8 dat);//写入一个指令
void LCD_Address_Set(u16 x1,u16 y1,u16 x2,u16 y2);//设置坐标函数
void LCD_Init(void);//LCD初始化
#endif




