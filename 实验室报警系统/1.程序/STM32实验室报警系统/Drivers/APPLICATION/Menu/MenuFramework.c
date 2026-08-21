#include ".\APPLICATION\Menu\MenuFramework.h"
#include <stdio.h>

/* ==================== 内部状态 ==================== */

/** 导航模式 */
typedef enum
{
    NAV_MENU = 0,    // 正常菜单列表浏览
    NAV_CUSTOM_PAGE, // 自定义整页模式
    NAV_VALUE_EDIT,  // 数值编辑模式
} NavMode_t;

/** 导航栈帧 */
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

/** 获取当前菜单的当前选中条目 */
static MF_Item_t *MF_GetSelectedItem(void)
{
    if (!cur_menu || cur_menu->item_count == 0)
        return NULL;
    return &cur_menu->items[cur_menu->select_index];
}

/** 压栈：进入子菜单 */
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

/** 弹栈：返回上级 */
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

/** 更新滚动偏移，保证选中项可见 */
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

/** 绘制光标 */
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

/** 渲染标题栏 */
static void MF_DrawTitle(const char *title)
{
    OLED_ShowString(0, 0, (char *)title, MF_FONT);
    OLED_DrawLine(0, MF_TITLE_HEIGHT, MF_SCREEN_WIDTH, MF_TITLE_HEIGHT);
}

/** 构造显示文本（带状态后缀） */
static char line_buf[32]; // 行文本缓冲

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
            snprintf(line_buf, sizeof(line_buf), "%s: %s", item->text, state ? "ON" : "OFF");
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
    MF_DrawTitle("Set Value");

    OLED_ShowString(0, MF_TITLE_HEIGHT + 2, (char *)editing_item->text, OLED_6X8);

    snprintf(line_buf, sizeof(line_buf), "Value:<%ld%s>", (long)*pval, u);
    OLED_ShowString(0, MF_TITLE_HEIGHT + 14, line_buf, OLED_8X16);

    snprintf(line_buf, sizeof(line_buf), "Range:%ld~%ld", (long)editing_item->value.min_val, (long)editing_item->value.max_val);
    OLED_ShowString(0, MF_TITLE_HEIGHT + 34, line_buf, OLED_6X8);
}

/* ==================== 自定义页面模式 ==================== */
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

void MF_SetCursorStyle(MF_CursorStyle_t style)
{
    cursor_style = style;
}

MF_CursorStyle_t MF_GetCursorStyle(void)
{
    return cursor_style;
}

void MF_Start(MF_Menu_t *root_menu)
{
    cur_menu = root_menu;
    nav_depth = -1;
    nav_mode = NAV_MENU;
    anim_cursor_y = (float)MF_TITLE_HEIGHT;
}

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

