/***IAR环境编译，直接把这个文件包括进去，然后调用里边的函数就可以用***/
#include "config.h"
#include "PCF8563.h"
//extern uchar xdata TX_buf[50];
extern void Hard_Uart_SendByteB(uint8 x);

uint8 g8563_Store[6] = {0,0,0,0,0,0}; /*时间交换区,全局变量声明*/
uint8 g8563_Write[6] = {0,0,0,0,0,0}; /*时间交换区,全局变量声明*/
uint8 c8563_Store[6]={0x10,0x09,0x18,0x10,0x40,0x00}; /*写入时间初值：星期一 07:59:00*/

/********************************************
内部函数，延时1
********************************************/
void Delay(uint8 i)
{
        while(i--)
        {
                __delay_cycles(1);/*根据晶振频率制定延时时间*/
        }
}
/********************************************
内部函数，I2C开始
********************************************/
void Start()
{ 
      //  _CLI();
        _WDR();
        PCF8563T_SDA_ON;
        Delay(4);
        PCF8563T_SCL_ON;
        Delay(8);
        PCF8563T_SDA_OFF;
        Delay(4);
        PCF8563T_SCL_OFF;
        Delay(4);
        _WDR();
}
/********************************************
内部函数，I2C结束
********************************************/
void Stop()
{
        _WDR();
        PCF8563T_SCL_OFF; 
        Delay(4);
        PCF8563T_SDA_OFF;
        Delay(8);
        PCF8563T_SCL_ON; 
        Delay(4);
        PCF8563T_SDA_ON; 
        Delay(4);
        _WDR();
        //_SEI();
}
/********************************************
内部函数，输出ACK ,每个字节传输完成，输出ack=0,结束读书据，ack=1;
********************************************/
void WriteACK(uint8 ack)
{
        _WDR();
        if(ack)
                PCF8563T_SDA_ON;
        else
                PCF8563T_SDA_OFF;
        Delay(4);
        PCF8563T_SCL_ON;
        Delay(4);
        PCF8563T_SCL_OFF;
        _WDR();
}
/********************************************
内部函数，等待ACK
********************************************/
void WaitACK()
{  
        uint8 errtime = 50;
        //PCF8563T_SDA_ON;
       // Delay(4); /*读ACK*/
        PCF8563T_DAT_PIN;
        PCF8563T_SCL_ON;
        Delay(4);
        while(PCF8563T_SDA_PIN)
        {  
            errtime--;
            if(!errtime) 
               Stop();
        }
        PCF8563T_SCL_OFF; 
        PCF8563T_DAT_POUT;  //改变数据线端口方向为输出状态
}
/********************************************
内部函数.输出数据字节
入口:B=数据
********************************************/
void writebyte(uint8 wdata)
{
        uint8 i;
        _WDR();
        for(i=0;i<8;i++)
        {
            if(wdata&0x80) 
               PCF8563T_SDA_ON;
            else 
               PCF8563T_SDA_OFF;
            wdata <<= 1;
            PCF8563T_SCL_ON; 
            Delay(4);
            PCF8563T_SCL_OFF;
        }
        WaitACK();     //I2C器件或通讯出错，将会退出I2C通讯
        _WDR();
}
/********************************************
内部函数.输入数据
出口:B
********************************************/
uint8 Readbyte()
{
        uint8 i = 0;
        uint8 bytedata = 0;
        PCF8563T_DAT_PIN;
        Delay(8);
        for(i=0;i<8;i++)
        {
                PCF8563T_SCL_ON;  
                bytedata<<=1;
                bytedata |= ((PINA&0x08)>>3);
                PCF8563T_SCL_OFF;
                Delay(4);
        }
        PCF8563T_DAT_POUT;
        return(bytedata);
}
/********************************************
输出数据->pcf8563
********************************************/
void writeData(uint8 address,uint8 mdata)
{
        Start();
        writebyte(0xa2); /*写命令*/
        writebyte(address); /*写地址*/
        writebyte(mdata); /*写数据*/
        Stop();
}
/********************************************
输入数据<-pcf8563
********************************************/
uint8 ReadData(uint8 address) /*单字节*/
{  
        uint8 rdata = 0;
        Start();
        writebyte(0xa2); /*写命令*/
        writebyte(address); /*写地址*/
        Start();
        writebyte(0xa3); /*读命令*/
        rdata=Readbyte();
        WriteACK(1);
        Stop();
        return(rdata);
}
void ReadData1(uint8 address,uint8 count,uint8 * buff) /*多字节*/
{  
        uint8 i;
        Start();
        writebyte(0xa2); /*写命令*/
        writebyte(address); /*写地址*/
        Start();
        writebyte(0xa3); /*读命令*/
        for(i=0;i<count;i++)
        {
                buff[i]=Readbyte();
                if(i<count-1) WriteACK(0);
        }
        WriteACK(1);
        Stop();
}  
/********************************************
内部函数,读入时间到内部缓冲区
********************************************/
void P8563_Read()
{   
        uint8 time[7] = {0,0,0,0,0,0,0};
        _WDR();
        ReadData1(0x02,0x07,time);
        g8563_Store[0]=time[0]&0x7f; /*秒 */
        g8563_Store[1]=time[1]&0x7f; /*分 */
        g8563_Store[2]=time[2]&0x3f; /*小时 */
        g8563_Store[3]=time[3]&0x3f; /*日 */
        g8563_Store[4]=time[5]&0x1f; /*月 */
        g8563_Store[5]=time[6]; /*年  */
        _WDR();
        /*PORTA |= (1<<Pin_485_DE);//发给串口以检测对错
        Hard_Uart_SendByteB(g8563_Store[5]);
        __delay_cycles(10000);__delay_cycles(10000);
        Hard_Uart_SendByteB(g8563_Store[4]);
        __delay_cycles(10000);__delay_cycles(10000);
        Hard_Uart_SendByteB(g8563_Store[3]);
        __delay_cycles(10000);__delay_cycles(10000);
        Hard_Uart_SendByteB(g8563_Store[2]);
        __delay_cycles(10000);__delay_cycles(10000);
        Hard_Uart_SendByteB(g8563_Store[1]);
        __delay_cycles(10000);__delay_cycles(10000);
        Hard_Uart_SendByteB(g8563_Store[0]);
        __delay_cycles(10000);__delay_cycles(10000);__delay_cycles(10000);
         PORTA &= ~(1<<Pin_485_DE);*/
}
/********************************************
读入时间到内部缓冲区----外部调用 
********************************************/
void P8563_gettime()
{   
        //_CLI();
        _WDR();
        P8563_Read();
        if(g8563_Store[0]==0)
                P8563_Read(); /*如果为秒=0，为防止时间变化，再读一次*/
        _WDR();
        //_SEI();
}	
/********************************************
写时间修改值
********************************************/
void P8563_settime()	//设置时间 
{
        ///_CLI();
        _WDR();
	writeData(8,g8563_Write[0]); //年 
	writeData(7,g8563_Write[1]); //月 
	writeData(5,g8563_Write[2]); //日 
	writeData(4,g8563_Write[3]); //时 
   	writeData(3,g8563_Write[4]); //分  
	writeData(2,g8563_Write[5]); //秒 
        _WDR();
        //_SEI();
}
/********************************************
P8563的初始化-----外部调用
********************************************/
void P8563_init()
{
       // uint8 i;
	//for(i=0;i<=5;i++) g8563_Write[i]=c8563_Store[i]; /*初始化时间*/
        //        P8563_settime();  

}