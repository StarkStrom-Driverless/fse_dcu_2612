/**
 * @file        ui_styles.c
 * @brief       LVGL style initialisations for the DCU UI
 *
 * @ingroup     dcu_ui_styles
 *
 * @details     Allocates and initialises every shared lv_style_t instance
 *              declared in ui_styles.h. The instances have external linkage;
 *              the header declares them extern.
 *
 *              One init function per style family keeps ui_styles_init() a
 *              readable list and gives each group of related settings a place
 *              to grow.
 *
 *              ### Header gradient
 *              The ACCENT → DARK gradient requires a lv_grad_dsc_t that must
 *              outlive the style that references it — lv_style_set_bg_grad()
 *              stores the pointer, not a copy — so it is a static in this
 *              translation unit.
 *
 *              Its stops currently span a single pixel, which turns the
 *              gradient into a hard color edge. The commented-out lines in
 *              init_header_style() show the original intent: a fade across the
 *              full display width, queried from the default LVGL display.
 *
 *              The colors are repeated as literals there because
 *              lv_grad_init_stops() needs a compile-time constant array, which
 *              the lv_color_hex() macros in the palette cannot provide. Keep
 *              them in sync with UI_C_ACCENT and UI_C_DARK by hand.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-02
 *
 * @version     0.1.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

/*
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * Version  Date        Author          Description
 * 0.1.0    2026-06-02  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_styles.h"


/* ── Style Instance Definitions ─────────────────────────────────────────────────────────────── */

lv_style_t ui_style_screen;

lv_style_t ui_style_header;

lv_style_t ui_style_card;

lv_style_t ui_style_label_subtitle;
lv_style_t ui_style_label_unit;
lv_style_t ui_style_label_title;
lv_style_t ui_style_label_caption;
lv_style_t ui_style_label_value_lg;
lv_style_t ui_style_label_value_md;

lv_style_t ui_style_btn_default;
lv_style_t ui_style_btn_checked;
lv_style_t ui_style_btn_focused;

lv_style_t ui_style_slider_main;
lv_style_t ui_style_slider_indicator;

lv_style_t ui_style_status_ok;
lv_style_t ui_style_status_warn;
lv_style_t ui_style_status_fault;

lv_style_t ui_style_level_warn;
lv_style_t ui_style_level_crit;
lv_style_t ui_style_level_warn_indicator;
lv_style_t ui_style_level_crit_indicator;


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/**
 * @brief Gradient descriptor for the header bar (ACCENT → DARK, horizontal).
 *
 * Must remain valid for the entire lifetime of ui_style_header because
 * lv_style_set_bg_grad() stores a pointer to this struct.
 */
static lv_grad_dsc_t s_header_grad;


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

/*
 * One initialiser per style family. Each is called exactly once, from
 * ui_styles_init(), and has no dependency on the others.
 */
static void init_screen_styles(void);
static void init_header_style(void);
static void init_card_style(void);
static void init_label_styles(void);
static void init_button_styles(void);
static void init_slider_styles(void);
static void init_status_styles(void);
static void init_level_styles(void);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/** @brief Screen background: flat off-white, edge to edge, no decoration. */
static void init_screen_styles(void)
{
    lv_style_init(&ui_style_screen);
    lv_style_set_bg_opa(&ui_style_screen,    LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_screen,  UI_C_BG);
    lv_style_set_border_width(&ui_style_screen, 0);
    lv_style_set_radius(&ui_style_screen,    0);
    lv_style_set_pad_all(&ui_style_screen,   0);
}

/** @brief Header bar: ACCENT → DARK gradient, square, no padding. */
static void init_header_style(void)
{
    /*
     * Horizontal gradient: UI_C_ACCENT (gold) on the left, UI_C_DARK on the
     * right.  EXTEND_PAD fills everything outside the stop range with the
     * nearer endpoint color.
     *
     * The commented-out pair below is the original full-width fade: the end
     * x-coordinate came from the display's horizontal resolution, so it
     * adapted to either panel.  The active call places both stops one pixel
     * apart, so EXTEND_PAD turns the fade into a hard split at x = 209.
     */
    static const lv_color_t grad_colors[2] = {
        LV_COLOR_MAKE(0xDD, 0xCC, 0x00),   /* UI_C_ACCENT */
        LV_COLOR_MAKE(0x1A, 0x1A, 0x1A),   /* UI_C_DARK   */
    };
    static const lv_opa_t grad_opa[2] = { LV_OPA_COVER, LV_OPA_COVER };

    // lv_display_t *disp = lv_display_get_default();
    // int32_t disp_w = lv_display_get_horizontal_resolution(disp);

    lv_grad_init_stops(&s_header_grad, grad_colors, grad_opa, NULL, 2);
    // lv_grad_linear_init(&s_header_grad, 0, 0, disp_w, 0, LV_GRAD_EXTEND_PAD);
    lv_grad_linear_init(&s_header_grad, 209, 9, 210, 10, LV_GRAD_EXTEND_PAD);

    lv_style_init(&ui_style_header);
    lv_style_set_bg_grad(&ui_style_header,    &s_header_grad);
    lv_style_set_bg_opa(&ui_style_header,     LV_OPA_COVER);
    lv_style_set_radius(&ui_style_header,     0);
    lv_style_set_border_width(&ui_style_header, 0);
    lv_style_set_pad_all(&ui_style_header,    0);
}

/** @brief Card panel: white surface with a thin border, for grouped content. */
static void init_card_style(void)
{
    lv_style_init(&ui_style_card);
    lv_style_set_bg_opa(&ui_style_card,       LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_card,     UI_C_WHITE);
    lv_style_set_border_width(&ui_style_card, 1);
    lv_style_set_border_color(&ui_style_card, UI_C_BORDER);
    lv_style_set_radius(&ui_style_card,       0);
    lv_style_set_pad_all(&ui_style_card,      8);
}

/** @brief Label styles: one per typographic role, all in UI_C_DARK. */
static void init_label_styles(void)
{
    /* subtitle — widget captions and slider / button titles */
    lv_style_init(&ui_style_label_subtitle);
    lv_style_set_text_font(&ui_style_label_subtitle,  &BarlowCondensed_BoldItalic_18);
    lv_style_set_text_color(&ui_style_label_subtitle, UI_C_DARK);

    /* unit — unit suffix text (%, rpm, V, °C …) */
    lv_style_init(&ui_style_label_unit);
    lv_style_set_text_font(&ui_style_label_unit,  &BarlowCondensed_Italic_20);
    lv_style_set_text_color(&ui_style_label_unit, UI_C_DARK);

    /* title — screen titles, header text, medium numeric values */
    lv_style_init(&ui_style_label_title);
    lv_style_set_text_font(&ui_style_label_title,  &BarlowCondensed_BoldItalic_32);
    lv_style_set_text_color(&ui_style_label_title, UI_C_DARK);

    /* caption — screen sub-headings and secondary captions */
    lv_style_init(&ui_style_label_caption);
    lv_style_set_text_font(&ui_style_label_caption,  &BarlowCondensed_Italic_44);
    lv_style_set_text_color(&ui_style_label_caption, UI_C_DARK);

    /* value_lg — hero numeric display (RPM, SoC …) */
    lv_style_init(&ui_style_label_value_lg);
    lv_style_set_text_font(&ui_style_label_value_lg,  &BarlowCondensed_BoldItalic_100);
    lv_style_set_text_color(&ui_style_label_value_lg, UI_C_DARK);

    /* value_md — hero numeric display (RPM, SoC …) */
    lv_style_init(&ui_style_label_value_md);
    lv_style_set_text_font(&ui_style_label_value_md,  &BarlowCondensed_BoldItalic_80);
    lv_style_set_text_color(&ui_style_label_value_md, UI_C_DARK);
}

/**
 * @brief Button styles for the three visual states.
 *
 * Outline rather than border, so the frame does not eat into the button's
 * content area and the three states stay the same size.
 */
static void init_button_styles(void)
{
    /* Default — white bg, dark outline, dark text */
    lv_style_init(&ui_style_btn_default);
    lv_style_set_radius(&ui_style_btn_default,        0);
    lv_style_set_bg_opa(&ui_style_btn_default,        LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_btn_default,      UI_C_WHITE);
    lv_style_set_outline_width(&ui_style_btn_default, 2);
    lv_style_set_outline_color(&ui_style_btn_default, UI_C_DARK);
    lv_style_set_text_color(&ui_style_btn_default,    UI_C_DARK);
    lv_style_set_pad_all(&ui_style_btn_default,       2);

    /* Checked — green bg, dark outline, white text */
    lv_style_init(&ui_style_btn_checked);
    lv_style_set_radius(&ui_style_btn_checked,        0);
    lv_style_set_bg_opa(&ui_style_btn_checked,        LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_btn_checked,      UI_C_GREEN);
    lv_style_set_outline_width(&ui_style_btn_checked, 2);
    lv_style_set_outline_color(&ui_style_btn_checked, UI_C_DARK);
    lv_style_set_text_color(&ui_style_btn_checked,    UI_C_WHITE);
    lv_style_set_pad_all(&ui_style_btn_checked,       2);

    /* Focused (encoder) — accent (gold) bg, dark outline, dark text */
    lv_style_init(&ui_style_btn_focused);
    lv_style_set_radius(&ui_style_btn_focused,        0);
    lv_style_set_bg_opa(&ui_style_btn_focused,        LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_btn_focused,      UI_C_ACCENT);
    lv_style_set_outline_width(&ui_style_btn_focused, 2);
    lv_style_set_outline_color(&ui_style_btn_focused, UI_C_DARK);
    lv_style_set_text_color(&ui_style_btn_focused,    UI_C_DARK);
    lv_style_set_pad_all(&ui_style_btn_focused,       2);
}

/**
 * @brief Slider and bar styles: track (LV_PART_MAIN) and fill (LV_PART_INDICATOR).
 *
 * Callers must lv_obj_remove_style_all() first — these replace the LVGL
 * defaults rather than layering on top of them.
 */
static void init_slider_styles(void)
{
    /* Track (LV_PART_MAIN) — white bg with dark outline */
    lv_style_init(&ui_style_slider_main);
    lv_style_set_bg_opa(&ui_style_slider_main,        LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_slider_main,      UI_C_WHITE);
    lv_style_set_outline_color(&ui_style_slider_main, UI_C_DARK);
    lv_style_set_outline_width(&ui_style_slider_main, 2);
    lv_style_set_radius(&ui_style_slider_main,        0);

    /* Fill (LV_PART_INDICATOR) — solid green */
    lv_style_init(&ui_style_slider_indicator);
    lv_style_set_bg_opa(&ui_style_slider_indicator,   LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_slider_indicator, UI_C_GREEN);
    lv_style_set_radius(&ui_style_slider_indicator,   0);
}

/** @brief Circular status dots. Currently unused; see ui_styles.h. */
static void init_status_styles(void)
{
    /* OK indicator — green circle */
    lv_style_init(&ui_style_status_ok);
    lv_style_set_bg_opa(&ui_style_status_ok,    LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_status_ok,  UI_C_GREEN);
    lv_style_set_border_width(&ui_style_status_ok, 0);
    lv_style_set_radius(&ui_style_status_ok,    LV_RADIUS_CIRCLE);

    /* Warning indicator — gold/accent circle */
    lv_style_init(&ui_style_status_warn);
    lv_style_set_bg_opa(&ui_style_status_warn,   LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_status_warn, UI_C_ACCENT);
    lv_style_set_border_width(&ui_style_status_warn, 0);
    lv_style_set_radius(&ui_style_status_warn,   LV_RADIUS_CIRCLE);

    /* Fault indicator — red circle */
    lv_style_init(&ui_style_status_fault);
    lv_style_set_bg_opa(&ui_style_status_fault,   LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_status_fault, UI_C_RED);
    lv_style_set_border_width(&ui_style_status_fault, 0);
    lv_style_set_radius(&ui_style_status_fault,   LV_RADIUS_CIRCLE);
}

/**
 * @brief Warning and critical level styles, for text and for indicator fills.
 *
 * Applied together with lv_obj_bind_state_if_lt/gt() and the threshold defines
 * generated into ui_subjects_gen.h, which is what makes a value color itself
 * without any per-frame code.
 *
 * Text and fill are separate styles because a spangroup needs text_color while
 * a bar needs bg_color on LV_PART_INDICATOR.
 */
static void init_level_styles(void)
{
    /* Warning text — gold/accent (UI_STATE_WARN, labels/spans) */
    lv_style_init(&ui_style_level_warn);
    lv_style_set_text_color(&ui_style_level_warn, UI_C_ACCENT);

    /* Critical text — red (UI_STATE_CRIT, higher priority) */
    lv_style_init(&ui_style_level_crit);
    lv_style_set_text_color(&ui_style_level_crit, UI_C_RED);

    /* Warning indicator fill — gold/accent (UI_STATE_WARN | LV_PART_INDICATOR) */
    lv_style_init(&ui_style_level_warn_indicator);
    lv_style_set_bg_opa(&ui_style_level_warn_indicator,   LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_level_warn_indicator, UI_C_ACCENT);

    /* Critical indicator fill — red (UI_STATE_CRIT | LV_PART_INDICATOR) */
    lv_style_init(&ui_style_level_crit_indicator);
    lv_style_set_bg_opa(&ui_style_level_crit_indicator,   LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_level_crit_indicator, UI_C_RED);
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

void ui_styles_init(void)
{
    init_screen_styles();
    init_header_style();
    init_card_style();
    init_label_styles();
    init_button_styles();
    init_slider_styles();
    init_status_styles();
    init_level_styles();
}
