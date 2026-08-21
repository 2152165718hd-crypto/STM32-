#include <reg52.h>
extern unsigned char TX_buf[10];
void uart_init(void)
{
	TMOD=0x20;//设置定时器1为工作方式2
	TH1=0xfd;//装初值
	TL1=0xfd;
	TR1=1; //启动定时器
	TI=1;
	//REN=1; //允许接收
	SM0=0; //设置工作方式 1
	SM1=1;
	EA=1;   //开串口中断
	ES=1;  
    //IP = 0x10;	 //设置串口为高优先级
}
void uart_test(unsigned char length)
{

		unsigned char i;
	    for(i=0;i<length;i++)
		{
				ES=0;
		        SBUF = TX_buf[i];//将接受的数据发出去
				while(!TI);	
				TI=0;				
		}	
		ES=1;	  
}			  
