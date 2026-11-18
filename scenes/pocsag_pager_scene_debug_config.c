#include "../pocsag_pager_app_i.h"

/*
 * The rows that exist for working on the app rather than using it: the crystal
 * correction, the two SD captures and the soft-decision decoder. Grouped out
 * of the settings list as Filtering and Alerting were.
 *
 * Laid out as the settings list is: the index enum is the list order, so a row
 * added here must be added to the table in the same place.
 */
enum PCSGDebugIndex {
    PCSGDebugIndexFreqCorr,
    PCSGDebugIndexRawCapture,
    PCSGDebugIndexStatsLog,
    PCSGDebugIndexWeakDecode,
};

#define DEBUG_ON_OFF_COUNT 2
static const char* const debug_on_off_text[DEBUG_ON_OFF_COUNT] = {
    "Off",
    "On",
};

// Index 0 is the lowest the arrows reach; the centre index is no correction.
#define FREQ_CORR_COUNT \
    ((PCSG_FREQ_CORRECTION_LIST_MAX_HZ * 2) / PCSG_FREQ_CORRECTION_STEP_HZ + 1)

typedef struct {
    const char* label;
    uint16_t offset;
} PCSGDebugRow;

#define PCSG_DEBUG_ROW(member) offsetof(POCSAGPagerApp, member)

_Static_assert(sizeof(bool) == 1, "row fields are byte-wide");

/*
 * Only the three switches are described here; the correction has its own
 * setter and its own way of showing a value, so it is built by hand below.
 */
static const PCSGDebugRow pcsg_debug_rows[] = {
    // Record the raw FLEX4 sample stream to SD for offline analysis.
    [PCSGDebugIndexRawCapture] = {"Raw Capture:", PCSG_DEBUG_ROW(filter.raw_capture)},
    // Periodic counter-only snapshots for untethered reception tests.
    [PCSGDebugIndexStatsLog] = {"Stats Log:", PCSG_DEBUG_ROW(filter.stats_log)},
    // Soft-decision (Chase) recovery of weak FLEX4 words. Off by default: it
    // allocates its reliability buffer in the decoder only while enabled.
    [PCSGDebugIndexWeakDecode] = {"Weak Decode:", PCSG_DEBUG_ROW(filter.weak_decode)},
};

static void pcsg_debug_show_freq_corr(VariableItem* item, int32_t hz) {
    if(!item) return;
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
static uint8_t pcsg_debug_freq_corr_index(int32_t hz) {
    if(hz > PCSG_FREQ_CORRECTION_LIST_MAX_HZ) {
        hz = PCSG_FREQ_CORRECTION_LIST_MAX_HZ;
    } else if(hz < -PCSG_FREQ_CORRECTION_LIST_MAX_HZ) {
        hz = -PCSG_FREQ_CORRECTION_LIST_MAX_HZ;
    }
    const int32_t offset = hz + PCSG_FREQ_CORRECTION_LIST_MAX_HZ;
    return (uint8_t)((offset + PCSG_FREQ_CORRECTION_STEP_HZ / 2) / PCSG_FREQ_CORRECTION_STEP_HZ);
}

static void pcsg_debug_set_freq_corr(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->filter.frequency_correction_hz =
        -PCSG_FREQ_CORRECTION_LIST_MAX_HZ + index * PCSG_FREQ_CORRECTION_STEP_HZ;
    pcsg_debug_show_freq_corr(item, app->filter.frequency_correction_hz);
    // The receiver retunes with it, as changing the channel does.
    app->txrx->rx_restart = true;
}

// The row being changed is always the selected one, which is how this finds
// its description without a separate callback per row.
static void pcsg_debug_row_changed(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_list_get_selected_item_index(app->variable_item_list);
    if(index >= COUNT_OF(pcsg_debug_rows)) return;
    const PCSGDebugRow* row = &pcsg_debug_rows[index];
    if(!row->label) return;

    const uint8_t value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, debug_on_off_text[value]);
    *((uint8_t*)app + row->offset) = value != 0U;
}

// OK on the correction types a value; the switches are stepped in place. The
// list is shared and its reset does not clear this, so a handler is installed
// whether or not a row wants one.
static void pcsg_debug_enter_callback(void* context, uint32_t index) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    if(index == PCSGDebugIndexFreqCorr) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, PCSGCustomEventSceneSettingFreqCorr);
    }
}

void pocsag_pager_scene_debug_config_on_enter(void* context) {
    POCSAGPagerApp* app = context;

    VariableItem* item = variable_item_list_add(
        app->variable_item_list, "Correction:", FREQ_CORR_COUNT, pcsg_debug_set_freq_corr, app);
    variable_item_set_current_value_index(
        item, pcsg_debug_freq_corr_index(app->filter.frequency_correction_hz));
    // Show what is stored, not the index: a typed value need not be on the grid.
    pcsg_debug_show_freq_corr(item, app->filter.frequency_correction_hz);

    for(uint8_t index = PCSGDebugIndexRawCapture; index < COUNT_OF(pcsg_debug_rows); index++) {
        const PCSGDebugRow* row = &pcsg_debug_rows[index];
        uint8_t* field = (uint8_t*)app + row->offset;
        const uint8_t value = *field ? 1U : 0U;
        item = variable_item_list_add(
            app->variable_item_list, row->label, DEBUG_ON_OFF_COUNT, pcsg_debug_row_changed, app);
        variable_item_set_current_value_index(item, value);
        variable_item_set_current_value_text(item, debug_on_off_text[value]);
    }

    variable_item_list_set_enter_callback(app->variable_item_list, pcsg_debug_enter_callback, app);
    variable_item_list_set_selected_item(
        app->variable_item_list, app->debug_config_selected_index);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewVariableItemList);
}

bool pocsag_pager_scene_debug_config_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != PCSGCustomEventSceneSettingFreqCorr) return false;

    scene_manager_set_scene_state(
        app->scene_manager, POCSAGPagerSceneCapcodeInput, PCSGNumberFieldFreqCorrection);
    scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneCapcodeInput);
    return true;
}

void pocsag_pager_scene_debug_config_on_exit(void* context) {
    POCSAGPagerApp* app = context;
    app->debug_config_selected_index =
        variable_item_list_get_selected_item_index(app->variable_item_list);
    variable_item_list_set_selected_item(app->variable_item_list, 0);
    variable_item_list_reset(app->variable_item_list);
    pcsg_settings_save(app);
}
