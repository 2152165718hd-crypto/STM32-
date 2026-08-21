#include "APPLICATION/Menu/MenuFramework.h"

#include <stdio.h>

typedef enum
{
    NAV_MENU = 0,
    NAV_CUSTOM_PAGE,
    NAV_VALUE_EDIT,
} NavMode_t;

typedef struct
{
    MF_Menu_t *menu;
} NavFrame_t;

static MF_Menu_t menu_pool[MF_MAX_MENUS];
static uint8_t menu_pool_count = 0;

static NavFrame_t nav_stack[MF_NAV_STACK_DEPTH];
static int8_t nav_depth = -1;

static MF_Menu_t *cur_menu = NULL;
static NavMode_t nav_mode = NAV_MENU;
static MF_CursorStyle_t cursor_style = MF_CURSOR_REVERSE;
static float anim_cursor_y = 0.0f;

static MF_CustomPageCb custom_page_cb = NULL;
static MF_Item_t *editing_item = NULL;
static int32_t edit_backup = 0;
static char line_buf[MF_LINE_BUF_SIZE];

static MF_Item_t *MF_GetSelectedItem(void)
{
    if (cur_menu == NULL || cur_menu->item_count == 0)
        return NULL;

    return &cur_menu->items[cur_menu->select_index];
}

static int MF_StrPixelWidth(const char *text, int font_width)
{
    int len = 0;

    if (text == NULL)
        return 0;

    while (*text++ != '\0')
        len++;

    return len * font_width;
}

static void MF_PushMenu(MF_Menu_t *menu)
{
    if (menu == NULL || nav_depth >= (MF_NAV_STACK_DEPTH - 1))
        return;

    nav_depth++;
    nav_stack[nav_depth].menu = cur_menu;
    cur_menu = menu;
    anim_cursor_y = (float)MF_TITLE_HEIGHT;
}

static void MF_PopMenu(void)
{
    int8_t rel = 0;

    if (nav_depth < 0)
        return;

    cur_menu = nav_stack[nav_depth].menu;
    nav_depth--;

    if (cur_menu == NULL)
        return;

    rel = cur_menu->select_index - cur_menu->scroll_offset;
    anim_cursor_y = (float)(MF_TITLE_HEIGHT + rel * MF_LINE_HEIGHT);
}

static void MF_UpdateScroll(MF_Menu_t *menu)
{
    int max_offset = 0;

    if (menu == NULL)
        return;

    if (menu->select_index >= (menu->scroll_offset + MF_DISPLAY_LINES))
        menu->scroll_offset = menu->select_index - MF_DISPLAY_LINES + 1;

    if (menu->select_index < menu->scroll_offset)
        menu->scroll_offset = menu->select_index;

    if (menu->scroll_offset < 0)
        menu->scroll_offset = 0;

    max_offset = (int)menu->item_count - MF_DISPLAY_LINES;
    if (max_offset < 0)
        max_offset = 0;

    if (menu->scroll_offset > max_offset)
        menu->scroll_offset = max_offset;
}

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

static void MF_DrawTitle(const char *title)
{
    int text_w = MF_StrPixelWidth(title, MF_TITLE_FONT_WIDTH);
    int x = (MF_SCREEN_WIDTH - text_w) / 2;

    if (x < 0)
        x = 0;

    OLED_ShowString(x, 0, (char *)(title ? title : ""), MF_TITLE_FONT);
}

static void MF_DrawProgressBar(int x, int y, int w, int h, float ratio)
{
    int fill_w = 0;

    if (ratio < 0.0f)
        ratio = 0.0f;
    if (ratio > 1.0f)
        ratio = 1.0f;

    OLED_DrawRectangle(x, y, w, h, OLED_UNFILLED);

    fill_w = (int)((w - 2) * ratio);
    if (fill_w > 0)
        OLED_DrawRectangle(x + 1, y + 1, fill_w, h - 2, OLED_FILLED);
}

static void MF_RenderMenuList(void)
{
    int i = 0;
    MF_Menu_t *menu = cur_menu;

    if (menu == NULL)
        return;

    MF_DrawTitle(menu->title);

    for (i = 0; i < MF_DISPLAY_LINES; i++)
    {
        int idx = menu->scroll_offset + i;
        int y = MF_TITLE_HEIGHT + i * MF_LINE_HEIGHT;
        MF_Item_t *item = NULL;

        if (idx >= menu->item_count)
            break;

        item = &menu->items[idx];

        switch (item->type)
        {
        case MF_ITEM_SUBMENU:
            snprintf(line_buf, sizeof(line_buf), "%s >", item->text);
            OLED_ShowString(4, y, line_buf, MF_ITEM_FONT);
            break;

        case MF_ITEM_ACTION:
            OLED_ShowString(4, y, (char *)item->text, MF_ITEM_FONT);
            break;

        case MF_ITEM_TOGGLE:
        {
            const char *state_str = "OFF";
            int sw = 0;
            uint8_t state = item->toggle.bool_ptr ? *(item->toggle.bool_ptr) : 0;

            OLED_ShowString(4, y, (char *)item->text, MF_ITEM_FONT);

            if (state)
                state_str = "ON";

            sw = MF_StrPixelWidth(state_str, MF_ITEM_FONT_WIDTH);
            OLED_ShowString(MF_SCREEN_WIDTH - sw - 4, y, (char *)state_str, MF_ITEM_FONT);
            break;
        }

        case MF_ITEM_VALUE:
        {
            int vw = 0;
            int32_t value = item->value.value_ptr ? *(item->value.value_ptr) : 0;
            const char *unit = item->value.unit ? item->value.unit : "";

            OLED_ShowString(4, y, (char *)item->text, MF_ITEM_FONT);
            snprintf(line_buf, sizeof(line_buf), "%ld%s", (long)value, unit);
            vw = MF_StrPixelWidth(line_buf, MF_ITEM_FONT_WIDTH);
            OLED_ShowString(MF_SCREEN_WIDTH - vw - 4, y, line_buf, MF_ITEM_FONT);
            break;
        }

        case MF_ITEM_CUSTOM_PAGE:
            snprintf(line_buf, sizeof(line_buf), "%s >", item->text);
            OLED_ShowString(4, y, line_buf, MF_ITEM_FONT);
            break;
        }
    }

    if (menu->item_count > MF_DISPLAY_LINES)
    {
        if (menu->scroll_offset > 0)
            OLED_ShowChar(120, MF_TITLE_HEIGHT, '^', MF_ITEM_FONT);

        if (menu->scroll_offset < (menu->item_count - MF_DISPLAY_LINES))
            OLED_ShowChar(120, MF_SCREEN_HEIGHT - MF_LINE_HEIGHT, 'v', MF_ITEM_FONT);
    }

    {
        int8_t rel = menu->select_index - menu->scroll_offset;
        float target_y = (float)(MF_TITLE_HEIGHT + rel * MF_LINE_HEIGHT);
        anim_cursor_y += (target_y - anim_cursor_y) * MF_ANIM_SPEED;
        MF_DrawCursor((int)anim_cursor_y, MF_SCREEN_WIDTH, MF_LINE_HEIGHT);
    }
}

static void MF_HandleMenuInput(KeyEvent_t key)
{
    MF_Menu_t *menu = cur_menu;

    if (menu == NULL || menu->item_count == 0)
        return;

    if (key == KEY_DOWN)
    {
        menu->select_index++;
        if (menu->select_index >= menu->item_count)
            menu->select_index = 0;
        MF_UpdateScroll(menu);
    }
    else if (key == KEY_UP)
    {
        menu->select_index--;
        if (menu->select_index < 0)
            menu->select_index = menu->item_count - 1;
        MF_UpdateScroll(menu);
    }
    else if (key == KEY_ENTER)
    {
        MF_Item_t *item = MF_GetSelectedItem();

        if (item == NULL)
            return;

        switch (item->type)
        {
        case MF_ITEM_SUBMENU:
            MF_PushMenu(item->submenu.child_menu);
            break;

        case MF_ITEM_ACTION:
            if (item->action.callback != NULL)
                item->action.callback();
            break;

        case MF_ITEM_TOGGLE:
            if (item->toggle.bool_ptr != NULL)
            {
                *(item->toggle.bool_ptr) ^= 1U;
                if (item->toggle.on_change != NULL)
                    item->toggle.on_change();
            }
            break;

        case MF_ITEM_VALUE:
            editing_item = item;
            if (item->value.value_ptr != NULL)
                edit_backup = *(item->value.value_ptr);
            nav_mode = NAV_VALUE_EDIT;
            break;

        case MF_ITEM_CUSTOM_PAGE:
            if (item->custom.render != NULL)
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

static void MF_HandleValueInput(KeyEvent_t key)
{
    int32_t *value = NULL;

    if (editing_item == NULL || editing_item->value.value_ptr == NULL)
    {
        nav_mode = NAV_MENU;
        editing_item = NULL;
        return;
    }

    value = editing_item->value.value_ptr;

    if (key == KEY_UP)
    {
        *value += editing_item->value.step;
        if (*value > editing_item->value.max_val)
            *value = editing_item->value.max_val;
        if (editing_item->value.on_change != NULL)
            editing_item->value.on_change(*value);
    }
    else if (key == KEY_DOWN)
    {
        *value -= editing_item->value.step;
        if (*value < editing_item->value.min_val)
            *value = editing_item->value.min_val;
        if (editing_item->value.on_change != NULL)
            editing_item->value.on_change(*value);
    }
    else if (key == KEY_ENTER)
    {
        nav_mode = NAV_MENU;
        editing_item = NULL;
    }
    else if (key == KEY_BACK)
    {
        *value = edit_backup;
        if (editing_item->value.on_change != NULL)
            editing_item->value.on_change(*value);
        nav_mode = NAV_MENU;
        editing_item = NULL;
    }
}

static void MF_RenderValueEdit(void)
{
    int32_t value = 0;
    int32_t range = 0;
    int text_w = 0;
    int text_x = 0;
    float ratio = 0.0f;
    const char *unit = "";

    if (editing_item == NULL || editing_item->value.value_ptr == NULL)
        return;

    value = *(editing_item->value.value_ptr);
    unit = editing_item->value.unit ? editing_item->value.unit : "";

    MF_DrawTitle("Edit Value");
    OLED_ShowString(4, MF_TITLE_HEIGHT + 2, (char *)editing_item->text, MF_ITEM_FONT);

    snprintf(line_buf, sizeof(line_buf), "< %ld %s >", (long)value, unit);
    text_w = MF_StrPixelWidth(line_buf, MF_ITEM_FONT_WIDTH);
    text_x = (MF_SCREEN_WIDTH - text_w) / 2;
    if (text_x < 0)
        text_x = 0;
    OLED_ShowString(text_x, MF_TITLE_HEIGHT + 20, line_buf, MF_ITEM_FONT);

    range = editing_item->value.max_val - editing_item->value.min_val;
    if (range > 0)
        ratio = (float)(value - editing_item->value.min_val) / (float)range;

    MF_DrawProgressBar(4, MF_TITLE_HEIGHT + 38, MF_SCREEN_WIDTH - 8, 6, ratio);
    OLED_ShowString(4, MF_SCREEN_HEIGHT - 8, "OK:Save  B:Cancel", OLED_6X8);
}

static void MF_HandleCustomInput(KeyEvent_t key)
{
    uint8_t exit_flag = 0;

    if (custom_page_cb == NULL)
    {
        nav_mode = NAV_MENU;
        return;
    }

    custom_page_cb(key, &exit_flag);
    if (exit_flag)
    {
        nav_mode = NAV_MENU;
        custom_page_cb = NULL;
    }
}

MF_Menu_t *MF_CreateMenu(const char *title)
{
    MF_Menu_t *menu = NULL;

    if (menu_pool_count >= MF_MAX_MENUS)
        return NULL;

    menu = &menu_pool[menu_pool_count++];
    memset(menu, 0, sizeof(MF_Menu_t));
    menu->title = title;
    return menu;
}

void MF_AddSubmenu(MF_Menu_t *parent, const char *text, MF_Menu_t *child)
{
    MF_Item_t *item = NULL;

    if (parent == NULL || parent->item_count >= MF_MAX_ITEMS_PER_MENU)
        return;

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(MF_Item_t));
    item->text = text;
    item->type = MF_ITEM_SUBMENU;
    item->submenu.child_menu = child;
}

void MF_AddAction(MF_Menu_t *parent, const char *text, MF_ActionCb cb)
{
    MF_Item_t *item = NULL;

    if (parent == NULL || parent->item_count >= MF_MAX_ITEMS_PER_MENU)
        return;

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(MF_Item_t));
    item->text = text;
    item->type = MF_ITEM_ACTION;
    item->action.callback = cb;
}

void MF_AddToggle(MF_Menu_t *parent, const char *text, uint8_t *bool_ptr, MF_ActionCb on_change)
{
    MF_Item_t *item = NULL;

    if (parent == NULL || parent->item_count >= MF_MAX_ITEMS_PER_MENU)
        return;

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(MF_Item_t));
    item->text = text;
    item->type = MF_ITEM_TOGGLE;
    item->toggle.bool_ptr = bool_ptr;
    item->toggle.on_change = on_change;
}

void MF_AddValue(MF_Menu_t *parent, const char *text, int32_t *value_ptr, int32_t min_v, int32_t max_v,
                 int32_t step, const char *unit, MF_ValueChangeCb on_change)
{
    MF_Item_t *item = NULL;

    if (parent == NULL || parent->item_count >= MF_MAX_ITEMS_PER_MENU)
        return;

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(MF_Item_t));
    item->text = text;
    item->type = MF_ITEM_VALUE;
    item->value.value_ptr = value_ptr;
    item->value.min_val = min_v;
    item->value.max_val = max_v;
    item->value.step = step;
    item->value.unit = unit;
    item->value.on_change = on_change;
}

void MF_AddCustomPage(MF_Menu_t *parent, const char *text, MF_CustomPageCb render_cb)
{
    MF_Item_t *item = NULL;

    if (parent == NULL || parent->item_count >= MF_MAX_ITEMS_PER_MENU)
        return;

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(MF_Item_t));
    item->text = text;
    item->type = MF_ITEM_CUSTOM_PAGE;
    item->custom.render = render_cb;
}

void MF_EnterCustomPage(MF_CustomPageCb render_cb)
{
    if (render_cb == NULL)
        return;

    custom_page_cb = render_cb;
    nav_mode = NAV_CUSTOM_PAGE;
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
    editing_item = NULL;
    custom_page_cb = NULL;
}

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

    if (nav_mode == NAV_CUSTOM_PAGE && custom_page_cb != NULL)
    {
        uint8_t exit_flag = 0;

        OLED_Clear();
        custom_page_cb(key, &exit_flag);
        OLED_Update();

        if (exit_flag)
        {
            nav_mode = NAV_MENU;
            custom_page_cb = NULL;
        }
        return;
    }

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

MF_CustomPageCb MF_GetCustomPageCb(void)
{
    return custom_page_cb;
}

uint8_t MF_IsMainMenu(void)
{
    return (uint8_t)((nav_mode == NAV_MENU) && (nav_depth == -1));
}

void MF_Reset(void)
{
    menu_pool_count = 0;
    memset(menu_pool, 0, sizeof(menu_pool));
    nav_depth = -1;
    cur_menu = NULL;
    nav_mode = NAV_MENU;
    cursor_style = MF_CURSOR_REVERSE;
    anim_cursor_y = 0.0f;
    custom_page_cb = NULL;
    editing_item = NULL;
    edit_backup = 0;
}
