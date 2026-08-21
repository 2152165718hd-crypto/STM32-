#ifndef __MENU_FRAMEWORK_H
#define __MENU_FRAMEWORK_H

#include <stdint.h>
#include <string.h>
#include "Hardware/OLED/OLED.h"
#include "Hardware/KEY/KEY.h"

#define MF_MAX_MENUS          20
#define MF_MAX_ITEMS_PER_MENU 10
#define MF_NAV_STACK_DEPTH    8
#define MF_DISPLAY_LINES      3
#define MF_LINE_HEIGHT        16
#define MF_TITLE_HEIGHT       16
#define MF_SCREEN_WIDTH       128
#define MF_SCREEN_HEIGHT      64
#define MF_FONT               OLED_8X16
#define MF_FONT_WIDTH         8
#define MF_ANIM_SPEED         0.45f
#define MF_CURSOR_PADDING     1
#define MF_LINE_BUF_SIZE      48

typedef enum
{
    MF_CURSOR_REVERSE = 0,
    MF_CURSOR_BOX,
} MF_CursorStyle_t;

typedef enum
{
    MF_ITEM_SUBMENU = 0,
    MF_ITEM_ACTION,
    MF_ITEM_TOGGLE,
    MF_ITEM_VALUE,
    MF_ITEM_CUSTOM_PAGE,
} MF_ItemType_t;

typedef struct MF_Menu_s MF_Menu_t;

typedef void (*MF_ActionCb)(void);
/* key == KEY_NONE means render-only for custom pages. */
typedef void (*MF_CustomPageCb)(KeyEvent_t key, uint8_t *exit_flag);
typedef void (*MF_ValueChangeCb)(int32_t new_value);

typedef struct
{
    const char *text;
    MF_ItemType_t type;

    union
    {
        struct
        {
            MF_Menu_t *child_menu;
        } submenu;

        struct
        {
            MF_ActionCb callback;
        } action;

        struct
        {
            uint8_t *bool_ptr;
            MF_ActionCb on_change;
        } toggle;

        struct
        {
            int32_t *value_ptr;
            int32_t min_val;
            int32_t max_val;
            int32_t step;
            int32_t display_divisor;
            uint8_t fractional_digits;
            const char *unit;
            MF_ValueChangeCb on_change;
        } value;

        struct
        {
            MF_CustomPageCb render;
        } custom;
    };
} MF_Item_t;

struct MF_Menu_s
{
    const char *title;
    MF_Item_t items[MF_MAX_ITEMS_PER_MENU];
    uint8_t item_count;
    int8_t select_index;
    int8_t scroll_offset;
};

MF_Menu_t *MF_CreateMenu(const char *title);
void MF_AddSubmenu(MF_Menu_t *parent, const char *text, MF_Menu_t *child);
void MF_AddAction(MF_Menu_t *parent, const char *text, MF_ActionCb cb);
void MF_AddToggle(MF_Menu_t *parent, const char *text,
                  uint8_t *bool_ptr, MF_ActionCb on_change);
void MF_AddValue(MF_Menu_t *parent, const char *text,
                 int32_t *value_ptr, int32_t min_v, int32_t max_v, int32_t step,
                 const char *unit, MF_ValueChangeCb on_change);
void MF_AddValueEx(MF_Menu_t *parent, const char *text,
                   int32_t *value_ptr, int32_t min_v, int32_t max_v, int32_t step,
                   int32_t display_div, uint8_t frac_digits,
                   const char *unit, MF_ValueChangeCb on_change);
void MF_AddCustomPage(MF_Menu_t *parent, const char *text, MF_CustomPageCb render_cb);

void MF_SetCursorStyle(MF_CursorStyle_t style);
MF_CursorStyle_t MF_GetCursorStyle(void);

void MF_Start(MF_Menu_t *root_menu);
void MF_Loop(void);
void MF_Process(KeyEvent_t key);
void MF_Render(void);

void MF_PushOverlayCustomPage(MF_CustomPageCb render_cb);
uint8_t MF_IsCustomPageActive(MF_CustomPageCb render_cb);

MF_Menu_t *MF_GetCurrentMenu(void);
int8_t MF_GetNavDepth(void);
void MF_Reset(void);

#endif /* __MENU_FRAMEWORK_H */
