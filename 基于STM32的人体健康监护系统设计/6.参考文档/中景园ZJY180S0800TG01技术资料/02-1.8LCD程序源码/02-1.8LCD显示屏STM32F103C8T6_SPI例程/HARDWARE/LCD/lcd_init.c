#include "lcd_init.h"
#include "delay.h"

static void LCD_GPIO_Config(GPIO_TypeDef *GPIOx, u16 pin)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	GPIO_InitStructure.GPIO_Pin = pin;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOx, &GPIO_InitStructure);
}

static void LCD_Write_Byte(u8 dat)
{
	u8 i;
	for(i=0;i<8;i++)
	{
		LCD_SCLK_Clr();
		if(dat&0x80)
		{
			LCD_MOSI_Set();
		}
		else
		{
			LCD_MOSI_Clr();
		}
		LCD_SCLK_Set();
		dat<<=1;
	}
}

static void LCD_Write_Cmd_Data(u8 cmd, const u8 *data, u8 len)
{
	u8 i;

	LCD_CS_Clr();
	LCD_DC_Clr();
	LCD_Write_Byte(cmd);
	LCD_DC_Set();

	for(i=0;i<len;i++)
	{
		LCD_Write_Byte(data[i]);
	}

	LCD_CS_Set();
}

void LCD_GPIO_Init(void)
{
	RCC_APB2PeriphClockCmd(LCD_GPIO_ALL_CLK, ENABLE);

	LCD_GPIO_Config(LCD_SCLK_GPIO_PORT, LCD_SCLK_GPIO_PIN);
	LCD_GPIO_Config(LCD_MOSI_GPIO_PORT, LCD_MOSI_GPIO_PIN);
	LCD_GPIO_Config(LCD_RES_GPIO_PORT, LCD_RES_GPIO_PIN);
	LCD_GPIO_Config(LCD_DC_GPIO_PORT, LCD_DC_GPIO_PIN);
	LCD_GPIO_Config(LCD_CS_GPIO_PORT, LCD_CS_GPIO_PIN);
	LCD_GPIO_Config(LCD_BLK_GPIO_PORT, LCD_BLK_GPIO_PIN);

	LCD_SCLK_Set();
	LCD_MOSI_Set();
	LCD_RES_Set();
	LCD_DC_Set();
	LCD_CS_Set();
	LCD_BLK_Set();
}


/******************************************************************************
      函数说明：LCD串行数据写入函数
      入口数据：dat  要写入的串行数据
      返回值：  无
******************************************************************************/
void LCD_Writ_Bus(u8 dat) 
{	
	LCD_CS_Clr();
	LCD_Write_Byte(dat);
  LCD_CS_Set();	
}


/******************************************************************************
      函数说明：LCD写入数据
      入口数据：dat 写入的数据
      返回值：  无
******************************************************************************/
void LCD_WR_DATA8(u8 dat)
{
	LCD_CS_Clr();
	LCD_DC_Set();
	LCD_Write_Byte(dat);
	LCD_CS_Set();
}


/******************************************************************************
      函数说明：LCD写入数据
      入口数据：dat 写入的数据
      返回值：  无
******************************************************************************/
void LCD_WR_DATA(u16 dat)
{
	LCD_CS_Clr();
	LCD_DC_Set();
	LCD_Write_Byte(dat>>8);
	LCD_Write_Byte(dat);
	LCD_CS_Set();
}


/******************************************************************************
      函数说明：LCD写入命令
      入口数据：dat 写入的命令
      返回值：  无
******************************************************************************/
void LCD_WR_REG(u8 dat)
{
	LCD_CS_Clr();
	LCD_DC_Clr();//写命令
	LCD_Write_Byte(dat);
	LCD_CS_Set();
	LCD_DC_Set();//写数据
}


/******************************************************************************
      函数说明：设置起始和结束地址
      入口数据：x1,x2 设置列的起始和结束地址
                y1,y2 设置行的起始和结束地址
      返回值：  无
******************************************************************************/
void LCD_Address_Set(u16 x1,u16 y1,u16 x2,u16 y2)
{
	u8 data[4];
	u16 x_offset;
	u16 y_offset;

	if(x1>=LCD_W)x1=LCD_W-1;
	if(x2>=LCD_W)x2=LCD_W-1;
	if(y1>=LCD_H)y1=LCD_H-1;
	if(y2>=LCD_H)y2=LCD_H-1;

	if(x1>x2)
	{
		u16 tmp=x1;
		x1=x2;
		x2=tmp;
	}
	if(y1>y2)
	{
		u16 tmp=y1;
		y1=y2;
		y2=tmp;
	}

	if(USE_HORIZONTAL==0)
	{
		x_offset=2;
		y_offset=1;
	}
	else if(USE_HORIZONTAL==1)
	{
		x_offset=2;
		y_offset=1;
	}
	else if(USE_HORIZONTAL==2)
	{
		x_offset=1;
		y_offset=2;
	}
	else
	{
		x_offset=1;
		y_offset=2;
	}

	x1+=x_offset;
	x2+=x_offset;
	y1+=y_offset;
	y2+=y_offset;

	data[0]=(u8)(x1>>8);
	data[1]=(u8)x1;
	data[2]=(u8)(x2>>8);
	data[3]=(u8)x2;
	LCD_Write_Cmd_Data(0x2a,data,4);//列地址设置

	data[0]=(u8)(y1>>8);
	data[1]=(u8)y1;
	data[2]=(u8)(y2>>8);
	data[3]=(u8)y2;
	LCD_Write_Cmd_Data(0x2b,data,4);//行地址设置

	LCD_WR_REG(0x2c);//储存器写
}

void LCD_Init(void)
{
	LCD_GPIO_Init();//初始化GPIO
	
	LCD_RES_Clr();//复位
	delay_ms(100);
	LCD_RES_Set();
	delay_ms(100);
	
	LCD_BLK_Set();//打开背光
  delay_ms(100);
	
	//************* Start Initial Sequence **********//
	LCD_WR_REG(0x11); //Sleep out 
	delay_ms(120);              //Delay 120ms 
	//------------------------------------ST7735S Frame Rate-----------------------------------------// 
	LCD_WR_REG(0xB1); 
	LCD_WR_DATA8(0x05); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_REG(0xB2); 
	LCD_WR_DATA8(0x05);
	LCD_WR_DATA8(0x3C); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_REG(0xB3); 
	LCD_WR_DATA8(0x05); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_DATA8(0x05); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_DATA8(0x3C); 
	//------------------------------------End ST7735S Frame Rate---------------------------------// 
	LCD_WR_REG(0xB4); //Dot inversion 
	LCD_WR_DATA8(0x03); 
	//------------------------------------ST7735S Power Sequence---------------------------------// 
	LCD_WR_REG(0xC0); 
	LCD_WR_DATA8(0x28); 
	LCD_WR_DATA8(0x08); 
	LCD_WR_DATA8(0x04); 
	LCD_WR_REG(0xC1); 
	LCD_WR_DATA8(0XC0); 
	LCD_WR_REG(0xC2); 
	LCD_WR_DATA8(0x0D); 
	LCD_WR_DATA8(0x00); 
	LCD_WR_REG(0xC3); 
	LCD_WR_DATA8(0x8D); 
	LCD_WR_DATA8(0x2A); 
	LCD_WR_REG(0xC4); 
	LCD_WR_DATA8(0x8D); 
	LCD_WR_DATA8(0xEE); 
	//---------------------------------End ST7735S Power Sequence-------------------------------------// 
	LCD_WR_REG(0xC5); //VCOM 
	LCD_WR_DATA8(0x1A); 
	LCD_WR_REG(0x36); //MX, MY, RGB mode 
	if(USE_HORIZONTAL==0)LCD_WR_DATA8(0x00);
	else if(USE_HORIZONTAL==1)LCD_WR_DATA8(0xC0);
	else if(USE_HORIZONTAL==2)LCD_WR_DATA8(0x70);
	else LCD_WR_DATA8(0xA0); 
	//------------------------------------ST7735S Gamma Sequence---------------------------------// 
	LCD_WR_REG(0xE0); 
	LCD_WR_DATA8(0x04); 
	LCD_WR_DATA8(0x22); 
	LCD_WR_DATA8(0x07); 
	LCD_WR_DATA8(0x0A); 
	LCD_WR_DATA8(0x2E); 
	LCD_WR_DATA8(0x30); 
	LCD_WR_DATA8(0x25); 
	LCD_WR_DATA8(0x2A); 
	LCD_WR_DATA8(0x28); 
	LCD_WR_DATA8(0x26); 
	LCD_WR_DATA8(0x2E); 
	LCD_WR_DATA8(0x3A); 
	LCD_WR_DATA8(0x00); 
	LCD_WR_DATA8(0x01); 
	LCD_WR_DATA8(0x03); 
	LCD_WR_DATA8(0x13); 
	LCD_WR_REG(0xE1); 
	LCD_WR_DATA8(0x04); 
	LCD_WR_DATA8(0x16); 
	LCD_WR_DATA8(0x06); 
	LCD_WR_DATA8(0x0D); 
	LCD_WR_DATA8(0x2D); 
	LCD_WR_DATA8(0x26); 
	LCD_WR_DATA8(0x23); 
	LCD_WR_DATA8(0x27); 
	LCD_WR_DATA8(0x27); 
	LCD_WR_DATA8(0x25); 
	LCD_WR_DATA8(0x2D); 
	LCD_WR_DATA8(0x3B); 
	LCD_WR_DATA8(0x00); 
	LCD_WR_DATA8(0x01); 
	LCD_WR_DATA8(0x04); 
	LCD_WR_DATA8(0x13); 
	//------------------------------------End ST7735S Gamma Sequence-----------------------------// 
	LCD_WR_REG(0x3A); //65k mode 
	LCD_WR_DATA8(0x05); 
	LCD_WR_REG(0x29); //Display on 
} 







