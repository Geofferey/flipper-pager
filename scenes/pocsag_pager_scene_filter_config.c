#include "../pocsag_pager_app_i.h"

/*
 * Everything that decides which messages reach the list, which had grown to
 * seventeen rows scattered through the settings list. They belong together
 * and they are the rows most often changed, so they get a door of their own.
 *
 * Laid out as the settings list is: the index enum is the list order, so a row
 * added here must be added to the table and to on_enter in the same place.
 */
enum PCSGFilterIndex {
    PCSGFilterIndexShowAlpha,
    PCSGFilterIndexFlexDups,
    PCSGFilterIndexShowNumeric,
    PCSGFilterIndexShowTone,
    PCSGFilterIndexShowBinary,
    PCSGFilterIndexEncrypted,
    PCSGFilterIndexPartial,
    PCSGFilterIndexCapcodeFilter,
    PCSGFilterIndexCapcodeMin,
    PCSGFilterIndexCapcodeMax,
    PCSGFilterIndexRicFilter,
    PCSGFilterIndexRicMin,
    PCSGFilterIndexRicMax,
    PCSGFilterIndexPhase,
    PCSGFilterIndexFrameFilter,
    PCSGFilterIndexFrameMin,
    PCSGFilterIndexFrameMax,
};

#define HIDE_SHOW_COUNT 2
static const char* const hide_show_text[HIDE_SHOW_COUNT] = {
    "Hide",
    "Show",
};

// Order follows PCSGFlexDups: Hide, Show, then Caps.
static const char* const flex_dups_text[PCSGFlexDupsCount] = {
    "Hide",
    "Show",
    "CAPS",
};

static const char* const capcode_filter_text[PCSGCapcodeFilterCount] = {
    "Off",
    "Exact",
    "Range",
    "Alert",
    "AlertRng",
};

static const char* const phase_filter_text[PCSGPhaseFilterCount] = {
    "All",
    "A",
    "B",
    "C",
    "D",
};

typedef enum {
    PCSGFilterRowNone, // a number row, typed rather than stepped
    PCSGFilterRowEnum, // field = index
    PCSGFilterRowBool, // field = index != 0
    PCSGFilterRowBoolInv, // the row shows "Show", the field holds "hidden"
} PCSGFilterRowKind;

typedef enum {
    PCSGFilterSideNone,
    PCSGFilterSideCapcodeMax,
    PCSGFilterSideRicMax,
    PCSGFilterSideFrameMax,
} PCSGFilterRowSide;

typedef struct {
    const char* label;
    const char* const* text;
    uint16_t offset;
    uint8_t count;
    uint8_t kind;
    uint8_t side;
} PCSGFilterRow;

#define PCSG_FILTER_ROW(member) offsetof(POCSAGPagerApp, member)

// Every field below is one byte, which is what lets them share one accessor.
_Static_assert(sizeof(bool) == 1, "row fields are byte-wide");

static const PCSGFilterRow pcsg_filter_rows[] = {
    [PCSGFilterIndexShowAlpha] =
        {"Alphanumeric:", hide_show_text, PCSG_FILTER_ROW(filter.show_alpha), HIDE_SHOW_COUNT,
         PCSGFilterRowBool, PCSGFilterSideNone},
    [PCSGFilterIndexFlexDups] =
        {"Duplicates:", flex_dups_text, PCSG_FILTER_ROW(filter.flex_dups), PCSGFlexDupsCount,
         PCSGFilterRowEnum, PCSGFilterSideNone},
    [PCSGFilterIndexShowNumeric] =
        {"Numbers:", hide_show_text, PCSG_FILTER_ROW(filter.show_numeric), HIDE_SHOW_COUNT,
         PCSGFilterRowBool, PCSGFilterSideNone},
    [PCSGFilterIndexShowTone] =
        {"Tone only:", hide_show_text, PCSG_FILTER_ROW(filter.show_tone), HIDE_SHOW_COUNT,
         PCSGFilterRowBool, PCSGFilterSideNone},
    [PCSGFilterIndexShowBinary] =
        {"Binary:", hide_show_text, PCSG_FILTER_ROW(filter.show_binary), HIDE_SHOW_COUNT,
         PCSGFilterRowBool, PCSGFilterSideNone},
    [PCSGFilterIndexEncrypted] =
        {"Encrypted:", hide_show_text, PCSG_FILTER_ROW(filter.hide_encrypted), HIDE_SHOW_COUNT,
         PCSGFilterRowBoolInv, PCSGFilterSideNone},
    // Hide = discard messages that could not be fully decoded.
    [PCSGFilterIndexPartial] =
        {"Partial Msgs:", hide_show_text, PCSG_FILTER_ROW(filter.hide_partial), HIDE_SHOW_COUNT,
         PCSGFilterRowBoolInv, PCSGFilterSideNone},
    [PCSGFilterIndexCapcodeFilter] =
        {"Cap. Filter:", capcode_filter_text, PCSG_FILTER_ROW(filter.capcode_filter),
         PCSGCapcodeFilterCount, PCSGFilterRowEnum, PCSGFilterSideCapcodeMax},
    [PCSGFilterIndexCapcodeMin] = {"Capcode:", NULL, 0, 0, PCSGFilterRowNone, PCSGFilterSideNone},
    [PCSGFilterIndexCapcodeMax] = {"Cap. Max:", NULL, 0, 0, PCSGFilterRowNone, PCSGFilterSideNone},
    [PCSGFilterIndexRicFilter] =
        {"RIC Filter:", capcode_filter_text, PCSG_FILTER_ROW(filter.ric_filter),
         PCSGCapcodeFilterCount, PCSGFilterRowEnum, PCSGFilterSideRicMax},
    [PCSGFilterIndexRicMin] = {"RIC:", NULL, 0, 0, PCSGFilterRowNone, PCSGFilterSideNone},
    [PCSGFilterIndexRicMax] = {"RIC Max:", NULL, 0, 0, PCSGFilterRowNone, PCSGFilterSideNone},
    [PCSGFilterIndexPhase] =
        {"FLEX Phase:", phase_filter_text, PCSG_FILTER_ROW(filter.phase_filter),
         PCSGPhaseFilterCount, PCSGFilterRowEnum, PCSGFilterSideNone},
    // Stops before the alert modes: a frame is not an address to alert on.
    [PCSGFilterIndexFrameFilter] =
        {"Frame Filter:", capcode_filter_text, PCSG_FILTER_ROW(filter.frame_filter),
         PCSG_FRAME_FILTER_COUNT, PCSGFilterRowEnum, PCSGFilterSideFrameMax},
    [PCSGFilterIndexFrameMin] = {"Frame:", NULL, 0, 0, PCSGFilterRowNone, PCSGFilterSideNone},
    [PCSGFilterIndexFrameMax] = {"Frame Max:", NULL, 0, 0, PCSGFilterRowNone, PCSGFilterSideNone},
};

/*
 * Each "max" row is looked up by position every time instead of keeping the
 * VariableItem* that variable_item_list_add() returned. The list holds its
 * items in an m-array, so adding the rows that follow eventually reallocates
 * it and moves every item; a kept pointer then refers to freed memory, and
 * writing a row's text through it corrupts the heap.
 */
static VariableItem* pcsg_filter_item(POCSAGPagerApp* app, uint8_t index) {
    return variable_item_list_get(app->variable_item_list, index);
}

static void pcsg_filter_set_number_text(VariableItem* item, uint32_t value) {
    if(!item) return;
    char text_buf[10] = {0};
    snprintf(text_buf, sizeof(text_buf), "%lu", value);
    variable_item_set_current_value_text(item, text_buf);
}

// Both range modes use the maximum, the alert one included; the rest do not.
static bool pcsg_filter_uses_max(PCSGCapcodeFilter mode) {
    return mode == PCSGCapcodeFilterRange || mode == PCSGCapcodeFilterAlertRange;
}

static void pcsg_filter_update_capcode_max(POCSAGPagerApp* app) {
    VariableItem* item = pcsg_filter_item(app, PCSGFilterIndexCapcodeMax);
    if(!item) return;
    if(pcsg_filter_uses_max(app->filter.capcode_filter)) {
        pcsg_filter_set_number_text(item, app->filter.capcode_max);
    } else {
        variable_item_set_current_value_text(item, "--");
    }
}

static void pcsg_filter_update_ric_max(POCSAGPagerApp* app) {
    VariableItem* item = pcsg_filter_item(app, PCSGFilterIndexRicMax);
    if(!item) return;
    if(pcsg_filter_uses_max(app->filter.ric_filter)) {
        pcsg_filter_set_number_text(item, app->filter.ric_max);
    } else {
        variable_item_set_current_value_text(item, "--");
    }
}

static void pcsg_filter_update_frame_max(POCSAGPagerApp* app) {
    VariableItem* item = pcsg_filter_item(app, PCSGFilterIndexFrameMax);
    if(!item) return;
    if(app->filter.frame_filter == PCSGCapcodeFilterRange) {
        pcsg_filter_set_number_text(item, app->filter.frame_max);
    } else {
        variable_item_set_current_value_text(item, "--");
    }
}

static uint8_t pcsg_filter_row_value(const POCSAGPagerApp* app, const PCSGFilterRow* row) {
    const uint8_t stored = *((const uint8_t*)app + row->offset);
    if(row->kind == PCSGFilterRowBoolInv) return stored ? 0U : 1U;
    return stored;
}

// The row being changed is always the selected one, which is how this finds
// its description without a separate callback per row.
static void pcsg_filter_row_changed(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_list_get_selected_item_index(app->variable_item_list);
    if(index >= COUNT_OF(pcsg_filter_rows)) return;
    const PCSGFilterRow* row = &pcsg_filter_rows[index];
    if(row->kind == PCSGFilterRowNone) return;

    const uint8_t value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, row->text[value]);

    uint8_t* field = (uint8_t*)app + row->offset;
    switch(row->kind) {
    case PCSGFilterRowEnum:
        *field = value;
        break;
    case PCSGFilterRowBoolInv:
        *field = value == 0U;
        break;
    default:
        *field = value != 0U;
        break;
    }

    switch(row->side) {
    case PCSGFilterSideCapcodeMax:
        pcsg_filter_update_capcode_max(app);
        break;
    case PCSGFilterSideRicMax:
        pcsg_filter_update_ric_max(app);
        break;
    case PCSGFilterSideFrameMax:
        pcsg_filter_update_frame_max(app);
        break;
    default:
        break;
    }
}

// OK on a number row types a value; the max rows only take one in range mode.
static void pcsg_filter_enter_callback(void* context, uint32_t index) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    PCSGCustomEvent event = 0;

    switch(index) {
    case PCSGFilterIndexCapcodeMin:
        event = PCSGCustomEventSceneSettingCapcodeMin;
        break;
    case PCSGFilterIndexCapcodeMax:
        if(pcsg_filter_uses_max(app->filter.capcode_filter)) {
            event = PCSGCustomEventSceneSettingCapcodeMax;
        }
        break;
    case PCSGFilterIndexRicMin:
        event = PCSGCustomEventSceneSettingRicMin;
        break;
    case PCSGFilterIndexRicMax:
        if(pcsg_filter_uses_max(app->filter.ric_filter)) {
            event = PCSGCustomEventSceneSettingRicMax;
        }
        break;
    case PCSGFilterIndexFrameMin:
        event = PCSGCustomEventSceneSettingFrameMin;
        break;
    case PCSGFilterIndexFrameMax:
        if(app->filter.frame_filter == PCSGCapcodeFilterRange) {
            event = PCSGCustomEventSceneSettingFrameMax;
        }
        break;
    default:
        break;
    }

    if(event) view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

void pocsag_pager_scene_filter_config_on_enter(void* context) {
    POCSAGPagerApp* app = context;

    for(uint8_t index = 0; index < COUNT_OF(pcsg_filter_rows); index++) {
        const PCSGFilterRow* row = &pcsg_filter_rows[index];
        if(row->kind == PCSGFilterRowNone) {
            // A typed value, so one step and no stepping callback; the text is
            // filled in by the update below, or directly for the minimums.
            variable_item_list_add(app->variable_item_list, row->label, 1, NULL, app);
            continue;
        }
        // A settings file from another build can carry a value this row no
        // longer has; the text lookup below would read past its array.
        uint8_t* field = (uint8_t*)app + row->offset;
        if(*field >= row->count) *field = 0U;
        VariableItem* item = variable_item_list_add(
            app->variable_item_list, row->label, row->count, pcsg_filter_row_changed, app);
        const uint8_t value = pcsg_filter_row_value(app, row);
        variable_item_set_current_value_index(item, value);
        variable_item_set_current_value_text(item, row->text[value]);
    }

    pcsg_filter_set_number_text(
        pcsg_filter_item(app, PCSGFilterIndexCapcodeMin), app->filter.capcode_min);
    pcsg_filter_update_capcode_max(app);
    pcsg_filter_set_number_text(
        pcsg_filter_item(app, PCSGFilterIndexRicMin), app->filter.ric_min);
    pcsg_filter_update_ric_max(app);
    pcsg_filter_set_number_text(
        pcsg_filter_item(app, PCSGFilterIndexFrameMin), app->filter.frame_min);
    pcsg_filter_update_frame_max(app);

    variable_item_list_set_enter_callback(
        app->variable_item_list, pcsg_filter_enter_callback, app);
    variable_item_list_set_selected_item(
        app->variable_item_list, app->filter_config_selected_index);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewVariableItemList);
}

bool pocsag_pager_scene_filter_config_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    PCSGNumberField field;
    switch(event.event) {
    case PCSGCustomEventSceneSettingCapcodeMin:
        field = PCSGNumberFieldCapcodeMin;
        break;
    case PCSGCustomEventSceneSettingCapcodeMax:
        field = PCSGNumberFieldCapcodeMax;
        break;
    case PCSGCustomEventSceneSettingRicMin:
        field = PCSGNumberFieldRicMin;
        break;
    case PCSGCustomEventSceneSettingRicMax:
        field = PCSGNumberFieldRicMax;
        break;
    case PCSGCustomEventSceneSettingFrameMin:
        field = PCSGNumberFieldFrameMin;
        break;
    case PCSGCustomEventSceneSettingFrameMax:
        field = PCSGNumberFieldFrameMax;
        break;
    default:
        return false;
    }

    scene_manager_set_scene_state(app->scene_manager, POCSAGPagerSceneCapcodeInput, field);
    scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneCapcodeInput);
    return true;
}

void pocsag_pager_scene_filter_config_on_exit(void* context) {
    POCSAGPagerApp* app = context;
    app->filter_config_selected_index =
        variable_item_list_get_selected_item_index(app->variable_item_list);
    variable_item_list_set_selected_item(app->variable_item_list, 0);
    variable_item_list_reset(app->variable_item_list);
    pcsg_settings_save(app);
}
