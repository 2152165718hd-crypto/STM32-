#include ".\Hardware\RC522\RC522.h"

#include <string.h>

#define RC522_CASCADE_TAG 0x88u

static SPI_HandleTypeDef s_rc522_spi;
static RC522_CardInfo_t s_card_db[RC522_MAX_CARDS];
static uint8_t s_card_count = 0u;

static void RC522_EnableGPIOClock(GPIO_TypeDef *port);
static void RC522_GPIO_Init(void);
static void RC522_SPI_Init(void);
static void RC522_CS_Low(void);
static void RC522_CS_High(void);
static void RC522_RST_Low(void);
static void RC522_RST_High(void);
static uint8_t RC522_SPI_TransferByte(uint8_t data);
static void RC522_WriteReg(uint8_t reg, uint8_t value);
static uint8_t RC522_ReadReg(uint8_t reg);
static void RC522_SetRegBits(uint8_t reg, uint8_t mask);
static void RC522_ClearRegBits(uint8_t reg, uint8_t mask);
static RC522_Status_t RC522_CalcCRC(const uint8_t *data, uint8_t length, uint8_t *out_crc);
static RC522_Status_t RC522_Communicate(uint8_t command, uint8_t *tx_data, uint8_t tx_length,
                                        uint8_t *rx_data, uint16_t *rx_bits);
static RC522_Status_t RC522_AnticollLevel(uint8_t command, uint8_t out_uid[5]);
static RC522_Status_t RC522_SelectLevel(uint8_t command, const uint8_t uid_data[5], uint8_t *sak);
static bool RC522_CompareUID(const RC522_CardInfo_t *left, const RC522_CardInfo_t *right);
static int RC522_FindCardIndex(const RC522_CardInfo_t *card);

static void RC522_EnableGPIOClock(GPIO_TypeDef *port)
{
    if (port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
}

static void RC522_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    RC522_EnableGPIOClock(RC522_CS_PORT);
    RC522_EnableGPIOClock(RC522_RST_PORT);
    RC522_EnableGPIOClock(RC522_IRQ_PORT);

    gpio_init.Pin = RC522_CS_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RC522_CS_PORT, &gpio_init);

    gpio_init.Pin = RC522_RST_PIN;
    HAL_GPIO_Init(RC522_RST_PORT, &gpio_init);

    gpio_init.Pin = RC522_IRQ_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(RC522_IRQ_PORT, &gpio_init);

    RC522_CS_High();
    RC522_RST_High();
}

static void RC522_SPI_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_SPI2_CLK_ENABLE();
    RC522_EnableGPIOClock(RC522_SCK_PORT);
    RC522_EnableGPIOClock(RC522_MOSI_PORT);
    RC522_EnableGPIOClock(RC522_MISO_PORT);

    gpio_init.Pin = RC522_SCK_PIN | RC522_MOSI_PIN;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RC522_SCK_PORT, &gpio_init);

    gpio_init.Pin = RC522_MISO_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(RC522_MISO_PORT, &gpio_init);

    s_rc522_spi.Instance = SPI2;
    s_rc522_spi.Init.Mode = SPI_MODE_MASTER;
    s_rc522_spi.Init.Direction = SPI_DIRECTION_2LINES;
    s_rc522_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    s_rc522_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    s_rc522_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    s_rc522_spi.Init.NSS = SPI_NSS_SOFT;
    s_rc522_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    s_rc522_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_rc522_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    s_rc522_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_rc522_spi.Init.CRCPolynomial = 7u;
    HAL_SPI_Init(&s_rc522_spi);
}

static void RC522_CS_Low(void)
{
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_RESET);
}

static void RC522_CS_High(void)
{
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_SET);
}

static void RC522_RST_Low(void)
{
    HAL_GPIO_WritePin(RC522_RST_PORT, RC522_RST_PIN, GPIO_PIN_RESET);
}

static void RC522_RST_High(void)
{
    HAL_GPIO_WritePin(RC522_RST_PORT, RC522_RST_PIN, GPIO_PIN_SET);
}

static uint8_t RC522_SPI_TransferByte(uint8_t data)
{
    uint8_t rx_data = 0u;
    HAL_SPI_TransmitReceive(&s_rc522_spi, &data, &rx_data, 1u, 100u);
    return rx_data;
}

static void RC522_WriteReg(uint8_t reg, uint8_t value)
{
    RC522_CS_Low();
    RC522_SPI_TransferByte((uint8_t)((reg << 1) & 0x7Eu));
    RC522_SPI_TransferByte(value);
    RC522_CS_High();
}

static uint8_t RC522_ReadReg(uint8_t reg)
{
    uint8_t value;

    RC522_CS_Low();
    RC522_SPI_TransferByte((uint8_t)(((reg << 1) & 0x7Eu) | 0x80u));
    value = RC522_SPI_TransferByte(0x00u);
    RC522_CS_High();
    return value;
}

static void RC522_SetRegBits(uint8_t reg, uint8_t mask)
{
    RC522_WriteReg(reg, (uint8_t)(RC522_ReadReg(reg) | mask));
}

static void RC522_ClearRegBits(uint8_t reg, uint8_t mask)
{
    RC522_WriteReg(reg, (uint8_t)(RC522_ReadReg(reg) & (uint8_t)(~mask)));
}

static RC522_Status_t RC522_CalcCRC(const uint8_t *data, uint8_t length, uint8_t *out_crc)
{
    uint32_t timeout = 0xFFFFu;
    uint8_t index;

    if ((data == NULL) || (out_crc == NULL))
    {
        return RC522_INVALID_PARAM;
    }

    RC522_ClearRegBits(MFRC522_REG_DIV_IRQ, 0x04u);
    RC522_SetRegBits(MFRC522_REG_FIFO_LEVEL, 0x80u);

    for (index = 0u; index < length; index++)
    {
        RC522_WriteReg(MFRC522_REG_FIFO_DATA, data[index]);
    }

    RC522_WriteReg(MFRC522_REG_COMMAND, PCD_CALCCRC);

    while (timeout > 0u)
    {
        if ((RC522_ReadReg(MFRC522_REG_DIV_IRQ) & 0x04u) != 0u)
        {
            RC522_WriteReg(MFRC522_REG_COMMAND, PCD_IDLE);
            out_crc[0] = RC522_ReadReg(MFRC522_REG_CRC_RESULT_L);
            out_crc[1] = RC522_ReadReg(MFRC522_REG_CRC_RESULT_H);
            return RC522_OK;
        }
        timeout--;
    }

    return RC522_TIMEOUT;
}

static RC522_Status_t RC522_Communicate(uint8_t command, uint8_t *tx_data, uint8_t tx_length,
                                        uint8_t *rx_data, uint16_t *rx_bits)
{
    uint8_t irq_enable = 0u;
    uint8_t wait_irq = 0u;
    uint8_t fifo_level;
    uint8_t last_bits;
    uint32_t timeout = 0xFFFFu;
    uint8_t status;
    uint8_t index;

    if (command == PCD_MFAUTHENT)
    {
        irq_enable = 0x12u;
        wait_irq = 0x10u;
    }
    else if (command == PCD_TRANSCEIVE)
    {
        irq_enable = 0x77u;
        wait_irq = 0x30u;
    }

    RC522_WriteReg(MFRC522_REG_COM_I_EN, (uint8_t)(irq_enable | 0x80u));
    RC522_ClearRegBits(MFRC522_REG_COM_IRQ, 0x80u);
    RC522_SetRegBits(MFRC522_REG_FIFO_LEVEL, 0x80u);
    RC522_WriteReg(MFRC522_REG_COMMAND, PCD_IDLE);

    for (index = 0u; index < tx_length; index++)
    {
        RC522_WriteReg(MFRC522_REG_FIFO_DATA, tx_data[index]);
    }

    RC522_WriteReg(MFRC522_REG_COMMAND, command);

    if (command == PCD_TRANSCEIVE)
    {
        RC522_SetRegBits(MFRC522_REG_BIT_FRAMING, 0x80u);
    }

    while (timeout > 0u)
    {
        status = RC522_ReadReg(MFRC522_REG_COM_IRQ);
        if ((status & wait_irq) != 0u)
        {
            break;
        }
        if ((status & 0x01u) != 0u)
        {
            break;
        }
        timeout--;
    }

    RC522_ClearRegBits(MFRC522_REG_BIT_FRAMING, 0x80u);

    if (timeout == 0u)
    {
        return RC522_TIMEOUT;
    }

    status = RC522_ReadReg(MFRC522_REG_ERROR);
    if ((status & 0x1Bu) != 0u)
    {
        if ((status & 0x08u) != 0u)
        {
            return RC522_COLLISION;
        }
        return RC522_ERR;
    }

    if (command == PCD_TRANSCEIVE)
    {
        fifo_level = RC522_ReadReg(MFRC522_REG_FIFO_LEVEL);
        last_bits = (uint8_t)(RC522_ReadReg(MFRC522_REG_CONTROL) & 0x07u);

        if (rx_bits != NULL)
        {
            if (last_bits != 0u)
            {
                *rx_bits = (uint16_t)(((fifo_level - 1u) * 8u) + last_bits);
            }
            else
            {
                *rx_bits = (uint16_t)(fifo_level * 8u);
            }
        }

        if (fifo_level == 0u)
        {
            fifo_level = 1u;
        }

        if (rx_data != NULL)
        {
            if (fifo_level > 64u)
            {
                fifo_level = 64u;
            }
            for (index = 0u; index < fifo_level; index++)
            {
                rx_data[index] = RC522_ReadReg(MFRC522_REG_FIFO_DATA);
            }
        }
    }

    return RC522_OK;
}

static RC522_Status_t RC522_AnticollLevel(uint8_t command, uint8_t out_uid[5])
{
    uint8_t buffer[2];
    uint16_t rx_bits = 0u;
    uint8_t checksum = 0u;
    uint8_t index;
    RC522_Status_t status;

    buffer[0] = command;
    buffer[1] = 0x20u;

    RC522_WriteReg(MFRC522_REG_BIT_FRAMING, 0x00u);
    RC522_ClearRegBits(MFRC522_REG_COLL, 0x80u);

    status = RC522_Communicate(PCD_TRANSCEIVE, buffer, 2u, out_uid, &rx_bits);
    RC522_SetRegBits(MFRC522_REG_COLL, 0x80u);
    if ((status != RC522_OK) || (rx_bits != 40u))
    {
        return RC522_ERR;
    }

    for (index = 0u; index < 4u; index++)
    {
        checksum ^= out_uid[index];
    }

    if (checksum != out_uid[4])
    {
        return RC522_ERR;
    }

    return RC522_OK;
}

static RC522_Status_t RC522_SelectLevel(uint8_t command, const uint8_t uid_data[5], uint8_t *sak)
{
    uint8_t buffer[9];
    uint8_t response[3];
    uint16_t rx_bits = 0u;
    uint8_t index;

    buffer[0] = command;
    buffer[1] = 0x70u;
    for (index = 0u; index < 5u; index++)
    {
        buffer[index + 2u] = uid_data[index];
    }

    if (RC522_CalcCRC(buffer, 7u, &buffer[7]) != RC522_OK)
    {
        return RC522_ERR;
    }

    if (RC522_Communicate(PCD_TRANSCEIVE, buffer, 9u, response, &rx_bits) != RC522_OK)
    {
        return RC522_ERR;
    }

    if (rx_bits != 24u)
    {
        return RC522_ERR;
    }

    if (sak != NULL)
    {
        *sak = response[0];
    }

    return RC522_OK;
}

void RC522_Init(void)
{
    RC522_GPIO_Init();
    RC522_SPI_Init();

    RC522_RST_Low();
    HAL_Delay(1u);
    RC522_RST_High();
    HAL_Delay(50u);

    RC522_Reset();
    HAL_Delay(50u);

    RC522_WriteReg(MFRC522_REG_T_MODE, 0x8Du);
    RC522_WriteReg(MFRC522_REG_T_PRESCALER, 0x3Eu);
    RC522_WriteReg(MFRC522_REG_T_RELOAD_H, 0x00u);
    RC522_WriteReg(MFRC522_REG_T_RELOAD_L, 0x1Eu);
    RC522_WriteReg(MFRC522_REG_TX_ASK, 0x40u);
    RC522_WriteReg(MFRC522_REG_MODE, 0x3Du);

    RC522_AntennaOn();

    s_card_count = 0u;
    memset(s_card_db, 0, sizeof(s_card_db));
}

void RC522_Reset(void)
{
    RC522_WriteReg(MFRC522_REG_COMMAND, PCD_SOFTRESET);
}

void RC522_AntennaOn(void)
{
    if ((RC522_ReadReg(MFRC522_REG_TX_CONTROL) & 0x03u) == 0u)
    {
        RC522_SetRegBits(MFRC522_REG_TX_CONTROL, 0x03u);
    }
}

void RC522_AntennaOff(void)
{
    RC522_ClearRegBits(MFRC522_REG_TX_CONTROL, 0x03u);
}

uint8_t RC522_GetVersion(void)
{
    return RC522_ReadReg(MFRC522_REG_VERSION);
}

RC522_Status_t RC522_Request(uint8_t reqMode, uint8_t *pCardType)
{
    uint16_t rx_bits = 0u;
    RC522_Status_t status;

    if (pCardType == NULL)
    {
        return RC522_INVALID_PARAM;
    }

    RC522_ClearRegBits(MFRC522_REG_STATUS2, 0x08u);
    RC522_WriteReg(MFRC522_REG_BIT_FRAMING, 0x07u);
    RC522_SetRegBits(MFRC522_REG_TX_CONTROL, 0x03u);

    pCardType[0] = reqMode;
    status = RC522_Communicate(PCD_TRANSCEIVE, pCardType, 1u, pCardType, &rx_bits);
    if ((status != RC522_OK) || (rx_bits != 16u))
    {
        return (status == RC522_TIMEOUT) ? RC522_NO_CARD : RC522_ERR;
    }

    return RC522_OK;
}

RC522_Status_t RC522_Anticoll(RC522_CardInfo_t *pCardInfo)
{
    return RC522_DetectCard(pCardInfo);
}

RC522_Status_t RC522_SelectCard(RC522_CardInfo_t *pCardInfo)
{
    uint8_t sak = 0u;
    uint8_t uid_data[5];
    RC522_Status_t status = RC522_ERR;

    if ((pCardInfo == NULL) || (pCardInfo->uid_len == 0u))
    {
        return RC522_INVALID_PARAM;
    }

    if (pCardInfo->uid_len == 4u)
    {
        uid_data[0] = pCardInfo->uid[0];
        uid_data[1] = pCardInfo->uid[1];
        uid_data[2] = pCardInfo->uid[2];
        uid_data[3] = pCardInfo->uid[3];
        uid_data[4] = (uint8_t)(uid_data[0] ^ uid_data[1] ^ uid_data[2] ^ uid_data[3]);
        status = RC522_SelectLevel(PICC_SELECT1, uid_data, &sak);
    }
    else if (pCardInfo->uid_len == 7u)
    {
        uid_data[0] = RC522_CASCADE_TAG;
        uid_data[1] = pCardInfo->uid[0];
        uid_data[2] = pCardInfo->uid[1];
        uid_data[3] = pCardInfo->uid[2];
        uid_data[4] = (uint8_t)(uid_data[0] ^ uid_data[1] ^ uid_data[2] ^ uid_data[3]);
        status = RC522_SelectLevel(PICC_SELECT1, uid_data, &sak);
        if (status == RC522_OK)
        {
            uid_data[0] = pCardInfo->uid[3];
            uid_data[1] = pCardInfo->uid[4];
            uid_data[2] = pCardInfo->uid[5];
            uid_data[3] = pCardInfo->uid[6];
            uid_data[4] = (uint8_t)(uid_data[0] ^ uid_data[1] ^ uid_data[2] ^ uid_data[3]);
            status = RC522_SelectLevel(PICC_SELECT2, uid_data, &sak);
        }
    }
    else if (pCardInfo->uid_len == 10u)
    {
        uid_data[0] = RC522_CASCADE_TAG;
        uid_data[1] = pCardInfo->uid[0];
        uid_data[2] = pCardInfo->uid[1];
        uid_data[3] = pCardInfo->uid[2];
        uid_data[4] = (uint8_t)(uid_data[0] ^ uid_data[1] ^ uid_data[2] ^ uid_data[3]);
        status = RC522_SelectLevel(PICC_SELECT1, uid_data, &sak);
        if (status == RC522_OK)
        {
            uid_data[0] = RC522_CASCADE_TAG;
            uid_data[1] = pCardInfo->uid[3];
            uid_data[2] = pCardInfo->uid[4];
            uid_data[3] = pCardInfo->uid[5];
            uid_data[4] = (uint8_t)(uid_data[0] ^ uid_data[1] ^ uid_data[2] ^ uid_data[3]);
            status = RC522_SelectLevel(PICC_SELECT2, uid_data, &sak);
        }
        if (status == RC522_OK)
        {
            uid_data[0] = pCardInfo->uid[6];
            uid_data[1] = pCardInfo->uid[7];
            uid_data[2] = pCardInfo->uid[8];
            uid_data[3] = pCardInfo->uid[9];
            uid_data[4] = (uint8_t)(uid_data[0] ^ uid_data[1] ^ uid_data[2] ^ uid_data[3]);
            status = RC522_SelectLevel(PICC_SELECT3, uid_data, &sak);
        }
    }

    if (status == RC522_OK)
    {
        pCardInfo->type = sak;
    }

    return status;
}

RC522_Status_t RC522_DetectCard(RC522_CardInfo_t *pCardInfo)
{
    static const uint8_t cascade_commands[3] = {PICC_ANTICOLL1, PICC_ANTICOLL2, PICC_ANTICOLL3};
    uint8_t atqa[2];
    uint8_t level_uid[5];
    uint8_t sak = 0u;
    uint8_t level;
    uint8_t uid_length = 0u;
    RC522_Status_t status;

    if (pCardInfo == NULL)
    {
        return RC522_INVALID_PARAM;
    }

    memset(pCardInfo, 0, sizeof(*pCardInfo));

    status = RC522_Request(PICC_REQIDL, atqa);
    if (status != RC522_OK)
    {
        return RC522_NO_CARD;
    }

    for (level = 0u; level < 3u; level++)
    {
        status = RC522_AnticollLevel(cascade_commands[level], level_uid);
        if (status != RC522_OK)
        {
            return status;
        }

        status = RC522_SelectLevel(cascade_commands[level], level_uid, &sak);
        if (status != RC522_OK)
        {
            return status;
        }

        if (level_uid[0] == RC522_CASCADE_TAG)
        {
            if ((uid_length + 3u) > RC522_UID_MAX_LEN)
            {
                return RC522_ERR;
            }
            memcpy(&pCardInfo->uid[uid_length], &level_uid[1], 3u);
            uid_length = (uint8_t)(uid_length + 3u);
        }
        else
        {
            if ((uid_length + 4u) > RC522_UID_MAX_LEN)
            {
                return RC522_ERR;
            }
            memcpy(&pCardInfo->uid[uid_length], &level_uid[0], 4u);
            uid_length = (uint8_t)(uid_length + 4u);
        }

        if ((sak & 0x04u) == 0u)
        {
            pCardInfo->uid_len = uid_length;
            pCardInfo->type = sak;
            return RC522_OK;
        }
    }

    return RC522_ERR;
}

RC522_Status_t RC522_HaltCard(void)
{
    uint8_t buffer[4];
    uint16_t rx_bits = 0u;

    buffer[0] = PICC_HALT;
    buffer[1] = 0x00u;
    if (RC522_CalcCRC(buffer, 2u, &buffer[2]) != RC522_OK)
    {
        return RC522_ERR;
    }

    (void)RC522_Communicate(PCD_TRANSCEIVE, buffer, 4u, buffer, &rx_bits);
    return RC522_OK;
}

RC522_Status_t RC522_Auth(uint8_t authMode, uint8_t blockAddr, uint8_t *pKey, RC522_CardInfo_t *pCardInfo)
{
    uint8_t buffer[12];
    uint16_t rx_bits = 0u;
    uint8_t index;
    RC522_Status_t status;

    if ((pKey == NULL) || (pCardInfo == NULL) || (pCardInfo->uid_len < 4u))
    {
        return RC522_INVALID_PARAM;
    }

    buffer[0] = authMode;
    buffer[1] = blockAddr;
    for (index = 0u; index < 6u; index++)
    {
        buffer[index + 2u] = pKey[index];
    }
    for (index = 0u; index < 4u; index++)
    {
        buffer[index + 8u] = pCardInfo->uid[index];
    }

    status = RC522_Communicate(PCD_MFAUTHENT, buffer, 12u, buffer, &rx_bits);
    if ((status != RC522_OK) || ((RC522_ReadReg(MFRC522_REG_STATUS2) & 0x08u) == 0u))
    {
        return RC522_AUTH_FAIL;
    }

    return RC522_OK;
}

RC522_Status_t RC522_ReadBlock(uint8_t blockAddr, uint8_t *pData)
{
    uint8_t buffer[4];
    uint16_t rx_bits = 0u;
    RC522_Status_t status;

    if (pData == NULL)
    {
        return RC522_INVALID_PARAM;
    }

    buffer[0] = PICC_READ;
    buffer[1] = blockAddr;
    if (RC522_CalcCRC(buffer, 2u, &buffer[2]) != RC522_OK)
    {
        return RC522_ERR;
    }

    status = RC522_Communicate(PCD_TRANSCEIVE, buffer, 4u, pData, &rx_bits);
    if ((status != RC522_OK) || (rx_bits != 144u))
    {
        return RC522_ERR;
    }

    return RC522_OK;
}

RC522_Status_t RC522_WriteBlock(uint8_t blockAddr, uint8_t *pData)
{
    uint8_t buffer[18];
    uint8_t ack[4];
    uint16_t rx_bits = 0u;
    RC522_Status_t status;

    if (pData == NULL)
    {
        return RC522_INVALID_PARAM;
    }

    buffer[0] = PICC_WRITE;
    buffer[1] = blockAddr;
    if (RC522_CalcCRC(buffer, 2u, &buffer[2]) != RC522_OK)
    {
        return RC522_ERR;
    }

    status = RC522_Communicate(PCD_TRANSCEIVE, buffer, 4u, ack, &rx_bits);
    if ((status != RC522_OK) || (rx_bits != 4u) || ((ack[0] & 0x0Fu) != 0x0Au))
    {
        return RC522_ERR;
    }

    memcpy(buffer, pData, 16u);
    if (RC522_CalcCRC(buffer, 16u, &buffer[16]) != RC522_OK)
    {
        return RC522_ERR;
    }

    status = RC522_Communicate(PCD_TRANSCEIVE, buffer, 18u, ack, &rx_bits);
    if ((status != RC522_OK) || (rx_bits != 4u) || ((ack[0] & 0x0Fu) != 0x0Au))
    {
        return RC522_ERR;
    }

    return RC522_OK;
}

static bool RC522_CompareUID(const RC522_CardInfo_t *left, const RC522_CardInfo_t *right)
{
    if ((left == NULL) || (right == NULL) || (left->uid_len != right->uid_len) || (left->uid_len == 0u))
    {
        return false;
    }

    return (memcmp(left->uid, right->uid, left->uid_len) == 0);
}

static int RC522_FindCardIndex(const RC522_CardInfo_t *card)
{
    uint8_t index;

    for (index = 0u; index < s_card_count; index++)
    {
        if (RC522_CompareUID(&s_card_db[index], card))
        {
            return index;
        }
    }

    return -1;
}

RC522_Status_t RC522_AddCard(RC522_CardInfo_t *pCardInfo)
{
    if ((pCardInfo == NULL) || (pCardInfo->uid_len == 0u))
    {
        return RC522_INVALID_PARAM;
    }

    if (RC522_FindCardIndex(pCardInfo) >= 0)
    {
        return RC522_CARD_ALREADY_EXIST;
    }

    if (s_card_count >= RC522_MAX_CARDS)
    {
        return RC522_DATABASE_FULL;
    }

    memcpy(&s_card_db[s_card_count], pCardInfo, sizeof(RC522_CardInfo_t));
    s_card_count++;
    return RC522_OK;
}

RC522_Status_t RC522_RemoveCard(RC522_CardInfo_t *pCardInfo)
{
    int index;

    if ((pCardInfo == NULL) || (pCardInfo->uid_len == 0u))
    {
        return RC522_INVALID_PARAM;
    }

    index = RC522_FindCardIndex(pCardInfo);
    if (index < 0)
    {
        return RC522_CARD_NOT_FOUND;
    }

    for (; index < (int)(s_card_count - 1u); index++)
    {
        memcpy(&s_card_db[index], &s_card_db[index + 1], sizeof(RC522_CardInfo_t));
    }

    memset(&s_card_db[s_card_count - 1u], 0, sizeof(RC522_CardInfo_t));
    s_card_count--;
    return RC522_OK;
}

bool RC522_IsCardAuthorized(RC522_CardInfo_t *pCardInfo)
{
    if ((pCardInfo == NULL) || (pCardInfo->uid_len == 0u))
    {
        return false;
    }

    return (RC522_FindCardIndex(pCardInfo) >= 0);
}

RC522_Status_t RC522_VerifyAccess(RC522_CardInfo_t *pCardInfo)
{
    RC522_CardInfo_t temp_card;
    RC522_CardInfo_t *card = (pCardInfo != NULL) ? pCardInfo : &temp_card;
    RC522_Status_t status = RC522_DetectCard(card);

    if (status != RC522_OK)
    {
        return status;
    }

    RC522_HaltCard();
    return RC522_IsCardAuthorized(card) ? RC522_OK : RC522_CARD_NOT_FOUND;
}

uint8_t RC522_GetCardCount(void)
{
    return s_card_count;
}

void RC522_ClearAllCards(void)
{
    s_card_count = 0u;
    memset(s_card_db, 0, sizeof(s_card_db));
}

RC522_Status_t RC522_GetCardByIndex(uint8_t index, RC522_CardInfo_t *pCardInfo)
{
    if ((pCardInfo == NULL) || (index >= s_card_count))
    {
        return RC522_INVALID_PARAM;
    }

    memcpy(pCardInfo, &s_card_db[index], sizeof(RC522_CardInfo_t));
    return RC522_OK;
}
