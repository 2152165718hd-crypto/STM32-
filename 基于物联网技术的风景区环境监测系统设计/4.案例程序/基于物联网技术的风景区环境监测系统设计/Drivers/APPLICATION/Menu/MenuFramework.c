#include "APPLICATION/Menu/MenuFramework.h"
#include <stdio.h>

/* ==================== 鍐呴儴鐘舵€?==================== */

/** 瀵艰埅妯″紡 */
typedef enum
{
    NAV_MENU = 0,    /**< 姝ｅ父鑿滃崟鍒楄〃娴忚   */
    NAV_CUSTOM_PAGE, /**< 鑷畾涔夋暣椤垫ā寮?    */
    NAV_VALUE_EDIT,  /**< 鏁板€肩紪杈戞ā寮?      */
} NavMode_t;

/** 瀵艰埅鏍堝抚 */
typedef struct
{
    MF_Menu_t *menu;
} NavFrame_t;

/* --- 闈欐€佸瓨鍌ㄦ睜 --- */
static MF_Menu_t menu_pool[MF_MAX_MENUS];
static uint8_t   menu_pool_count = 0;

/* --- 瀵艰埅鏍?--- */
static NavFrame_t nav_stack[MF_NAV_STACK_DEPTH];
static int8_t     nav_depth = -1; /* 褰撳墠鏍堥《绱㈠紩, -1 琛ㄧず绌?*/

/* --- 褰撳墠鐘舵€?--- */
static MF_Menu_t       *cur_menu     = NULL;
static NavMode_t        nav_mode     = NAV_MENU;
static MF_CursorStyle_t cursor_style = MF_CURSOR_REVERSE;

/* --- 鍔ㄧ敾 --- */
static float anim_cursor_y = 0.0f;

/* --- 鑷畾涔夐〉闈复鏃跺瓨鍌?--- */
static MF_CustomPageCb custom_page_cb = NULL;

/* --- 鏁板€肩紪杈戜复鏃跺瓨鍌?--- */
static MF_Item_t *editing_item   = NULL;
static int32_t    edit_backup    = 0; /* 杩涘叆缂栬緫鍓嶇殑澶囦唤鍊?*/

/* --- 琛屾覆鏌撶紦鍐插尯 --- */
static char line_buf[MF_LINE_BUF_SIZE];

/* ==================== 鍐呴儴杈呭姪鍑芥暟 ==================== */

/**
 * @brief  鑾峰彇褰撳墠鑿滃崟鐨勫綋鍓嶉€変腑鏉＄洰
 * @retval 鏉＄洰鎸囬拡; 鑿滃崟涓虹┖鏃惰繑鍥?NULL
 */
static MF_Item_t *MF_GetSelectedItem(void)
{
    if (!cur_menu || cur_menu->item_count == 0)
        return NULL;
    return &cur_menu->items[cur_menu->select_index];
}

/**
 * @brief  璁＄畻瀛楃涓插儚绱犲搴?(绛夊瀛椾綋)
 */
static int MF_StrPixelWidth(const char *s)
{
    int len = 0;
    while (*s++) len++;
    return len * MF_FONT_WIDTH;
}

static float MF_GetCursorTargetY(void)
{
    int8_t rel;

    if ((nav_mode != NAV_MENU) || (cur_menu == NULL) || (cur_menu->item_count == 0))
    {
        return anim_cursor_y;
    }

    rel = cur_menu->select_index - cur_menu->scroll_offset;
    return (float)(MF_TITLE_HEIGHT + rel * MF_LINE_HEIGHT);
}

/**
 * @brief  鍘嬫爤锛氳繘鍏ュ瓙鑿滃崟
 * @note   鏍堟弧鏃舵湰娆℃搷浣滃皢琚?**涓㈠純**锛屼笉浼氫慨鏀?cur_menu
 */
static void MF_PushMenu(MF_Menu_t *menu)
{
    if (nav_depth >= MF_NAV_STACK_DEPTH - 1)
        return; /* 鏍堟弧 鈥?瀹夊叏閫€鍑?*/

    nav_depth++;
    nav_stack[nav_depth].menu = cur_menu;
    cur_menu = menu;

    /* 閲嶇疆鍔ㄧ敾璧风偣, 涓嶉噸缃瓙鑿滃崟鐨勯€変腑鐘舵€?*/
    anim_cursor_y = (float)MF_TITLE_HEIGHT;
}

/**
 * @brief  寮规爤锛氳繑鍥炰笂绾? */
static void MF_PopMenu(void)
{
    if (nav_depth < 0)
        return; /* 宸插湪鏍硅彍鍗?鈥?鏃犳搷浣?*/

    cur_menu = nav_stack[nav_depth].menu;
    nav_depth--;

    /* 鎭㈠鍏夋爣鍔ㄧ敾浣嶇疆 */
    int8_t rel = cur_menu->select_index - cur_menu->scroll_offset;
    anim_cursor_y = (float)(MF_TITLE_HEIGHT + rel * MF_LINE_HEIGHT);
}

/**
 * @brief  鏇存柊婊氬姩鍋忕Щ锛屼繚璇侀€変腑椤瑰彲瑙? */
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

/* ==================== 娓叉煋杈呭姪 ==================== */

/**
 * @brief  缁樺埗鍏夋爣锛堝弽鑹叉垨鏂规锛? */
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
 * @brief  娓叉煋鏍囬鏍忥紙灞呬腑 + 鍒嗛殧绾匡級
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
 * @brief  缁樺埗杩涘害鏉?(鐢ㄤ簬鏁板€肩紪杈戠晫闈?
 * @param  x, y      宸︿笂瑙掑潗鏍? * @param  w, h      澶栨灏哄
 * @param  ratio     褰撳墠姣斾緥 (0.0 ~ 1.0)
 */
static void MF_DrawProgressBar(int x, int y, int w, int h, float ratio)
{
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    /* 澶栨 */
    OLED_DrawRectangle(x, y, w, h, OLED_UNFILLED);

    /* 鍐呴儴濉厖 (鐣?1px 鍐呰竟璺? */
    int fill_w = (int)((w - 2) * ratio);
    if (fill_w > 0)
        OLED_DrawRectangle(x + 1, y + 1, fill_w, h - 2, OLED_FILLED);
}

/**
 * @brief  娓叉煋鑿滃崟鍒楄〃锛堝惈鍔ㄧ敾鍏夋爣锛? */
static void MF_RenderMenuList(void)
{
    MF_Menu_t *m = cur_menu;
    float target_y;
    float diff;
    float abs_diff;
    int cursor_y;

    /* 鏍囬 */
    MF_DrawTitle(m->title);

    /* 鑿滃崟鏉＄洰 */
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
            /* 宸︿晶鏂囨湰 */
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            /* 鍙充晶鐘舵€?(鍙冲榻? */
            const char *state_str = state ? "ON" : "OFF";
            int sw = MF_StrPixelWidth(state_str);
            OLED_ShowString(MF_SCREEN_WIDTH - sw - 4, y, (char *)state_str, MF_FONT);
            break;
        }

        case MF_ITEM_VALUE:
        {
            int32_t val = item->value.value_ptr ? *(item->value.value_ptr) : 0;
            const char *u = item->value.unit ? item->value.unit : "";
            /* 宸︿晶鏉＄洰鍚?*/
            OLED_ShowString(4, y, (char *)item->text, MF_FONT);
            /* 鍙充晶鏁板€?(鍙冲榻? */
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

    /* 婊氬姩鎸囩ず绠ご */
    if (m->item_count > MF_DISPLAY_LINES)
    {
        if (m->scroll_offset > 0)
            OLED_ShowChar(120, MF_TITLE_HEIGHT, '^', MF_FONT);
        if (m->scroll_offset < m->item_count - MF_DISPLAY_LINES)
            OLED_ShowChar(120, MF_SCREEN_HEIGHT - MF_LINE_HEIGHT, 'v', MF_FONT);
    }

    /* 鍏夋爣鍔ㄧ敾 (绾挎€ф彃鍊? */
    target_y = MF_GetCursorTargetY();
    diff = target_y - anim_cursor_y;
    abs_diff = (diff >= 0.0f) ? diff : -diff;

    if (abs_diff <= MF_ANIM_SNAP_DISTANCE)
    {
        anim_cursor_y = target_y;
    }
    else
    {
        anim_cursor_y += diff * MF_ANIM_SPEED;
    }

    cursor_y = (int)(anim_cursor_y + 0.5f);
    MF_DrawCursor(cursor_y, MF_SCREEN_WIDTH, MF_LINE_HEIGHT);
}

/* ==================== 鑿滃崟鍒楄〃妯″紡澶勭悊 ==================== */
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
            /* 杩涘叆鏁板€肩紪杈戞ā寮? 澶囦唤鍘熷€间互渚?BACK 鍙栨秷 */
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

/* ==================== 鏁板€肩紪杈戞ā寮?==================== */
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
        /* 纭 鈥?鐩存帴閫€鍑虹紪杈?*/
        nav_mode = NAV_MENU;
        editing_item = NULL;
        return;
    }
    else if (key == KEY_BACK)
    {
        /* 鍙栨秷 鈥?鎭㈠鍘熷€煎悗閫€鍑虹紪杈?*/
        *pval = edit_backup;
        if (editing_item->value.on_change)
            editing_item->value.on_change(*pval);
        nav_mode = NAV_MENU;
        editing_item = NULL;
        return;
    }
}

/**
 * @brief  娓叉煋鏁板€肩紪杈戠晫闈? */
static void MF_RenderValueEdit(void)
{
    if (!editing_item || !editing_item->value.value_ptr)
        return;

    int32_t    val = *(editing_item->value.value_ptr);
    const char *u  = editing_item->value.unit ? editing_item->value.unit : "";

    /* 鏍囬 */
    MF_DrawTitle("Edit Value");

    /* 鏉＄洰鍚嶇О */
    OLED_ShowString(4, MF_TITLE_HEIGHT + 2, (char *)editing_item->text, MF_FONT);

    /* 褰撳墠鍊?(灞呬腑鏄剧ず) */
    snprintf(line_buf, sizeof(line_buf), "< %ld %s >", (long)val, u);
    int vw = MF_StrPixelWidth(line_buf);
    int vx = (MF_SCREEN_WIDTH - vw) / 2;
    if (vx < 0) vx = 0;
    OLED_ShowString(vx, MF_TITLE_HEIGHT + 20, line_buf, MF_FONT);

    /* 杩涘害鏉?*/
    int32_t range = editing_item->value.max_val - editing_item->value.min_val;
    float ratio = 0.0f;
    if (range > 0)
        ratio = (float)(val - editing_item->value.min_val) / (float)range;
    MF_DrawProgressBar(4, MF_TITLE_HEIGHT + 38, MF_SCREEN_WIDTH - 8, 6, ratio);

    /* 搴曢儴鎻愮ず */
    OLED_ShowString(4, MF_SCREEN_HEIGHT - 10, "OK:Save  B:Cancel", OLED_6X8);
}

/* ==================== 鑷畾涔夐〉闈㈡ā寮?==================== */
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

static void MF_RenderCustomPage(void)
{
    uint8_t exit_flag = 0;

    if (!custom_page_cb)
    {
        nav_mode = NAV_MENU;
        return;
    }

    custom_page_cb(KEY_NONE, &exit_flag);

    if (exit_flag)
    {
        nav_mode = NAV_MENU;
        custom_page_cb = NULL;
    }
}

/* ==================== 鍏紑 API 瀹炵幇 ==================== */

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

void MF_OpenCustomPage(MF_CustomPageCb render_cb)
{
    if (render_cb == NULL)
    {
        return;
    }

    custom_page_cb = render_cb;
    nav_mode = NAV_CUSTOM_PAGE;
    editing_item = NULL;
    edit_backup = 0;
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

/* ----------- 瑙ｈ€?API ----------- */

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
        MF_RenderCustomPage();
        /* 鑷畾涔夐〉闈㈢敱鍥炶皟鑷缁樺埗, 宸插湪 Process 涓皟鐢?*/
        break;
    case NAV_VALUE_EDIT:
        MF_RenderValueEdit();
        break;
    }

    OLED_Update();
}

uint8_t MF_IsAnimating(void)
{
    float diff;

    if ((nav_mode != NAV_MENU) || (cur_menu == NULL) || (cur_menu->item_count == 0))
    {
        return 0u;
    }

    diff = MF_GetCursorTargetY() - anim_cursor_y;
    if (diff < 0.0f)
    {
        diff = -diff;
    }

    return (diff > MF_ANIM_SNAP_DISTANCE) ? 1u : 0u;
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
