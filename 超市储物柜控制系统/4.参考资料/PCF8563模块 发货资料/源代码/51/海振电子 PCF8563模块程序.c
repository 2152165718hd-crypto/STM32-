#include <reg52.h>

extern void uart_init(void);
extern void P8563_init();
extern void P8563_gettime();
extern void uart_test(unsigned char length);

unsigned char TX_buf[10];
unsigned char jj,Rx_Byte,Length;
bit CmdValid;
unsigned char RX_buf[10];
 	
void main(void)
{
		uart_init();

	  	P8563_init();
		WDT_CONTR = 0x30;
		while(1)
  		{
				//WDT_CONTR = 0x3c;
				WDT_CONTR = 0x30;
				P8563_gettime();
				/*TX_buf[0] = 0x01;
				TX_buf[1] = 0x02;
				TX_buf[2] = 0x02;
				TX_buf[3] = 0x03;	
				uart_test(4);	   */
		}
} 
/*void ser() interrupt 4
{	
		if(RI)
		{
	    		RI=0;
	   			RX_buf[Rx_Byte]=SBUF;
				if(RX_buf[0]=='\n') 
						Rx_Byte=0;
				if(RX_buf[0]==' ')
			 			Rx_Byte=0;
	   			if(	RX_buf[0] == 0xff)
	      				Rx_Byte = 0;		  
		   		else
		  	    {	  
		        		Length = RX_buf[0];
		 	    		if(Rx_Byte ==Length)
		        		{
	    		        		jj=0;
			                    CmdValid = 1;
		      			}
			   			Rx_Byte++;  
	  	    	}	  
	 	}	 	
  	 	else if(!RI && TI)
	 	{
		   		TI=0;
		   		if(jj<4)
       					SBUF=TX_buf[jj++];//将接受的数据发出去
				else 
				{
						Rx_Byte=0;
						jj = 0;
				}
	  }	
}	    	*/
