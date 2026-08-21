#ifndef __RC522_H
#define __RC522_H

#include "stm32f1xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#define RC522_CS_PORT GPIOB
#define RC522_CS_PIN GPIO_PIN_12

#define RC522_MOSI_PORT GPIOB
#define RC522_MOSI_PIN GPIO_PIN_15

#define RC522_MISO_PORT GPIOB
#define RC522_MISO_PIN GPIO_PIN_14

#define RC522_SCK_PORT GPIOB
#define RC522_SCK_PIN GPIO_PIN_13

#define RC522_RST_PORT GPIOA
#define RC522_RST_PIN GPIO_PIN_9

#define RC522_IRQ_PORT GPIOA
#define RC522_IRQ_PIN GPIO_PIN_8

#define RC522_MAX_CARDS 20u
#define RC522_UID_MAX_LEN 10u
#define RC522_DEFAULT_KEY_A {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}

#define MFRC522_REG_COMMAND 0x01u
#define MFRC522_REG_COM_I_EN 0x02u
#define MFRC522_REG_DIV_I_EN 0x03u
#define MFRC522_REG_COM_IRQ 0x04u
#define MFRC522_REG_DIV_IRQ 0x05u
#define MFRC522_REG_ERROR 0x06u
#define MFRC522_REG_STATUS1 0x07u
#define MFRC522_REG_STATUS2 0x08u
#define MFRC522_REG_FIFO_DATA 0x09u
#define MFRC522_REG_FIFO_LEVEL 0x0Au
#define MFRC522_REG_CONTROL 0x0Cu
#define MFRC522_REG_BIT_FRAMING 0x0Du
#define MFRC522_REG_COLL 0x0Eu

#define MFRC522_REG_MODE 0x11u
#define MFRC522_REG_TX_MODE 0x12u
#define MFRC522_REG_RX_MODE 0x13u
#define MFRC522_REG_TX_CONTROL 0x14u
#define MFRC522_REG_TX_ASK 0x15u

#define MFRC522_REG_CRC_RESULT_H 0x21u
#define MFRC522_REG_CRC_RESULT_L 0x22u
#define MFRC522_REG_T_MODE 0x2Au
#define MFRC522_REG_T_PRESCALER 0x2Bu
#define MFRC522_REG_T_RELOAD_H 0x2Cu
#define MFRC522_REG_T_RELOAD_L 0x2Du
#define MFRC522_REG_VERSION 0x37u

#define PCD_IDLE 0x00u
#define PCD_CALCCRC 0x03u
#define PCD_TRANSCEIVE 0x0Cu
#define PCD_MFAUTHENT 0x0Eu
#define PCD_SOFTRESET 0x0Fu

#define PICC_REQIDL 0x26u
#define PICC_REQALL 0x52u
#define PICC_ANTICOLL1 0x93u
#define PICC_ANTICOLL2 0x95u
#define PICC_ANTICOLL3 0x97u
#define PICC_SELECT1 0x93u
#define PICC_SELECT2 0x95u
#define PICC_SELECT3 0x97u
#define PICC_AUTHENT1A 0x60u
#define PICC_AUTHENT1B 0x61u
#define PICC_READ 0x30u
#define PICC_WRITE 0xA0u
#define PICC_HALT 0x50u

typedef enum
{
    RC522_OK = 0,
    RC522_ERR,
    RC522_TIMEOUT,
    RC522_NO_CARD,
    RC522_COLLISION,
    RC522_AUTH_FAIL,
    RC522_CARD_NOT_FOUND,
    RC522_CARD_ALREADY_EXIST,
    RC522_DATABASE_FULL,
    RC522_INVALID_PARAM
} RC522_Status_t;

typedef struct
{
    uint8_t uid[RC522_UID_MAX_LEN];
    uint8_t uid_len;
    uint8_t type;
} RC522_CardInfo_t;

void RC522_Init(void);
void RC522_Reset(void);
void RC522_AntennaOn(void);
void RC522_AntennaOff(void);
uint8_t RC522_GetVersion(void);

RC522_Status_t RC522_Request(uint8_t reqMode, uint8_t *pCardType);
RC522_Status_t RC522_Anticoll(RC522_CardInfo_t *pCardInfo);
RC522_Status_t RC522_SelectCard(RC522_CardInfo_t *pCardInfo);
RC522_Status_t RC522_DetectCard(RC522_CardInfo_t *pCardInfo);
RC522_Status_t RC522_HaltCard(void);

RC522_Status_t RC522_Auth(uint8_t authMode, uint8_t blockAddr, uint8_t *pKey, RC522_CardInfo_t *pCardInfo);
RC522_Status_t RC522_ReadBlock(uint8_t blockAddr, uint8_t *pData);
RC522_Status_t RC522_WriteBlock(uint8_t blockAddr, uint8_t *pData);

RC522_Status_t RC522_AddCard(RC522_CardInfo_t *pCardInfo);
RC522_Status_t RC522_RemoveCard(RC522_CardInfo_t *pCardInfo);
bool RC522_IsCardAuthorized(RC522_CardInfo_t *pCardInfo);
RC522_Status_t RC522_VerifyAccess(RC522_CardInfo_t *pCardInfo);
uint8_t RC522_GetCardCount(void);
void RC522_ClearAllCards(void);
RC522_Status_t RC522_GetCardByIndex(uint8_t index, RC522_CardInfo_t *pCardInfo);

#endif
