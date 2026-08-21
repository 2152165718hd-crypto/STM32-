/********************(C) COPYRIGHT 2019 Crownto electronic **************************
 * 文件名  : main.c
 * 描述    : STM32F103C8T6 pH计、温度采集与 OLED 显示主程序 (加入去极值滤波)
 **********************************************************************************/
#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>        
#include "delay.h"
#include "bsp_SysTick.h"
#include "math.h"
#include "bsp_adc.h"
#include "ds18b20.h"

#include "OLED_I2C.h"
#include "timer.h"
#include "bsp_usart1.h"
#include "bsp_usart2.h"

volatile uint32_t time = 0; 
GPIO_InitTypeDef  GPIO_InitStructure; 
unsigned char AD_CHANNEL=0;

// pH 与电压计算相关的全局浮点变量
float PH=0.0, PH_voltage;
float PH_value=0.0, voltage_value;
float TEMP_Value=0.0;

// 字符串显示缓存区
char  TEMP_Buff[5];   
char  PH_Buff[6];   
char  VOLT_Buff[6];   

extern u8 SET_Flag, SET_Count; 
extern u8 CLC_Flag; 
extern u8 Warning_flag;
u8 Warning_count=0;

// 存储ADC DMA转换结果的数组 (包含4个通道，[0]通常为PA0)
extern __IO uint16_t ADC_ConvertedValue[4];


void GPIO_Configuration(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
	GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);		
}

void TEMP_Value_Conversion()
{
	TEMP_Value = DS18B20_Get_Temp(); 
	TEMP_Buff[0] = (int)(TEMP_Value)%1000/100 + '0';	
	TEMP_Buff[1] = (int)(TEMP_Value)%100/10 + '0';    
	TEMP_Buff[2] = '.';                               
	TEMP_Buff[3] = (int)(TEMP_Value)%10 + '0';        
	TEMP_Buff[4] = '\0';                              
}

// =========================================================================
// 【新增】核心滤波算法：去极值平均滤波 (剔除最大值和最小值后求平均)
// =========================================================================
double avergearray(int* arr, int number) {
	int i;
	int max, min;
	double avg;
	long amount = 0;
    
	if(number <= 0) {
		printf("Error number for the array to avraging!\r\n");
		return 0;
	}
	if(number < 5) {   
		for(i = 0; i < number; i++) {
			amount += arr[i];
		}
		avg = (double)amount / number;
		return avg;
	} else {
		if(arr[0] < arr[1]) {
			min = arr[0]; max = arr[1];
		} else {
			min = arr[1]; max = arr[0];
		}
		for(i = 2; i < number; i++) {
			if(arr[i] < min) {
				amount += min;        
				min = arr[i];
			} else {
				if(arr[i] > max) {
					amount += max;    
					max = arr[i];
				} else {
					amount += arr[i]; 
				}
			}
		}
		avg = (double)amount / (number - 2);
	}
	return avg;
}

// =========================================================================
// 读取 ADC 并转换为稳定电压和 pH 值的函数 (彻底重构)
// =========================================================================
#define ARRAY_LENGTH 40   // 采样次数

void PH_Value_Conversion(void)
{
	/* ---- 第一步：变量必须全部集中在函数最开头定义（C89标准） ---- */
	int pHArray[ARRAY_LENGTH];
	double avg_adc_value = 0.0;
	int i; // 将变量 i 提前到这里定义！
    
	/* ---- 第二步：执行逻辑代码 ---- */
	// 1. 采集数据阶段 (注意：这里的 for 括号里面绝对不能再有 int 字眼了)
	for(i = 0; i < ARRAY_LENGTH; i++)
	{
		pHArray[i] = ADC_ConvertedValue[0]; // 从 DMA 拿到当前原始 ADC 数字量 (0-4095)
		delay_ms(1); // 每次采样间隔1毫秒，能有效滤除工频干扰
	}
    
	// 2. 核心滤波：过滤掉突变乱跳的 ADC 值
	avg_adc_value = avergearray(pHArray, ARRAY_LENGTH);
    
	// 3. 将稳定后的平均 ADC 值转换为电压 (STM32为12位ADC，参考电压3.3V)
	voltage_value = (float)avg_adc_value / 4096.0 * 3.3;
    
	// 4. 将稳定电压代入公式计算 pH 值 (k值与Offset根据你的探头调整)
	PH_value = -5.6342 * voltage_value + 16.413;
    
	// 5. 软件限幅保护
	if(PH_value <= 0) { PH_value = 0.0; }
	if(PH_value > 14.0) { PH_value = 14.0; }
	
	// --- 下面是 OLED 字符串转换逻辑 ---
	PH_Buff[0] = (int)(PH_value*100)/1000 + '0';         
	PH_Buff[1] = (int)(PH_value*100)%1000/100 + '0';     
	PH_Buff[2] = '.';
	PH_Buff[3] = (int)(PH_value*100)%100/10 + '0';       
	PH_Buff[4] = (int)(PH_value*100)%10 + '0';           
	PH_Buff[5] = '\0';

	VOLT_Buff[0] = (int)(voltage_value*100)/100 + '0';   
	VOLT_Buff[1] = '.';
	VOLT_Buff[2] = (int)(voltage_value*100)%100/10 + '0';
	VOLT_Buff[3] = (int)(voltage_value*100)%10 + '0';    
	VOLT_Buff[4] = 'V';
	VOLT_Buff[5] = '\0';
}

void Display_Data()
{
	OLED_ShowStr(36, 4, PH_Buff, 2);   
	OLED_ShowStr(36, 6, VOLT_Buff, 2); 
}

int main(void)
{	 
	GPIO_Configuration();    
    USART1_Config();
    USART2_Config();
    SysTick_Init();          
	TIM3_Init();             
	I2C_Configuration();     
	OLED_Init();             
	ADCx_Init();		     
	DS18B20_Init();          
		
    OLED_CLS();              
	OLED_ShowStr(0, 4, "PH:", 2);
	OLED_ShowStr(0, 6, "V:", 2);

    printf("--- STM32 pH Meter System Started ---\r\n");

    while(1)
	{	
		TEMP_Value_Conversion(); 
		PH_Value_Conversion();   // 经过彻底滤波的稳定转换
		Display_Data();	         
        
        // 串口实时打印监控
        printf("Voltage: %.2f V | pH Value: %.2f\r\n", voltage_value, PH_value);

		delay_ms(500);           
	}	
}

/*********************************************END OF FILE**********************/
