#ifndef __MENU_FRAMEWORK_H
#define __MENU_FRAMEWORK_H

#include <stdint.h>
#include <string.h>
#include "Hardware/OLED/OLED.h" /* OLED 显示驱动 */
#include "Hardware/Key/Key.h"   /* 按键驱动     */

/* ==================== 可配置参数 ==================== */
#define MF_MAX_MENUS          2    /**< 最大菜单页数                      */
#define MF_MAX_ITEMS_PER_MENU 6   /**< 每个菜单页最大条目数              */
#define MF_NAV_STACK_DEPTH    1    /**< 导航栈深度（支持多少层子菜单）    */
#define MF_DISPLAY_LINES      3    /**< 屏幕同时显示的菜单行数            */
#define MF_LINE_HEIGHT        16   /**< 每行高度 (像素)                   */
#define MF_TITLE_HEIGHT       16   /**< 标题栏高度 (像素)                 */
#define MF_SCREEN_WIDTH       128  /**< 屏幕宽度 (像素)                   */
#define MF_SCREEN_HEIGHT      64   /**< 屏幕高度 (像素)                   */
#define MF_FONT               OLED_8X16  /**< 默认字体                   */
#define MF_FONT_WIDTH         8    /**< 默认字体单字符宽度 (像素)         */
#define MF_ANIM_SPEED         0.75f /**< 光标动画插值速度 (0~1)           */
#define MF_CURSOR_PADDING     1    /**< 方框光标内边距 (像素)             */
#define MF_LINE_BUF_SIZE      24   /**< 行渲染文本缓冲区大小             */

/* ==================== 类型定义 ==================== */

/** 光标样式 */
typedef enum
{
    MF_CURSOR_REVERSE = 0, /**< 反色高亮 */
    MF_CURSOR_BOX,         /**< 方框描边 */
} MF_CursorStyle_t;

/** 菜单条目类型 */
typedef enum
{
    MF_ITEM_SUBMENU = 0, /**< 子菜单入口 → 点击后跳转到另一个菜单页 */
    MF_ITEM_ACTION,      /**< 动作按钮   → 点击后执行回调           */
    MF_ITEM_TOGGLE,      /**< 开关切换   → 点击后翻转 bool 值       */
    MF_ITEM_VALUE,       /**< 数值调节   → 上下键可调数值           */
    MF_ITEM_CUSTOM_PAGE, /**< 自定义整页 → 进入后由用户回调接管屏幕 */
} MF_ItemType_t;

/* 前置声明 */
typedef struct MF_Menu_s MF_Menu_t;

/** 动作回调 */
typedef void (*MF_ActionCb)(void);
/** 自定义页面回调: key 为当前按键, *exit_flag 置 1 表示退出页面 */
typedef void (*MF_CustomPageCb)(KeyEvent_t key, uint8_t *exit_flag);
/** 数值变更回调: new_value 为变更后的值 */
typedef void (*MF_ValueChangeCb)(int32_t new_value);

/** 菜单条目 */
typedef struct
{
    const char   *text; /**< 显示文本 */
    MF_ItemType_t type; /**< 条目类型 */

    /** 根据 type 使用不同字段 (匿名联合) */
    union
    {
        /** MF_ITEM_SUBMENU */
        struct
        {
            MF_Menu_t *child_menu; /**< 指向子菜单 */
        } submenu;

        /** MF_ITEM_ACTION */
        struct
        {
            MF_ActionCb callback; /**< 点击回调 */
        } action;

        /** MF_ITEM_TOGGLE */
        struct
        {
            uint8_t    *bool_ptr;  /**< 指向用户的 bool 变量 */
            MF_ActionCb on_change; /**< 切换后回调 (可为 NULL) */
        } toggle;

        /** MF_ITEM_VALUE */
        struct
        {
            int32_t       *value_ptr; /**< 指向用户的数值变量        */
            int32_t        min_val;   /**< 最小值                    */
            int32_t        max_val;   /**< 最大值                    */
            int32_t        step;      /**< 步进量                    */
            const char    *unit;      /**< 单位字符串, 如 "C", "%"   */
            MF_ValueChangeCb on_change; /**< 数值变更后回调          */
        } value;

        /** MF_ITEM_CUSTOM_PAGE */
        struct
        {
            MF_CustomPageCb render; /**< 用户自定义渲染回调 */
        } custom;
    };
} MF_Item_t;

/** 菜单页 */
struct MF_Menu_s
{
    const char *title;                    /**< 菜单标题     */
    MF_Item_t   items[MF_MAX_ITEMS_PER_MENU]; /**< 条目数组 */
    uint8_t     item_count;               /**< 当前条目数   */
    int8_t      select_index;             /**< 选中索引     */
    int8_t      scroll_offset;            /**< 滚动偏移     */
};

/* ==================== 公开 API ==================== */

/**
 * @brief  创建一个菜单页 (从内部静态池分配)
 * @param  title  菜单标题 (const 字符串, 框架不拷贝)
 * @retval 菜单指针; 池满则返回 NULL
 */
MF_Menu_t *MF_CreateMenu(const char *title);

/**
 * @brief  添加子菜单入口条目
 * @param  parent 父菜单指针
 * @param  text   显示文本
 * @param  child  子菜单指针
 */
void MF_AddSubmenu(MF_Menu_t *parent, const char *text, MF_Menu_t *child);

/**
 * @brief  添加动作按钮条目
 * @param  parent 父菜单指针
 * @param  text   显示文本
 * @param  cb     点击后执行的回调
 */
void MF_AddAction(MF_Menu_t *parent, const char *text, MF_ActionCb cb);

/**
 * @brief  添加开关切换条目
 * @param  parent    父菜单指针
 * @param  text      显示文本
 * @param  bool_ptr  指向用户的 bool 变量 (0/1)
 * @param  on_change 切换后的回调 (可传 NULL)
 */
void MF_AddToggle(MF_Menu_t *parent, const char *text,
                  uint8_t *bool_ptr, MF_ActionCb on_change);

/**
 * @brief  添加数值调节条目
 * @param  parent    父菜单指针
 * @param  text      显示文本
 * @param  value_ptr 指向用户的 int32_t 变量
 * @param  min_v     最小值
 * @param  max_v     最大值
 * @param  step      步进量 (> 0)
 * @param  unit      单位字符串 (如 "C", "%", 可传 NULL)
 * @param  on_change 数值变更后回调 (可传 NULL)
 */
void MF_AddValue(MF_Menu_t *parent, const char *text,
                 int32_t *value_ptr, int32_t min_v, int32_t max_v, int32_t step,
                 const char *unit, MF_ValueChangeCb on_change);

/**
 * @brief  添加自定义整页条目 (进入后完全由回调绘制)
 * @param  parent    父菜单指针
 * @param  text      显示文本
 * @param  render_cb 自定义页面渲染回调
 */
void MF_AddCustomPage(MF_Menu_t *parent, const char *text, MF_CustomPageCb render_cb);

/**
 * @brief  设置光标样式
 * @param  style  MF_CURSOR_REVERSE 或 MF_CURSOR_BOX
 */
void MF_SetCursorStyle(MF_CursorStyle_t style);

/**
 * @brief  获取当前光标样式
 * @retval 当前样式枚举值
 */
MF_CursorStyle_t MF_GetCursorStyle(void);

/**
 * @brief  启动菜单系统 (设定根菜单)
 * @param  root_menu 根菜单指针
 */
void MF_Start(MF_Menu_t *root_menu);

/**
 * @brief  菜单主循环 (在 while(1) 中调用)
 *         内部完成: 按键扫描 → 逻辑处理 → 渲染 → OLED 刷新
 * @note   等价于 key = Key_Scan(); MF_Process(key); MF_Render();
 */
void MF_Loop(void);

/* ----------- 解耦 API (高级用法) ----------- */

/**
 * @brief  处理一次按键输入 (纯逻辑, 不涉及扫描和渲染)
 * @param  key  按键事件
 * @note   适合外部自行管理按键源与刷新节奏时使用
 */
void MF_Process(KeyEvent_t key);

/**
 * @brief  执行一次渲染 (OLED_Clear → 绘制 → OLED_Update)
 * @note   在 MF_Process() 后调用以刷新画面
 */
void MF_Render(void);

/**
 * @brief  获取当前活动菜单指针
 * @retval 当前菜单指针, 未初始化时返回 NULL
 */
MF_Menu_t *MF_GetCurrentMenu(void);

/**
 * @brief  获取当前导航栈深度
 * @retval 深度值 (0 = 根菜单, -1 = 未初始化)
 */
int8_t MF_GetNavDepth(void);

/**
 * @brief  完全重置菜单系统 (清空菜单池 + 导航栈)
 * @note   调用后需重新 MF_CreateMenu / MF_Start
 */
void MF_Reset(void);

#endif /* __MENU_FRAMEWORK_H */
