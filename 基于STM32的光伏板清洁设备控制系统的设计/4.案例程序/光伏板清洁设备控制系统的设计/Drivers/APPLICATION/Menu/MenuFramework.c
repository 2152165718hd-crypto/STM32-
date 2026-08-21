#include "APPLICATION/Menu/MenuFramework.h"

#include <stdio.h>
#include <string.h>

typedef enum
{
    MF_NAV_MENU = 0,
    MF_NAV_CUSTOM_PAGE,
    MF_NAV_VALUE_EDIT
} MF_NavMode_t;

typedef struct
{
    MF_Menu_t *menu;
} MF_NavFrame_t;

static MF_Menu_t g_menu_pool[MF_MAX_MENUS];
static uint8_t g_menu_pool_count = 0u;
static MF_NavFrame_t g_nav_stack[MF_NAV_STACK_DEPTH];
static int8_t g_nav_depth = -1;
static MF_Menu_t *g_current_menu = NULL;
static MF_NavMode_t g_nav_mode = MF_NAV_MENU;
static MF_CursorStyle_t g_cursor_style = MF_CURSOR_REVERSE;
static MF_CustomPageCb g_custom_page_cb = NULL;
static MF_Item_t *g_editing_item = NULL;
static int32_t g_edit_backup = 0;
static KeyEvent_t g_edit_repeat_key = KEY_NONE;
static uint32_t g_edit_repeat_tick = 0u;
static uint8_t g_edit_repeat_count = 0u;
static char g_line_buffer[64];

static MF_Item_t *MF_GetSelectedItem(void);
static void MF_DrawTitle(const char *title);
static void MF_DrawCursor(int16_t y);
static void MF_ClampScroll(MF_Menu_t *menu);
static void MF_PushMenu(MF_Menu_t *menu);
static void MF_PopMenu(void);
static void MF_HandleMenuInput(KeyEvent_t key);
static void MF_HandleValueInput(KeyEvent_t key);
static void MF_HandleCustomInput(KeyEvent_t key);
static void MF_RenderMenuList(void);
static void MF_RenderValueEditor(void);
static void MF_FormatValue(char *buffer, uint32_t size, int32_t value, uint8_t decimals, const char *unit);

MF_Menu_t *MF_CreateMenu(const char *title)
{
    MF_Menu_t *menu;

    if (g_menu_pool_count >= MF_MAX_MENUS)
    {
        return NULL;
    }

    menu = &g_menu_pool[g_menu_pool_count++];
    memset(menu, 0, sizeof(MF_Menu_t));
    menu->title = title;
    return menu;
}

void MF_AddSubmenu(MF_Menu_t *parent, const char *text, MF_Menu_t *child)
{
    MF_Item_t *item;

    if ((parent == NULL) || (parent->item_count >= MF_MAX_ITEMS_PER_MENU))
    {
        return;
    }

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(MF_Item_t));
    item->text = text;
    item->type = MF_ITEM_SUBMENU;
    item->submenu.child_menu = child;
}

void MF_AddAction(MF_Menu_t *parent, const char *text, MF_ActionCb cb)
{
    MF_Item_t *item;

    if ((parent == NULL) || (parent->item_count >= MF_MAX_ITEMS_PER_MENU))
    {
        return;
    }

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(MF_Item_t));
    item->text = text;
    item->type = MF_ITEM_ACTION;
    item->action.callback = cb;
}

void MF_AddToggle(MF_Menu_t *parent, const char *text, uint8_t *bool_ptr, MF_ActionCb on_change)
{
    MF_Item_t *item;

    if ((parent == NULL) || (parent->item_count >= MF_MAX_ITEMS_PER_MENU))
    {
        return;
    }

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(MF_Item_t));
    item->text = text;
    item->type = MF_ITEM_TOGGLE;
    item->toggle.bool_ptr = bool_ptr;
    item->toggle.on_change = on_change;
}

void MF_AddValue(MF_Menu_t *parent, const char *text, int32_t *value_ptr, int32_t min_v, int32_t max_v, int32_t step, uint8_t decimals, const char *unit, MF_ValueChangeCb on_change)
{
    MF_Item_t *item;

    if ((parent == NULL) || (parent->item_count >= MF_MAX_ITEMS_PER_MENU))
    {
        return;
    }

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(MF_Item_t));
    item->text = text;
    item->type = MF_ITEM_VALUE;
    item->value.value_ptr = value_ptr;
    item->value.min_val = min_v;
    item->value.max_val = max_v;
    item->value.step = step;
    item->value.decimals = decimals;
    item->value.unit = unit;
    item->value.on_change = on_change;
}

void MF_AddCustomPage(MF_Menu_t *parent, const char *text, MF_CustomPageCb render_cb)
{
    MF_Item_t *item;

    if ((parent == NULL) || (parent->item_count >= MF_MAX_ITEMS_PER_MENU))
    {
        return;
    }

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(MF_Item_t));
    item->text = text;
    item->type = MF_ITEM_CUSTOM_PAGE;
    item->custom.render = render_cb;
}

void MF_SetCursorStyle(MF_CursorStyle_t style)
{
    g_cursor_style = style;
}

MF_CursorStyle_t MF_GetCursorStyle(void)
{
    return g_cursor_style;
}

void MF_Start(MF_Menu_t *root_menu)
{
    g_current_menu = root_menu;
    g_nav_depth = -1;
    g_nav_mode = MF_NAV_MENU;
    g_custom_page_cb = NULL;
    g_editing_item = NULL;
    g_edit_repeat_key = KEY_NONE;
    g_edit_repeat_tick = 0u;
    g_edit_repeat_count = 0u;
}

void MF_Process(KeyEvent_t key)
{
    if (key == KEY_NONE)
    {
        return;
    }

    switch (g_nav_mode)
    {
    case MF_NAV_MENU:
        MF_HandleMenuInput(key);
        break;
    case MF_NAV_CUSTOM_PAGE:
        MF_HandleCustomInput(key);
        break;
    case MF_NAV_VALUE_EDIT:
        MF_HandleValueInput(key);
        break;
    default:
        break;
    }
}

void MF_Render(void)
{
    OLED_Clear();

    switch (g_nav_mode)
    {
    case MF_NAV_MENU:
        MF_RenderMenuList();
        break;
    case MF_NAV_CUSTOM_PAGE:
        if (g_custom_page_cb != NULL)
        {
            uint8_t exit_flag = 0u;
            g_custom_page_cb(KEY_NONE, &exit_flag);
            if (exit_flag != 0u)
            {
                g_nav_mode = MF_NAV_MENU;
                g_custom_page_cb = NULL;
                MF_RenderMenuList();
            }
        }
        break;
    case MF_NAV_VALUE_EDIT:
        MF_RenderValueEditor();
        break;
    default:
        break;
    }

    OLED_Update();
}

void MF_Loop(void)
{
    MF_Process(Key_Scan());
    MF_Render();
}

MF_Menu_t *MF_GetCurrentMenu(void)
{
    return g_current_menu;
}

int8_t MF_GetNavDepth(void)
{
    return g_nav_depth;
}

void MF_Reset(void)
{
    memset(g_menu_pool, 0, sizeof(g_menu_pool));
    memset(g_nav_stack, 0, sizeof(g_nav_stack));
    g_menu_pool_count = 0u;
    g_nav_depth = -1;
    g_current_menu = NULL;
    g_nav_mode = MF_NAV_MENU;
    g_cursor_style = MF_CURSOR_REVERSE;
    g_custom_page_cb = NULL;
    g_editing_item = NULL;
    g_edit_backup = 0;
    g_edit_repeat_key = KEY_NONE;
    g_edit_repeat_tick = 0u;
    g_edit_repeat_count = 0u;
}

static void MF_FormatValue(char *buffer, uint32_t size, int32_t value, uint8_t decimals, const char *unit)
{
    int32_t scale = 1;
    int32_t abs_value;
    uint8_t i;

    if ((buffer == NULL) || (size == 0u))
    {
        return;
    }

    if (unit == NULL)
    {
        unit = "";
    }

    if (decimals == 0u)
    {
        snprintf(buffer, size, "%ld%s", (long)value, unit);
        return;
    }

    for (i = 0u; i < decimals; i++)
    {
        scale *= 10;
    }

    abs_value = (value >= 0) ? value : -value;
    snprintf(buffer,
             size,
             "%s%ld.%0*ld%s",
             (value < 0) ? "-" : "",
             (long)(abs_value / scale),
             (int)decimals,
             (long)(abs_value % scale),
             unit);
}

static MF_Item_t *MF_GetSelectedItem(void)
{
    if ((g_current_menu == NULL) || (g_current_menu->item_count == 0u))
    {
        return NULL;
    }

    if (g_current_menu->select_index < 0)
    {
        g_current_menu->select_index = 0;
    }

    if (g_current_menu->select_index >= (int8_t)g_current_menu->item_count)
    {
        g_current_menu->select_index = (int8_t)g_current_menu->item_count - 1;
    }

    return &g_current_menu->items[g_current_menu->select_index];
}

static void MF_DrawTitle(const char *title)
{
    int16_t title_x = 0;
    int16_t title_len = 0;
    const char *walker = title;

    while ((walker != NULL) && (*walker != '\0'))
    {
        title_len++;
        walker++;
    }

    title_x = (int16_t)((MF_SCREEN_WIDTH - title_len * MF_FONT_WIDTH) / 2);
    if (title_x < 0)
    {
        title_x = 0;
    }

    OLED_ShowString(title_x, 0, (char *)title, MF_FONT);
    OLED_DrawLine(0, MF_TITLE_HEIGHT, MF_SCREEN_WIDTH, MF_TITLE_HEIGHT);
}

static void MF_DrawCursor(int16_t y)
{
    if (g_cursor_style == MF_CURSOR_REVERSE)
    {
        OLED_ReverseArea(0, y, MF_SCREEN_WIDTH, MF_LINE_HEIGHT);
    }
    else
    {
        OLED_DrawRectangle(MF_CURSOR_PADDING,
                           y + MF_CURSOR_PADDING,
                           MF_SCREEN_WIDTH - 2 * MF_CURSOR_PADDING,
                           MF_LINE_HEIGHT - 2 * MF_CURSOR_PADDING,
                           OLED_UNFILLED);
    }
}

static void MF_ClampScroll(MF_Menu_t *menu)
{
    int8_t max_offset;

    if (menu == NULL)
    {
        return;
    }

    if (menu->select_index < 0)
    {
        menu->select_index = 0;
    }
    else if (menu->select_index >= (int8_t)menu->item_count)
    {
        menu->select_index = (int8_t)menu->item_count - 1;
    }

    if (menu->select_index < menu->scroll_offset)
    {
        menu->scroll_offset = menu->select_index;
    }
    else if (menu->select_index >= (menu->scroll_offset + MF_DISPLAY_LINES))
    {
        menu->scroll_offset = menu->select_index - MF_DISPLAY_LINES + 1;
    }

    if (menu->scroll_offset < 0)
    {
        menu->scroll_offset = 0;
    }

    max_offset = (int8_t)menu->item_count - MF_DISPLAY_LINES;
    if (max_offset < 0)
    {
        max_offset = 0;
    }

    if (menu->scroll_offset > max_offset)
    {
        menu->scroll_offset = max_offset;
    }
}

static void MF_PushMenu(MF_Menu_t *menu)
{
    if ((menu == NULL) || (g_nav_depth >= (MF_NAV_STACK_DEPTH - 1)))
    {
        return;
    }

    g_nav_depth++;
    g_nav_stack[g_nav_depth].menu = g_current_menu;
    g_current_menu = menu;
    MF_ClampScroll(g_current_menu);
}

static void MF_PopMenu(void)
{
    if (g_nav_depth < 0)
    {
        return;
    }

    g_current_menu = g_nav_stack[g_nav_depth].menu;
    g_nav_depth--;
}

static void MF_HandleMenuInput(KeyEvent_t key)
{
    MF_Item_t *item;

    if ((g_current_menu == NULL) || (g_current_menu->item_count == 0u))
    {
        return;
    }

    if (key == KEY_DOWN)
    {
        g_current_menu->select_index++;
        if (g_current_menu->select_index >= (int8_t)g_current_menu->item_count)
        {
            g_current_menu->select_index = 0;
        }
        MF_ClampScroll(g_current_menu);
        return;
    }

    if (key == KEY_UP)
    {
        g_current_menu->select_index--;
        if (g_current_menu->select_index < 0)
        {
            g_current_menu->select_index = (int8_t)g_current_menu->item_count - 1;
        }
        MF_ClampScroll(g_current_menu);
        return;
    }

    if (key == KEY_BACK)
    {
        MF_PopMenu();
        return;
    }

    if (key != KEY_ENTER)
    {
        return;
    }

    item = MF_GetSelectedItem();
    if (item == NULL)
    {
        return;
    }

    switch (item->type)
    {
    case MF_ITEM_SUBMENU:
        MF_PushMenu(item->submenu.child_menu);
        break;
    case MF_ITEM_ACTION:
        if (item->action.callback != NULL)
        {
            item->action.callback();
        }
        break;
    case MF_ITEM_TOGGLE:
        if (item->toggle.bool_ptr != NULL)
        {
            *item->toggle.bool_ptr ^= 1u;
            if (item->toggle.on_change != NULL)
            {
                item->toggle.on_change();
            }
        }
        break;
    case MF_ITEM_VALUE:
        g_editing_item = item;
        g_nav_mode = MF_NAV_VALUE_EDIT;
        if (item->value.value_ptr != NULL)
        {
            g_edit_backup = *item->value.value_ptr;
        }
        break;
    case MF_ITEM_CUSTOM_PAGE:
        g_custom_page_cb = item->custom.render;
        g_nav_mode = MF_NAV_CUSTOM_PAGE;
        break;
    default:
        break;
    }
}

static void MF_HandleValueInput(KeyEvent_t key)
{
    int32_t *value_ptr;
    int32_t next_value;
    int32_t step_delta;
    uint32_t now;

    if ((g_editing_item == NULL) || (g_editing_item->value.value_ptr == NULL))
    {
        g_editing_item = NULL;
        g_nav_mode = MF_NAV_MENU;
        g_edit_repeat_key = KEY_NONE;
        g_edit_repeat_count = 0u;
        return;
    }

    value_ptr = g_editing_item->value.value_ptr;

    if (key == KEY_ENTER)
    {
        g_editing_item = NULL;
        g_nav_mode = MF_NAV_MENU;
        g_edit_repeat_key = KEY_NONE;
        g_edit_repeat_count = 0u;
        return;
    }

    if (key == KEY_BACK)
    {
        *value_ptr = g_edit_backup;
        if (g_editing_item->value.on_change != NULL)
        {
            g_editing_item->value.on_change(*value_ptr);
        }
        g_editing_item = NULL;
        g_nav_mode = MF_NAV_MENU;
        g_edit_repeat_key = KEY_NONE;
        g_edit_repeat_count = 0u;
        return;
    }

    next_value = *value_ptr;
    step_delta = g_editing_item->value.step;
    if (key == KEY_UP)
    {
        now = HAL_GetTick();
        if ((g_edit_repeat_key == key) && ((now - g_edit_repeat_tick) <= 120u))
        {
            if (g_edit_repeat_count < 255u)
            {
                g_edit_repeat_count++;
            }
        }
        else
        {
            g_edit_repeat_count = 0u;
        }

        g_edit_repeat_key = key;
        g_edit_repeat_tick = now;

        if (g_edit_repeat_count >= 8u)
        {
            step_delta *= 10;
        }
        else if (g_edit_repeat_count >= 3u)
        {
            step_delta *= 5;
        }

        next_value += step_delta;
    }
    else if (key == KEY_DOWN)
    {
        now = HAL_GetTick();
        if ((g_edit_repeat_key == key) && ((now - g_edit_repeat_tick) <= 120u))
        {
            if (g_edit_repeat_count < 255u)
            {
                g_edit_repeat_count++;
            }
        }
        else
        {
            g_edit_repeat_count = 0u;
        }

        g_edit_repeat_key = key;
        g_edit_repeat_tick = now;

        if (g_edit_repeat_count >= 8u)
        {
            step_delta *= 10;
        }
        else if (g_edit_repeat_count >= 3u)
        {
            step_delta *= 5;
        }

        next_value -= step_delta;
    }
    else
    {
        g_edit_repeat_key = KEY_NONE;
        g_edit_repeat_count = 0u;
        return;
    }

    if (next_value > g_editing_item->value.max_val)
    {
        next_value = g_editing_item->value.max_val;
    }
    if (next_value < g_editing_item->value.min_val)
    {
        next_value = g_editing_item->value.min_val;
    }

    *value_ptr = next_value;
    if (g_editing_item->value.on_change != NULL)
    {
        g_editing_item->value.on_change(*value_ptr);
    }
}

static void MF_HandleCustomInput(KeyEvent_t key)
{
    uint8_t exit_flag = 0u;

    if (g_custom_page_cb == NULL)
    {
        g_nav_mode = MF_NAV_MENU;
        return;
    }

    g_custom_page_cb(key, &exit_flag);
    if (exit_flag != 0u)
    {
        g_nav_mode = MF_NAV_MENU;
        g_custom_page_cb = NULL;
    }
}

static void MF_RenderMenuList(void)
{
    int8_t line_index;
    int8_t item_index;

    if (g_current_menu == NULL)
    {
        return;
    }

    MF_ClampScroll(g_current_menu);
    MF_DrawTitle(g_current_menu->title);

    if (g_current_menu->item_count == 0u)
    {
        return;
    }

    for (line_index = 0; line_index < MF_DISPLAY_LINES; line_index++)
    {
        MF_Item_t *item;
        int16_t y = (int16_t)(MF_TITLE_HEIGHT + line_index * MF_LINE_HEIGHT);

        item_index = g_current_menu->scroll_offset + line_index;
        if (item_index >= (int8_t)g_current_menu->item_count)
        {
            break;
        }

        item = &g_current_menu->items[item_index];

        switch (item->type)
        {
        case MF_ITEM_SUBMENU:
        case MF_ITEM_CUSTOM_PAGE:
            snprintf(g_line_buffer, sizeof(g_line_buffer), "%s >", item->text);
            OLED_ShowString(4, y, g_line_buffer, MF_FONT);
            break;
        case MF_ITEM_ACTION:
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            break;
        case MF_ITEM_TOGGLE:
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            snprintf(g_line_buffer, sizeof(g_line_buffer), "%s", (item->toggle.bool_ptr != NULL && *item->toggle.bool_ptr != 0u) ? "ON" : "OFF");
            OLED_ShowString(92, y, g_line_buffer, MF_FONT);
            break;
        case MF_ITEM_VALUE:
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            if (item->value.value_ptr != NULL)
            {
                MF_FormatValue(g_line_buffer,
                               sizeof(g_line_buffer),
                               *item->value.value_ptr,
                               item->value.decimals,
                               item->value.unit);
                OLED_ShowString(72, y, g_line_buffer, MF_FONT);
            }
            break;
        default:
            break;
        }
    }

    MF_DrawCursor((int16_t)(MF_TITLE_HEIGHT + (g_current_menu->select_index - g_current_menu->scroll_offset) * MF_LINE_HEIGHT));
}

static void MF_RenderValueEditor(void)
{
    char value_text[32];
    const char *unit;
    int32_t value;

    if ((g_editing_item == NULL) || (g_editing_item->value.value_ptr == NULL))
    {
        return;
    }

    unit = (g_editing_item->value.unit != NULL) ? g_editing_item->value.unit : "";
    value = *g_editing_item->value.value_ptr;

    MF_DrawTitle("Edit Value");
    OLED_ShowString(4, 18, (char *)g_editing_item->text, MF_FONT);
    MF_FormatValue(g_line_buffer,
                   sizeof(g_line_buffer),
                   value,
                   g_editing_item->value.decimals,
                   unit);
    snprintf(value_text, sizeof(value_text), "< %s >", g_line_buffer);
    OLED_ShowString(16, 36, value_text, MF_FONT);
    OLED_ShowString(0, 56, "OK save  B cancel", OLED_6X8);
}
