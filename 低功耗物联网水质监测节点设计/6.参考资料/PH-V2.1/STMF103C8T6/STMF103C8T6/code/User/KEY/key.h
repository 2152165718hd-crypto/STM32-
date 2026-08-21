#ifndef __KEY_H
#define __KEY_H	 
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////	 


#define KEY1  GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_13)//读取按键1
//#define KEY2  GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_14)//读取按键2
//#define KEY3  GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_15)//读取按键3
#define WK_UP   GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_14)//读取按键0(WK_UP) 

 

#define KEY1_PRES 	1	//KEY1按下
//#define KEY2_PRES	  2	//KEY2按下
//#define KEY3_PRES	  3//KEY2按下
#define WKUP_PRES   0//KEY_UP按下(即WK_UP/KEY_UP)


void KEY_Init(void);//IO初始化
u8 KEY_Scan(u8);  	//按键扫描函数	
void Key_SET_Scan(void);
void Key_CONFIRM_Scan(void);
void Temp_set(void);
void PH_set(void);
void TU_set(void);
void TDS_set(void);

void Temp_H_set(void);     //温度上限设定子函数
void Temp_L_set(void);   //温度下限设定子函数
void PH_H_set(void);   //PH上限设定函数
void PH_L_set(void);  //PH 下限设定函数
void TDS_H_set(void);   //EC上限设定子函数
void TDS_L_set(void);   //EC下限设定子函数

#endif
