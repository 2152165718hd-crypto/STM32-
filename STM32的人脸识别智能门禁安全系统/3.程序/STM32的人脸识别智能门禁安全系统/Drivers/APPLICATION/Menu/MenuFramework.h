#ifndef __MENU_FRAMEWORK_H
#define __MENU_FRAMEWORK_H

#include <stdint.h>
#include <string.h>
#include ".\Hardware\OLED\OLED.h" // 你的OLED驱动
#include ".\Hardware\Key\Key.h"  // 你的按键驱动

/* ==================== 可配置参数 ==================== */
#define MF_MAX_MENUS 20          // 最大菜单页数
#define MF_MAX_ITEMS_PER_MENU 10 // 每个菜单页最大条目数
#define MF_NAV_STACK_DEPTH 8     // 导航栈深度（支持多少层子菜单）
#define MF_DISPLAY_LINES 3       // 屏幕同时显示的菜单行数
#define MF_LINE_HEIGHT 16        // 每行高度(像素)
#define MF_TITLE_HEIGHT 16       // 标题栏高度(像素)
#define MF_SCREEN_WIDTH 128
#define MF_SCREEN_HEIGHT 64
#define MF_FONT OLED_8X16
#define MF_ANIM_SPEED 0.3f // 光标动画速度 (0~1)
#define MF_CURSOR_PADDING 1

/* ==================== 类型定义 ==================== */

/** 光标样式 */
typedef enum
{
    MF_CURSOR_REVERSE = 0, // 反色高亮
    MF_CURSOR_BOX,         // 方框
} MF_CursorStyle_t;

/** 菜单条目类型 */
typedef enum
{
    MF_ITEM_SUBMENU = 0, // 子菜单入口 → 点击后跳转到另一个菜单页
    MF_ITEM_ACTION,      // 动作按钮 → 点击后执行回调
    MF_ITEM_TOGGLE,      // 开关切换 → 点击后翻转 bool 值
    MF_ITEM_VALUE,       // 数值显示/调节 → 上下键可调数值
    MF_ITEM_CUSTOM_PAGE, // 自定义整页 → 进入后由用户回调接管整个屏幕
} MF_ItemType_t;

/* 前置声明 */
typedef struct MF_Menu_s MF_Menu_t;

/** 回调函数类型 */
typedef void (*MF_ActionCb)(void);                                   // 动作回调
typedef void (*MF_CustomPageCb)(KeyEvent_t key, uint8_t *exit_flag); // 自定义页面回调
typedef void (*MF_ValueChangeCb)(int32_t new_value);                 // 数值变更回调

/** 菜单条目 */
typedef struct
{
    const char *text;   // 显示文本
    MF_ItemType_t type; // 条目类型

    /* 根据 type 使用不同字段 */
    union
    {
        /* MF_ITEM_SUBMENU */
        struct
        {
            MF_Menu_t *child_menu; // 指向子菜单
        } submenu;

        /* MF_ITEM_ACTION */
        struct
        {
            MF_ActionCb callback;
        } action;

        /* MF_ITEM_TOGGLE */
        struct
        {
            uint8_t *bool_ptr;     // 指向用户的 bool 变量
            MF_ActionCb on_change; // 切换后回调 (可为NULL)
        } toggle;

        /* MF_ITEM_VALUE */
        struct
        {
            int32_t *value_ptr; // 指向用户的数值变量
            int32_t min_val;
            int32_t max_val;
            int32_t step;
            const char *unit; // 单位字符串, 如 "C", "%"
            MF_ValueChangeCb on_change;
        } value;

        /* MF_ITEM_CUSTOM_PAGE */
        struct
        {
            MF_CustomPageCb render; // 用户自定义渲染回调
        } custom;
    };
} MF_Item_t;

/** 菜单页 */
struct MF_Menu_s
{
    const char *title; // 菜单标题
    MF_Item_t items[MF_MAX_ITEMS_PER_MENU];
    uint8_t item_count;   // 当前条目数
    int8_t select_index;  // 选中索引
    int8_t scroll_offset; // 滚动偏移
};

/* ==================== 公开 API ==================== */

/**
 * @brief  创建一个菜单页
 * @param  title  菜单标题
 * @return 菜单指针, 失败返回 NULL
 */
MF_Menu_t *MF_CreateMenu(const char *title);

/**
 * @brief  添加子菜单入口条目
 */
void MF_AddSubmenu(MF_Menu_t *parent, const char *text, MF_Menu_t *child);

/**
 * @brief  添加动作按钮条目
 */
void MF_AddAction(MF_Menu_t *parent, const char *text, MF_ActionCb cb);

/**
 * @brief  添加开关切换条目
 */
void MF_AddToggle(MF_Menu_t *parent, const char *text, uint8_t *bool_ptr, MF_ActionCb on_change);

/**
 * @brief  添加数值调节条目
 */
void MF_AddValue(MF_Menu_t *parent, const char *text,
                 int32_t *value_ptr, int32_t min_v, int32_t max_v, int32_t step,
                 const char *unit, MF_ValueChangeCb on_change);

/**
 * @brief  添加自定义整页条目（进入后完全由回调绘制）
 */
void MF_AddCustomPage(MF_Menu_t *parent, const char *text, MF_CustomPageCb render_cb);

/**
 * @brief  设置光标样式
 */
void MF_SetCursorStyle(MF_CursorStyle_t style);

/**
 * @brief  获取当前光标样式
 */
MF_CursorStyle_t MF_GetCursorStyle(void);

/**
 * @brief  启动菜单系统 (设定根菜单)
 */
void MF_Start(MF_Menu_t *root_menu);

/**
 * @brief  外部事件强制进入自定义页面（不改变菜单栈）
 *         用于红外检测到人→自动跳转人脸识别、RFID感应→自动跳转识别等场景
 * @param  render_cb  自定义页面渲染回调
 */
void MF_EnterCustomPage(MF_CustomPageCb render_cb);

/**
 * @brief  获取当前活动的自定义页面回调指针
 * @return 当前自定义页面回调, 无则返回 NULL
 */
MF_CustomPageCb MF_GetCustomPageCb(void);

/**
 * @brief  判断当前是否在主菜单
 * @return 1=是, 0=否
 */
uint8_t MF_IsMainMenu(void);

/**
 * @brief  菜单主循环 (在 while(1) 中调用)
 *         内部完成: 按键扫描 → 逻辑处理 → 渲染 → OLED刷新
 */
void MF_Loop(void);

#endif /* __MENU_FRAMEWORK_H */
