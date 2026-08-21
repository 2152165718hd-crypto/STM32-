#include "APPLICATION/Menu/MenuFramework.h"
#include <stdio.h>

/* ==================== 内部状态 ==================== */

/** 导航模式 */
typedef enum
{
    NAV_MENU = 0,    /**< 正常菜单列表浏览   */
    NAV_CUSTOM_PAGE, /**< 自定义整页模式     */
    NAV_VALUE_EDIT,  /**< 数值编辑模式       */
} NavMode_t;

/** 导航栈帧 */
typedef struct
{
    MF_Menu_t *menu;
} NavFrame_t;

/* --- 静态存储池 --- */
static MF_Menu_t menu_pool[MF_MAX_MENUS];
static uint8_t   menu_pool_count = 0;

/* --- 导航栈 --- */
static NavFrame_t nav_stack[MF_NAV_STACK_DEPTH];
static int8_t     nav_depth = -1; /* 当前栈顶索引, -1 表示空 */

/* --- 当前状态 --- */
static MF_Menu_t       *cur_menu     = NULL;
static NavMode_t        nav_mode     = NAV_MENU;
static MF_CursorStyle_t cursor_style = MF_CURSOR_REVERSE;

/* --- 动画 --- */
static float anim_cursor_y = 0.0f;

/* --- 自定义页面临时存储 --- */
static MF_CustomPageCb custom_page_cb = NULL;

/* --- 数值编辑临时存储 --- */
static MF_Item_t *editing_item   = NULL;
static int32_t    edit_backup    = 0; /* 进入编辑前的备份值 */

/* --- 行渲染缓冲区 --- */
static char line_buf[MF_LINE_BUF_SIZE];

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief  获取当前菜单的当前选中条目
 * @retval 条目指针; 菜单为空时返回 NULL
 */
static MF_Item_t *MF_GetSelectedItem(void)
{
    if (!cur_menu || cur_menu->item_count == 0)
        return NULL;
    return &cur_menu->items[cur_menu->select_index];
}

/**
 * @brief  计算字符串像素宽度 (等宽字体)
 */
static int MF_StrPixelWidth(const char *s)
{
    int len = 0;
    while (*s++) len++;
    return len * MF_FONT_WIDTH;
}

/**
 * @brief  压栈：进入子菜单
 * @note   栈满时本次操作将被 **丢弃**，不会修改 cur_menu
 */
static void MF_PushMenu(MF_Menu_t *menu)
{
    if (nav_depth >= MF_NAV_STACK_DEPTH - 1)
        return; /* 栈满 — 安全退出 */

    nav_depth++;
    nav_stack[nav_depth].menu = cur_menu;
    cur_menu = menu;

    /* 重置动画起点, 不重置子菜单的选中状态 */
    anim_cursor_y = (float)MF_TITLE_HEIGHT;
}

/**
 * @brief  弹栈：返回上级
 */
static void MF_PopMenu(void)
{
    if (nav_depth < 0)
        return; /* 已在根菜单 — 无操作 */

    cur_menu = nav_stack[nav_depth].menu;
    nav_depth--;

    /* 恢复光标动画位置 */
    int8_t rel = cur_menu->select_index - cur_menu->scroll_offset;
    anim_cursor_y = (float)(MF_TITLE_HEIGHT + rel * MF_LINE_HEIGHT);
}

/**
 * @brief  更新滚动偏移，保证选中项可见
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

/* ==================== 渲染辅助 ==================== */

/**
 * @brief  绘制光标（反色或方框）
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
                           width  - 2 * MF_CURSOR_PADDING,
                           height - 2 * MF_CURSOR_PADDING,
                           OLED_UNFILLED);
    }
}

/**
 * @brief  渲染标题栏（居中 + 分隔线）
 */
static void MF_DrawTitle(const char *title)
{
    int text_w = MF_StrPixelWidth(title);
    int x = (MF_SCREEN_WIDTH - text_w) / 2;
    if (x < 0) x = 0;

    OLED_ShowString(x, 0, (char *)title, MF_FONT);
    OLED_DrawLine(0, MF_TITLE_HEIGHT, MF_SCREEN_WIDTH, MF_TITLE_HEIGHT);
}

/**
 * @brief  绘制进度条 (用于数值编辑界面)
 * @param  x, y      左上角坐标
 * @param  w, h      外框尺寸
 * @param  ratio     当前比例 (0.0 ~ 1.0)
 */
static void MF_DrawProgressBar(int x, int y, int w, int h, float ratio)
{
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    /* 外框 */
    OLED_DrawRectangle(x, y, w, h, OLED_UNFILLED);

    /* 内部填充 (留 1px 内边距) */
    int fill_w = (int)((w - 2) * ratio);
    if (fill_w > 0)
        OLED_DrawRectangle(x + 1, y + 1, fill_w, h - 2, OLED_FILLED);
}

/**
 * @brief  渲染菜单列表（含动画光标）
 */
static void MF_RenderMenuList(void)
{
    MF_Menu_t *m = cur_menu;

    /* 标题 */
    MF_DrawTitle(m->title);

    /* 菜单条目 */
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
            snprintf(line_buf, sizeof(line_buf), "%s >", item->text);
            OLED_ShowString(4, y, line_buf, MF_FONT);
            break;

        case MF_ITEM_ACTION:
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            break;

        case MF_ITEM_TOGGLE:
        {
            uint8_t state = item->toggle.bool_ptr ? *(item->toggle.bool_ptr) : 0;
            /* 左侧文本 */
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            /* 右侧状态 (右对齐) */
            const char *state_str = state ? "ON" : "OFF";
            int sw = MF_StrPixelWidth(state_str);
            OLED_ShowString(MF_SCREEN_WIDTH - sw - 4, y, (char *)state_str, MF_FONT);
            break;
        }

        case MF_ITEM_VALUE:
        {
            int32_t val = item->value.value_ptr ? *(item->value.value_ptr) : 0;
            const char *u = item->value.unit ? item->value.unit : "";
            /* 左侧条目名 */
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            /* 右侧数值 (右对齐) */
            snprintf(line_buf, sizeof(line_buf), "%ld%s", (long)val, u);
            int vw = MF_StrPixelWidth(line_buf);
            OLED_ShowString(MF_SCREEN_WIDTH - vw - 4, y, line_buf, MF_FONT);
            break;
        }

        case MF_ITEM_CUSTOM_PAGE:
            snprintf(line_buf, sizeof(line_buf), "%s >", item->text);
            OLED_ShowString(4, y, line_buf, MF_FONT);
            break;
        }
    }

    /* 滚动指示箭头 */
    if (m->item_count > MF_DISPLAY_LINES)
    {
        if (m->scroll_offset > 0)
            OLED_ShowChar(120, MF_TITLE_HEIGHT, '^', MF_FONT);
        if (m->scroll_offset < m->item_count - MF_DISPLAY_LINES)
            OLED_ShowChar(120, MF_SCREEN_HEIGHT - MF_LINE_HEIGHT, 'v', MF_FONT);
    }

    /* 光标动画 (线性插值) */
    int8_t rel = m->select_index - m->scroll_offset;
    float target_y = (float)(MF_TITLE_HEIGHT + rel * MF_LINE_HEIGHT);
    anim_cursor_y += (target_y - anim_cursor_y) * MF_ANIM_SPEED;
    MF_DrawCursor((int)anim_cursor_y, MF_SCREEN_WIDTH, MF_LINE_HEIGHT);
}

/* ==================== 菜单列表模式处理 ==================== */
static void MF_HandleMenuInput(KeyEvent_t key)
{
    MF_Menu_t *m = cur_menu;
    if (!m || m->item_count == 0)
        return;

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
        if (!item) return;

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
            /* 进入数值编辑模式, 备份原值以便 BACK 取消 */
            editing_item = item;
            if (item->value.value_ptr)
                edit_backup = *(item->value.value_ptr);
            nav_mode = NAV_VALUE_EDIT;
            break;

        case MF_ITEM_CUSTOM_PAGE:
            if (item->custom.render)
            {
                custom_page_cb = item->custom.render;
                nav_mode = NAV_CUSTOM_PAGE;
            }
            break;
        }
    }
    else if (key == KEY_BACK)
    {
        MF_PopMenu();
    }
}

/* ==================== 数值编辑模式 ==================== */
static void MF_HandleValueInput(KeyEvent_t key)
{
    if (!editing_item || !editing_item->value.value_ptr)
    {
        nav_mode = NAV_MENU;
        editing_item = NULL;
        return;
    }

    int32_t *pval = editing_item->value.value_ptr;
    int32_t  step = editing_item->value.step;

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
    else if (key == KEY_ENTER)
    {
        /* 确认 — 直接退出编辑 */
        nav_mode = NAV_MENU;
        editing_item = NULL;
        return;
    }
    else if (key == KEY_BACK)
    {
        /* 取消 — 恢复原值后退出编辑 */
        *pval = edit_backup;
        if (editing_item->value.on_change)
            editing_item->value.on_change(*pval);
        nav_mode = NAV_MENU;
        editing_item = NULL;
        return;
    }
}

/**
 * @brief  渲染数值编辑界面
 */
static void MF_RenderValueEdit(void)
{
    if (!editing_item || !editing_item->value.value_ptr)
        return;

    int32_t    val = *(editing_item->value.value_ptr);
    const char *u  = editing_item->value.unit ? editing_item->value.unit : "";

    /* 标题 */
    MF_DrawTitle("Edit Value");

    /* 条目名称 */
    OLED_ShowString(4, MF_TITLE_HEIGHT + 2, (char *)editing_item->text, MF_FONT);

    /* 当前值 (居中显示) */
    snprintf(line_buf, sizeof(line_buf), "< %ld %s >", (long)val, u);
    int vw = MF_StrPixelWidth(line_buf);
    int vx = (MF_SCREEN_WIDTH - vw) / 2;
    if (vx < 0) vx = 0;
    OLED_ShowString(vx, MF_TITLE_HEIGHT + 20, line_buf, MF_FONT);

    /* 进度条 */
    int32_t range = editing_item->value.max_val - editing_item->value.min_val;
    float ratio = 0.0f;
    if (range > 0)
        ratio = (float)(val - editing_item->value.min_val) / (float)range;
    MF_DrawProgressBar(4, MF_TITLE_HEIGHT + 38, MF_SCREEN_WIDTH - 8, 6, ratio);

    /* 底部提示 */
    OLED_ShowString(4, MF_SCREEN_HEIGHT - 10, "OK:Save  B:Cancel", OLED_6X8);
}

/* ==================== 自定义页面模式 ==================== */
static void MF_HandleCustomInput(KeyEvent_t key)
{
    if (!custom_page_cb)
    {
        nav_mode = NAV_MENU;
        return;
    }

    uint8_t exit_flag = 0;
    custom_page_cb(key, &exit_flag);

    if (exit_flag)
    {
        nav_mode = NAV_MENU;
        custom_page_cb = NULL;
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

void MF_AddToggle(MF_Menu_t *parent, const char *text,
                  uint8_t *bool_ptr, MF_ActionCb on_change)
{
    if (!parent || parent->item_count >= MF_MAX_ITEMS_PER_MENU)
        return;
    MF_Item_t *it = &parent->items[parent->item_count++];
    memset(it, 0, sizeof(MF_Item_t));
    it->text = text;
    it->type = MF_ITEM_TOGGLE;
    it->toggle.bool_ptr  = bool_ptr;
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
    it->value.min_val   = min_v;
    it->value.max_val   = max_v;
    it->value.step      = step;
    it->value.unit      = unit;
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
    cur_menu      = root_menu;
    nav_depth     = -1;
    nav_mode      = NAV_MENU;
    anim_cursor_y = (float)MF_TITLE_HEIGHT;
    editing_item  = NULL;
    custom_page_cb = NULL;
}

/* ----------- 解耦 API ----------- */

void MF_Process(KeyEvent_t key)
{
    if (key == KEY_NONE)
        return;

    switch (nav_mode)
    {
    case NAV_MENU:
        MF_HandleMenuInput(key);
        break;
    case NAV_CUSTOM_PAGE:
        MF_HandleCustomInput(key);
        break;
    case NAV_VALUE_EDIT:
        MF_HandleValueInput(key);
        break;
    }
}

void MF_Render(void)
{
    OLED_Clear();

    switch (nav_mode)
    {
    case NAV_MENU:
        MF_RenderMenuList();
        break;
    case NAV_CUSTOM_PAGE:
        /* 自定义页面由回调自行绘制, 已在 Process 中调用 */
        break;
    case NAV_VALUE_EDIT:
        MF_RenderValueEdit();
        break;
    }

    OLED_Update();
}

void MF_Loop(void)
{
    KeyEvent_t key = Key_Scan();
    MF_Process(key);
    MF_Render();
}

MF_Menu_t *MF_GetCurrentMenu(void)
{
    return cur_menu;
}

int8_t MF_GetNavDepth(void)
{
    return nav_depth;
}

void MF_Reset(void)
{
    menu_pool_count = 0;
    memset(menu_pool, 0, sizeof(menu_pool));

    nav_depth      = -1;
    cur_menu       = NULL;
    nav_mode       = NAV_MENU;
    cursor_style   = MF_CURSOR_REVERSE;
    anim_cursor_y  = 0.0f;
    custom_page_cb = NULL;
    editing_item   = NULL;
    edit_backup    = 0;
}
