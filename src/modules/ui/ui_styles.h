/**
 * @file        ui_styles.h
 * @brief       Shared LVGL styles, colours, and font declarations for the DCU UI
 *
 * @details     Defines the visual design language used across all screens:
 *
 *              Colour palette
 *              ──────────────
 *              UI_C_BG      Warm off-white — screen background
 *              UI_C_DARK    Near-black     — text, borders, header gradient end
 *              UI_C_ACCENT  FSE gold       — header gradient start, focus highlight
 *              UI_C_GREEN   Racing green   — OK state, active indicator, progress fill
 *              UI_C_RED     Fault red      — error / safety-fault indicator
 *              UI_C_WHITE   Pure white     — widget backgrounds
 *              UI_C_BAR_BG  Warm beige     — progress-bar track background
 *              UI_C_BORDER  Dark grey      — border lines
 *
 *              Typography  (Barlow Condensed font family)
 *              ──────────────────────────────────────────
 *              BarlowCondensed_BoldItalic_18   subtitle / widget captions
 *              BarlowCondensed_Italic_20        unit suffixes (%, rpm, V …)
 *              BarlowCondensed_BoldItalic_32    screen titles, medium values
 *              BarlowCondensed_Italic_44        screen sub-headings
 *              BarlowCondensed_BoldItalic_100   hero numeric display
 *
 *              Styles
 *              ──────
 *              One global lv_style_t instance per semantic role. All styles
 *              are initialised by ui_styles_init(); do not call lv_obj_add_style()
 *              before that function returns.
 *
 *              Intended usage:
 *              @code
 *                  lv_obj_add_style(obj, &ui_style_btn_default, 0);
 *                  lv_obj_add_style(obj, &ui_style_btn_checked, LV_STATE_CHECKED);
 *                  lv_obj_add_style(obj, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
 *              @endcode
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-02
 *
 * @version     0.1.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann
 *              SPDX-License-Identifier: Apache-2.0
 *
 * @note        Target RTOS : Zephyr RTOS (https://zephyrproject.org)
 *              UI Library  : LVGL (https://lvgl.io)
 *
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Version  Date        Author          Description
 * 0.1.0    2026-06-02  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_UI_STYLES_H
#define MODULES_UI_UI_STYLES_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Colour Palette ──────────────────────────────────────────────────────────────────────────── */

/** @brief Warm off-white — used as the default screen background. */
#define UI_C_BG         lv_color_hex(0xFAF8F3)

/** @brief Near-black — primary text colour and widget border colour. */
#define UI_C_DARK       lv_color_hex(0x1A1A1A)

/** @brief FSE gold / yellow — accent, focus highlight, header gradient start. */
#define UI_C_ACCENT     lv_color_hex(0xDDCC00)

/** @brief Racing green — OK / connected / active / progress-bar fill. */
#define UI_C_GREEN      lv_color_hex(0x15803D)

/** @brief Fault red — error conditions and safety-fault indicators. */
#define UI_C_RED        lv_color_hex(0xDC2626)

/** @brief Pure white — widget backgrounds (sliders, buttons, cards). */
#define UI_C_WHITE      lv_color_hex(0xFFFFFF)

/** @brief Warm beige — progress-bar track background. */
#define UI_C_BAR_BG     lv_color_hex(0xF5F0E0)

/** @brief Dark grey — secondary border lines. */
#define UI_C_BORDER     lv_color_hex(0x333333)


/* ── Font Declarations ───────────────────────────────────────────────────────────────────────── */

/*
 * Custom Barlow Condensed fonts compiled into the firmware.
 * The LV_FONT_DECLARE macro makes the font symbol externally visible so it
 * can be referenced from multiple translation units.
 */

/** @brief 18 pt bold-italic — widget captions, slider / button titles. */
LV_FONT_DECLARE(BarlowCondensed_BoldItalic_18)

/** @brief 20 pt italic — unit suffixes (%, rpm, V, °C …). */
LV_FONT_DECLARE(BarlowCondensed_Italic_20)

/** @brief 32 pt bold-italic — screen titles, header text, medium values. */
LV_FONT_DECLARE(BarlowCondensed_BoldItalic_32)

/** @brief 44 pt italic — screen sub-headings and secondary captions. */
LV_FONT_DECLARE(BarlowCondensed_Italic_44)

/** @brief 80 pt bold-italic — hero numeric display (RPM, SoC …). */
LV_FONT_DECLARE(BarlowCondensed_BoldItalic_80)

/** @brief 100 pt bold-italic — hero numeric display (RPM, SoC …). */
LV_FONT_DECLARE(BarlowCondensed_BoldItalic_100)


/* ── Style Declarations ──────────────────────────────────────────────────────────────────────── */

/*
 * All styles are allocated in ui_styles.c and initialised by ui_styles_init().
 * Screen builders add styles to objects; they never call lv_style_init() on
 * these themselves.
 */

/* --- Screen base ------------------------------------------------------------------ */

/**
 * @brief Base style applied to every screen object.
 *
 * Sets the warm off-white background and removes default padding, border,
 * and radius so screens fill the display edge-to-edge.
 */
extern lv_style_t ui_style_screen;

/* --- Header bar ------------------------------------------------------------------- */

/**
 * @brief Full-width header bar with a horizontal ACCENT → DARK gradient.
 *
 * Apply to an lv_obj sized to lv_pct(100) × 15 % of screen height.
 * Gradient end coordinate is computed from the display width at init time.
 */
extern lv_style_t ui_style_header;

/* --- Card / container panel ------------------------------------------------------- */

/**
 * @brief Bordered white panel for grouping related content.
 *
 * White background with a 1 px BORDER-coloured border, 8 px internal
 * padding, and no radius (square corners match the overall aesthetic).
 */
extern lv_style_t ui_style_card;

/* --- Labels ----------------------------------------------------------------------- */

/**
 * @brief Widget caption and slider / button title text.
 * Font: BarlowCondensed_BoldItalic_18. Colour: UI_C_DARK.
 */
extern lv_style_t ui_style_label_subtitle;

/**
 * @brief Unit-suffix text (%, rpm, V, °C …).
 * Font: BarlowCondensed_Italic_20. Colour: UI_C_DARK.
 */
extern lv_style_t ui_style_label_unit;

/**
 * @brief Screen title and medium numeric value text.
 * Font: BarlowCondensed_BoldItalic_32. Colour: UI_C_DARK.
 */
extern lv_style_t ui_style_label_title;

/**
 * @brief Screen sub-heading and secondary caption text.
 * Font: BarlowCondensed_Italic_44. Colour: UI_C_DARK.
 */
extern lv_style_t ui_style_label_caption;

/**
 * @brief Hero numeric display (RPM, SoC …).
 * Font: BarlowCondensed_BoldItalic_100. Colour: UI_C_DARK.
 */
extern lv_style_t ui_style_label_value_lg;

/**
 * @brief Hero numeric display (RPM, SoC …).
 * Font: BarlowCondensed_BoldItalic_80. Colour: UI_C_DARK.
 */
extern lv_style_t ui_style_label_value_md;

/* --- Buttons ---------------------------------------------------------------------- */

/**
 * @brief Default (unchecked / unfocused) button appearance.
 *
 * White background, 2 px DARK outline, DARK text, no radius, 2 px padding.
 * Pair with ui_style_btn_checked for LV_STATE_CHECKED and
 * ui_style_btn_focused for LV_STATE_FOCUS_KEY.
 */
extern lv_style_t ui_style_btn_default;

/**
 * @brief Button appearance when toggled / confirmed (LV_STATE_CHECKED).
 *
 * Green background, 2 px DARK outline, white text.
 * Use for the active mission, confirmed checklist items, and RTD active state.
 */
extern lv_style_t ui_style_btn_checked;

/**
 * @brief Button appearance when focused via encoder (LV_STATE_FOCUS_KEY).
 *
 * Accent (gold) background, 2 px DARK outline, DARK text.
 * Provides clear visual feedback for encoder-driven navigation.
 */
extern lv_style_t ui_style_btn_focused;

/* --- Progress bar (slider) -------------------------------------------------------- */

/**
 * @brief Progress-bar track (LV_PART_MAIN).
 *
 * White background with a 2 px DARK outline and no radius.
 * Remove all default styles before adding this one.
 */
extern lv_style_t ui_style_slider_main;

/**
 * @brief Progress-bar fill (LV_PART_INDICATOR).
 *
 * Solid green fill. Remove all default styles before adding this one.
 */
extern lv_style_t ui_style_slider_indicator;

/* --- Status indicators ------------------------------------------------------------ */

/**
 * @brief Circular OK / connected indicator dot.
 * Green fill, no border, circle radius.
 */
extern lv_style_t ui_style_status_ok;

/**
 * @brief Circular warning indicator dot.
 * Accent (gold) fill, no border, circle radius.
 */
extern lv_style_t ui_style_status_warn;

/**
 * @brief Circular fault / error indicator dot.
 * Red fill, no border, circle radius.
 */
extern lv_style_t ui_style_status_fault;

/* --- Level indicators (UI_STATE_WARN / UI_STATE_CRIT) ----------------------------- */

/**
 * @brief LVGL state alias for the WARNING level.
 *
 * Maps to LV_STATE_USER_1. Use this alias everywhere instead of the raw
 * LVGL constant so the assignment can be changed in a single place.
 */
#define UI_STATE_WARN   LV_STATE_USER_1

/**
 * @brief LVGL state alias for the CRITICAL level.
 *
 * Maps to LV_STATE_USER_2, which has higher priority than USER_1 —
 * the critical style overrides the warning style when both states are active.
 */
#define UI_STATE_CRIT   LV_STATE_USER_2

/**
 * @brief Text colour style for the WARNING level (UI_STATE_WARN).
 * Sets text_color to UI_C_ACCENT (gold). For labels and span groups.
 */
extern lv_style_t ui_style_level_warn;

/**
 * @brief Text colour style for the CRITICAL level (UI_STATE_CRIT).
 * Sets text_color to UI_C_RED. Higher priority than ui_style_level_warn.
 */
extern lv_style_t ui_style_level_crit;

/**
 * @brief Background/fill colour style for the WARNING level (UI_STATE_WARN).
 * Sets bg_color to UI_C_ACCENT (gold). For slider indicators, arcs, progress fills.
 * Apply with: lv_obj_add_style(obj, &ui_style_level_warn_indicator,
 *                              UI_STATE_WARN | LV_PART_INDICATOR);
 */
extern lv_style_t ui_style_level_warn_indicator;

/**
 * @brief Background/fill colour style for the CRITICAL level (UI_STATE_CRIT).
 * Sets bg_color to UI_C_RED. Higher priority than ui_style_level_warn_indicator.
 * Apply with: lv_obj_add_style(obj, &ui_style_level_crit_indicator,
 *                              UI_STATE_CRIT | LV_PART_INDICATOR);
 */
extern lv_style_t ui_style_level_crit_indicator;


/* ── Initialisation ──────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise all shared UI styles.
 *
 * Must be called once from the UI module after LVGL has been initialised
 * and before any screen builder function runs. Calling it a second time
 * re-initialises all styles (safe but discouraged).
 *
 * The header gradient is computed from the active display's horizontal
 * resolution at call time, so the default LVGL display must already be
 * registered.
 */
void ui_styles_init(void);

#endif /* MODULES_UI_UI_STYLES_H */
