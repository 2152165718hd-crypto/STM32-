/*********************函数声明***************************************/
void P8563_init() ;
void P8563_gettime();

/****************PCF8563 I2C接口定义*****************/
#define PCF8563T_SDA_ON      PORTA|=0x08       //i2c数据线高电平
#define PCF8563T_SDA_OFF     PORTA&=~0x08      //i2c数据线底电平
#define PCF8563T_SCL_ON      PORTA|=0x04      //i2c数据线底电平
#define PCF8563T_SCL_OFF     PORTA&=~0x04      //i2c数据线底电平
#define PCF8563T_DAT_PIN     PORTA|=0x08;DDRA&=~0x08       //i2c数据线端口置为输入
#define PCF8563T_DAT_POUT    DDRA|=0x08        //i2c数据线端口置为输出
#define PCF8563T_SDA_PIN     (PINA&0x08)        //i2c数据线输入电平

/****************PCF8563 寄存器接口定义*****************/
#define PCF8563T_GET_ADR      0xA3              //PCF8563读统一地址
#define PCF8563T_SEND_ADR     0xA2              //PCF8563写统一地址
#define PCF8563T_SYS1         0x00              //PCF8563状态寄存器地址1
#define PCF8563T_SYS2         0x01              //PCF8563状态寄存器地址2
#define PCF8563T_S            0x02              //秒
#define PCF8563T_MIN          0x03              //分
#define PCF8563T_OUR          0x04              //时
#define PCF8563T_DAT          0x05              //日
#define PCF8563T_             0x06              //星期
#define PCF8563T_MATH         0x07              //月
#define PCF8563T_YER          0x08              //年
#define PCF8563T_BJ1          0x09               //分钟报警
#define PCF8563T_BJ2          0x0A               //小时报警
#define PCF8563T_BJ3          0x0B               //日报警
#define PCF8563T_BJ4          0x0C               //星期报警
#define PCF8563T_CLK          0x0D              //时钟测试
#define PCF8563T_CT           0x0E              //定时器工作方式
#define PCF8563T_COUNT        0x0F              //定时器工作方式
