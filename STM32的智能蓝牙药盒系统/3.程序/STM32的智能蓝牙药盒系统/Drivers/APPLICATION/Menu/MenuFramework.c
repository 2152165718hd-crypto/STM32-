#include ".\APPLICATION\Menu\MenuFramework.h"
#include <stdio.h>

/**
 * @file MenuFramework.c
 * @brief 通用菜单框架实现。
 * @note 负责菜单导航、数值编辑、自定义页面切换以及 OLED 菜单绘制。
 */

#define MF_TEXT_ON "\xE5\xBC\x80"
#define MF_TEXT_OFF "\xE5\x85\xB3"
#define MF_TEXT_EDIT_TITLE "\xE5\x8F\x82\xE6\x95\xB0\xE8\xAE\xBE\xE7\xBD\xAE"
#define MF_TEXT_EDIT_HINT "\xE4\xB8\x8A\xE4\xB8\x8B\xE8\xB0\x83\xE6\x95\xB4\xE7\xA1\xAE\xE8\xAE\xA4\xE8\xBF\x94\xE5\x9B\x9E"

/* ==================== 内部状态 ==================== */

/** @brief 菜单框架的导航模式。 */
typedef enum
{
    NAV_MENU = 0,    // 正常菜单列表浏览
    NAV_CUSTOM_PAGE, // 自定义整页模式
    NAV_VALUE_EDIT,  // 数值编辑模式
} NavMode_t;

/** @brief 导航栈中的单层记录。 */
typedef struct
{
    MF_Menu_t *menu;
} NavFrame_t;

/* --- 静态存储池 --- */
static MF_Menu_t menu_pool[MF_MAX_MENUS];
static uint8_t menu_pool_count = 0;

/* --- 导航栈 --- */
static NavFrame_t nav_stack[MF_NAV_STACK_DEPTH];
static int8_t nav_depth = -1; // 当前栈顶索引, -1 表示空

/* --- 当前状态 --- */
static MF_Menu_t *cur_menu = NULL;
static NavMode_t nav_mode = NAV_MENU;
static MF_CursorStyle_t cursor_style = MF_CURSOR_REVERSE;

/* --- 动画 --- */
static float anim_cursor_y = 0.0f;

/* --- 自定义页面临时存储 --- */
static MF_CustomPageCb custom_page_cb = NULL;

/* --- 数值编辑临时存储 --- */
static MF_Item_t *editing_item = NULL;

/* ==================== 内部函数 ==================== */

/**
 * @brief 获取当前菜单中被选中的条目。
 * @return 当前选中条目指针；若菜单为空则返回 `NULL`。
 */
static MF_Item_t *MF_GetSelectedItem(void)
{
    if (!cur_menu || cur_menu->item_count == 0)
        return NULL;
    return &cur_menu->items[cur_menu->select_index];
}

/**
 * @brief 进入子菜单并将当前菜单压入导航栈。
 * @param menu 目标子菜单。
 */
static void MF_PushMenu(MF_Menu_t *menu)
{
    if (nav_depth < MF_NAV_STACK_DEPTH - 1)
    {
        nav_depth++;
        nav_stack[nav_depth].menu = cur_menu;
    }
    cur_menu = menu;
    /* 不重置子菜单的选中状态，保留用户上次位置 */
    anim_cursor_y = MF_TITLE_HEIGHT; // 重置动画起点
}

/**
 * @brief 从导航栈弹出上一层菜单。
 * @note 若当前已经位于根菜单，则不会发生任何切换。
 */
static void MF_PopMenu(void)
{
    if (nav_depth >= 0)
    {
        cur_menu = nav_stack[nav_depth].menu;
        nav_depth--;
        /* 恢复动画位置 */
        int8_t rel = cur_menu->select_index - cur_menu->scroll_offset;
        anim_cursor_y = (float)(MF_TITLE_HEIGHT + rel * MF_LINE_HEIGHT);
    }
}

/**
 * @brief 更新滚动偏移，确保选中项始终处于可见区域。
 * @param m 当前菜单对象。
 */
static void MF_UpdateScroll(MF_Menu_t *m)
{
    if (m->select_index >= m->scroll_offset + MF_DISPLAY_LINES)
        m->scroll_offset = m->select_index - MF_DISPLAY_LINES + 1;
    if (m->select_index < m->scroll_offset)
        m->scroll_offset = m->select_index;

    if (m->scroll_offset < 0)
        m->scroll_offset = 0;

    int max_offset = (int)m->item_count - MF_DISPLAY_LINES;
    if (max_offset < 0)
        max_offset = 0;
    if (m->scroll_offset > max_offset)
        m->scroll_offset = max_offset;
}

/**
 * @brief 按当前样式绘制菜单光标。
 * @param y 光标左上角 Y 坐标。
 * @param width 光标宽度。
 * @param height 光标高度。
 */
static void MF_DrawCursor(int y, int width, int height)
{
    if (cursor_style == MF_CURSOR_REVERSE)
    {
        OLED_ReverseArea(0, y, width, height);
    }
    else
    {
        OLED_DrawRectangle(MF_CURSOR_PADDING, y + MF_CURSOR_PADDING,
                           width - 2 * MF_CURSOR_PADDING,
                           height - 2 * MF_CURSOR_PADDING,
                           OLED_UNFILLED);
    }
}

/**
 * @brief 绘制菜单标题栏。
 * @param title 标题文本。
 */
static void MF_DrawTitle(const char *title)
{
    OLED_ShowString(0, 0, (char *)title, MF_FONT);
    OLED_DrawLine(0, MF_TITLE_HEIGHT, MF_SCREEN_WIDTH, MF_TITLE_HEIGHT);
}

/** @brief 菜单行的临时显示缓冲区。 */
static char line_buf[32]; // 行文本缓冲

/**
 * @brief 渲染当前菜单页的列表区域。
 * @note 该函数同时完成标题、条目、滚动箭头与光标动画的绘制。
 */
static void MF_RenderMenuList(void)
{
    MF_Menu_t *m = cur_menu;

    // 绘制标题
    MF_DrawTitle(m->title);

    // 绘制菜单条目
    for (int i = 0; i < MF_DISPLAY_LINES; i++)
    {
        int idx = m->scroll_offset + i;
        if (idx >= m->item_count)
            break;

        MF_Item_t *item = &m->items[idx];
        int y = MF_TITLE_HEIGHT + i * MF_LINE_HEIGHT;

        switch (item->type)
        {
        case MF_ITEM_SUBMENU:
            // "文本 >"
            snprintf(line_buf, sizeof(line_buf), "%s >", item->text);
            OLED_ShowString(4, y, line_buf, MF_FONT);
            break;

        case MF_ITEM_ACTION:
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            break;

        case MF_ITEM_TOGGLE:
        {
            uint8_t state = item->toggle.bool_ptr ? *(item->toggle.bool_ptr) : 0;
            snprintf(line_buf, sizeof(line_buf), "%s:%s", item->text, state ? MF_TEXT_ON : MF_TEXT_OFF);
            OLED_ShowString(4, y, line_buf, MF_FONT);
            break;
        }

        case MF_ITEM_VALUE:
        {
            int32_t val = item->value.value_ptr ? *(item->value.value_ptr) : 0;
            const char *u = item->value.unit ? item->value.unit : "";
            snprintf(line_buf, sizeof(line_buf), "%s:%ld%s", item->text, (long)val, u);
            OLED_ShowString(4, y, line_buf, MF_FONT);
            break;
        }

        case MF_ITEM_CUSTOM_PAGE:
            snprintf(line_buf, sizeof(line_buf), "%s >", item->text);
            OLED_ShowString(4, y, line_buf, MF_FONT);
            break;
        }
    }

    // 滚动指示箭头
    if (m->item_count > MF_DISPLAY_LINES)
    {
        if (m->scroll_offset > 0)
            OLED_ShowChar(120, MF_TITLE_HEIGHT, '^', MF_FONT);
        if (m->scroll_offset < m->item_count - MF_DISPLAY_LINES)
            OLED_ShowChar(120, MF_SCREEN_HEIGHT - MF_LINE_HEIGHT, 'v', MF_FONT);
    }

    // 光标动画
    int8_t rel = m->select_index - m->scroll_offset;
    float target_y = MF_TITLE_HEIGHT + rel * MF_LINE_HEIGHT;
    anim_cursor_y += (target_y - anim_cursor_y) * MF_ANIM_SPEED;
    MF_DrawCursor((int)anim_cursor_y, MF_SCREEN_WIDTH, MF_LINE_HEIGHT);
}

/* ==================== 菜单列表模式处理 ==================== */
/**
 * @brief 处理普通菜单浏览模式下的按键逻辑。
 * @param key 当前扫描到的按键事件。
 */
static void MF_HandleMenuMode(KeyEvent_t key)
{
    MF_Menu_t *m = cur_menu;
    if (!m || m->item_count == 0)
        return;

    /* --- 按键处理 --- */
    if (key == KEY_DOWN)
    {
        m->select_index++;
        if (m->select_index >= m->item_count)
            m->select_index = 0;
        MF_UpdateScroll(m);
    }
    else if (key == KEY_UP)
    {
        m->select_index--;
        if (m->select_index < 0)
            m->select_index = m->item_count - 1;
        MF_UpdateScroll(m);
    }
    else if (key == KEY_ENTER)
    {
        MF_Item_t *item = MF_GetSelectedItem();
        if (!item)
            return;

        switch (item->type)
        {
        case MF_ITEM_SUBMENU:
            if (item->submenu.child_menu)
                MF_PushMenu(item->submenu.child_menu);
            break;

        case MF_ITEM_ACTION:
            if (item->action.callback)
                item->action.callback();
            break;

        case MF_ITEM_TOGGLE:
            if (item->toggle.bool_ptr)
            {
                *(item->toggle.bool_ptr) ^= 1;
                if (item->toggle.on_change)
                    item->toggle.on_change();
            }
            break;

        case MF_ITEM_VALUE:
            // 进入数值编辑模式
            nav_mode = NAV_VALUE_EDIT;
            editing_item = item;
            break;

        case MF_ITEM_CUSTOM_PAGE:
            if (item->custom.render)
            {
                nav_mode = NAV_CUSTOM_PAGE;
                custom_page_cb = item->custom.render;
            }
            break;
        }
    }
    else if (key == KEY_BACK)
    {
        MF_PopMenu();
    }

    /* --- 渲染 --- */
    MF_RenderMenuList();
}

/* ==================== 数值编辑模式 ==================== */
/**
 * @brief 处理数值编辑模式。
 * @param key 当前扫描到的按键事件。
 * @note 上下键调整绑定变量，确认键或返回键退出编辑模式。
 */
static void MF_HandleValueEdit(KeyEvent_t key)
{
    if (!editing_item || !editing_item->value.value_ptr)
    {
        nav_mode = NAV_MENU;
        return;
    }

    int32_t *pval = editing_item->value.value_ptr;
    int32_t step = editing_item->value.step;

    if (key == KEY_UP)
    {
        *pval += step;
        if (*pval > editing_item->value.max_val)
            *pval = editing_item->value.max_val;
        if (editing_item->value.on_change)
            editing_item->value.on_change(*pval);
    }
    else if (key == KEY_DOWN)
    {
        *pval -= step;
        if (*pval < editing_item->value.min_val)
            *pval = editing_item->value.min_val;
        if (editing_item->value.on_change)
            editing_item->value.on_change(*pval);
    }
    else if (key == KEY_ENTER || key == KEY_BACK)
    {
        nav_mode = NAV_MENU;
        editing_item = NULL;
        MF_RenderMenuList();
        return;
    }

    /* --- 渲染编辑界面 --- */
    const char *u = editing_item->value.unit ? editing_item->value.unit : "";
    MF_DrawTitle(MF_TEXT_EDIT_TITLE);

    OLED_ShowString(0, MF_TITLE_HEIGHT, (char *)editing_item->text, MF_FONT);

    snprintf(line_buf, sizeof(line_buf), "< %ld%s >", (long)*pval, u);
    OLED_ShowString(0, MF_TITLE_HEIGHT + MF_LINE_HEIGHT, line_buf, MF_FONT);

    // 底部提示
    OLED_ShowString(0, MF_TITLE_HEIGHT + MF_LINE_HEIGHT * 2, MF_TEXT_EDIT_HINT, MF_FONT);
}

/* ==================== 自定义页面模式 ==================== */
/**
 * @brief 处理自定义整页模式。
 * @param key 当前扫描到的按键事件。
 */
static void MF_HandleCustomPage(KeyEvent_t key)
{
    if (!custom_page_cb)
    {
        nav_mode = NAV_MENU;
        MF_RenderMenuList();
        return;
    }

    uint8_t exit_flag = 0;
    custom_page_cb(key, &exit_flag);

    if (exit_flag) 
    {
        nav_mode = NAV_MENU;
        custom_page_cb = NULL;
        MF_RenderMenuList();
    }
}

/* ==================== 公开 API 实现 ==================== */

/**
 * @brief 从静态菜单池中分配并初始化一个菜单页。
 * @param title 菜单标题。
 * @return 新菜单页指针；菜单池耗尽时返回 `NULL`。
 */
MF_Menu_t *MF_CreateMenu(const char *title)
{
    if (menu_pool_count >= MF_MAX_MENUS)
        return NULL;

    MF_Menu_t *m = &menu_pool[menu_pool_count++];
    memset(m, 0, sizeof(MF_Menu_t));
    m->title = title;
    m->item_count = 0;
    m->select_index = 0;
    m->scroll_offset = 0;
    return m;
}

/**
 * @brief 向父菜单追加一个子菜单入口条目。
 * @param parent 父菜单。
 * @param text 条目文本。
 * @param child 子菜单对象。
 */
void MF_AddSubmenu(MF_Menu_t *parent, const char *text, MF_Menu_t *child)
{
    if (!parent || parent->item_count >= MF_MAX_ITEMS_PER_MENU)
        return;
    MF_Item_t *it = &parent->items[parent->item_count++];
    memset(it, 0, sizeof(MF_Item_t));
    it->text = text;
    it->type = MF_ITEM_SUBMENU;
    it->submenu.child_menu = child;
}

/**
 * @brief 向父菜单追加一个动作条目。
 * @param parent 父菜单。
 * @param text 条目文本。
 * @param cb 触发后的回调。
 */
void MF_AddAction(MF_Menu_t *parent, const char *text, MF_ActionCb cb)
{
    if (!parent || parent->item_count >= MF_MAX_ITEMS_PER_MENU)
        return;
    MF_Item_t *it = &parent->items[parent->item_count++];
    memset(it, 0, sizeof(MF_Item_t));
    it->text = text;
    it->type = MF_ITEM_ACTION;
    it->action.callback = cb;
}

/**
 * @brief 向父菜单追加一个开关条目。
 * @param parent 父菜单。
 * @param text 条目文本。
 * @param bool_ptr 绑定的开关变量地址。
 * @param on_change 状态变化后的回调，可为 `NULL`。
 */
void MF_AddToggle(MF_Menu_t *parent, const char *text, uint8_t *bool_ptr, MF_ActionCb on_change)
{
    if (!parent || parent->item_count >= MF_MAX_ITEMS_PER_MENU)
        return;
    MF_Item_t *it = &parent->items[parent->item_count++];
    memset(it, 0, sizeof(MF_Item_t));
    it->text = text;
    it->type = MF_ITEM_TOGGLE;
    it->toggle.bool_ptr = bool_ptr;
    it->toggle.on_change = on_change;
}

/**
 * @brief 向父菜单追加一个数值调节条目。
 * @param parent 父菜单。
 * @param text 条目文本。
 * @param value_ptr 绑定的数值变量地址。
 * @param min_v 最小值。
 * @param max_v 最大值。
 * @param step 每次调整步长。
 * @param unit 单位字符串，可为 `NULL`。
 * @param on_change 数值变化回调，可为 `NULL`。
 */
void MF_AddValue(MF_Menu_t *parent, const char *text,
                 int32_t *value_ptr, int32_t min_v, int32_t max_v, int32_t step,
                 const char *unit, MF_ValueChangeCb on_change)
{
    if (!parent || parent->item_count >= MF_MAX_ITEMS_PER_MENU)
        return;
    MF_Item_t *it = &parent->items[parent->item_count++];
    memset(it, 0, sizeof(MF_Item_t));
    it->text = text;
    it->type = MF_ITEM_VALUE;
    it->value.value_ptr = value_ptr;
    it->value.min_val = min_v;
    it->value.max_val = max_v;
    it->value.step = step;
    it->value.unit = unit;
    it->value.on_change = on_change;
}

/**
 * @brief 向父菜单追加一个自定义整页条目。
 * @param parent 父菜单。
 * @param text 条目文本。
 * @param render_cb 自定义页面回调。
 */
void MF_AddCustomPage(MF_Menu_t *parent, const char *text, MF_CustomPageCb render_cb)
{
    if (!parent || parent->item_count >= MF_MAX_ITEMS_PER_MENU)
        return;
    MF_Item_t *it = &parent->items[parent->item_count++];
    memset(it, 0, sizeof(MF_Item_t));
    it->text = text;
    it->type = MF_ITEM_CUSTOM_PAGE;
    it->custom.render = render_cb;
}

/**
 * @brief 设置菜单光标样式。
 * @param style 光标样式枚举值。
 */
void MF_SetCursorStyle(MF_CursorStyle_t style)
{
    cursor_style = style;
}

/**
 * @brief 获取当前菜单光标样式。
 * @return 当前生效的光标样式。
 */
MF_CursorStyle_t MF_GetCursorStyle(void)
{
    return cursor_style;
}

/**
 * @brief 启动菜单框架并指定根菜单。
 * @param root_menu 根菜单指针。
 */
void MF_Start(MF_Menu_t *root_menu)
{
    cur_menu = root_menu;
    nav_depth = -1;
    nav_mode = NAV_MENU;
    anim_cursor_y = (float)MF_TITLE_HEIGHT;
}

/**
 * @brief 菜单框架的单次循环处理入口。
 * @note 调用顺序为“读取按键 -> 清屏 -> 分派模式处理 -> OLED 刷新”。
 */
void MF_Loop(void)
{
    KeyEvent_t key = Key_Scan();
    OLED_Clear();

    switch (nav_mode)
    {
    case NAV_MENU:
        MF_HandleMenuMode(key);
        break;
    case NAV_CUSTOM_PAGE:
        MF_HandleCustomPage(key);
        break;
    case NAV_VALUE_EDIT:
        MF_HandleValueEdit(key);
        break;
    }

    OLED_Update();
}
