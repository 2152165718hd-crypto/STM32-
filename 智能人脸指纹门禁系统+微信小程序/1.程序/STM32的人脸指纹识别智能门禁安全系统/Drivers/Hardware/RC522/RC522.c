#include ".\Hardware\RC522\RC522.h"
#include <string.h>

/* ==================== 内部变量 ==================== */

// SPI句柄 (由RC522_Init内部初始化)
SPI_HandleTypeDef RC522_SPI;

// 授权卡片数据库 (RAM)
static RC522_CardInfo_t rc522_card_db[RC522_MAX_CARDS];
static uint8_t rc522_card_count = 0;

/* ==================== 底层SPI / GPIO 操作 ==================== */

/**
 * @brief  CS片选拉低 (选中RC522)
 */
static void RC522_CS_Low(void)
{
    HAL_GPIO_WritePin(RC522_PORT, RC522_CS_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  CS片选拉高 (取消选中)
 */
static void RC522_CS_High(void)
{
    HAL_GPIO_WritePin(RC522_PORT, RC522_CS_PIN, GPIO_PIN_SET);
}

/**
 * @brief  RST复位引脚拉低
 */
static void RC522_RST_Low(void)
{
    HAL_GPIO_WritePin(RC522_PORT, RC522_RST_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  RST复位引脚拉高
 */
static void RC522_RST_High(void)
{
    HAL_GPIO_WritePin(RC522_PORT, RC522_RST_PIN, GPIO_PIN_SET);
}

/**
 * @brief  SPI读写一个字节
 * @param  data: 发送的字节
 * @retval 接收到的字节
 */
static uint8_t RC522_SPI_TransferByte(uint8_t data)
{
    uint8_t rxData = 0;
    HAL_SPI_TransmitReceive(&RC522_SPI, &data, &rxData, 1, 100);
    return rxData;
}

/**
 * @brief  向MFRC522寄存器写入一个字节
 * @param  reg: 寄存器地址
 * @param  value: 写入值
 */
static void RC522_WriteReg(uint8_t reg, uint8_t value)
{
    RC522_CS_Low();
    // 地址格式: 0XXXXXX0, 最高位0表示写, bit0固定0
    RC522_SPI_TransferByte((reg << 1) & 0x7E);
    RC522_SPI_TransferByte(value);
    RC522_CS_High();
}

/**
 * @brief  从MFRC522寄存器读取一个字节
 * @param  reg: 寄存器地址
 * @retval 读取的值
 */
static uint8_t RC522_ReadReg(uint8_t reg)
{
    uint8_t value;
    RC522_CS_Low();
    // 地址格式: 1XXXXXX0, 最高位1表示读, bit0固定0
    RC522_SPI_TransferByte(((reg << 1) & 0x7E) | 0x80);
    value = RC522_SPI_TransferByte(0x00);
    RC522_CS_High();
    return value;
}

/**
 * @brief  对寄存器指定位置1
 * @param  reg: 寄存器地址
 * @param  mask: 需要置1的位掩码
 */
static void RC522_SetRegBits(uint8_t reg, uint8_t mask)
{
    uint8_t tmp = RC522_ReadReg(reg);
    RC522_WriteReg(reg, tmp | mask);
}

/**
 * @brief  对寄存器指定位清零
 * @param  reg: 寄存器地址
 * @param  mask: 需要清零的位掩码
 */
static void RC522_ClearRegBits(uint8_t reg, uint8_t mask)
{
    uint8_t tmp = RC522_ReadReg(reg);
    RC522_WriteReg(reg, tmp & (~mask));
}

/* ==================== MFRC522 核心操作 ==================== */

/**
 * @brief  计算CRC (使用MFRC522内部CRC协处理器)
 * @param  pInData: 输入数据
 * @param  len: 数据长度
 * @param  pOutData: 输出2字节CRC
 * @retval RC522_OK/RC522_TIMEOUT
 */
static RC522_Status_t RC522_CalcCRC(uint8_t *pInData, uint8_t len, uint8_t *pOutData)
{
    uint32_t i = 0;

    // 清除CRC中断标志
    RC522_ClearRegBits(MFRC522_REG_DIV_IRQ, 0x04);
    // 刷新FIFO
    RC522_SetRegBits(MFRC522_REG_FIFO_LEVEL, 0x80);

    // 向FIFO写数据
    for (i = 0; i < len; i++)
    {
        RC522_WriteReg(MFRC522_REG_FIFO_DATA, pInData[i]);
    }

    // 执行CRC计算命令
    RC522_WriteReg(MFRC522_REG_COMMAND, PCD_CALCCRC);

    // 等待CRC计算完成
    i = 0xFFFF;
    while (i--)
    {
        uint8_t n = RC522_ReadReg(MFRC522_REG_DIV_IRQ);
        if (n & 0x04) // CRC计算完成
        {
            RC522_WriteReg(MFRC522_REG_COMMAND, PCD_IDLE);
            pOutData[0] = RC522_ReadReg(MFRC522_REG_CRC_RESULT_L);
            pOutData[1] = RC522_ReadReg(MFRC522_REG_CRC_RESULT_H);
            return RC522_OK;
        }
    }

    return RC522_TIMEOUT;
}

/**
 * @brief  MFRC522与PICC通信 (核心收发函数)
 * @param  command: MFRC522命令 (PCD_TRANSCEIVE / PCD_MFAUTHENT)
 * @param  pInData: 发送数据
 * @param  inLen: 发送数据长度
 * @param  pOutData: 接收数据缓冲
 * @param  pOutLen: 接收数据位数 (注意是bit数)
 * @retval RC522_OK / RC522_ERR / RC522_TIMEOUT / RC522_COLLISION
 */
static RC522_Status_t RC522_CommunicateWithPICC(uint8_t command,
                                                 uint8_t *pInData, uint8_t inLen,
                                                 uint8_t *pOutData, uint16_t *pOutLen)
{
    RC522_Status_t status = RC522_ERR;
    uint8_t irqEn = 0x00;
    uint8_t waitIRq = 0x00;
    uint8_t lastBits;
    uint8_t n;
    uint32_t i;

    switch (command)
    {
    case PCD_MFAUTHENT:
        irqEn = 0x12;  // IdleIEn, ErrIEn
        waitIRq = 0x10; // IdleIRq
        break;
    case PCD_TRANSCEIVE:
        irqEn = 0x77;  // TxIEn, RxIEn, IdleIEn, LoAlertIEn, ErrIEn, TimerIEn
        waitIRq = 0x30; // RxIRq, IdleIRq
        break;
    default:
        break;
    }

    // 使能中断
    RC522_WriteReg(MFRC522_REG_COM_I_EN, irqEn | 0x80);
    // 清除中断标志
    RC522_ClearRegBits(MFRC522_REG_COM_IRQ, 0x80);
    // 刷新FIFO
    RC522_SetRegBits(MFRC522_REG_FIFO_LEVEL, 0x80);
    // 取消当前命令
    RC522_WriteReg(MFRC522_REG_COMMAND, PCD_IDLE);

    // 向FIFO写入数据
    for (i = 0; i < inLen; i++)
    {
        RC522_WriteReg(MFRC522_REG_FIFO_DATA, pInData[i]);
    }

    // 执行命令
    RC522_WriteReg(MFRC522_REG_COMMAND, command);

    if (command == PCD_TRANSCEIVE)
    {
        RC522_SetRegBits(MFRC522_REG_BIT_FRAMING, 0x80); // StartSend=1, 启动发送
    }

    // 等待通信完成
    i = 0xFFFF;
    while (i--)
    {
        n = RC522_ReadReg(MFRC522_REG_COM_IRQ);
        if (n & waitIRq) // 等待的IRQ触发
        {
            break;
        }
        if (n & 0x01) // TimerIRq: 超时
        {
            break;
        }
    }

    // 关闭StartSend
    RC522_ClearRegBits(MFRC522_REG_BIT_FRAMING, 0x80);

    if (i == 0) // 超时退出
    {
        return RC522_TIMEOUT;
    }

    // 检查错误
    if (RC522_ReadReg(MFRC522_REG_ERROR) & 0x1B) // BufferOvfl, CollErr, ParityErr, ProtocolErr
    {
        // 检查是否仅仅是冲突错误
        if (RC522_ReadReg(MFRC522_REG_ERROR) & 0x08)
        {
            return RC522_COLLISION;
        }
        return RC522_ERR;
    }

    status = RC522_OK;

    if (command == PCD_TRANSCEIVE)
    {
        // 获取接收数据
        n = RC522_ReadReg(MFRC522_REG_FIFO_LEVEL);
        lastBits = RC522_ReadReg(MFRC522_REG_CONTROL) & 0x07;

        if (lastBits)
        {
            *pOutLen = (uint16_t)(n - 1) * 8 + lastBits;
        }
        else
        {
            *pOutLen = (uint16_t)n * 8;
        }

        if (n == 0)
        {
            n = 1;
        }
        if (n > 64)
        {
            n = 64;
        }

        // 从FIFO读取数据
        for (i = 0; i < n; i++)
        {
            pOutData[i] = RC522_ReadReg(MFRC522_REG_FIFO_DATA);
        }
    }

    return status;
}

/* ==================== 初始化相关 ==================== */

/**
 * @brief  初始化GPIO引脚 (CS, RST, IRQ)
 */
static void RC522_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 使能GPIOB时钟
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // CS引脚 - 推挽输出
    GPIO_InitStruct.Pin = RC522_CS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RC522_PORT, &GPIO_InitStruct);

    // RST引脚 - 推挽输出
    GPIO_InitStruct.Pin = RC522_RST_PIN;
    HAL_GPIO_Init(RC522_PORT, &GPIO_InitStruct);

    // IRQ引脚 - 浮空输入 (暂不使用中断)
    GPIO_InitStruct.Pin = RC522_IRQ_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(RC522_PORT, &GPIO_InitStruct);

    // 默认CS拉高, RST拉高
    RC522_CS_High();
    RC522_RST_High();
}

/**
 * @brief  初始化SPI2外设
 */
static void RC522_SPI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 使能SPI2和GPIOB时钟
    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // SCK, MOSI - 复用推挽输出
    GPIO_InitStruct.Pin = RC522_SPI_SCK_PIN | RC522_SPI_MOSI_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RC522_PORT, &GPIO_InitStruct);

    // MISO - 浮空输入
    GPIO_InitStruct.Pin = RC522_SPI_MISO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(RC522_PORT, &GPIO_InitStruct);

    // SPI2配置
    RC522_SPI.Instance = SPI2;
    RC522_SPI.Init.Mode = SPI_MODE_MASTER;
    RC522_SPI.Init.Direction = SPI_DIRECTION_2LINES;
    RC522_SPI.Init.DataSize = SPI_DATASIZE_8BIT;
    RC522_SPI.Init.CLKPolarity = SPI_POLARITY_LOW;   // CPOL=0
    RC522_SPI.Init.CLKPhase = SPI_PHASE_1EDGE;       // CPHA=0 -> SPI Mode 0
    RC522_SPI.Init.NSS = SPI_NSS_SOFT;               // 软件控制CS
    RC522_SPI.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8; // APB1=36MHz, SPI=4.5MHz
    RC522_SPI.Init.FirstBit = SPI_FIRSTBIT_MSB;
    RC522_SPI.Init.TIMode = SPI_TIMODE_DISABLE;
    RC522_SPI.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    RC522_SPI.Init.CRCPolynomial = 10;

    HAL_SPI_Init(&RC522_SPI);
}

/**
 * @brief  初始化RC522模块 (SPI + GPIO + MFRC522芯片配置)
 */
void RC522_Init(void)
{
    RC522_GPIO_Init();
    RC522_SPI_Init();

    // 硬件复位
    RC522_RST_Low();
    HAL_Delay(1);
    RC522_RST_High();
    HAL_Delay(50);

    // 软复位
    RC522_Reset();
    HAL_Delay(50);

    // 配置定时器: TModeReg[7..0] + TPrescalerReg
    // f_timer = 13.56MHz / (2 * TPreScaler + 1)
    // 超时时间 = TReloadVal / f_timer
    RC522_WriteReg(MFRC522_REG_T_MODE, 0x8D);       // TAuto=1, TPreScaler高4位
    RC522_WriteReg(MFRC522_REG_T_PRESCALER, 0x3E);  // TPreScaler低8位
    RC522_WriteReg(MFRC522_REG_T_RELOAD_H, 0x00);   // TReload高8位
    RC522_WriteReg(MFRC522_REG_T_RELOAD_L, 0x1E);   // TReload低8位 -> 约25ms超时

    // 发射调制设置
    RC522_WriteReg(MFRC522_REG_TX_ASK, 0x40);       // 100%ASK调制
    RC522_WriteReg(MFRC522_REG_MODE, 0x3D);         // CRC初始值0x6363

    // 开启天线
    RC522_AntennaOn();

    // 清空卡片数据库
    rc522_card_count = 0;
    memset(rc522_card_db, 0, sizeof(rc522_card_db));
}

/**
 * @brief  MFRC522软复位
 */
void RC522_Reset(void)
{
    RC522_WriteReg(MFRC522_REG_COMMAND, PCD_SOFTRESET);
}

/**
 * @brief  开启天线
 */
void RC522_AntennaOn(void)
{
    uint8_t tmp = RC522_ReadReg(MFRC522_REG_TX_CONTROL);
    if (!(tmp & 0x03))
    {
        RC522_SetRegBits(MFRC522_REG_TX_CONTROL, 0x03);
    }
}

/**
 * @brief  关闭天线
 */
void RC522_AntennaOff(void)
{
    RC522_ClearRegBits(MFRC522_REG_TX_CONTROL, 0x03);
}

/**
 * @brief  获取MFRC522固件版本号
 * @retval 版本号字节
 */
uint8_t RC522_GetVersion(void)
{
    return RC522_ReadReg(MFRC522_REG_VERSION);
}

/* ==================== 卡片检测与识别 ==================== */

/**
 * @brief  寻卡
 * @param  reqMode: PICC_REQIDL(寻未休眠卡) / PICC_REQALL(寻全部卡)
 * @param  pCardType: 返回2字节ATQA
 * @retval RC522_OK / RC522_NO_CARD / RC522_ERR
 */
RC522_Status_t RC522_Request(uint8_t reqMode, uint8_t *pCardType)
{
    RC522_Status_t status;
    uint16_t backLen = 0;

    // 清除认证状态
    RC522_ClearRegBits(MFRC522_REG_STATUS2, 0x08);
    // BitFramingReg: 发送7位 (短帧)
    RC522_WriteReg(MFRC522_REG_BIT_FRAMING, 0x07);
    // 开启天线
    RC522_SetRegBits(MFRC522_REG_TX_CONTROL, 0x03);

    pCardType[0] = reqMode;
    status = RC522_CommunicateWithPICC(PCD_TRANSCEIVE, pCardType, 1, pCardType, &backLen);

    if (status != RC522_OK || backLen != 0x10) // ATQA长度应为16bits
    {
        return (status == RC522_TIMEOUT) ? RC522_NO_CARD : RC522_ERR;
    }

    return RC522_OK;
}

/**
 * @brief  防冲突，获取卡片UID (CL1, 获取4字节UID)
 * @param  pCardInfo: 输出卡片信息
 * @retval RC522_OK / RC522_ERR
 */
RC522_Status_t RC522_Anticoll(RC522_CardInfo_t *pCardInfo)
{
    RC522_Status_t status;
    uint8_t serNum[5]; // 4字节UID + 1字节BCC
    uint16_t backLen = 0;
    uint8_t i;
    uint8_t checksum = 0;

    RC522_WriteReg(MFRC522_REG_BIT_FRAMING, 0x00); // 完整字节传输
    RC522_ClearRegBits(MFRC522_REG_COLL, 0x80);    // ValuesAfterColl=0

    serNum[0] = PICC_ANTICOLL1;
    serNum[1] = 0x20; // NVB: 2字节命令, 0位数据

    status = RC522_CommunicateWithPICC(PCD_TRANSCEIVE, serNum, 2, serNum, &backLen);

    if (status == RC522_OK)
    {
        // 校验: BCC = UID0 ^ UID1 ^ UID2 ^ UID3
        for (i = 0; i < 4; i++)
        {
            checksum ^= serNum[i];
        }
        if (checksum != serNum[4])
        {
            return RC522_ERR;
        }

        // 填充卡片信息
        memcpy(pCardInfo->uid, serNum, 4);
        pCardInfo->uid_len = 4;
    }

    RC522_SetRegBits(MFRC522_REG_COLL, 0x80); // ValuesAfterColl恢复

    return status;
}

/**
 * @brief  选定卡片
 * @param  pCardInfo: 卡片信息(使用4字节UID)
 * @retval RC522_OK / RC522_ERR
 */
RC522_Status_t RC522_SelectCard(RC522_CardInfo_t *pCardInfo)
{
    RC522_Status_t status;
    uint8_t buf[9]; // SEL(1) + NVB(1) + UID(4) + BCC(1) + CRC(2)
    uint16_t backLen = 0;
    uint8_t i;

    buf[0] = PICC_SELECT1;
    buf[1] = 0x70; // NVB: 7字节
    for (i = 0; i < 4; i++)
    {
        buf[i + 2] = pCardInfo->uid[i];
    }
    // BCC
    buf[6] = buf[2] ^ buf[3] ^ buf[4] ^ buf[5];

    // 计算CRC
    RC522_CalcCRC(buf, 7, &buf[7]);

    // 清除认证状态
    RC522_ClearRegBits(MFRC522_REG_STATUS2, 0x08);

    uint8_t recvBuf[3]; // SAK (1byte) + CRC (2bytes)
    status = RC522_CommunicateWithPICC(PCD_TRANSCEIVE, buf, 9, recvBuf, &backLen);

    if (status == RC522_OK)
    {
        if (backLen == 0x18) // 24bits = 3bytes
        {
            pCardInfo->type = recvBuf[0]; // SAK
        }
        else
        {
            status = RC522_ERR;
        }
    }

    return status;
}

/**
 * @brief  一键识别卡片 (寻卡 + 防冲突 + 选卡)
 * @param  pCardInfo: 输出卡片信息
 * @retval RC522_OK 成功; 其他 失败
 */
RC522_Status_t RC522_DetectCard(RC522_CardInfo_t *pCardInfo)
{
    RC522_Status_t status;
    uint8_t cardType[2];

    // 清空信息
    memset(pCardInfo, 0, sizeof(RC522_CardInfo_t));

    // 1. 寻卡
    status = RC522_Request(PICC_REQIDL, cardType);
    if (status != RC522_OK)
    {
        return RC522_NO_CARD;
    }

    // 2. 防冲突, 获取UID
    status = RC522_Anticoll(pCardInfo);
    if (status != RC522_OK)
    {
        return RC522_ERR;
    }

    // 3. 选卡
    status = RC522_SelectCard(pCardInfo);
    if (status != RC522_OK)
    {
        return RC522_ERR;
    }

    return RC522_OK;
}

/**
 * @brief  让卡片进入休眠状态
 * @retval RC522_OK
 */
RC522_Status_t RC522_HaltCard(void)
{
    uint8_t buf[4];
    uint16_t backLen = 0;

    buf[0] = PICC_HALT;
    buf[1] = 0x00;
    RC522_CalcCRC(buf, 2, &buf[2]);

    RC522_CommunicateWithPICC(PCD_TRANSCEIVE, buf, 4, buf, &backLen);

    return RC522_OK;
}

/* ==================== 卡片认证与数据读写 ==================== */

/**
 * @brief  用密钥对指定块进行认证
 * @param  authMode: PICC_AUTHENT1A / PICC_AUTHENT1B
 * @param  blockAddr: 块地址
 * @param  pKey: 6字节密钥
 * @param  pCardInfo: 已选卡的卡片信息
 * @retval RC522_OK / RC522_AUTH_FAIL
 */
RC522_Status_t RC522_Auth(uint8_t authMode, uint8_t blockAddr,
                          uint8_t *pKey, RC522_CardInfo_t *pCardInfo)
{
    RC522_Status_t status;
    uint8_t buf[12]; // Auth(1) + Addr(1) + Key(6) + UID(4)
    uint16_t backLen = 0;
    uint8_t i;

    buf[0] = authMode;
    buf[1] = blockAddr;
    for (i = 0; i < 6; i++)
    {
        buf[i + 2] = pKey[i];
    }
    for (i = 0; i < 4; i++)
    {
        buf[i + 8] = pCardInfo->uid[i];
    }

    status = RC522_CommunicateWithPICC(PCD_MFAUTHENT, buf, 12, buf, &backLen);

    // 检查认证状态位
    if (status != RC522_OK || !(RC522_ReadReg(MFRC522_REG_STATUS2) & 0x08))
    {
        return RC522_AUTH_FAIL;
    }

    return RC522_OK;
}

/**
 * @brief  读取指定块的数据 (16字节)
 * @param  blockAddr: 块地址
 * @param  pData: 输出缓冲 (至少18字节: 16数据 + 2CRC)
 * @retval RC522_OK / RC522_ERR
 */
RC522_Status_t RC522_ReadBlock(uint8_t blockAddr, uint8_t *pData)
{
    RC522_Status_t status;
    uint8_t buf[4];
    uint16_t backLen = 0;

    buf[0] = PICC_READ;
    buf[1] = blockAddr;
    RC522_CalcCRC(buf, 2, &buf[2]);

    status = RC522_CommunicateWithPICC(PCD_TRANSCEIVE, buf, 4, pData, &backLen);

    if (status != RC522_OK || backLen != 0x90) // 144bits = 18bytes (16data + 2crc)
    {
        return RC522_ERR;
    }

    return RC522_OK;
}

/**
 * @brief  向指定块写入数据 (16字节)
 * @param  blockAddr: 块地址
 * @param  pData: 待写入的16字节数据
 * @retval RC522_OK / RC522_ERR
 */
RC522_Status_t RC522_WriteBlock(uint8_t blockAddr, uint8_t *pData)
{
    RC522_Status_t status;
    uint8_t buf[18];
    uint16_t backLen = 0;
    uint8_t recvBuf[4];

    // 第一步: 发送写命令 + 地址 + CRC
    buf[0] = PICC_WRITE;
    buf[1] = blockAddr;
    RC522_CalcCRC(buf, 2, &buf[2]);

    status = RC522_CommunicateWithPICC(PCD_TRANSCEIVE, buf, 4, recvBuf, &backLen);

    // 检查ACK (4bits, 值=0x0A)
    if (status != RC522_OK || (backLen != 4) || ((recvBuf[0] & 0x0F) != 0x0A))
    {
        return RC522_ERR;
    }

    // 第二步: 发送16字节数据 + CRC
    memcpy(buf, pData, 16);
    RC522_CalcCRC(buf, 16, &buf[16]);

    status = RC522_CommunicateWithPICC(PCD_TRANSCEIVE, buf, 18, recvBuf, &backLen);

    if (status != RC522_OK || (backLen != 4) || ((recvBuf[0] & 0x0F) != 0x0A))
    {
        return RC522_ERR;
    }

    return RC522_OK;
}

/* ==================== 卡片管理 (添加/注销/验证) ==================== */

/**
 * @brief  比较两张卡的UID是否相同
 * @param  pCard1: 卡片1
 * @param  pCard2: 卡片2
 * @retval true: 相同; false: 不同
 */
static bool RC522_CompareUID(RC522_CardInfo_t *pCard1, RC522_CardInfo_t *pCard2)
{
    if (pCard1->uid_len != pCard2->uid_len || pCard1->uid_len == 0)
    {
        return false;
    }
    return (memcmp(pCard1->uid, pCard2->uid, pCard1->uid_len) == 0);
}

/**
 * @brief  在数据库中查找卡片
 * @param  pCardInfo: 待查找的卡片
 * @retval 找到的索引; -1表示未找到
 */
static int RC522_FindCardInDB(RC522_CardInfo_t *pCardInfo)
{
    for (uint8_t i = 0; i < rc522_card_count; i++)
    {
        if (RC522_CompareUID(&rc522_card_db[i], pCardInfo))
        {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief  添加一张RFID卡到授权数据库
 * @param  pCardInfo: 要添加的卡片信息
 * @retval RC522_OK / RC522_CARD_ALREADY_EXIST / RC522_DATABASE_FULL
 */
RC522_Status_t RC522_AddCard(RC522_CardInfo_t *pCardInfo)
{
    if (pCardInfo == NULL || pCardInfo->uid_len == 0)
    {
        return RC522_INVALID_PARAM;
    }

    // 检查是否已存在
    if (RC522_FindCardInDB(pCardInfo) >= 0)
    {
        return RC522_CARD_ALREADY_EXIST;
    }

    // 检查数据库是否已满
    if (rc522_card_count >= RC522_MAX_CARDS)
    {
        return RC522_DATABASE_FULL;
    }

    // 添加到数据库
    memcpy(&rc522_card_db[rc522_card_count], pCardInfo, sizeof(RC522_CardInfo_t));
    rc522_card_count++;

    return RC522_OK;
}

/**
 * @brief  从授权数据库中注销(删除)一张RFID卡
 * @param  pCardInfo: 要删除的卡片信息
 * @retval RC522_OK / RC522_CARD_NOT_FOUND
 */
RC522_Status_t RC522_RemoveCard(RC522_CardInfo_t *pCardInfo)
{
    if (pCardInfo == NULL || pCardInfo->uid_len == 0)
    {
        return RC522_INVALID_PARAM;
    }

    int index = RC522_FindCardInDB(pCardInfo);
    if (index < 0)
    {
        return RC522_CARD_NOT_FOUND;
    }

    // 将后面的元素前移覆盖被删除的元素
    for (uint8_t i = (uint8_t)index; i < rc522_card_count - 1; i++)
    {
        memcpy(&rc522_card_db[i], &rc522_card_db[i + 1], sizeof(RC522_CardInfo_t));
    }

    // 清除最后一个位置
    memset(&rc522_card_db[rc522_card_count - 1], 0, sizeof(RC522_CardInfo_t));
    rc522_card_count--;

    return RC522_OK;
}

/**
 * @brief  检查指定卡片是否在授权数据库中
 * @param  pCardInfo: 卡片信息
 * @retval true: 已授权; false: 未授权
 */
bool RC522_IsCardAuthorized(RC522_CardInfo_t *pCardInfo)
{
    if (pCardInfo == NULL || pCardInfo->uid_len == 0)
    {
        return false;
    }
    return (RC522_FindCardInDB(pCardInfo) >= 0);
}

/**
 * @brief  自动检测卡片并验证是否已授权 (一键式门禁验证)
 * @param  pCardInfo: 输出检测到的卡片信息 (可为NULL, 此时内部分配临时变量)
 * @retval RC522_OK 卡片已授权
 *         RC522_NO_CARD 未检测到卡片
 *         RC522_CARD_NOT_FOUND 卡片未授权
 *         RC522_ERR 通信错误
 */
RC522_Status_t RC522_VerifyAccess(RC522_CardInfo_t *pCardInfo)
{
    RC522_Status_t status;
    RC522_CardInfo_t tempCard;
    RC522_CardInfo_t *pCard = (pCardInfo != NULL) ? pCardInfo : &tempCard;

    // 检测卡片
    status = RC522_DetectCard(pCard);
    if (status != RC522_OK)
    {
        return status;
    }

    // 验证是否已授权
    if (RC522_IsCardAuthorized(pCard))
    {
        // 让卡片休眠, 避免重复识别
        RC522_HaltCard();
        return RC522_OK;
    }

    // 让卡片休眠
    RC522_HaltCard();
    return RC522_CARD_NOT_FOUND;
}

/**
 * @brief  获取当前授权数据库中已注册的卡片数量
 * @retval 已注册卡片数量
 */
uint8_t RC522_GetCardCount(void)
{
    return rc522_card_count;
}

/**
 * @brief  清空授权数据库中所有卡片
 */
void RC522_ClearAllCards(void)
{
    rc522_card_count = 0;
    memset(rc522_card_db, 0, sizeof(rc522_card_db));
}

/**
 * @brief  获取授权数据库中指定索引的卡片信息
 * @param  index: 卡片索引 (0 ~ RC522_GetCardCount()-1)
 * @param  pCardInfo: 输出卡片信息
 * @retval RC522_OK / RC522_INVALID_PARAM
 */
RC522_Status_t RC522_GetCardByIndex(uint8_t index, RC522_CardInfo_t *pCardInfo)
{
    if (index >= rc522_card_count || pCardInfo == NULL)
    {
        return RC522_INVALID_PARAM;
    }

    memcpy(pCardInfo, &rc522_card_db[index], sizeof(RC522_CardInfo_t));
    return RC522_OK;
}
