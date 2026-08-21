#include ".\Hardware\DHT11\DHT11.h"

DHT11_Data_t DHT11_Data; /* DHT11数据结构体变�?*/

/**
 * @brief  设置DHT11数据引脚为输出模�?
 */
static void
DHT11_PIN_OUT(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_Data_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; // 推挽输出
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_Data_PORT, &GPIO_InitStruct);
}

/**
 * @brief  设置DHT11数据引脚为输入模�?
 */
static void DHT11_PIN_IN(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_Data_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT; // 浮空输入
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_Data_PORT, &GPIO_InitStruct);
}

/**
 * @brief  数据线写�?低电�?
 */
static void DHT11_DATA_Write(uint8_t state)
{
    HAL_GPIO_WritePin(DHT11_Data_PORT, DHT11_Data_PIN,
                      state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief  读取数据线电�?
 */
static uint8_t DHT11_DATA_Read(void)
{
    return (uint8_t)HAL_GPIO_ReadPin(DHT11_Data_PORT, DHT11_Data_PIN);
}

/* ==================== DHT11 协议实现 ==================== */

/**
 * @brief  主机发送起始信号，检测DHT11是否应答
 * @retval 0: DHT11应答成功
 *         1: DHT11无应�?
 */
static uint8_t DHT11_Start(void)
{
    uint8_t retry = 0;

    /* �?主机拉低数据�?�?18ms，发起起始信�?*/
    DHT11_PIN_OUT();
    DHT11_DATA_Write(0);
    delay_ms(20); // 拉低 20ms

    /* �?主机释放数据线，拉高 20~40us */
    DHT11_DATA_Write(1);
    delay_us(30);

    /* �?切换为输入，等待DHT11应答 */
    DHT11_PIN_IN();

    /* �?DHT11 拉低 80us 作为应答信号 */
    retry = 0;
    while (DHT11_DATA_Read() && retry < 100)
    {
        retry++;
        delay_us(1);
    }
    if (retry >= 100)
        return 1; // 超时，无应答

    /* �?DHT11 拉高 80us 准备发送数�?*/
    retry = 0;
    while (!DHT11_DATA_Read() && retry < 100)
    {
        retry++;
        delay_us(1);
    }
    if (retry >= 100)
        return 1; // 超时

    retry = 0;
    while (DHT11_DATA_Read() && retry < 100)
    {
        retry++;
        delay_us(1);
    }
    if (retry >= 100)
        return 1; // 超时

    return 0; // 应答成功
}

/**
 * @brief  读取一个字节（8bit�?
 * @retval 读到的字节数�?
 *
 *  DHT11 每个 bit 的时序：
 *    - 先拉�?50us（起始）
 *    - 再拉高：26~28us 表示 '0'�?0us 表示 '1'
 */
static uint8_t DHT11_ReadByte(void)
{
    uint8_t byte = 0;
    uint8_t retry;

    for (uint8_t i = 0; i < 8; i++)
    {
        /* 等待低电平结束（50us 的起始低电平�?*/
        retry = 0;
        while (!DHT11_DATA_Read() && retry < 100)
        {
            retry++;
            delay_us(1);
        }

        /* 延时 40us 后采�?
         * 如果此时仍为高电�?�?bit = 1（高电平持续70us�?
         * 如果此时已为低电�?�?bit = 0（高电平�?6~28us�?
         */
        delay_us(40);

        byte <<= 1;
        if (DHT11_DATA_Read())
        {
            byte |= 0x01;

            /* 等待�?bit 的高电平结束 */
            retry = 0;
            while (DHT11_DATA_Read() && retry < 100)
            {
                retry++;
                delay_us(1);
            }
        }
    }

    return byte;
}

/* ==================== 对外接口 ==================== */

/**
 * @brief  DHT11 初始化（检测是否存在）
 * @retval 0: 检测到DHT11
 *         1: 未检测到DHT11
 */
uint8_t DHT11_Init(void)
{
    /* 使能GPIOA时钟（如已在其他地方使能可省略） */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 先设为输出拉高，给DHT11上电稳定时间 */
    DHT11_PIN_OUT();
    DHT11_DATA_Write(1);
    delay_ms(1000); // DHT11 上电后需等待 1s

    /* 发送起始信号，检测应�?*/
    return DHT11_Start();
}

/**
 * @brief  读取DHT11温湿度数�?
 *         数据将存储在内部全局变量 DHT11_Data �?
 * @retval 0: 读取成功
 *         1: 起始信号无应�?
 *         2: 校验和错�?
 */
uint8_t DHT11_ReadData(void)
{
    uint8_t buf[5] = {0};
    uint8_t i;

    /* 发送起始信�?*/
    if (DHT11_Start() != 0)
    {
        return 1; // 无应�?
    }

    /* 读取 40bit = 5字节数据 */
    for (i = 0; i < 5; i++)
    {
        buf[i] = DHT11_ReadByte();
    }

    /* 切回输出模式，释放总线拉高 */
    DHT11_PIN_OUT();
    DHT11_DATA_Write(1);

    /* 校验和验证：�?字节之和 == �?字节 */
    if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4])
    {
        return 2; // 校验失败
    }

    /* 解析数据并存储到全局变量 */
    DHT11_Data.humi_int = buf[0];
    DHT11_Data.temp_int = buf[2];
    DHT11_Data.humidity = buf[0];
    DHT11_Data.temperature = buf[2];

    return 0; // 读取成功
}

/**
 * @brief  获取最新的温度值（整数部分�?
 *         函数内部会自动尝试读取一次DHT11数据
 * @retval 温度�?0~50)
 */
uint8_t DHT11_GetTemperature(void)
{
    DHT11_ReadData();
    return DHT11_Data.temp_int;
}

/**
 * @brief  获取最新的湿度值（整数部分�?
 *         函数内部会自动尝试读取一次DHT11数据
 * @retval 湿度�?20~90)
 */
uint8_t DHT11_GetHumidity(void)
{
    DHT11_ReadData();
    return DHT11_Data.humi_int;
}

