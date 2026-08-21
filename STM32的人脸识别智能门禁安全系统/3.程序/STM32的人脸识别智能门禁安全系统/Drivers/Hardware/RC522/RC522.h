#ifndef __RC522_H
#define __RC522_H

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ==================== 硬件引脚定义 ==================== */
#define RC522_PORT GPIOB

#define RC522_CS_PIN GPIO_PIN_12
#define RC522_SPI_MOSI_PIN GPIO_PIN_15
#define RC522_SPI_MISO_PIN GPIO_PIN_14
#define RC522_SPI_SCK_PIN GPIO_PIN_13
#define RC522_RST_PIN GPIO_PIN_11
#define RC522_IRQ_PIN GPIO_PIN_10

#define RC522_SPI hspi2

/* ==================== 卡片数据库配置 ==================== */
#define RC522_MAX_CARDS 20   // 最大可存储的RFID卡数量
#define RC522_UID_MAX_LEN 10 // UID最大字节数（支持4/7/10字节UID）
#define RC522_DEFAULT_KEY_A {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

/* ==================== MFRC522 寄存器地址 ==================== */
/* Command and Status Registers */
#define MFRC522_REG_COMMAND 0x01
#define MFRC522_REG_COM_I_EN 0x02
#define MFRC522_REG_DIV_I_EN 0x03
#define MFRC522_REG_COM_IRQ 0x04
#define MFRC522_REG_DIV_IRQ 0x05
#define MFRC522_REG_ERROR 0x06
#define MFRC522_REG_STATUS1 0x07
#define MFRC522_REG_STATUS2 0x08
#define MFRC522_REG_FIFO_DATA 0x09
#define MFRC522_REG_FIFO_LEVEL 0x0A
#define MFRC522_REG_WATER_LEVEL 0x0B
#define MFRC522_REG_CONTROL 0x0C
#define MFRC522_REG_BIT_FRAMING 0x0D
#define MFRC522_REG_COLL 0x0E

/* Communication Registers */
#define MFRC522_REG_MODE 0x11
#define MFRC522_REG_TX_MODE 0x12
#define MFRC522_REG_RX_MODE 0x13
#define MFRC522_REG_TX_CONTROL 0x14
#define MFRC522_REG_TX_ASK 0x15

/* Configuration Registers */
#define MFRC522_REG_MOD_WIDTH 0x24
#define MFRC522_REG_RF_CFG 0x26
#define MFRC522_REG_GS_N 0x27
#define MFRC522_REG_CW_GS_P 0x28
#define MFRC522_REG_MOD_GS_P 0x29
#define MFRC522_REG_T_MODE 0x2A
#define MFRC522_REG_T_PRESCALER 0x2B
#define MFRC522_REG_T_RELOAD_H 0x2C
#define MFRC522_REG_T_RELOAD_L 0x2D
#define MFRC522_REG_T_COUNTER_VAL_H 0x2E
#define MFRC522_REG_T_COUNTER_VAL_L 0x2F

/* Test Registers */
#define MFRC522_REG_AUTO_TEST 0x36
#define MFRC522_REG_VERSION 0x37

/* CRC Registers */
#define MFRC522_REG_CRC_RESULT_H 0x21
#define MFRC522_REG_CRC_RESULT_L 0x22

/* ==================== MFRC522 命令字 ==================== */
#define PCD_IDLE 0x00        // 空闲
#define PCD_MEM 0x01         // 存储器操作
#define PCD_GENRANDOMID 0x02 // 生成随机ID
#define PCD_CALCCRC 0x03     // CRC计算
#define PCD_TRANSMIT 0x04    // 发射
#define PCD_NOCMDCHANGE 0x07 // 无命令改变
#define PCD_RECEIVE 0x08     // 接收
#define PCD_TRANSCEIVE 0x0C  // 发射并接收
#define PCD_MFAUTHENT 0x0E   // 认证
#define PCD_SOFTRESET 0x0F   // 软复位

/* ==================== PICC Card Commands ==================== */
#define PICC_REQIDL 0x26    // 寻天线区内未进入休眠的卡
#define PICC_REQALL 0x52    // 寻天线区内全部卡
#define PICC_ANTICOLL1 0x93 // 防冲突，1级
#define PICC_ANTICOLL2 0x95 // 防冲突，2级
#define PICC_ANTICOLL3 0x97 // 防冲突，3级
#define PICC_SELECT1 0x93   // 选卡，1级
#define PICC_SELECT2 0x95   // 选卡，2级
#define PICC_SELECT3 0x97   // 选卡，3级
#define PICC_AUTHENT1A 0x60 // 用密钥A验证
#define PICC_AUTHENT1B 0x61 // 用密钥B验证
#define PICC_READ 0x30      // 读块
#define PICC_WRITE 0xA0     // 写块
#define PICC_DECREMENT 0xC0 // 扣费
#define PICC_INCREMENT 0xC1 // 充费
#define PICC_RESTORE 0xC2   // 恢复到缓冲区
#define PICC_TRANSFER 0xB0  // 保存缓冲区数据
#define PICC_HALT 0x50      // 休眠

/* ==================== 返回状态码 ==================== */
typedef enum
{
    RC522_OK = 0,             // 操作成功
    RC522_ERR,                // 通信错误
    RC522_TIMEOUT,            // 超时
    RC522_NO_CARD,            // 未检测到卡片
    RC522_COLLISION,          // 冲突
    RC522_AUTH_FAIL,          // 认证失败
    RC522_CARD_NOT_FOUND,     // 数据库中未找到该卡
    RC522_CARD_ALREADY_EXIST, // 卡片已存在
    RC522_DATABASE_FULL,      // 卡片数据库已满
    RC522_INVALID_PARAM       // 参数无效
} RC522_Status_t;

/* ==================== 卡片信息结构体 ==================== */
typedef struct
{
    uint8_t uid[RC522_UID_MAX_LEN]; // 卡片UID
    uint8_t uid_len;                // UID长度（4/7/10）
    uint8_t type;                   // 卡片类型（SAK值）
} RC522_CardInfo_t;

/* ==================== 对外API函数声明 ==================== */

/* ---------- 初始化与底层操作 ---------- */

/**
 * @brief  初始化RC522模块（SPI + GPIO + MFRC522芯片配置）
 */
void RC522_Init(void);

/**
 * @brief  MFRC522软复位
 */
void RC522_Reset(void);

/**
 * @brief  开启天线（使能射频信号输出）
 */
void RC522_AntennaOn(void);

/**
 * @brief  关闭天线
 */
void RC522_AntennaOff(void);

/**
 * @brief  获取MFRC522固件版本号
 * @retval 版本号字节，通常为0x91或0x92
 */
uint8_t RC522_GetVersion(void);

/* ---------- 卡片检测与识别 ---------- */

/**
 * @brief  寻卡（检测是否有卡片进入射频场）
 * @param  reqMode: 寻卡模式
 *         - PICC_REQIDL: 寻未进入休眠的卡
 *         - PICC_REQALL: 寻所有卡
 * @param  pCardType: 返回卡片类型（2字节ATQA）
 * @retval RC522_OK 成功；RC522_NO_CARD 无卡；RC522_ERR 错误
 */
RC522_Status_t RC522_Request(uint8_t reqMode, uint8_t *pCardType);

/**
 * @brief  防冲突，获取卡片序列号UID
 * @param  pCardInfo: 返回卡片信息（UID + 长度）
 * @retval RC522_OK 成功；RC522_ERR 错误
 */
RC522_Status_t RC522_Anticoll(RC522_CardInfo_t *pCardInfo);

/**
 * @brief  选定卡片
 * @param  pCardInfo: 卡片信息（使用其UID进行选择）
 * @retval RC522_OK 成功；RC522_ERR 错误
 */
RC522_Status_t RC522_SelectCard(RC522_CardInfo_t *pCardInfo);

/**
 * @brief  一键识别卡片（寻卡 + 防冲突 + 选卡），获取完整的卡片信息
 * @param  pCardInfo: 输出卡片信息
 * @retval RC522_OK 成功；其他 失败
 */
RC522_Status_t RC522_DetectCard(RC522_CardInfo_t *pCardInfo);

/**
 * @brief  让卡片进入休眠状态
 * @retval RC522_OK
 */
RC522_Status_t RC522_HaltCard(void);

/* ---------- 卡片认证与数据读写 ---------- */

/**
 * @brief  用密钥对卡片指定扇区块进行认证
 * @param  authMode: 认证模式 PICC_AUTHENT1A / PICC_AUTHENT1B
 * @param  blockAddr: 块地址
 * @param  pKey: 6字节密钥
 * @param  pCardInfo: 已选定的卡片信息
 * @retval RC522_OK 成功；RC522_AUTH_FAIL 失败
 */
RC522_Status_t RC522_Auth(uint8_t authMode, uint8_t blockAddr,
                          uint8_t *pKey, RC522_CardInfo_t *pCardInfo);

/**
 * @brief  读取指定块的数据（16字节）
 * @param  blockAddr: 块地址
 * @param  pData: 输出16字节数据缓冲
 * @retval RC522_OK 成功；RC522_ERR 失败
 */
RC522_Status_t RC522_ReadBlock(uint8_t blockAddr, uint8_t *pData);

/**
 * @brief  向指定块写入数据（16字节）
 * @param  blockAddr: 块地址
 * @param  pData: 待写入的16字节数据
 * @retval RC522_OK 成功；RC522_ERR 失败
 */
RC522_Status_t RC522_WriteBlock(uint8_t blockAddr, uint8_t *pData);

/* ---------- 卡片管理（添加/注销/验证） ---------- */

/**
 * @brief  添加一张RFID卡到授权数据库
 * @param  pCardInfo: 要添加的卡片信息
 * @retval RC522_OK 添加成功
 *         RC522_CARD_ALREADY_EXIST 卡片已存在
 *         RC522_DATABASE_FULL 数据库已满
 */
RC522_Status_t RC522_AddCard(RC522_CardInfo_t *pCardInfo);

/**
 * @brief  从授权数据库中注销（删除）一张RFID卡
 * @param  pCardInfo: 要删除的卡片信息
 * @retval RC522_OK 删除成功
 *         RC522_CARD_NOT_FOUND 未找到该卡
 */
RC522_Status_t RC522_RemoveCard(RC522_CardInfo_t *pCardInfo);

/**
 * @brief  检查指定卡片是否在授权数据库中
 * @param  pCardInfo: 卡片信息
 * @retval true：已授权；false：未授权
 */
bool RC522_IsCardAuthorized(RC522_CardInfo_t *pCardInfo);

/**
 * @brief  自动检测卡片并验证是否已授权（一键式门禁验证）
 * @param  pCardInfo: 输出检测到的卡片信息（可为NULL）
 * @retval RC522_OK 卡片已授权
 *         RC522_NO_CARD 未检测到卡片
 *         RC522_CARD_NOT_FOUND 卡片未授权
 *         RC522_ERR 通信错误
 */
RC522_Status_t RC522_VerifyAccess(RC522_CardInfo_t *pCardInfo);

/**
 * @brief  获取当前授权数据库中已注册的卡片数量
 * @retval 已注册卡片数量
 */
uint8_t RC522_GetCardCount(void);

/**
 * @brief  清空授权数据库中所有卡片
 */
void RC522_ClearAllCards(void);

/**
 * @brief  获取授权数据库中指定索引的卡片信息
 * @param  index: 卡片索引（0 ~ RC522_GetCardCount()-1）
 * @param  pCardInfo: 输出卡片信息
 * @retval RC522_OK 成功；RC522_INVALID_PARAM 索引无效
 */
RC522_Status_t RC522_GetCardByIndex(uint8_t index, RC522_CardInfo_t *pCardInfo);

#endif // __RC522_H
