#ifndef __MENU_FRAMEWORK_H
#define __MENU_FRAMEWORK_H

/**
 * @file MenuFramework.h
 * @brief 通用 OLED 菜单框架接口定义。
 * @note 该框架负责菜单树组织、按键导航、数值编辑与自定义页面切换。
 */

#include <stdint.h>
#include <string.h>
#include ".\Hardware\OLED\OLED.h"
#include ".\Hardware\Key\Key.h"

/* ==================== 可配置参数 ==================== */
#define MF_MAX_MENUS 30          // 最大菜单页数
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

/** @brief 光标显示样式。 */
typedef enum
{
    MF_CURSOR_REVERSE = 0, // 反色高亮
    MF_CURSOR_BOX,         // 方框
} MF_CursorStyle_t;

/** @brief 菜单条目类型。 */
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

/** @brief 动作类条目的回调函数类型。 */
typedef void (*MF_ActionCb)(void);                                   // 动作回调
/** @brief 自定义整页渲染回调函数类型。 */
typedef void (*MF_CustomPageCb)(KeyEvent_t key, uint8_t *exit_flag); // 自定义页面回调
/** @brief 数值类条目发生变化时的回调函数类型。 */
typedef void (*MF_ValueChangeCb)(int32_t new_value);                 // 数值变更回调

/** @brief 菜单条目描述结构。 */
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

/** @brief 菜单页对象。 */
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
 * @brief 创建一个菜单页。
 * @param title 菜单标题。
 * @return 新建菜单页指针；当静态菜单池已满时返回 `NULL`。
 */
MF_Menu_t *MF_CreateMenu(const char *title);

/**
 * @brief 添加子菜单入口条目。
 * @param parent 父菜单指针。
 * @param text 条目显示文本。
 * @param child 子菜单指针。
 */
void MF_AddSubmenu(MF_Menu_t *parent, const char *text, MF_Menu_t *child);

/**
 * @brief 添加动作按钮条目。
 * @param parent 父菜单指针。
 * @param text 条目显示文本。
 * @param cb 触发条目后的执行回调。
 */
void MF_AddAction(MF_Menu_t *parent, const char *text, MF_ActionCb cb);

/**
 * @brief 添加开关切换条目。
 * @param parent 父菜单指针。
 * @param text 条目显示文本。
 * @param bool_ptr 绑定的开关变量地址。
 * @param on_change 状态变化后执行的回调，可为 `NULL`。
 */
void MF_AddToggle(MF_Menu_t *parent, const char *text, uint8_t *bool_ptr, MF_ActionCb on_change);

/**
 * @brief 添加数值调节条目。
 * @param parent 父菜单指针。
 * @param text 条目显示文本。
 * @param value_ptr 绑定的数值变量地址。
 * @param min_v 允许的最小值。
 * @param max_v 允许的最大值。
 * @param step 每次按键调整步长。
 * @param unit 数值显示单位，可为 `NULL`。
 * @param on_change 数值变化后的回调，可为 `NULL`。
 */
void MF_AddValue(MF_Menu_t *parent, const char *text,
                 int32_t *value_ptr, int32_t min_v, int32_t max_v, int32_t step,
                 const char *unit, MF_ValueChangeCb on_change);

/**
 * @brief 添加自定义整页条目。
 * @param parent 父菜单指针。
 * @param text 条目显示文本。
 * @param render_cb 进入页面后的渲染与按键处理回调。
 */
void MF_AddCustomPage(MF_Menu_t *parent, const char *text, MF_CustomPageCb render_cb);

/**
 * @brief 设置光标样式。
 * @param style 目标光标样式。
 */
void MF_SetCursorStyle(MF_CursorStyle_t style);

/**
 * @brief 获取当前光标样式。
 * @return 当前使用的光标样式。
 */
MF_CursorStyle_t MF_GetCursorStyle(void);

/**
 * @brief 启动菜单系统。
 * @param root_menu 根菜单指针。
 * @note 调用后菜单导航会从根菜单开始，导航栈与动画状态会被复位。
 */
void MF_Start(MF_Menu_t *root_menu);

/**
 * @brief 菜单主循环。
 * @note 需要在 `while(1)` 中周期调用，内部完成按键扫描、状态处理、界面绘制与 OLED 刷新。
 */
void MF_Loop(void);

#endif /* __MENU_FRAMEWORK_H */
