#include "APPLICATION/Menu/MenuFramework.h"
#include <stdio.h>

typedef enum
{
    NAV_MENU = 0,
    NAV_CUSTOM_PAGE,
    NAV_VALUE_EDIT
} NavMode_t;

typedef struct
{
    MF_Menu_t *menu;
} NavFrame_t;

static MF_Menu_t menu_pool[MF_MAX_MENUS];
static uint8_t menu_pool_count = 0U;

static NavFrame_t nav_stack[MF_NAV_STACK_DEPTH];
static int8_t nav_depth = -1;

static MF_Menu_t *cur_menu = NULL;
static NavMode_t nav_mode = NAV_MENU;
static MF_CursorStyle_t cursor_style = MF_CURSOR_REVERSE;
static MF_CustomPageCb custom_page_cb = NULL;
static MF_Item_t *editing_item = NULL;
static int32_t edit_backup = 0;
static float anim_cursor_y = 0.0f;
static char line_buf[MF_LINE_BUF_SIZE];

static MF_Item_t *MF_GetSelectedItem(void)
{
    if ((cur_menu == NULL) || (cur_menu->item_count == 0U))
    {
        return NULL;
    }

    return &cur_menu->items[cur_menu->select_index];
}

static int MF_StrPixelWidth(const char *text)
{
    int width = 0;

    while ((text != NULL) && (*text != '\0'))
    {
        width += MF_FONT_WIDTH;
        text++;
    }

    return width;
}

static void MF_UpdateScroll(MF_Menu_t *menu)
{
    int max_offset = 0;

    if (menu == NULL)
    {
        return;
    }

    if (menu->select_index >= (menu->scroll_offset + MF_DISPLAY_LINES))
    {
        menu->scroll_offset = (int8_t)(menu->select_index - MF_DISPLAY_LINES + 1);
    }

    if (menu->select_index < menu->scroll_offset)
    {
        menu->scroll_offset = menu->select_index;
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
    if ((menu == NULL) || (nav_depth >= (MF_NAV_STACK_DEPTH - 1)))
    {
        return;
    }

    nav_depth++;
    nav_stack[nav_depth].menu = cur_menu;
    cur_menu = menu;
    MF_UpdateScroll(cur_menu);
    anim_cursor_y = (float)(MF_TITLE_HEIGHT + (cur_menu->select_index - cur_menu->scroll_offset) * MF_LINE_HEIGHT);
}

static void MF_PopMenu(void)
{
    if (nav_depth < 0)
    {
        return;
    }

    cur_menu = nav_stack[nav_depth].menu;
    nav_depth--;

    if (cur_menu != NULL)
    {
        MF_UpdateScroll(cur_menu);
        anim_cursor_y = (float)(MF_TITLE_HEIGHT + (cur_menu->select_index - cur_menu->scroll_offset) * MF_LINE_HEIGHT);
    }
}

static void MF_DrawCursor(int y)
{
    if (cursor_style == MF_CURSOR_BOX)
    {
        OLED_DrawRectangle(MF_CURSOR_PADDING,
                           y + MF_CURSOR_PADDING,
                           MF_SCREEN_WIDTH - (uint8_t)(2 * MF_CURSOR_PADDING),
                           MF_LINE_HEIGHT - (uint8_t)(2 * MF_CURSOR_PADDING),
                           OLED_UNFILLED);
    }
    else
    {
        OLED_ReverseArea(0, y, MF_SCREEN_WIDTH, MF_LINE_HEIGHT);
    }
}

static void MF_DrawTitle(const char *title)
{
    int x = 0;
    int width = MF_StrPixelWidth(title);

    if (width < MF_SCREEN_WIDTH)
    {
        x = (MF_SCREEN_WIDTH - width) / 2;
    }

    OLED_ShowString(x, 0, (char *)title, MF_FONT);
    OLED_DrawLine(0, MF_TITLE_HEIGHT, MF_SCREEN_WIDTH, MF_TITLE_HEIGHT);
}

static void MF_DrawProgressBar(int x, int y, int width, int height, float ratio)
{
    int fill_width = 0;

    if (ratio < 0.0f)
    {
        ratio = 0.0f;
    }
    else if (ratio > 1.0f)
    {
        ratio = 1.0f;
    }

    OLED_DrawRectangle(x, y, (uint8_t)width, (uint8_t)height, OLED_UNFILLED);

    fill_width = (int)((width - 2) * ratio);
    if (fill_width > 0)
    {
        OLED_DrawRectangle(x + 1, y + 1, (uint8_t)fill_width, (uint8_t)(height - 2), OLED_FILLED);
    }
}

static void MF_RenderMenuList(void)
{
    int i = 0;
    MF_Menu_t *menu = cur_menu;

    if (menu == NULL)
    {
        return;
    }

    MF_DrawTitle(menu->title);

    for (i = 0; i < MF_DISPLAY_LINES; i++)
    {
        int idx = menu->scroll_offset + i;
        int y = MF_TITLE_HEIGHT + i * MF_LINE_HEIGHT;
        MF_Item_t *item = NULL;

        if (idx >= menu->item_count)
        {
            break;
        }

        item = &menu->items[idx];

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
            const char *state_text = ((item->toggle.bool_ptr != NULL) && (*(item->toggle.bool_ptr) != 0U)) ? "ON" : "OFF";
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            OLED_ShowString(MF_SCREEN_WIDTH - MF_StrPixelWidth(state_text) - 4, y, (char *)state_text, MF_FONT);
            break;
        }

        case MF_ITEM_VALUE:
        {
            int32_t value = (item->value.value_ptr != NULL) ? *(item->value.value_ptr) : 0;
            const char *unit = (item->value.unit != NULL) ? item->value.unit : "";
            snprintf(line_buf, sizeof(line_buf), "%ld%s", (long)value, unit);
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            OLED_ShowString(MF_SCREEN_WIDTH - MF_StrPixelWidth(line_buf) - 4, y, line_buf, MF_FONT);
            break;
        }

        case MF_ITEM_CUSTOM_PAGE:
            snprintf(line_buf, sizeof(line_buf), "%s >", item->text);
            OLED_ShowString(4, y, line_buf, MF_FONT);
            break;

        default:
            break;
        }
    }

    if (menu->item_count > MF_DISPLAY_LINES)
    {
        if (menu->scroll_offset > 0)
        {
            OLED_ShowChar(120, MF_TITLE_HEIGHT, '^', MF_FONT);
        }

        if (menu->scroll_offset < (menu->item_count - MF_DISPLAY_LINES))
        {
            OLED_ShowChar(120, MF_SCREEN_HEIGHT - MF_LINE_HEIGHT, 'v', MF_FONT);
        }
    }

    anim_cursor_y = (float)(MF_TITLE_HEIGHT + (menu->select_index - menu->scroll_offset) * MF_LINE_HEIGHT);
    MF_DrawCursor((int)anim_cursor_y);
}

static void MF_HandleMenuInput(KeyEvent_t key)
{
    MF_Item_t *item = NULL;

    if ((cur_menu == NULL) || (cur_menu->item_count == 0U))
    {
        return;
    }

    if (key == KEY_DOWN)
    {
        cur_menu->select_index++;
        if (cur_menu->select_index >= cur_menu->item_count)
        {
            cur_menu->select_index = 0;
        }
        MF_UpdateScroll(cur_menu);
        return;
    }

    if (key == KEY_UP)
    {
        cur_menu->select_index--;
        if (cur_menu->select_index < 0)
        {
            cur_menu->select_index = (int8_t)(cur_menu->item_count - 1);
        }
        MF_UpdateScroll(cur_menu);
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
            *(item->toggle.bool_ptr) ^= 1U;
            if (item->toggle.on_change != NULL)
            {
                item->toggle.on_change();
            }
        }
        break;

    case MF_ITEM_VALUE:
        editing_item = item;
        if (item->value.value_ptr != NULL)
        {
            edit_backup = *(item->value.value_ptr);
            nav_mode = NAV_VALUE_EDIT;
        }
        break;

    case MF_ITEM_CUSTOM_PAGE:
        if (item->custom.render != NULL)
        {
            custom_page_cb = item->custom.render;
            nav_mode = NAV_CUSTOM_PAGE;
        }
        break;

    default:
        break;
    }
}

static void MF_HandleValueInput(KeyEvent_t key)
{
    int32_t *value_ptr = NULL;

    if ((editing_item == NULL) || (editing_item->value.value_ptr == NULL))
    {
        editing_item = NULL;
        nav_mode = NAV_MENU;
        return;
    }

    value_ptr = editing_item->value.value_ptr;

    if (key == KEY_UP)
    {
        *value_ptr += editing_item->value.step;
        if (*value_ptr > editing_item->value.max_val)
        {
            *value_ptr = editing_item->value.max_val;
        }
    }
    else if (key == KEY_DOWN)
    {
        *value_ptr -= editing_item->value.step;
        if (*value_ptr < editing_item->value.min_val)
        {
            *value_ptr = editing_item->value.min_val;
        }
    }
    else if (key == KEY_BACK)
    {
        *value_ptr = edit_backup;
        nav_mode = NAV_MENU;
        editing_item = NULL;
        return;
    }
    else if (key == KEY_ENTER)
    {
        nav_mode = NAV_MENU;
        editing_item = NULL;
        return;
    }

    if (editing_item != NULL && editing_item->value.on_change != NULL)
    {
        editing_item->value.on_change(*value_ptr);
    }
}

static void MF_RenderValueEdit(void)
{
    int32_t value = 0;
    const char *unit = "";
    int width = 0;
    int x = 0;
    int32_t range = 0;
    float ratio = 0.0f;

    if ((editing_item == NULL) || (editing_item->value.value_ptr == NULL))
    {
        return;
    }

    value = *(editing_item->value.value_ptr);
    unit = (editing_item->value.unit != NULL) ? editing_item->value.unit : "";

    MF_DrawTitle("Edit Value");
    OLED_ShowString(4, MF_TITLE_HEIGHT + 2, (char *)editing_item->text, MF_FONT);

    snprintf(line_buf, sizeof(line_buf), "< %ld %s >", (long)value, unit);
    width = MF_StrPixelWidth(line_buf);
    x = (width < MF_SCREEN_WIDTH) ? ((MF_SCREEN_WIDTH - width) / 2) : 0;
    OLED_ShowString(x, MF_TITLE_HEIGHT + 20, line_buf, MF_FONT);

    range = editing_item->value.max_val - editing_item->value.min_val;
    if (range > 0)
    {
        ratio = (float)(value - editing_item->value.min_val) / (float)range;
    }

    MF_DrawProgressBar(4, MF_TITLE_HEIGHT + 38, MF_SCREEN_WIDTH - 8, 6, ratio);
    OLED_ShowString(4, MF_SCREEN_HEIGHT - 10, "OK:Save  B:Cancel", OLED_6X8);
}

static void MF_HandleCustomInput(KeyEvent_t key)
{
    uint8_t exit_flag = 0U;

    if (custom_page_cb == NULL)
    {
        nav_mode = NAV_MENU;
        return;
    }

    custom_page_cb(key, &exit_flag);
    if (exit_flag != 0U)
    {
        custom_page_cb = NULL;
        nav_mode = NAV_MENU;
    }
}

MF_Menu_t *MF_CreateMenu(const char *title)
{
    MF_Menu_t *menu = NULL;

    if (menu_pool_count >= MF_MAX_MENUS)
    {
        return NULL;
    }

    menu = &menu_pool[menu_pool_count++];
    memset(menu, 0, sizeof(*menu));
    menu->title = title;
    return menu;
}

void MF_AddSubmenu(MF_Menu_t *parent, const char *text, MF_Menu_t *child)
{
    MF_Item_t *item = NULL;

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
    MF_Item_t *item = NULL;

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
    MF_Item_t *item = NULL;

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

void MF_AddValue(MF_Menu_t *parent,
                 const char *text,
                 int32_t *value_ptr,
                 int32_t min_v,
                 int32_t max_v,
                 int32_t step,
                 const char *unit,
                 MF_ValueChangeCb on_change)
{
    MF_Item_t *item = NULL;

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
    MF_Item_t *item = NULL;

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

void MF_EnterCustomPage(MF_CustomPageCb render_cb)
{
    if (render_cb == NULL)
    {
        return;
    }

    custom_page_cb = render_cb;
    editing_item = NULL;
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
    custom_page_cb = NULL;
    editing_item = NULL;
    edit_backup = 0;
    if (cur_menu != NULL)
    {
        MF_UpdateScroll(cur_menu);
        anim_cursor_y = (float)(MF_TITLE_HEIGHT + (cur_menu->select_index - cur_menu->scroll_offset) * MF_LINE_HEIGHT);
    }
    else
    {
        anim_cursor_y = 0.0f;
    }
}

void MF_Process(KeyEvent_t key)
{
    if (key == KEY_NONE)
    {
        return;
    }

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

    default:
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
        if (custom_page_cb != NULL)
        {
            uint8_t exit_flag = 0U;
            custom_page_cb(KEY_NONE, &exit_flag);
            if (exit_flag != 0U)
            {
                custom_page_cb = NULL;
                nav_mode = NAV_MENU;
            }
        }
        break;

    case NAV_VALUE_EDIT:
        MF_RenderValueEdit();
        break;

    default:
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
    memset(menu_pool, 0, sizeof(menu_pool));
    memset(nav_stack, 0, sizeof(nav_stack));

    menu_pool_count = 0U;
    nav_depth = -1;
    cur_menu = NULL;
    nav_mode = NAV_MENU;
    cursor_style = MF_CURSOR_REVERSE;
    custom_page_cb = NULL;
    editing_item = NULL;
    edit_backup = 0;
    anim_cursor_y = 0.0f;
}
