#include "../pocsag_pager_app_i.h"

/*
 * The FLEX parameters that describe the system rather than the page: the
 * system identification words, the roaming flag, the seconds trim, the message
 * number and how much acquisition runway a page gets. They live behind their
 * own row because none of them is touched in ordinary use, which is also where
 * PortaPack Mayhem's FLEX TX puts them (its "Set params" screen). Field
 * layouts and ranges follow it; see README.md.
 */
typedef enum {
    PCSGTxParamsExtSec,
    PCSGTxParamsSsid1,
    PCSGTxParamsLocalId,
    PCSGTxParamsCoverage,
    PCSGTxParamsSsid2,
    PCSGTxParamsCountry,
    PCSGTxParamsRoaming,
    PCSGTxParamsMsgNumber,
    PCSGTxParamsErs,
} PCSGTxParamsIndex;

static const char* const tx_params_off_on[] = {"Off", "On"};

#define PCSG_TX_PARAMS_ERS_COUNT (PCSG_TX_ERS_CYCLES_MAX / PCSG_TX_ERS_CYCLES_STEP + 1U)

static void pocsag_pager_scene_tx_params_show_toggle(VariableItem* item, bool value) {
    const uint8_t index = value ? 1U : 0U;
    variable_item_set_current_value_index(item, index);
    variable_item_set_current_value_text(item, tx_params_off_on[index]);
}

// Each cycle is 96 bits at 1600 baud, so 60 ms of runway apiece.
static void pocsag_pager_scene_tx_params_show_ers(POCSAGPagerApp* app) {
    VariableItem* item = variable_item_list_get(app->variable_item_list, PCSGTxParamsErs);
    if(!item) return;
    uint8_t cycles = app->tx_config.ers_cycles;
    if(cycles > PCSG_TX_ERS_CYCLES_MAX) cycles = PCSG_TX_ERS_CYCLES_DEFAULT;
    variable_item_set_current_value_index(item, cycles / PCSG_TX_ERS_CYCLES_STEP);
    if(cycles == 0U) {
        variable_item_set_current_value_text(item, "Off");
        return;
    }
    char text[16];
    const uint16_t ms = (uint16_t)cycles * 60U;
    snprintf(text, sizeof(text), "%u.%02us", ms / 1000U, (ms % 1000U) / 10U);
    variable_item_set_current_value_text(item, text);
}

static void pocsag_pager_scene_tx_params_show_msg_number(POCSAGPagerApp* app) {
    VariableItem* item = variable_item_list_get(app->variable_item_list, PCSGTxParamsMsgNumber);
    if(!item) return;
    uint8_t number = app->tx_config.msg_number;
    if(number > PCSG_TX_MSG_NUMBER_AUTO) number = PCSG_TX_MSG_NUMBER_AUTO;
    variable_item_set_current_value_index(item, number);
    if(number == PCSG_TX_MSG_NUMBER_AUTO) {
        variable_item_set_current_value_text(item, "Auto");
        return;
    }
    char text[8];
    snprintf(text, sizeof(text), "%u", number);
    variable_item_set_current_value_text(item, text);
}

static void
    pocsag_pager_scene_tx_params_show_number(POCSAGPagerApp* app, uint8_t index, uint16_t value) {
    VariableItem* item = variable_item_list_get(app->variable_item_list, index);
    if(!item) return;
    char text[8];
    snprintf(text, sizeof(text), "%u", value);
    variable_item_set_current_value_text(item, text);
}

static void pocsag_pager_scene_tx_params_set_ext_sec(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    app->tx_config.ext_sec = variable_item_get_current_value_index(item);
    char text[8];
    snprintf(text, sizeof(text), "%u", app->tx_config.ext_sec);
    variable_item_set_current_value_text(item, text);
}

static void pocsag_pager_scene_tx_params_set_ssid1(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    app->tx_config.send_ssid1 = variable_item_get_current_value_index(item) != 0U;
    pocsag_pager_scene_tx_params_show_toggle(item, app->tx_config.send_ssid1);
}

static void pocsag_pager_scene_tx_params_set_coverage(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    app->tx_config.coverage_zone = variable_item_get_current_value_index(item);
    char text[8];
    snprintf(text, sizeof(text), "%u", app->tx_config.coverage_zone);
    variable_item_set_current_value_text(item, text);
}

static void pocsag_pager_scene_tx_params_set_ssid2(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    app->tx_config.send_ssid2 = variable_item_get_current_value_index(item) != 0U;
    pocsag_pager_scene_tx_params_show_toggle(item, app->tx_config.send_ssid2);
}

/*
 * Roaming only means anything alongside an identified system, so turning it on
 * turns both SSID words on, as Mayhem's on_roaming_changed() does. Turning it
 * off leaves them, since they stand on their own.
 */
static void pocsag_pager_scene_tx_params_set_roaming(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    app->tx_config.roaming = variable_item_get_current_value_index(item) != 0U;
    pocsag_pager_scene_tx_params_show_toggle(item, app->tx_config.roaming);
    if(!app->tx_config.roaming) return;
    app->tx_config.send_ssid1 = true;
    app->tx_config.send_ssid2 = true;
    VariableItem* ssid1 = variable_item_list_get(app->variable_item_list, PCSGTxParamsSsid1);
    VariableItem* ssid2 = variable_item_list_get(app->variable_item_list, PCSGTxParamsSsid2);
    if(ssid1) pocsag_pager_scene_tx_params_show_toggle(ssid1, true);
    if(ssid2) pocsag_pager_scene_tx_params_show_toggle(ssid2, true);
}

static void pocsag_pager_scene_tx_params_set_msg_number(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    app->tx_config.msg_number = variable_item_get_current_value_index(item);
    pocsag_pager_scene_tx_params_show_msg_number(app);
}

static void pocsag_pager_scene_tx_params_set_ers(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    app->tx_config.ers_cycles =
        (uint8_t)(variable_item_get_current_value_index(item) * PCSG_TX_ERS_CYCLES_STEP);
    pocsag_pager_scene_tx_params_show_ers(app);
}

static void pocsag_pager_scene_tx_params_enter_callback(void* context, uint32_t index) {
    POCSAGPagerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void pocsag_pager_scene_tx_params_on_enter(void* context) {
    POCSAGPagerApp* app = context;
    VariableItem* item;

    item = variable_item_list_add(
        app->variable_item_list,
        "ExtSec:",
        PCSG_TX_EXT_SEC_MAX + 1U,
        pocsag_pager_scene_tx_params_set_ext_sec,
        app);
    if(app->tx_config.ext_sec > PCSG_TX_EXT_SEC_MAX) app->tx_config.ext_sec = 0U;
    variable_item_set_current_value_index(item, app->tx_config.ext_sec);
    pocsag_pager_scene_tx_params_show_number(app, PCSGTxParamsExtSec, app->tx_config.ext_sec);

    item = variable_item_list_add(
        app->variable_item_list,
        "SSID1:",
        COUNT_OF(tx_params_off_on),
        pocsag_pager_scene_tx_params_set_ssid1,
        app);
    pocsag_pager_scene_tx_params_show_toggle(item, app->tx_config.send_ssid1);

    variable_item_list_add(app->variable_item_list, "Local ID:", 1, NULL, app);
    if(app->tx_config.local_id > PCSG_TX_LOCAL_ID_MAX) app->tx_config.local_id = 0U;
    pocsag_pager_scene_tx_params_show_number(app, PCSGTxParamsLocalId, app->tx_config.local_id);

    item = variable_item_list_add(
        app->variable_item_list,
        "Cov zone:",
        PCSG_TX_COVERAGE_MAX + 1U,
        pocsag_pager_scene_tx_params_set_coverage,
        app);
    if(app->tx_config.coverage_zone > PCSG_TX_COVERAGE_MAX) app->tx_config.coverage_zone = 0U;
    variable_item_set_current_value_index(item, app->tx_config.coverage_zone);
    pocsag_pager_scene_tx_params_show_number(
        app, PCSGTxParamsCoverage, app->tx_config.coverage_zone);

    item = variable_item_list_add(
        app->variable_item_list,
        "SSID2:",
        COUNT_OF(tx_params_off_on),
        pocsag_pager_scene_tx_params_set_ssid2,
        app);
    pocsag_pager_scene_tx_params_show_toggle(item, app->tx_config.send_ssid2);

    variable_item_list_add(app->variable_item_list, "Country:", 1, NULL, app);
    if(app->tx_config.country_code > PCSG_TX_COUNTRY_MAX) app->tx_config.country_code = 0U;
    pocsag_pager_scene_tx_params_show_number(
        app, PCSGTxParamsCountry, app->tx_config.country_code);

    item = variable_item_list_add(
        app->variable_item_list,
        "Roaming:",
        COUNT_OF(tx_params_off_on),
        pocsag_pager_scene_tx_params_set_roaming,
        app);
    pocsag_pager_scene_tx_params_show_toggle(item, app->tx_config.roaming);

    variable_item_list_add(
        app->variable_item_list,
        "Msg #:",
        PCSG_TX_MSG_NUMBER_AUTO + 1U,
        pocsag_pager_scene_tx_params_set_msg_number,
        app);
    pocsag_pager_scene_tx_params_show_msg_number(app);

    variable_item_list_add(
        app->variable_item_list,
        "ERS:",
        PCSG_TX_PARAMS_ERS_COUNT,
        pocsag_pager_scene_tx_params_set_ers,
        app);
    pocsag_pager_scene_tx_params_show_ers(app);

    variable_item_list_set_enter_callback(
        app->variable_item_list, pocsag_pager_scene_tx_params_enter_callback, app);
    variable_item_list_set_selected_item(
        app->variable_item_list, app->tx_params_selected_index);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewVariableItemList);
}

bool pocsag_pager_scene_tx_params_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    // 512 and 1024 values are more than the arrows can reasonably reach, so
    // these two are typed on the shared number pad instead.
    if(event.event == PCSGTxParamsLocalId || event.event == PCSGTxParamsCountry) {
        scene_manager_set_scene_state(
            app->scene_manager,
            POCSAGPagerSceneCapcodeInput,
            event.event == PCSGTxParamsLocalId ? PCSGNumberFieldTxLocalId :
                                                 PCSGNumberFieldTxCountry);
        scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneCapcodeInput);
        return true;
    }
    return false;
}

void pocsag_pager_scene_tx_params_on_exit(void* context) {
    POCSAGPagerApp* app = context;
    app->tx_params_selected_index =
        variable_item_list_get_selected_item_index(app->variable_item_list);
    variable_item_list_reset(app->variable_item_list);
    pcsg_settings_save(app);
}
