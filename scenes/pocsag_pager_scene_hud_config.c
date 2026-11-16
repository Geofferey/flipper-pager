#include "../pocsag_pager_app_i.h"

/*
 * What the receiver's clock and Network info screen are made of. Display only:
 * the log timestamps from the Flipper's own clock whatever is set here, so
 * changing one of these never rewrites what has already been written down.
 *
 * The sources exist because a network can be wrong about its own clock and two
 * observed ones are - one sending a stale date, another today's day and month
 * with the year field wrapped - so any part can be taken locally instead.
 */
typedef enum {
    PCSGHudConfigDate,
    PCSGHudConfigYear,
    PCSGHudConfigHour,
    PCSGHudConfigMinute,
    PCSGHudConfigOffset,
    PCSGHudConfigFreqSwitch,
    PCSGHudConfigTicker,
    PCSGHudConfigTickerSecs,
    PCSGHudConfigZone,
    PCSGHudConfigShowDate,
    PCSGHudConfigSsid1,
    PCSGHudConfigSsid2,
    PCSGHudConfigRoaming,
    PCSGHudConfigFrame,
    PCSGHudConfigExtSec,
} PCSGHudConfigIndex;

static const char* const hud_source_text[PCSGHudSourceCount] = {"Network", "Local"};
static const char* const hud_hour_text[PCSGHudHourCount] = {"Network", "Local", "Hybrid"};
static const char* const hud_ticker_text[PCSGHudTickerCount] = {
    "Rotate",
    "Freq",
    "Clock",
    "Off"};
static const char* const hud_show_text[] = {"Hide", "Show"};
static const char* const hud_freq_text[PCSGHudFreqCount] = {"Discard", "Keep"};

#define PCSG_HUD_TICKER_SECS_COUNT \
    (PCSG_HUD_TICKER_SECS_MAX - PCSG_HUD_TICKER_SECS_MIN + 1U)

/*
 * Every row here is one byte of app->filter holding a small index, so one
 * setter covers all of them: store the choice, then show the text for it.
 * Rows whose stored value is not the index they display say so with a reader
 * and a writer of their own.
 */
typedef struct {
    const char* label;
    uint16_t offset;
    uint8_t count;
    const char* const* text;
} PCSGHudRow;

#define PCSG_HUD_FIELD(member) offsetof(POCSAGPagerApp, member)

static const PCSGHudRow pcsg_hud_rows[] = {
    // Day and month together; the year is its own row below, because that is
    // the part the observed networks get wrong.
    [PCSGHudConfigDate] =
        {"Day/Month:", PCSG_HUD_FIELD(filter.hud_date), PCSGHudSourceCount, hud_source_text},
    [PCSGHudConfigYear] =
        {"Year:", PCSG_HUD_FIELD(filter.hud_year), PCSGHudSourceCount, hud_source_text},
    [PCSGHudConfigHour] =
        {"Hour:", PCSG_HUD_FIELD(filter.hud_hour), PCSGHudHourCount, hud_hour_text},
    [PCSGHudConfigMinute] =
        {"Minute:", PCSG_HUD_FIELD(filter.hud_minute), PCSGHudSourceCount, hud_source_text},
    [PCSGHudConfigOffset] = {"Offset:", PCSG_HUD_FIELD(filter.hud_offset_hours), 0, NULL},
    [PCSGHudConfigFreqSwitch] =
        {"Freq switch:",
         PCSG_HUD_FIELD(filter.hud_freq_switch),
         PCSGHudFreqCount,
         hud_freq_text},
    [PCSGHudConfigTicker] =
        {"Ticker:", PCSG_HUD_FIELD(filter.hud_ticker), PCSGHudTickerCount, hud_ticker_text},
    [PCSGHudConfigTickerSecs] =
        {"Ticker secs:", PCSG_HUD_FIELD(filter.hud_ticker_secs), 0, NULL},
    [PCSGHudConfigZone] =
        {"Zone:", PCSG_HUD_FIELD(filter.hud_show_zone), COUNT_OF(hud_show_text), hud_show_text},
    [PCSGHudConfigShowDate] =
        {"Date row:",
         PCSG_HUD_FIELD(filter.hud_show_date),
         COUNT_OF(hud_show_text),
         hud_show_text},
    [PCSGHudConfigSsid1] =
        {"Local ID:",
         PCSG_HUD_FIELD(filter.hud_show_ssid1),
         COUNT_OF(hud_show_text),
         hud_show_text},
    [PCSGHudConfigSsid2] =
        {"Country:",
         PCSG_HUD_FIELD(filter.hud_show_ssid2),
         COUNT_OF(hud_show_text),
         hud_show_text},
    [PCSGHudConfigRoaming] =
        {"Roaming:",
         PCSG_HUD_FIELD(filter.hud_show_roaming),
         COUNT_OF(hud_show_text),
         hud_show_text},
    [PCSGHudConfigFrame] =
        {"Frame:", PCSG_HUD_FIELD(filter.hud_show_frame), COUNT_OF(hud_show_text), hud_show_text},
    [PCSGHudConfigExtSec] =
        {"ExtSec:",
         PCSG_HUD_FIELD(filter.hud_show_ext_sec),
         COUNT_OF(hud_show_text),
         hud_show_text},
};

// None in the middle, so left is behind and right ahead, as the sign reads.
static uint8_t pocsag_pager_scene_hud_offset_index(int8_t hours) {
    return (uint8_t)(hours + PCSG_HUD_OFFSET_MAX);
}

static void pocsag_pager_scene_hud_show_offset(VariableItem* item, int8_t hours) {
    if(hours == 0) {
        variable_item_set_current_value_text(item, "None");
        return;
    }
    char text[8];
    snprintf(text, sizeof(text), "%+d h", hours);
    variable_item_set_current_value_text(item, text);
}

static void pocsag_pager_scene_hud_show_ticker_secs(VariableItem* item, uint8_t secs) {
    char text[8];
    snprintf(text, sizeof(text), "%u s", secs);
    variable_item_set_current_value_text(item, text);
}

static void pocsag_pager_scene_hud_changed(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_list_get_selected_item_index(app->variable_item_list);
    if(index >= COUNT_OF(pcsg_hud_rows)) return;
    const PCSGHudRow* row = &pcsg_hud_rows[index];
    const uint8_t value = variable_item_get_current_value_index(item);

    if(index == PCSGHudConfigOffset) {
        const int8_t hours = (int8_t)((int16_t)value - PCSG_HUD_OFFSET_MAX);
        *(int8_t*)((uint8_t*)app + row->offset) = hours;
        pocsag_pager_scene_hud_show_offset(item, hours);
        return;
    }
    if(index == PCSGHudConfigTickerSecs) {
        const uint8_t secs = (uint8_t)(value + PCSG_HUD_TICKER_SECS_MIN);
        *((uint8_t*)app + row->offset) = secs;
        pocsag_pager_scene_hud_show_ticker_secs(item, secs);
        return;
    }
    *((uint8_t*)app + row->offset) = value;
    variable_item_set_current_value_text(item, row->text[value]);
}

void pocsag_pager_scene_hud_config_on_enter(void* context) {
    POCSAGPagerApp* app = context;

    for(uint8_t index = 0; index < COUNT_OF(pcsg_hud_rows); index++) {
        const PCSGHudRow* row = &pcsg_hud_rows[index];
        if(index == PCSGHudConfigOffset) {
            if(app->filter.hud_offset_hours > PCSG_HUD_OFFSET_MAX ||
               app->filter.hud_offset_hours < -PCSG_HUD_OFFSET_MAX) {
                app->filter.hud_offset_hours = 0;
            }
            VariableItem* item = variable_item_list_add(
                app->variable_item_list,
                row->label,
                PCSG_HUD_OFFSET_COUNT,
                pocsag_pager_scene_hud_changed,
                app);
            variable_item_set_current_value_index(
                item, pocsag_pager_scene_hud_offset_index(app->filter.hud_offset_hours));
            pocsag_pager_scene_hud_show_offset(item, app->filter.hud_offset_hours);
            continue;
        }
        if(index == PCSGHudConfigTickerSecs) {
            uint8_t secs = app->filter.hud_ticker_secs;
            if(secs < PCSG_HUD_TICKER_SECS_MIN || secs > PCSG_HUD_TICKER_SECS_MAX) {
                secs = PCSG_HUD_TICKER_SECS_DEFAULT;
                app->filter.hud_ticker_secs = secs;
            }
            VariableItem* item = variable_item_list_add(
                app->variable_item_list,
                row->label,
                PCSG_HUD_TICKER_SECS_COUNT,
                pocsag_pager_scene_hud_changed,
                app);
            variable_item_set_current_value_index(
                item, (uint8_t)(secs - PCSG_HUD_TICKER_SECS_MIN));
            pocsag_pager_scene_hud_show_ticker_secs(item, secs);
            continue;
        }

        uint8_t* field = (uint8_t*)app + row->offset;
        if(*field >= row->count) *field = 0U;
        VariableItem* item = variable_item_list_add(
            app->variable_item_list, row->label, row->count, pocsag_pager_scene_hud_changed, app);
        variable_item_set_current_value_index(item, *field);
        variable_item_set_current_value_text(item, row->text[*field]);
    }

    variable_item_list_set_selected_item(app->variable_item_list, app->hud_config_selected_index);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewVariableItemList);
}

bool pocsag_pager_scene_hud_config_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void pocsag_pager_scene_hud_config_on_exit(void* context) {
    POCSAGPagerApp* app = context;
    app->hud_config_selected_index =
        variable_item_list_get_selected_item_index(app->variable_item_list);
    variable_item_list_reset(app->variable_item_list);
    pcsg_settings_save(app);
}
