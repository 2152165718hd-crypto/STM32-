#include "APPLICATION/Menu/MenuFramework.h"

#include <stdio.h>

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

static MF_Menu_t s_menu_pool[MF_MAX_MENUS];
static uint8_t s_menu_pool_count = 0u;

static MF_NavFrame_t s_nav_stack[MF_NAV_STACK_DEPTH];
static int8_t s_nav_depth = -1;

static MF_Menu_t *s_current_menu = NULL;
static MF_NavMode_t s_nav_mode = MF_NAV_MENU;
static MF_CursorStyle_t s_cursor_style = MF_CURSOR_REVERSE;
static float s_anim_cursor_y = 0.0f;

static MF_CustomPageCb s_custom_page_cb = NULL;
static MF_Item_t *s_edit_item = NULL;
static int32_t s_edit_backup = 0;

static char s_line_buffer[MF_LINE_BUF_SIZE];

static int MF_StringWidth(const char *text)
{
    int width = 0;

    while ((text != NULL) && (*text != '\0'))
    {
        width += MF_FONT_WIDTH;
        text++;
    }

    return width;
}

static MF_Item_t *MF_GetSelectedItem(void)
{
    if ((s_current_menu == NULL) || (s_current_menu->item_count == 0u))
    {
        return NULL;
    }

    return &s_current_menu->items[s_current_menu->select_index];
}

static void MF_UpdateScroll(MF_Menu_t *menu)
{
    int max_offset;

    if (menu == NULL)
    {
        return;
    }

    if (menu->select_index < menu->scroll_offset)
    {
        menu->scroll_offset = menu->select_index;
    }

    if (menu->select_index >= (menu->scroll_offset + MF_DISPLAY_LINES))
    {
        menu->scroll_offset = (int8_t)(menu->select_index - MF_DISPLAY_LINES + 1);
    }

    if (menu->scroll_offset < 0)
    {
        menu->scroll_offset = 0;
    }

    max_offset = (int)menu->item_count - MF_DISPLAY_LINES;
    if (max_offset < 0)
    {
        max_offset = 0;
    }

    if (menu->scroll_offset > max_offset)
    {
        menu->scroll_offset = (int8_t)max_offset;
    }
}

static void MF_PushMenu(MF_Menu_t *menu)
{
    if ((menu == NULL) || (s_nav_depth >= (MF_NAV_STACK_DEPTH - 1)))
    {
        return;
    }

    s_nav_depth++;
    s_nav_stack[s_nav_depth].menu = s_current_menu;
    s_current_menu = menu;
    s_nav_mode = MF_NAV_MENU;
    s_anim_cursor_y = (float)MF_TITLE_HEIGHT;
    MF_UpdateScroll(s_current_menu);
}

static void MF_PopMenu(void)
{
    int8_t relative_index;

    if (s_nav_depth < 0)
    {
        return;
    }

    s_current_menu = s_nav_stack[s_nav_depth].menu;
    s_nav_depth--;
    s_nav_mode = MF_NAV_MENU;

    if (s_current_menu == NULL)
    {
        s_anim_cursor_y = 0.0f;
        return;
    }

    MF_UpdateScroll(s_current_menu);
    relative_index = (int8_t)(s_current_menu->select_index - s_current_menu->scroll_offset);
    s_anim_cursor_y = (float)(MF_TITLE_HEIGHT + (relative_index * MF_LINE_HEIGHT));
}

static void MF_DrawCursor(int y, int width, int height)
{
    if (s_cursor_style == MF_CURSOR_REVERSE)
    {
        OLED_ReverseArea(0, y, width, height);
    }
    else
    {
        OLED_DrawRectangle(MF_CURSOR_PADDING, y + MF_CURSOR_PADDING,
                           width - (2 * MF_CURSOR_PADDING),
                           height - (2 * MF_CURSOR_PADDING),
                           OLED_UNFILLED);
    }
}

static void MF_DrawTitle(const char *title)
{
    int x;

    if (title == NULL)
    {
        title = "";
    }

    x = (MF_SCREEN_WIDTH - MF_StringWidth(title)) / 2;
    if (x < 0)
    {
        x = 0;
    }

    OLED_ShowString(x, 0, (char *)title, MF_FONT);
    OLED_DrawLine(0, MF_TITLE_HEIGHT, MF_SCREEN_WIDTH, MF_TITLE_HEIGHT);
}

static void MF_DrawProgressBar(int x, int y, int width, int height, float ratio)
{
    int fill_width;

    if (ratio < 0.0f)
    {
        ratio = 0.0f;
    }
    if (ratio > 1.0f)
    {
        ratio = 1.0f;
    }

    OLED_DrawRectangle(x, y, width, height, OLED_UNFILLED);
    fill_width = (int)((width - 2) * ratio);
    if (fill_width > 0)
    {
        OLED_DrawRectangle(x + 1, y + 1, fill_width, height - 2, OLED_FILLED);
    }
}

static void MF_RenderMenuList(void)
{
    int line;
    int8_t relative_index;
    float target_y;

    if (s_current_menu == NULL)
    {
        return;
    }

    MF_DrawTitle(s_current_menu->title);

    for (line = 0; line < MF_DISPLAY_LINES; line++)
    {
        int item_index = s_current_menu->scroll_offset + line;
        int y = MF_TITLE_HEIGHT + (line * MF_LINE_HEIGHT);
        MF_Item_t *item;

        if (item_index >= s_current_menu->item_count)
        {
            break;
        }

        item = &s_current_menu->items[item_index];

        switch (item->type)
        {
        case MF_ITEM_SUBMENU:
            snprintf(s_line_buffer, sizeof(s_line_buffer), "%s >", item->text);
            OLED_ShowString(4, y, s_line_buffer, MF_FONT);
            break;

        case MF_ITEM_ACTION:
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            break;

        case MF_ITEM_TOGGLE:
        {
            const char *state_text = "OFF";
            int state_width;

            if ((item->toggle.bool_ptr != NULL) && (*item->toggle.bool_ptr != 0u))
            {
                state_text = "ON";
            }

            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            state_width = MF_StringWidth(state_text);
            OLED_ShowString(MF_SCREEN_WIDTH - state_width - 4, y, (char *)state_text, MF_FONT);
            break;
        }

        case MF_ITEM_VALUE:
        {
            int value_width;
            int32_t value = 0;

            if (item->value.value_ptr != NULL)
            {
                value = *item->value.value_ptr;
            }

            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            snprintf(s_line_buffer, sizeof(s_line_buffer), "%ld%s", (long)value,
                     (item->value.unit != NULL) ? item->value.unit : "");
            value_width = MF_StringWidth(s_line_buffer);
            OLED_ShowString(MF_SCREEN_WIDTH - value_width - 4, y, s_line_buffer, MF_FONT);
            break;
        }

        case MF_ITEM_CUSTOM_PAGE:
            snprintf(s_line_buffer, sizeof(s_line_buffer), "%s >", item->text);
            OLED_ShowString(4, y, s_line_buffer, MF_FONT);
            break;
        }
    }

    if (s_current_menu->item_count > MF_DISPLAY_LINES)
    {
        if (s_current_menu->scroll_offset > 0)
        {
            OLED_ShowChar(120, MF_TITLE_HEIGHT, '^', MF_FONT);
        }
        if (s_current_menu->scroll_offset < (s_current_menu->item_count - MF_DISPLAY_LINES))
        {
            OLED_ShowChar(120, MF_SCREEN_HEIGHT - MF_LINE_HEIGHT, 'v', MF_FONT);
        }
    }

    relative_index = (int8_t)(s_current_menu->select_index - s_current_menu->scroll_offset);
    target_y = (float)(MF_TITLE_HEIGHT + (relative_index * MF_LINE_HEIGHT));
    s_anim_cursor_y += (target_y - s_anim_cursor_y) * MF_ANIM_SPEED;
    MF_DrawCursor((int)s_anim_cursor_y, MF_SCREEN_WIDTH, MF_LINE_HEIGHT);
}

static void MF_RenderValueEdit(void)
{
    int x;
    int32_t value;
    int32_t range;
    float ratio = 0.0f;

    if ((s_edit_item == NULL) || (s_edit_item->value.value_ptr == NULL))
    {
        return;
    }

    value = *s_edit_item->value.value_ptr;
    range = s_edit_item->value.max_val - s_edit_item->value.min_val;
    if (range > 0)
    {
        ratio = (float)(value - s_edit_item->value.min_val) / (float)range;
    }

    MF_DrawTitle("Edit Value");
    OLED_ShowString(4, MF_TITLE_HEIGHT + 2, (char *)s_edit_item->text, MF_FONT);

    snprintf(s_line_buffer, sizeof(s_line_buffer), "< %ld%s >", (long)value,
             (s_edit_item->value.unit != NULL) ? s_edit_item->value.unit : "");
    x = (MF_SCREEN_WIDTH - MF_StringWidth(s_line_buffer)) / 2;
    if (x < 0)
    {
        x = 0;
    }
    OLED_ShowString(x, MF_TITLE_HEIGHT + 20, s_line_buffer, MF_FONT);
    MF_DrawProgressBar(4, MF_TITLE_HEIGHT + 40, MF_SCREEN_WIDTH - 8, 6, ratio);
    OLED_ShowString(0, 56, "ENT save  BACK undo", OLED_6X8);
}

static void MF_HandleMenuInput(KeyEvent_t key)
{
    MF_Item_t *item;

    if ((s_current_menu == NULL) || (s_current_menu->item_count == 0u))
    {
        return;
    }

    if (key == KEY_DOWN)
    {
        s_current_menu->select_index++;
        if (s_current_menu->select_index >= s_current_menu->item_count)
        {
            s_current_menu->select_index = 0;
        }
        MF_UpdateScroll(s_current_menu);
        return;
    }

    if (key == KEY_UP)
    {
        s_current_menu->select_index--;
        if (s_current_menu->select_index < 0)
        {
            s_current_menu->select_index = (int8_t)(s_current_menu->item_count - 1);
        }
        MF_UpdateScroll(s_current_menu);
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
            *item->toggle.bool_ptr = (uint8_t)(*item->toggle.bool_ptr == 0u);
            if (item->toggle.on_change != NULL)
            {
                item->toggle.on_change();
            }
        }
        break;

    case MF_ITEM_VALUE:
        if (item->value.value_ptr != NULL)
        {
            s_edit_item = item;
            s_edit_backup = *item->value.value_ptr;
            s_nav_mode = MF_NAV_VALUE_EDIT;
        }
        break;

    case MF_ITEM_CUSTOM_PAGE:
        if (item->custom.render != NULL)
        {
            s_custom_page_cb = item->custom.render;
            s_nav_mode = MF_NAV_CUSTOM_PAGE;
        }
        break;
    }
}

static void MF_HandleValueEdit(KeyEvent_t key)
{
    int32_t *value_ptr;

    if ((s_edit_item == NULL) || (s_edit_item->value.value_ptr == NULL))
    {
        s_nav_mode = MF_NAV_MENU;
        s_edit_item = NULL;
        return;
    }

    value_ptr = s_edit_item->value.value_ptr;

    if (key == KEY_UP)
    {
        *value_ptr += s_edit_item->value.step;
        if (*value_ptr > s_edit_item->value.max_val)
        {
            *value_ptr = s_edit_item->value.max_val;
        }
        if (s_edit_item->value.on_change != NULL)
        {
            s_edit_item->value.on_change(*value_ptr);
        }
        return;
    }

    if (key == KEY_DOWN)
    {
        *value_ptr -= s_edit_item->value.step;
        if (*value_ptr < s_edit_item->value.min_val)
        {
            *value_ptr = s_edit_item->value.min_val;
        }
        if (s_edit_item->value.on_change != NULL)
        {
            s_edit_item->value.on_change(*value_ptr);
        }
        return;
    }

    if (key == KEY_BACK)
    {
        *value_ptr = s_edit_backup;
        if (s_edit_item->value.on_change != NULL)
        {
            s_edit_item->value.on_change(*value_ptr);
        }
        s_edit_item = NULL;
        s_nav_mode = MF_NAV_MENU;
        return;
    }

    if (key == KEY_ENTER)
    {
        s_edit_item = NULL;
        s_nav_mode = MF_NAV_MENU;
    }
}

static void MF_RunCustomPage(KeyEvent_t key)
{
    uint8_t exit_flag = 0u;

    if (s_custom_page_cb == NULL)
    {
        s_nav_mode = MF_NAV_MENU;
        return;
    }

    s_custom_page_cb(key, &exit_flag);
    if (exit_flag != 0u)
    {
        s_custom_page_cb = NULL;
        s_nav_mode = MF_NAV_MENU;
    }
}

MF_Menu_t *MF_CreateMenu(const char *title)
{
    MF_Menu_t *menu;

    if (s_menu_pool_count >= MF_MAX_MENUS)
    {
        return NULL;
    }

    menu = &s_menu_pool[s_menu_pool_count++];
    memset(menu, 0, sizeof(*menu));
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
    memset(item, 0, sizeof(*item));
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
    memset(item, 0, sizeof(*item));
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
    memset(item, 0, sizeof(*item));
    item->text = text;
    item->type = MF_ITEM_TOGGLE;
    item->toggle.bool_ptr = bool_ptr;
    item->toggle.on_change = on_change;
}

void MF_AddValue(MF_Menu_t *parent, const char *text, int32_t *value_ptr, int32_t min_v,
                 int32_t max_v, int32_t step, const char *unit, MF_ValueChangeCb on_change)
{
    MF_Item_t *item;

    if ((parent == NULL) || (parent->item_count >= MF_MAX_ITEMS_PER_MENU))
    {
        return;
    }

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(*item));
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
    MF_Item_t *item;

    if ((parent == NULL) || (parent->item_count >= MF_MAX_ITEMS_PER_MENU))
    {
        return;
    }

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(*item));
    item->text = text;
    item->type = MF_ITEM_CUSTOM_PAGE;
    item->custom.render = render_cb;
}

void MF_SetCursorStyle(MF_CursorStyle_t style)
{
    s_cursor_style = style;
}

MF_CursorStyle_t MF_GetCursorStyle(void)
{
    return s_cursor_style;
}

void MF_Start(MF_Menu_t *root_menu)
{
    s_nav_depth = -1;
    s_current_menu = root_menu;
    s_nav_mode = MF_NAV_MENU;
    s_custom_page_cb = NULL;
    s_edit_item = NULL;
    s_edit_backup = 0;
    s_anim_cursor_y = (float)MF_TITLE_HEIGHT;
    if (s_current_menu != NULL)
    {
        MF_UpdateScroll(s_current_menu);
    }
}

void MF_OpenCustomPage(MF_CustomPageCb render_cb)
{
    s_custom_page_cb = render_cb;
    s_nav_mode = MF_NAV_CUSTOM_PAGE;
}

void MF_Process(KeyEvent_t key)
{
    if ((key == KEY_NONE) && (s_nav_mode != MF_NAV_CUSTOM_PAGE))
    {
        return;
    }

    switch (s_nav_mode)
    {
    case MF_NAV_MENU:
        MF_HandleMenuInput(key);
        break;

    case MF_NAV_CUSTOM_PAGE:
        MF_RunCustomPage(key);
        break;

    case MF_NAV_VALUE_EDIT:
        MF_HandleValueEdit(key);
        break;
    }
}

void MF_Render(void)
{
    OLED_Clear();

    switch (s_nav_mode)
    {
    case MF_NAV_MENU:
        MF_RenderMenuList();
        break;

    case MF_NAV_CUSTOM_PAGE:
        MF_RunCustomPage(KEY_NONE);
        break;

    case MF_NAV_VALUE_EDIT:
        MF_RenderValueEdit();
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
    return s_current_menu;
}

int8_t MF_GetNavDepth(void)
{
    return s_nav_depth;
}

uint8_t MF_CanExit(void)
{
    return (uint8_t)((s_nav_mode == MF_NAV_MENU) && (s_nav_depth < 0));
}

void MF_Reset(void)
{
    memset(s_menu_pool, 0, sizeof(s_menu_pool));
    memset(s_nav_stack, 0, sizeof(s_nav_stack));
    s_menu_pool_count = 0u;
    s_nav_depth = -1;
    s_current_menu = NULL;
    s_nav_mode = MF_NAV_MENU;
    s_cursor_style = MF_CURSOR_REVERSE;
    s_anim_cursor_y = 0.0f;
    s_custom_page_cb = NULL;
    s_edit_item = NULL;
    s_edit_backup = 0;
}
