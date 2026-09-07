/**
 * @file        screen_dv_driving.c
 * @brief       Driverless driving screen implementation
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Display-only. The contract and the layout sketch are in
 *              screen_dv_driving.h.
 *
 *              ### Mission readout
 *              A caption and, below it, the mission name. The name follows an
 *              observer on ui_tx_subj_drive_mode — the confirmed result of the
 *              mission-select screen, the same subject its own
 *              "Current Mission" label watches. k_mission_names[] mirrors the
 *              mission_id order and must stay in step with it.
 *
 *              Nothing here reads the CAN AMI_state signal: the screen shows
 *              what the driver picked on the DCU, not what the vehicle reports.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-09-07
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
 * 0.1.0    2026-09-07  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_dv_driving.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_styles.h"
#include "modules/ui/widgets/ui_header.h"
#include "modules/ui/widgets/ui_hintbar.h"
#include "generated/ui_tx_subjects_gen.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_dv_driving, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/**
 * @brief Mission names, indexed by @ref mission_id.
 *
 * Same list as screen_mission_select.c — kept local rather than shared because
 * the two screens are the only users and neither wants a dependency on the
 * other. Must match the enum order.
 */
static const char *const k_mission_names[] = {
    "None", "Acceleration", "Skidpad", "Trackdrive",
    "Braketest", "Inspection", "Autocross", "Manual Driving",
};


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void mission_observer_cb(lv_observer_t *observer, lv_subject_t *subject);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Update the mission label from the confirmed drive mode.
 *
 * @param observer  Observer whose target object is the label.
 * @param subject   ui_tx_subj_drive_mode; holds an index into k_mission_names.
 */
static void mission_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *lbl = lv_observer_get_target_obj(observer);
    int32_t   idx = lv_subject_get_int(subject);

    if (idx < 0 || idx >= (int32_t)ARRAY_SIZE(k_mission_names)) {
        idx = 0;
    }
    lv_label_set_text(lbl, k_mission_names[idx]);
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_dv_driving_create(lv_subject_t *status_subjects)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "DV DRIVING", status_subjects);

    lv_obj_t *lbl_caption = lv_label_create(scr);
    lv_obj_add_style(lbl_caption, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_caption, "MISSION");
    lv_obj_align(lbl_caption, LV_ALIGN_CENTER, 0, -24);

    lv_obj_t *lbl_mission = lv_label_create(scr);
    lv_obj_add_style(lbl_mission, &ui_style_label_caption, 0);
    lv_obj_align(lbl_mission, LV_ALIGN_CENTER, 0, 20);
    lv_subject_add_observer_obj(&ui_tx_subj_drive_mode, mission_observer_cb,
                                lbl_mission, NULL);

    ui_hintbar_create(scr, NULL);

    return scr;
}

lv_group_t *screen_dv_driving_get_right_encoder_group(void)
{
    return NULL;
}

lv_group_t *screen_dv_driving_get_left_button_group(void)
{
    return NULL;
}

lv_group_t *screen_dv_driving_get_right_button_group(void)
{
    return NULL;
}
