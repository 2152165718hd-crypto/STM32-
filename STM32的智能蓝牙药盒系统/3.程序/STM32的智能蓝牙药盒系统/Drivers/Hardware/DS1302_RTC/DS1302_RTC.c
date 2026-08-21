#include ".\Hardware\DS1302_RTC\DS1302_RTC.h"
#include ".\SYSTEM\delay\delay.h"

/**
 * @file DS1302_RTC.c
 * @brief DS1302 实时时钟驱动实现。
 */

/* ---- 内部宏 ---- */
#define DS1302_CLK_HIGH() HAL_GPIO_WritePin(DS1302_PORT, DS1302_CLK_PIN, GPIO_PIN_SET)
#define DS1302_CLK_LOW() HAL_GPIO_WritePin(DS1302_PORT, DS1302_CLK_PIN, GPIO_PIN_RESET)
#define DS1302_DAT_HIGH() HAL_GPIO_WritePin(DS1302_PORT, DS1302_DAT_PIN, GPIO_PIN_SET)
#define DS1302_DAT_LOW() HAL_GPIO_WritePin(DS1302_PORT, DS1302_DAT_PIN, GPIO_PIN_RESET)
#define DS1302_RST_HIGH() HAL_GPIO_WritePin(DS1302_PORT, DS1302_RST_PIN, GPIO_PIN_SET)
#define DS1302_RST_LOW() HAL_GPIO_WritePin(DS1302_PORT, DS1302_RST_PIN, GPIO_PIN_RESET)
#define DS1302_DAT_READ() HAL_GPIO_ReadPin(DS1302_PORT, DS1302_DAT_PIN)

/* ---- BCD 转换 ---- */
static uint8_t BCD_To_Dec(uint8_t bcd)
{
    return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

static uint8_t Dec_To_BCD(uint8_t dec)
{
    return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

/**
 * @brief 设置DAT引脚为输出模式
 */
static void DS1302_DAT_Output(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DS1302_DAT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DS1302_PORT, &GPIO_InitStruct);
}

/**
 * @brief 设置DAT引脚为输入模式
 */
static void DS1302_DAT_Input(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DS1302_DAT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DS1302_PORT, &GPIO_InitStruct);
}

/**
 * @brief 向DS1302写入1字节
 */
static void DS1302_WriteByte(uint8_t dat)
{
    uint8_t i;
    DS1302_DAT_Output();

    for (i = 0; i < 8; i++)
    {
        DS1302_CLK_LOW();
        if (dat & 0x01)
            DS1302_DAT_HIGH();
        else
            DS1302_DAT_LOW();
        delay_us(2);
        DS1302_CLK_HIGH();
        delay_us(2);
        dat >>= 1;
    }
}

/**
 * @brief 从DS1302读取1字节
 */
static uint8_t DS1302_ReadByte(void)
{
    uint8_t i, dat = 0;
    DS1302_DAT_Input();

    for (i = 0; i < 8; i++)
    {
        DS1302_CLK_LOW();
        delay_us(2);
        if (DS1302_DAT_READ())
            dat |= (1 << i);
        DS1302_CLK_HIGH();
        delay_us(2);
    }

    return dat;
}

/**
 * @brief 向DS1302指定地址写入数据
 */
static void DS1302_WriteReg(uint8_t addr, uint8_t dat)
{
    DS1302_RST_LOW();
    DS1302_CLK_LOW();
    delay_us(2);
    DS1302_RST_HIGH();
    delay_us(2);

    DS1302_WriteByte(addr);
    DS1302_WriteByte(dat);

    DS1302_RST_LOW();
    DS1302_CLK_LOW();
}

/**
 * @brief 从DS1302指定地址读取数据
 */
static uint8_t DS1302_ReadReg(uint8_t addr)
{
    uint8_t dat;

    DS1302_RST_LOW();
    DS1302_CLK_LOW();
    delay_us(2);
    DS1302_RST_HIGH();
    delay_us(2);

    DS1302_WriteByte(addr | 0x01); /* 读命令: 地址最低位置1 */
    dat = DS1302_ReadByte();

    DS1302_RST_LOW();
    DS1302_CLK_LOW();

    return dat;
}

/**
 * @brief DS1302初始化
 */
void DS1302_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    /* 关闭JTAG，保留SWD，释放PA15用作GPIO */
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    /* CLK、RST 配置为推挽输出 */
    GPIO_InitStruct.Pin = DS1302_CLK_PIN | DS1302_RST_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DS1302_PORT, &GPIO_InitStruct);

    /* DAT 配置为推挽输出（读取时动态切换） */
    GPIO_InitStruct.Pin = DS1302_DAT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DS1302_PORT, &GPIO_InitStruct);

    DS1302_RST_LOW();
    DS1302_CLK_LOW();

    /* 关闭写保护 */
    DS1302_WriteReg(DS1302_REG_WP, 0x00);
    /* 关闭涓流充电 */
    DS1302_WriteReg(DS1302_REG_CHARGE, 0x00);

    /* 如果秒寄存器最高位(CH)为1，说明时钟停止，需要启动 */
    uint8_t sec = DS1302_ReadReg(DS1302_REG_SEC);
    if (sec & 0x80)
    {
        /* 清除CH位，启动时钟振荡器 */
        DS1302_WriteReg(DS1302_REG_SEC, sec & 0x7F);
    }
}

/**
 * @brief 设置DS1302时间
 * @param time 时间结构体指针
 */
void DS1302_SetTime(DS1302_Time_t *time)
{
    DS1302_WriteReg(DS1302_REG_WP, 0x00); /* 关闭写保护 */
    DS1302_WriteReg(DS1302_REG_YEAR, Dec_To_BCD(time->year));
    DS1302_WriteReg(DS1302_REG_MON, Dec_To_BCD(time->month));
    DS1302_WriteReg(DS1302_REG_DATE, Dec_To_BCD(time->date));
    DS1302_WriteReg(DS1302_REG_DAY, Dec_To_BCD(time->day));
    DS1302_WriteReg(DS1302_REG_HOUR, Dec_To_BCD(time->hour));
    DS1302_WriteReg(DS1302_REG_MIN, Dec_To_BCD(time->minute));
    DS1302_WriteReg(DS1302_REG_SEC, Dec_To_BCD(time->second));
    DS1302_WriteReg(DS1302_REG_WP, 0x80); /* 开启写保护 */
}

/**
 * @brief 读取DS1302时间
 * @param time 时间结构体指针
 */
void DS1302_GetTime(DS1302_Time_t *time)
{
    time->year = BCD_To_Dec(DS1302_ReadReg(DS1302_REG_YEAR));
    time->month = BCD_To_Dec(DS1302_ReadReg(DS1302_REG_MON));
    time->date = BCD_To_Dec(DS1302_ReadReg(DS1302_REG_DATE));
    time->day = BCD_To_Dec(DS1302_ReadReg(DS1302_REG_DAY));
    time->hour = BCD_To_Dec(DS1302_ReadReg(DS1302_REG_HOUR));
    time->minute = BCD_To_Dec(DS1302_ReadReg(DS1302_REG_MIN));
    time->second = BCD_To_Dec(DS1302_ReadReg(DS1302_REG_SEC) & 0x7F);
}
