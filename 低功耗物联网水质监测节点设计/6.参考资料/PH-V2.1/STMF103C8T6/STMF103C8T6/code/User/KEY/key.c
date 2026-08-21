#include "stm32f10x.h"
#include "key.h"
#include "sys.h" 
#include "delay.h"
#include "OLED_I2C.h"

u8 SET_Flag=0; //设置标志位
u8 CLC_Flag=0; //清屏标志位
u8 SET_Count=0;
u8 Warning_flag=0;

float set_h_t=30; //温度上限值
float set_l_t=20; //温度下限值
float set_h_ph=7.0; //PH上限值
float set_l_ph=6.0; //PH下限值
float set_h_tds=600; //EC上限值
float set_l_tds=300; //EC下限值

char  SET_H_PH_Buff[5];   //PH数据存储数组
char  SET_L_PH_Buff[5];   //PH数据存储数组
char  SET_H_TEMP_Buff[3];   //浊度数据存储数组
char  SET_L_TEMP_Buff[3];   //浊度数据存储数组
char  SET_H_TDS_Buff[6];   //浊度数据存储数组
char  SET_L_TDS_Buff[6];   //浊度数据存储数组
								    
//按键初始化函数
void KEY_Init(void) //IO初始化
{ 
 	GPIO_InitTypeDef GPIO_InitStructure;
 
 	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);//使能PORTA,PORTE时钟

	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15;//KEY1-KEY4
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; //设置成上拉输入
 	GPIO_Init(GPIOB, &GPIO_InitStructure);//初始化

//	//初始化 WK_UP   下拉输入
//	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_14;
//	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD; //设置成输入，默认下拉	  
//	GPIO_Init(GPIOB, &GPIO_InitStructure);//初始化

}
//按键处理函数
//返回按键值
//mode:0,不支持连续按;1,支持连续按;
//0，没有任何按键按下
//1，KEY0按下
//2，KEY1按下
//3，KEY3按下 WK_UP
//注意此函数有响应优先级,KEY0>KEY1>KEY_UP!!
u8 KEY_Scan(u8 mode)
{	 
	static u8 key_up=1;//按键按松开标志
	if(mode)key_up=1;  //支持连按		  
	if(key_up&&(KEY1==0||WK_UP==1))
	{
		delay_ms(10);//去抖动 
		key_up=0;
		if(KEY1==0)return KEY1_PRES;
		else if(WK_UP==1)return WKUP_PRES;
	}else if(KEY1==1&&WK_UP==0)key_up=1; 	    
 	return -1;// 无按键按下
}

/**************************************************************************************
 * 描  述 : KEY输入检测，控制指示灯亮灭
 * 入  参 : 无
 * 返回值 : 无
 **************************************************************************************/
void Key_SET_Scan(void)
{
	 if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_12) == 0)
	  {
	    delay_ms(200);
	    if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_12) == 0)	     //模式按键按下
	    {
				SET_Flag=1;
				CLC_Flag=1;
				Warning_flag=0;
				GPIO_SetBits(GPIOB , GPIO_Pin_1);       //设置时关闭蜂鸣器
				SET_Count+=1;
				if(SET_Count>7) SET_Count=1;		
	    }
	  }
}

void Key_CONFIRM_Scan()
{
   if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_15) == 0)					//确定键    
		 {
		   	delay_ms(200);     //延时消抖
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_15) == 0)
		   	   {
						 	SET_Flag=0;
							CLC_Flag=1;
						  Warning_flag=1;
						  SET_Count=0;
			     }
		 }
}

/**************温度设置子函数******************/
void Temp_H_set(void)     //温度上限设定子函数
{		 
	 
	  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) ==0)					//“+”键  
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13)==0)
		   	 {
           set_h_t+=1;
           if(set_h_t>=99) set_h_t=99;
			   }
		 }
	   if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)					//“-”键 
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)
		   	 {
           set_h_t-=1;
           if(set_h_t<=0) set_h_t=0;
			   }
		 }
		 
		SET_H_TEMP_Buff[0]=(int)(set_h_t)/10+'0';	
	  SET_H_TEMP_Buff[1]=(int)(set_h_t)%10+'0';
		 
		 
	if(CLC_Flag==1)//只做一次清屏处理，防止闪屏
		{
			OLED_CLS();//清屏
			CLC_Flag=0;
		}
		
		OLED_ShowStr(0,0,"SET H TEMP",2);
		OLED_ShowStr(32,4,SET_H_TEMP_Buff,2);//测试6*8字符	 
}
void Temp_L_set(void)   //温度下限设定子函数
{		 
	 
	  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) ==0)					//“+”键  
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13)==0)
		   	 {
           set_l_t+=1;
           if(set_l_t>=100) set_l_t=100;
			   }
		 }
	   if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)					//“-”键 
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)
		   	 {
           set_l_t-=1;
           if(set_l_t<=0) set_l_t=0;
			   }
		 }
		 
		SET_L_TEMP_Buff[0]=(int)(set_l_t)/10+'0';	
	  SET_L_TEMP_Buff[1]=(int)(set_l_t)%10+'0';
		 
		 
	if(CLC_Flag==1)//只做一次清屏处理，防止闪屏
		{
			OLED_CLS();//清屏
			CLC_Flag=0;
		}
		
		OLED_ShowStr(0,0,"SET L TEMP",2);
		OLED_ShowStr(32,4,SET_L_TEMP_Buff,2);//测试6*8字符	 
}
/**************PH设置子函数******************/
void PH_H_set(void)   //PH上限设定函数
{		 
	 
	  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) ==0)					//“+”键  
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13)==0)
		   	 {
           set_h_ph+=0.1;
           if(set_h_ph>=14.0) set_h_ph=14.0;
			   }
		 }
	   if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)					//“-”键 
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)
		   	 {
           set_h_ph-=0.1;
           if(set_h_ph<=0.0) set_h_ph=0.0;
			   }
		 }
		 
		SET_H_PH_Buff[0]=(int)(set_h_ph*10)/100+'0';	
		SET_H_PH_Buff[1]=(int)(set_h_ph*10)%100/10+'0';
		SET_H_PH_Buff[2]='.';
		SET_H_PH_Buff[3]=(int)(set_h_ph*10)%10+'0';	
		 
		 
	if(CLC_Flag==1)//只做一次清屏处理，防止闪屏
		{
			OLED_CLS();//清屏
			CLC_Flag=0;
		}
		
		OLED_ShowStr(0,0,"SET H PH",2);
		OLED_ShowStr(32,4,SET_H_PH_Buff,2);//测试6*8字符
		 
		 
}

void PH_L_set(void)  //PH 下限设定函数
{		 
	 
	  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) ==0)					//“+”键  
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13)==0)
		   	 {
           set_l_ph+=0.1;
           if(set_l_ph>=14.0) set_l_ph=14.0;
			   }
		 }
	   if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)					//“-”键 
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)
		   	 {
           set_l_ph-=0.1;
           if(set_l_ph<=0.0) set_l_ph=0.0;
			   }
		 }
		 
		SET_L_PH_Buff[0]=(int)(set_l_ph*10)/100+'0';	
		SET_L_PH_Buff[1]=(int)(set_l_ph*10)%100/10+'0';
		SET_L_PH_Buff[2]='.';
		SET_L_PH_Buff[3]=(int)(set_l_ph*10)%10+'0';	
		 
		 
	if(CLC_Flag==1)//只做一次清屏处理，防止闪屏
		{
			OLED_CLS();//清屏
			CLC_Flag=0;
		}
		
		OLED_ShowStr(0,0,"SET L PH",2);
		OLED_ShowStr(32,4,SET_L_PH_Buff,2);//测试6*8字符
		 
		 
}

void TDS_H_set(void)   //EC上限设定子函数
{		 
	 
	  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) ==0)					//“+”键  
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13)==0)
		   	 {
           set_h_tds+=10;
           if(set_h_tds>=1400) set_h_tds=1400;
			   }
		 }
	   if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)					//“-”键 
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)
		   	 {
           set_h_tds-=10;
           if(set_h_tds<=0) set_h_tds=0;
			   }
		 }
		
		SET_H_TDS_Buff[0]=(int)(set_h_tds)/1000+'0';	
		SET_H_TDS_Buff[1]=(int)(set_h_tds)%1000/100+'0';
		SET_H_TDS_Buff[2]=(int)(set_h_tds)%100/10+'0';
		SET_H_TDS_Buff[3]=(int)(set_h_tds)%10+'0';	
		 
		 
	if(CLC_Flag==1)//只做一次清屏处理，防止闪屏
		{
			OLED_CLS();//清屏
			CLC_Flag=0;
		}
		
		OLED_ShowStr(0,0,"SET H TDS",2);
		OLED_ShowStr(32,4,SET_H_TDS_Buff,2);//测试6*8字符
		 
}
void TDS_L_set(void)   //EC下限设定子函数
{		 
	 
	  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) ==0)					//“+”键  
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13)==0)
		   	 {
           set_l_tds+=10;
           if(set_l_tds>=1400) set_l_tds=1400;
			   }
		 }
	   if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)					//“-”键 
		 {
		   	delay_ms(200);
			  if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14)==0)
		   	 {
           set_l_tds-=10;
           if(set_l_tds<=0) set_l_tds=0;
			   }
		 }
		
		SET_L_TDS_Buff[0]=(int)(set_l_tds)/1000+'0';	
		SET_L_TDS_Buff[1]=(int)(set_l_tds)%1000/100+'0';
		SET_L_TDS_Buff[2]=(int)(set_l_tds)%100/10+'0';
		SET_L_TDS_Buff[3]=(int)(set_l_tds)%10+'0';	
		 
		 
	if(CLC_Flag==1)//只做一次清屏处理，防止闪屏
		{
			OLED_CLS();//清屏
			CLC_Flag=0;
		}
		
		OLED_ShowStr(0,0,"SET L TDS",2);
		OLED_ShowStr(32,4,SET_L_TDS_Buff,2);//测试6*8字符
		 
}