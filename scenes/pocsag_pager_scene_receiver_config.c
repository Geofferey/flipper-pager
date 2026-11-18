#include "../pocsag_pager_app_i.h"

/*
 * The index enum is the list order, so a row added here must be added to the
 * table and to on_enter in the same place. What decides which messages reach
 * the list lives in its own scene now, behind the Filtering row.
 */
enum PCSGSettingIndex {
    PCSGSettingIndexFrequency,
    PCSGSettingIndexModulation,
    PCSGSettingIndexAutoReceive,
    PCSGSettingIndexFilter,
    PCSGSettingIndexAlerting,
    PCSGSettingIndexLogging,
    PCSGSettingIndexTimeSync,
    PCSGSettingIndexDst,
    PCSGSettingIndexFreqCorr,
    PCSGSettingIndexRawCapture,
    PCSGSettingIndexStatsLog,
    PCSGSettingIndexWeakDecode,
    PCSGSettingIndexHud,
    PCSGSettingIndexLock,
};

// The page-kind, duplicate, capcode/RIC/frame and phase rows and their text
// now live in pocsag_pager_scene_filter_config.c.

#define LOGGING_COUNT 2
const char* const logging_text[LOGGING_COUNT] = {
    "Save",
    "Temp",
};

const char* const time_sync_text[PCSGTimeSyncCount] = {
    "Off",
    "MM:SS",
    "HH:MM:SS",
};

// Daylight saving here, not on the network: each corrects the network that
// disagrees with it. Disabled corrects nothing.
const char* const dst_text[PCSGDstCount] = {
    "Off",
    "On",
    "Disabled",
};

// The alert, LED and signal-tone rows and their text now live in
// pocsag_pager_scene_alert_config.c.

uint8_t pocsag_pager_scene_receiver_config_next_frequency(const uint32_t value, void* context) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    uint8_t index = 0;
    for(uint8_t i = 0; i < subghz_setting_get_frequency_count(app->setting); i++) {
        if(value == subghz_setting_get_frequency(app->setting, i)) {
            index = i;
            break;
        } else {
            index = subghz_setting_get_frequency_default_index(app->setting);
        }
    }
    return index;
}

uint8_t pocsag_pager_scene_receiver_config_next_preset(const char* preset_name, void* context) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    uint8_t index = 0;
    for(uint8_t i = 0; i < subghz_setting_get_preset_count(app->setting); i++) {
        if(!strcmp(subghz_setting_get_preset_name(app->setting, i), preset_name)) {
            index = i;
            break;
        } else {
            //  index = subghz_setting_get_frequency_default_index(app ->setting);
        }
    }
    return index;
}

/*
 * Four decimals, not two: OK on this row types any frequency, and the
 * channels themselves carry 100 Hz detail that two would hide.
 */
static void pcsg_config_show_frequency(VariableItem* item, uint32_t hz) {
    char text_buf[12] = {0};
    snprintf(text_buf, sizeof(text_buf), "%lu.%04lu", hz / 1000000U, (hz % 1000000U) / 100U);
    variable_item_set_current_value_text(item, text_buf);
}

// Left and right step through the channel list; OK types a frequency, so
// on_enter shows the stored value rather than calling this, which reads the
// list index and would snap a typed one back onto the list.
static void pocsag_pager_scene_receiver_config_set_frequency(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    app->txrx->preset->frequency = subghz_setting_get_frequency(app->setting, index);
    pcsg_config_show_frequency(item, app->txrx->preset->frequency);
    app->txrx->rx_restart = true;
}

static void pocsag_pager_scene_receiver_config_set_preset(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(
        item, subghz_setting_get_preset_name(app->setting, index));
    pcsg_preset_init(
        app,
        subghz_setting_get_preset_name(app->setting, index),
        app->txrx->preset->frequency,
        subghz_setting_get_preset_data(app->setting, index),
        subghz_setting_get_preset_data_size(app->setting, index));
    app->txrx->rx_restart = true;
}


#define RAW_CAPTURE_COUNT 2
const char* const raw_capture_text[RAW_CAPTURE_COUNT] = {
    "Off",
    "On",
};

#define AUTO_RECEIVE_COUNT 2
const char* const auto_receive_text[AUTO_RECEIVE_COUNT] = {
    "Disable",
    "Enable",
};
// Index 0 is the lowest the arrows reach; the centre index is no correction.
#define FREQ_CORR_COUNT \
    ((PCSG_FREQ_CORRECTION_LIST_MAX_HZ * 2) / PCSG_FREQ_CORRECTION_STEP_HZ + 1)

static void pocsag_pager_scene_receiver_config_show_freq_corr(VariableItem* item, int32_t hz) {
    if(hz == 0) {
        variable_item_set_current_value_text(item, "Off");
        return;
    }
    char buf[16];
    const uint32_t magnitude = hz < 0 ? (uint32_t)-hz : (uint32_t)hz;
    snprintf(
        buf,
        sizeof(buf),
        "%c%lu.%01lu kHz",
        hz < 0 ? '-' : '+',
        magnitude / 1000U,
        (magnitude % 1000U) / 100U);
    variable_item_set_current_value_text(item, buf);
}

// A typed value can sit past what the arrows reach, so clamp rather than
// discard: the row still displays the real value, it just stops stepping.
static uint8_t pocsag_pager_scene_receiver_config_freq_corr_index(int32_t hz) {
    if(hz > PCSG_FREQ_CORRECTION_LIST_MAX_HZ) {
        hz = PCSG_FREQ_CORRECTION_LIST_MAX_HZ;
    } else if(hz < -PCSG_FREQ_CORRECTION_LIST_MAX_HZ) {
        hz = -PCSG_FREQ_CORRECTION_LIST_MAX_HZ;
    }
    const int32_t offset = hz + PCSG_FREQ_CORRECTION_LIST_MAX_HZ;
    return (uint8_t)((offset + PCSG_FREQ_CORRECTION_STEP_HZ / 2) / PCSG_FREQ_CORRECTION_STEP_HZ);
}

static void pocsag_pager_scene_receiver_config_set_freq_corr(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->filter.frequency_correction_hz =
        -PCSG_FREQ_CORRECTION_LIST_MAX_HZ + index * PCSG_FREQ_CORRECTION_STEP_HZ;
    pocsag_pager_scene_receiver_config_show_freq_corr(
        item, app->filter.frequency_correction_hz);
    // The receiver retunes with it, as changing the channel does.
    app->txrx->rx_restart = true;
}

/*
 * Every row that is just "pick a value, store it, maybe poke something" is
 * described here instead of getting its own setter and its own four lines in
 * on_enter. The entries are placed by PCSGSettingIndex, so the table and the
 * list cannot drift apart, and the gaps are the rows that genuinely differ
 * (frequency, preset, the number entries, the correction, Lock).
 */
typedef enum {
    PCSGRowNone, // a row this table does not describe
    PCSGRowEnum, // field = index
    PCSGRowBool, // field = index != 0
} PCSGRowKind;

typedef enum {
    PCSGSideNone,
    PCSGSideTimeSync,
} PCSGRowSide;

typedef struct {
    const char* label;
    const char* const* text;
    uint16_t offset;
    uint8_t count;
    uint8_t kind;
    uint8_t side;
} PCSGConfigRow;

#define PCSG_ROW(member) offsetof(POCSAGPagerApp, member)

// Every field below is one byte, which is what lets them share one accessor.
_Static_assert(sizeof(bool) == 1, "row fields are byte-wide");

static const PCSGConfigRow pcsg_config_rows[] = {
    // Doors, as HUD options is. Two values so the list draws its own arrow.
    [PCSGSettingIndexFilter] = {NULL, NULL, 0, 0, PCSGRowNone, PCSGSideNone},
    [PCSGSettingIndexAlerting] = {NULL, NULL, 0, 0, PCSGRowNone, PCSGSideNone},
    // Applies from the next receive session.
    [PCSGSettingIndexLogging] =
        {"Logging:", logging_text, PCSG_ROW(filter.log_temp), LOGGING_COUNT, PCSGRowBool,
         PCSGSideNone},
    // Receiving begins as the app opens, the menu going by unseen.
    [PCSGSettingIndexAutoReceive] =
        {"Auto RX:", auto_receive_text, PCSG_ROW(filter.auto_receive), AUTO_RECEIVE_COUNT,
         PCSGRowBool, PCSGSideNone},
    [PCSGSettingIndexTimeSync] =
        {"Time sync:", time_sync_text, PCSG_ROW(filter.time_sync), PCSGTimeSyncCount,
         PCSGRowEnum, PCSGSideTimeSync},
    // Resyncs like the row above: it changes the hour sync would write.
    [PCSGSettingIndexDst] =
        {"DST:", dst_text, PCSG_ROW(filter.dst), PCSGDstCount, PCSGRowEnum, PCSGSideTimeSync},
    // Debug: record the raw FLEX4 sample stream to SD for offline analysis.
    [PCSGSettingIndexRawCapture] =
        {"Raw Capture:", raw_capture_text, PCSG_ROW(filter.raw_capture), RAW_CAPTURE_COUNT,
         PCSGRowBool, PCSGSideNone},
    // Debug: periodic counter-only snapshots for untethered reception tests.
    [PCSGSettingIndexStatsLog] =
        {"Stats Log:", raw_capture_text, PCSG_ROW(filter.stats_log), RAW_CAPTURE_COUNT,
         PCSGRowBool, PCSGSideNone},
    // Soft-decision (Chase) recovery of weak FLEX4 words. Off by default: it
    // allocates its reliability buffer in the decoder only while enabled.
    [PCSGSettingIndexWeakDecode] =
        {"Weak Decode:", raw_capture_text, PCSG_ROW(filter.weak_decode), RAW_CAPTURE_COUNT,
         PCSGRowBool, PCSGSideNone},
    // Lock has no value to pick, and fixes the table's length to the list's.
    [PCSGSettingIndexLock] = {NULL, NULL, 0, 0, PCSGRowNone, PCSGSideNone},
};

static uint8_t pcsg_config_row_value(const POCSAGPagerApp* app, const PCSGConfigRow* row) {
    return *((const uint8_t*)app + row->offset);
}

// The row being changed is always the selected one, which is how this finds
// its description without a separate callback per row.
static void pcsg_config_row_changed(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_list_get_selected_item_index(app->variable_item_list);
    if(index >= COUNT_OF(pcsg_config_rows)) return;
    const PCSGConfigRow* row = &pcsg_config_rows[index];
    if(row->kind == PCSGRowNone) return;

    const uint8_t value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, row->text[value]);

    uint8_t* field = (uint8_t*)app + row->offset;
    *field = row->kind == PCSGRowEnum ? value : (value != 0U);

    // Changing either clock row makes the pending sync stale.
    if(row->side == PCSGSideTimeSync) app->time_sync_sequence = 0;
}

static void pocsag_pager_scene_receiver_config_hud_changed(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    // Nothing to step through: the second value is only there for the arrow.
    variable_item_set_current_value_index(item, 0);
    view_dispatcher_send_custom_event(app->view_dispatcher, PCSGCustomEventSceneSettingHud);
}

static void pocsag_pager_scene_receiver_config_filter_changed(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    variable_item_set_current_value_index(item, 0);
    view_dispatcher_send_custom_event(app->view_dispatcher, PCSGCustomEventSceneSettingFilter);
}

static void pocsag_pager_scene_receiver_config_alerting_changed(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    variable_item_set_current_value_index(item, 0);
    view_dispatcher_send_custom_event(app->view_dispatcher, PCSGCustomEventSceneSettingAlert);
}

static void pcsg_config_row_add(POCSAGPagerApp* app, uint8_t index) {
    const PCSGConfigRow* row = &pcsg_config_rows[index];
    VariableItem* item = variable_item_list_add(
        app->variable_item_list, row->label, row->count, pcsg_config_row_changed, app);
    const uint8_t value = pcsg_config_row_value(app, row);
    variable_item_set_current_value_index(item, value);
    variable_item_set_current_value_text(item, row->text[value]);
}

static void
    pocsag_pager_scene_receiver_config_var_list_enter_callback(void* context, uint32_t index) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    if(index == PCSGSettingIndexLock) {
        view_dispatcher_send_custom_event(app->view_dispatcher, PCSGCustomEventSceneSettingLock);
    } else if(index == PCSGSettingIndexHud) {
        view_dispatcher_send_custom_event(app->view_dispatcher, PCSGCustomEventSceneSettingHud);
    } else if(index == PCSGSettingIndexFilter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, PCSGCustomEventSceneSettingFilter);
    } else if(index == PCSGSettingIndexAlerting) {
        view_dispatcher_send_custom_event(app->view_dispatcher, PCSGCustomEventSceneSettingAlert);
    } else if(index == PCSGSettingIndexFrequency) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, PCSGCustomEventSceneSettingFrequency);
    } else if(index == PCSGSettingIndexFreqCorr) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, PCSGCustomEventSceneSettingFreqCorr);
    }
}

void pocsag_pager_scene_receiver_config_on_enter(void* context) {
    POCSAGPagerApp* app = context;
    VariableItem* item;
    uint8_t value_index;

    item = variable_item_list_add(
        app->variable_item_list,
        "Frequency:",
        subghz_setting_get_frequency_count(app->setting),
        pocsag_pager_scene_receiver_config_set_frequency,
        app);
    value_index =
        pocsag_pager_scene_receiver_config_next_frequency(app->txrx->preset->frequency, app);
    variable_item_set_current_value_index(item, value_index);
    pcsg_config_show_frequency(item, app->txrx->preset->frequency);

    item = variable_item_list_add(
        app->variable_item_list,
        "Modulation:",
        subghz_setting_get_preset_count(app->setting),
        pocsag_pager_scene_receiver_config_set_preset,
        app);
    value_index = pocsag_pager_scene_receiver_config_next_preset(
        furi_string_get_cstr(app->txrx->preset->name), app);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(
        item, subghz_setting_get_preset_name(app->setting, value_index));

    pcsg_config_row_add(app, PCSGSettingIndexAutoReceive);

    // What reaches the message list, behind a door of its own: page kinds,
    // duplicates, capcode/RIC/frame ranges and the phase.
    item = variable_item_list_add(
        app->variable_item_list,
        "Filtering",
        2,
        pocsag_pager_scene_receiver_config_filter_changed,
        app);
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, "");

    // How a message announces itself, and what the radio sounds like.
    item = variable_item_list_add(
        app->variable_item_list,
        "Alerting",
        2,
        pocsag_pager_scene_receiver_config_alerting_changed,
        app);
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, "");

    pcsg_config_row_add(app, PCSGSettingIndexLogging);
    pcsg_config_row_add(app, PCSGSettingIndexTimeSync);
    pcsg_config_row_add(app, PCSGSettingIndexDst);

    item = variable_item_list_add(
        app->variable_item_list,
        "Correction:",
        FREQ_CORR_COUNT,
        pocsag_pager_scene_receiver_config_set_freq_corr,
        app);
    variable_item_set_current_value_index(
        item,
        pocsag_pager_scene_receiver_config_freq_corr_index(app->filter.frequency_correction_hz));
    // Show what is stored, not the index: a typed value need not be on the grid.
    pocsag_pager_scene_receiver_config_show_freq_corr(item, app->filter.frequency_correction_hz);

    pcsg_config_row_add(app, PCSGSettingIndexRawCapture);
    pcsg_config_row_add(app, PCSGSettingIndexStatsLog);
    pcsg_config_row_add(app, PCSGSettingIndexWeakDecode);

    /*
     * A door, as the transmitter's FLEX params row is: two values so the list
     * draws its own arrow at the right edge rather than a ">" written as value
     * text landing mid column, and right opens it as OK does.
     */
    item = variable_item_list_add(
        app->variable_item_list,
        "HUD options",
        2,
        pocsag_pager_scene_receiver_config_hud_changed,
        app);
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, "");

    // Locking the keyboard only makes sense while receiving.
    uint8_t selected = app->config_selected_index;
    if(app->config_standalone) {
        if(selected >= PCSGSettingIndexLock) selected = 0;
    } else {
        variable_item_list_add(app->variable_item_list, "Lock Keyboard", 1, NULL, NULL);
    }
    variable_item_list_set_enter_callback(
        app->variable_item_list, pocsag_pager_scene_receiver_config_var_list_enter_callback, app);
    variable_item_list_set_selected_item(app->variable_item_list, selected);

    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewVariableItemList);
}

bool pocsag_pager_scene_receiver_config_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PCSGCustomEventSceneSettingLock) {
            app->lock = PCSGLockOn;
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        } else if(event.event == PCSGCustomEventSceneSettingHud) {
            scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneHudConfig);
            consumed = true;
        } else if(event.event == PCSGCustomEventSceneSettingFilter) {
            scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneFilterConfig);
            consumed = true;
        } else if(event.event == PCSGCustomEventSceneSettingAlert) {
            scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneAlertConfig);
            consumed = true;
        } else if(event.event == PCSGCustomEventSceneSettingFreqCorr) {
            scene_manager_set_scene_state(
                app->scene_manager, POCSAGPagerSceneCapcodeInput, PCSGNumberFieldFreqCorrection);
            scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneCapcodeInput);
            consumed = true;
        } else if(event.event == PCSGCustomEventSceneSettingFrequency) {
            scene_manager_set_scene_state(
                app->scene_manager, POCSAGPagerSceneCapcodeInput, PCSGNumberFieldRxFrequency);
            scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneCapcodeInput);
            consumed = true;
        }
    }
    return consumed;
}

void pocsag_pager_scene_receiver_config_on_exit(void* context) {
    POCSAGPagerApp* app = context;
    app->config_selected_index =
        variable_item_list_get_selected_item_index(app->variable_item_list);
    variable_item_list_set_selected_item(app->variable_item_list, 0);
    variable_item_list_reset(app->variable_item_list);
    pcsg_settings_save(app);
}
