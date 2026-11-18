#include "../pocsag_pager_app_i.h"

/*
 * How a message announces itself, and what the radio sounds like while it is
 * listening: the alert, the two indicator LEDs and the signal tones. Grouped
 * out of the settings list for the same reason Filtering was - they belong
 * together, and the list was long.
 *
 * Laid out as the settings list is: the index enum is the list order, so a row
 * added here must be added to the table in the same place.
 */
enum PCSGAlertIndex {
    PCSGAlertIndexAlert,
    PCSGAlertIndexAlertTone,
    PCSGAlertIndexReceiveLed,
    PCSGAlertIndexDecodeLed,
    PCSGAlertIndexSignalSound,
    PCSGAlertIndexToneVolume,
    PCSGAlertIndexTonePitch,
    PCSGAlertIndexToneSpread,
};

static const char* const alert_text[PCSGAlertCount] = {
    "Both",
    "Sound",
    "Vibro",
    "Off",
};

static const char* const alert_tone_text[PCSGAlertToneCount] = {
    "Default",
    "Classic",
};

// Off, then one entry per colour: index 0 turns the indicator off and
// anything past it selects the colour at index - 1.
#define LED_SELECTION_COUNT (PCSGLedColorCount + 1U)
static const char* const led_color_text[LED_SELECTION_COUNT] = {
    "Off",
    "Cyan",
    "Blue",
    "Green",
    "Yellow",
    "Magenta",
    "Red",
    "White",
    "Amber",
    "Lime",
    "Teal",
    "Sky",
    "Violet",
    "Pink",
    "Warm",
};

static const char* const signal_sound_text[PCSGSoundCount] = {
    "Off",
    "Raw",
    "Tone",
    "Synth",
};

static const char* const tone_volume_text[PCSGToneVolumeCount] = {
    "Low",
    "Medium",
    "High",
};

static const char* const tone_pitch_text[PCSGTonePitchCount] = {
    "Low",
    "Medium",
    "High",
};

static const char* const tone_spread_text[PCSGToneSpreadCount] = {
    "Narrow",
    "Normal",
    "Wide",
};

typedef enum {
    PCSGAlertRowEnum, // field = index
    PCSGAlertRowLed, // off, or one colour per index past it
} PCSGAlertRowKind;

typedef enum {
    PCSGAlertSideNone,
    PCSGAlertSideReset, // the classic alert holds state across a change
    PCSGAlertSideRxRestart, // the speaker is set up when receiving starts
} PCSGAlertRowSide;

typedef struct {
    const char* label;
    const char* const* text;
    uint16_t offset;
    // PCSGAlertRowLed only: the colour field that index 1 and up select.
    uint16_t extra;
    uint8_t count;
    uint8_t kind;
    uint8_t side;
} PCSGAlertRow;

#define PCSG_ALERT_ROW(member) offsetof(POCSAGPagerApp, member)

_Static_assert(sizeof(bool) == 1, "row fields are byte-wide");
_Static_assert(sizeof(PCSGAlert) == 1, "build is missing -fshort-enums");

static const PCSGAlertRow pcsg_alert_rows[] = {
    [PCSGAlertIndexAlert] =
        {"Alert:", alert_text, PCSG_ALERT_ROW(filter.alert), 0, PCSGAlertCount, PCSGAlertRowEnum,
         PCSGAlertSideReset},
    [PCSGAlertIndexAlertTone] =
        {"Alert Tone:", alert_tone_text, PCSG_ALERT_ROW(filter.alert_tone), 0, PCSGAlertToneCount,
         PCSGAlertRowEnum, PCSGAlertSideReset},
    [PCSGAlertIndexReceiveLed] =
        {"Receive LED:", led_color_text, PCSG_ALERT_ROW(filter.receive_led),
         PCSG_ALERT_ROW(filter.receive_led_color), LED_SELECTION_COUNT, PCSGAlertRowLed,
         PCSGAlertSideNone},
    [PCSGAlertIndexDecodeLed] =
        {"Decode LED:", led_color_text, PCSG_ALERT_ROW(filter.decode_led),
         PCSG_ALERT_ROW(filter.decode_led_color), LED_SELECTION_COUNT, PCSGAlertRowLed,
         PCSGAlertSideNone},
    [PCSGAlertIndexSignalSound] =
        {"Signal Sound:", signal_sound_text, PCSG_ALERT_ROW(filter.signal_sound), 0,
         PCSGSoundCount, PCSGAlertRowEnum, PCSGAlertSideRxRestart},
    [PCSGAlertIndexToneVolume] =
        {"Signal Vol:", tone_volume_text, PCSG_ALERT_ROW(filter.tone_volume), 0,
         PCSGToneVolumeCount, PCSGAlertRowEnum, PCSGAlertSideRxRestart},
    [PCSGAlertIndexTonePitch] =
        {"Signal Pitch:", tone_pitch_text, PCSG_ALERT_ROW(filter.tone_pitch), 0,
         PCSGTonePitchCount, PCSGAlertRowEnum, PCSGAlertSideRxRestart},
    [PCSGAlertIndexToneSpread] =
        {"Signal Spread:", tone_spread_text, PCSG_ALERT_ROW(filter.tone_spread), 0,
         PCSGToneSpreadCount, PCSGAlertRowEnum, PCSGAlertSideRxRestart},
};

static uint8_t pcsg_alert_row_value(const POCSAGPagerApp* app, const PCSGAlertRow* row) {
    const uint8_t stored = *((const uint8_t*)app + row->offset);
    if(row->kind == PCSGAlertRowLed) {
        return stored ? (uint8_t)(*((const uint8_t*)app + row->extra) + 1U) : 0U;
    }
    return stored;
}

// The row being changed is always the selected one, which is how this finds
// its description without a separate callback per row.
static void pcsg_alert_row_changed(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_list_get_selected_item_index(app->variable_item_list);
    if(index >= COUNT_OF(pcsg_alert_rows)) return;
    const PCSGAlertRow* row = &pcsg_alert_rows[index];

    const uint8_t value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, row->text[value]);

    uint8_t* field = (uint8_t*)app + row->offset;
    if(row->kind == PCSGAlertRowLed) {
        *field = value != 0U;
        if(value) *((uint8_t*)app + row->extra) = value - 1U;
    } else {
        *field = value;
    }

    switch(row->side) {
    case PCSGAlertSideReset:
        pcsg_classic_alert_reset(app);
        break;
    case PCSGAlertSideRxRestart:
        pcsg_rx_restart_request(app);
        break;
    default:
        break;
    }
}

/*
 * Nothing here opens a scene, but the list is shared and its reset frees only
 * the items: the enter callback outlives the scene that set it. Without one of
 * our own, OK on a row would run the settings list's handler against these
 * indices. NULL is not an option - the module furi_checks it.
 */
static void pcsg_alert_enter_callback(void* context, uint32_t index) {
    UNUSED(context);
    UNUSED(index);
}

void pocsag_pager_scene_alert_config_on_enter(void* context) {
    POCSAGPagerApp* app = context;

    for(uint8_t index = 0; index < COUNT_OF(pcsg_alert_rows); index++) {
        const PCSGAlertRow* row = &pcsg_alert_rows[index];
        /*
         * A settings file from another build can carry a value this row no
         * longer has. The LED rows hold two fields and are checked apart: the
         * colour is what can be out of range, the enable being a bool.
         */
        if(row->kind == PCSGAlertRowLed) {
            uint8_t* colour = (uint8_t*)app + row->extra;
            if(*colour >= PCSGLedColorCount) *colour = 0U;
        } else {
            uint8_t* field = (uint8_t*)app + row->offset;
            if(*field >= row->count) *field = 0U;
        }

        VariableItem* item = variable_item_list_add(
            app->variable_item_list, row->label, row->count, pcsg_alert_row_changed, app);
        const uint8_t value = pcsg_alert_row_value(app, row);
        variable_item_set_current_value_index(item, value);
        variable_item_set_current_value_text(item, row->text[value]);
    }

    variable_item_list_set_enter_callback(app->variable_item_list, pcsg_alert_enter_callback, app);
    variable_item_list_set_selected_item(
        app->variable_item_list, app->alert_config_selected_index);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewVariableItemList);
}

bool pocsag_pager_scene_alert_config_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    // Nothing here opens a scene of its own; every row is stepped in place.
    return false;
}

void pocsag_pager_scene_alert_config_on_exit(void* context) {
    POCSAGPagerApp* app = context;
    app->alert_config_selected_index =
        variable_item_list_get_selected_item_index(app->variable_item_list);
    variable_item_list_set_selected_item(app->variable_item_list, 0);
    variable_item_list_reset(app->variable_item_list);
    pcsg_settings_save(app);
}
