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

typedef struct
{
    uint8_t active;
    NavMode_t prev_nav_mode;
    MF_CustomPageCb prev_custom_page_cb;
    MF_Item_t *prev_editing_item;
    int32_t prev_edit_backup;
} OverlayState_t;

static MF_Menu_t menu_pool[MF_MAX_MENUS];
static uint8_t menu_pool_count = 0U;

static NavFrame_t nav_stack[MF_NAV_STACK_DEPTH];
static int8_t nav_depth = -1;

static MF_Menu_t *cur_menu = NULL;
static NavMode_t nav_mode = NAV_MENU;
static MF_CursorStyle_t cursor_style = MF_CURSOR_REVERSE;

static float anim_cursor_y = 0.0f;

static MF_CustomPageCb custom_page_cb = NULL;

static MF_Item_t *editing_item = NULL;
static int32_t edit_backup = 0;

static OverlayState_t overlay_state = {0};

static char line_buf[MF_LINE_BUF_SIZE];

static MF_Item_t *MF_GetSelectedItem(void)
{
    if ((cur_menu == NULL) || (cur_menu->item_count == 0U))
    {
        return NULL;
    }

    return &cur_menu->items[cur_menu->select_index];
}

static int MF_StrPixelWidth(const char *s)
{
    int len = 0;

    while ((s != NULL) && (*s != '\0'))
    {
        ++len;
        ++s;
    }

    return len * MF_FONT_WIDTH;
}

static void MF_FormatValueText(char *buf, size_t buf_size, const MF_Item_t *item, int32_t value)
{
    int32_t divisor;
    uint8_t frac_digits;
    const char *unit;

    if ((buf == NULL) || (buf_size == 0U))
    {
        return;
    }

    if (item == NULL)
    {
        snprintf(buf, buf_size, "%ld", (long)value);
        return;
    }

    divisor = item->value.display_divisor;
    frac_digits = item->value.fractional_digits;
    unit = (item->value.unit != NULL) ? item->value.unit : "";

    if ((divisor <= 1) || (frac_digits == 0U))
    {
        snprintf(buf, buf_size, "%ld%s", (long)value, unit);
        return;
    }

    if (value < 0)
    {
        int32_t abs_value = -value;
        snprintf(buf, buf_size, "-%ld.%0*ld%s",
                 (long)(abs_value / divisor),
                 (int)frac_digits,
                 (long)(abs_value % divisor),
                 unit);
    }
    else
    {
        snprintf(buf, buf_size, "%ld.%0*ld%s",
                 (long)(value / divisor),
                 (int)frac_digits,
                 (long)(value % divisor),
                 unit);
    }
}

static void MF_PushMenu(MF_Menu_t *menu)
{
    if ((menu == NULL) || (nav_depth >= (MF_NAV_STACK_DEPTH - 1)))
    {
        return;
    }

    ++nav_depth;
    nav_stack[nav_depth].menu = cur_menu;
    cur_menu = menu;
    anim_cursor_y = (float)MF_TITLE_HEIGHT;
}

static void MF_PopMenu(void)
{
    int8_t rel;

    if (nav_depth < 0)
    {
        return;
    }

    cur_menu = nav_stack[nav_depth].menu;
    --nav_depth;

    if (cur_menu == NULL)
    {
        return;
    }

    rel = cur_menu->select_index - cur_menu->scroll_offset;
    anim_cursor_y = (float)(MF_TITLE_HEIGHT + rel * MF_LINE_HEIGHT);
}

static void MF_UpdateScroll(MF_Menu_t *menu)
{
    int max_offset;

    if (menu == NULL)
    {
        return;
    }

    if (menu->select_index >= (menu->scroll_offset + MF_DISPLAY_LINES))
    {
        menu->scroll_offset = menu->select_index - MF_DISPLAY_LINES + 1;
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

static void MF_DrawCursor(int y, int width, int height)
{
    if (cursor_style == MF_CURSOR_REVERSE)
    {
        OLED_ReverseArea(0, y, width, height);
    }
    else
    {
        OLED_DrawRectangle(MF_CURSOR_PADDING,
                           y + MF_CURSOR_PADDING,
                           width - (2 * MF_CURSOR_PADDING),
                           height - (2 * MF_CURSOR_PADDING),
                           OLED_UNFILLED);
    }
}

static void MF_DrawTitle(const char *title)
{
    int text_w;
    int x;

    if (title == NULL)
    {
        title = "";
    }

    text_w = MF_StrPixelWidth(title);
    x = (MF_SCREEN_WIDTH - text_w) / 2;
    if (x < 0)
    {
        x = 0;
    }

    OLED_ShowString(x, 0, (char *)title, MF_FONT);
    OLED_DrawLine(0, MF_TITLE_HEIGHT, MF_SCREEN_WIDTH, MF_TITLE_HEIGHT);
}

static void MF_DrawProgressBar(int x, int y, int w, int h, float ratio)
{
    int fill_w;

    if (ratio < 0.0f)
    {
        ratio = 0.0f;
    }
    if (ratio > 1.0f)
    {
        ratio = 1.0f;
    }

    OLED_DrawRectangle(x, y, w, h, OLED_UNFILLED);

    fill_w = (int)((w - 2) * ratio);
    if (fill_w > 0)
    {
        OLED_DrawRectangle(x + 1, y + 1, fill_w, h - 2, OLED_FILLED);
    }
}

static void MF_RestoreOverlayState(void)
{
    nav_mode = overlay_state.prev_nav_mode;
    custom_page_cb = overlay_state.prev_custom_page_cb;
    editing_item = overlay_state.prev_editing_item;
    edit_backup = overlay_state.prev_edit_backup;
    memset(&overlay_state, 0, sizeof(overlay_state));
}

static void MF_CloseCurrentCustomPage(void)
{
    if (overlay_state.active != 0U)
    {
        MF_RestoreOverlayState();
    }
    else
    {
        nav_mode = NAV_MENU;
        custom_page_cb = NULL;
        editing_item = NULL;
    }
}

static void MF_RenderMenuList(void)
{
    MF_Menu_t *menu = cur_menu;
    int i;

    if (menu == NULL)
    {
        return;
    }

    MF_DrawTitle(menu->title);

    for (i = 0; i < MF_DISPLAY_LINES; ++i)
    {
        int idx = menu->scroll_offset + i;
        int y = MF_TITLE_HEIGHT + i * MF_LINE_HEIGHT;
        MF_Item_t *item;

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
            const char *state_str;
            int sw;
            uint8_t state = (item->toggle.bool_ptr != NULL) ? *(item->toggle.bool_ptr) : 0U;

            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            state_str = state ? "ON" : "OFF";
            sw = MF_StrPixelWidth(state_str);
            OLED_ShowString(MF_SCREEN_WIDTH - sw - 4, y, (char *)state_str, MF_FONT);
            break;
        }

        case MF_ITEM_VALUE:
        {
            int vw;
            int32_t val = (item->value.value_ptr != NULL) ? *(item->value.value_ptr) : 0;

            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            MF_FormatValueText(line_buf, sizeof(line_buf), item, val);
            vw = MF_StrPixelWidth(line_buf);
            OLED_ShowString(MF_SCREEN_WIDTH - vw - 4, y, line_buf, MF_FONT);
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
        if (menu->scroll_offset < ((int)menu->item_count - MF_DISPLAY_LINES))
        {
            OLED_ShowChar(120, MF_SCREEN_HEIGHT - MF_LINE_HEIGHT, 'v', MF_FONT);
        }
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

    if ((menu == NULL) || (menu->item_count == 0U))
    {
        return;
    }

    if (key == KEY_DOWN)
    {
        ++menu->select_index;
        if (menu->select_index >= menu->item_count)
        {
            menu->select_index = 0;
        }
        MF_UpdateScroll(menu);
    }
    else if (key == KEY_UP)
    {
        --menu->select_index;
        if (menu->select_index < 0)
        {
            menu->select_index = menu->item_count - 1;
        }
        MF_UpdateScroll(menu);
    }
    else if (key == KEY_ENTER)
    {
        MF_Item_t *item = MF_GetSelectedItem();

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
            }
            nav_mode = NAV_VALUE_EDIT;
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
    else if (key == KEY_BACK)
    {
        MF_PopMenu();
    }
}

static void MF_HandleValueInput(KeyEvent_t key)
{
    int32_t *pval;
    int32_t step;

    if ((editing_item == NULL) || (editing_item->value.value_ptr == NULL))
    {
        nav_mode = NAV_MENU;
        editing_item = NULL;
        return;
    }

    pval = editing_item->value.value_ptr;
    step = editing_item->value.step;

    if (key == KEY_UP)
    {
        *pval += step;
        if (*pval > editing_item->value.max_val)
        {
            *pval = editing_item->value.max_val;
        }
        if (editing_item->value.on_change != NULL)
        {
            editing_item->value.on_change(*pval);
        }
    }
    else if (key == KEY_DOWN)
    {
        *pval -= step;
        if (*pval < editing_item->value.min_val)
        {
            *pval = editing_item->value.min_val;
        }
        if (editing_item->value.on_change != NULL)
        {
            editing_item->value.on_change(*pval);
        }
    }
    else if (key == KEY_ENTER)
    {
        nav_mode = NAV_MENU;
        editing_item = NULL;
    }
    else if (key == KEY_BACK)
    {
        *pval = edit_backup;
        if (editing_item->value.on_change != NULL)
        {
            editing_item->value.on_change(*pval);
        }
        nav_mode = NAV_MENU;
        editing_item = NULL;
    }
}

static void MF_RenderValueEdit(void)
{
    int32_t val;
    int32_t range;
    float ratio = 0.0f;
    int vw;
    int vx;
    char value_buf[MF_LINE_BUF_SIZE];

    if ((editing_item == NULL) || (editing_item->value.value_ptr == NULL))
    {
        return;
    }

    val = *(editing_item->value.value_ptr);

    MF_DrawTitle("Edit Value");
    OLED_ShowString(4, MF_TITLE_HEIGHT + 2, (char *)editing_item->text, MF_FONT);

    MF_FormatValueText(value_buf, sizeof(value_buf), editing_item, val);
    snprintf(line_buf, sizeof(line_buf), "< %s >", value_buf);
    vw = MF_StrPixelWidth(line_buf);
    vx = (MF_SCREEN_WIDTH - vw) / 2;
    if (vx < 0)
    {
        vx = 0;
    }
    OLED_ShowString(vx, MF_TITLE_HEIGHT + 20, line_buf, MF_FONT);

    range = editing_item->value.max_val - editing_item->value.min_val;
    if (range > 0)
    {
        ratio = (float)(val - editing_item->value.min_val) / (float)range;
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
        MF_CloseCurrentCustomPage();
    }
}

static void MF_RenderCurrentMode(void)
{
    uint8_t rerender = 1U;

    while (rerender != 0U)
    {
        rerender = 0U;

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
                    MF_CloseCurrentCustomPage();
                    rerender = 1U;
                }
            }
            break;

        case NAV_VALUE_EDIT:
            MF_RenderValueEdit();
            break;

        default:
            break;
        }
    }
}

MF_Menu_t *MF_CreateMenu(const char *title)
{
    MF_Menu_t *menu;

    if (menu_pool_count >= MF_MAX_MENUS)
    {
        return NULL;
    }

    menu = &menu_pool[menu_pool_count++];
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

void MF_AddToggle(MF_Menu_t *parent, const char *text,
                  uint8_t *bool_ptr, MF_ActionCb on_change)
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

void MF_AddValue(MF_Menu_t *parent, const char *text,
                 int32_t *value_ptr, int32_t min_v, int32_t max_v, int32_t step,
                 const char *unit, MF_ValueChangeCb on_change)
{
    MF_AddValueEx(parent, text, value_ptr, min_v, max_v, step, 1, 0, unit, on_change);
}

void MF_AddValueEx(MF_Menu_t *parent, const char *text,
                   int32_t *value_ptr, int32_t min_v, int32_t max_v, int32_t step,
                   int32_t display_div, uint8_t frac_digits,
                   const char *unit, MF_ValueChangeCb on_change)
{
    MF_Item_t *item;

    if ((parent == NULL) || (parent->item_count >= MF_MAX_ITEMS_PER_MENU))
    {
        return;
    }

    if (display_div <= 0)
    {
        display_div = 1;
    }

    item = &parent->items[parent->item_count++];
    memset(item, 0, sizeof(MF_Item_t));
    item->text = text;
    item->type = MF_ITEM_VALUE;
    item->value.value_ptr = value_ptr;
    item->value.min_val = min_v;
    item->value.max_val = max_v;
    item->value.step = step;
    item->value.display_divisor = display_div;
    item->value.fractional_digits = frac_digits;
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
    custom_page_cb = NULL;
    editing_item = NULL;
    edit_backup = 0;
    memset(&overlay_state, 0, sizeof(overlay_state));
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
    MF_RenderCurrentMode();
    OLED_Update();
}

void MF_Loop(void)
{
    KeyEvent_t key = Key_Scan();
    MF_Process(key);
    MF_Render();
}

void MF_PushOverlayCustomPage(MF_CustomPageCb render_cb)
{
    if (render_cb == NULL)
    {
        return;
    }

    if ((overlay_state.active != 0U) || ((nav_mode == NAV_CUSTOM_PAGE) && (custom_page_cb == render_cb)))
    {
        return;
    }

    overlay_state.active = 1U;
    overlay_state.prev_nav_mode = nav_mode;
    overlay_state.prev_custom_page_cb = custom_page_cb;
    overlay_state.prev_editing_item = editing_item;
    overlay_state.prev_edit_backup = edit_backup;

    nav_mode = NAV_CUSTOM_PAGE;
    custom_page_cb = render_cb;
    editing_item = NULL;
}

uint8_t MF_IsCustomPageActive(MF_CustomPageCb render_cb)
{
    if ((nav_mode != NAV_CUSTOM_PAGE) || (custom_page_cb == NULL))
    {
        return 0U;
    }

    if (render_cb == NULL)
    {
        return 1U;
    }

    return (custom_page_cb == render_cb) ? 1U : 0U;
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
    menu_pool_count = 0U;
    memset(menu_pool, 0, sizeof(menu_pool));
    memset(nav_stack, 0, sizeof(nav_stack));

    nav_depth = -1;
    cur_menu = NULL;
    nav_mode = NAV_MENU;
    cursor_style = MF_CURSOR_REVERSE;
    anim_cursor_y = 0.0f;
    custom_page_cb = NULL;
    editing_item = NULL;
    edit_backup = 0;
    memset(&overlay_state, 0, sizeof(overlay_state));
    memset(line_buf, 0, sizeof(line_buf));
}
