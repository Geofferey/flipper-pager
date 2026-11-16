#include "../pocsag_pager_app_i.h"

static void pocsag_pager_scene_tx_ric_input_callback(void* context, int32_t number) {
    POCSAGPagerApp* app = context;
    if(app->tx_config.protocol != PagerTxProtocolFlex) {
        app->tx_config.ric[0] = number;
    } else {
        /*
         * A capcode's phase is part of its address, so it is filed under the
         * phase it belongs to rather than the entry that was open. The row
         * then steps to that phase, which is how the letter shows where the
         * capcode went. Clearing an entry leaves it where it was.
         */
        uint8_t order[4];
        const uint8_t phases = flex_tx_mode_phases(&app->tx_config, order);
        const uint8_t phase = number ? flex_tx_capcode_slot((uint32_t)number) :
                                       order[app->tx_ric_slot];
        app->tx_config.ric[phase] = number;
        for(uint8_t i = 0; i < phases; i++) {
            if(order[i] == phase) app->tx_ric_slot = i;
        }
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, PCSGCustomEventTxRicInputDone);
}

// Which phase the open entry is editing.
static uint8_t pocsag_pager_tx_ric_phase(POCSAGPagerApp* app) {
    uint8_t order[4];
    const uint8_t phases = flex_tx_mode_phases(&app->tx_config, order);
    return order[app->tx_ric_slot < phases ? app->tx_ric_slot : 0U];
}

void pocsag_pager_scene_tx_ric_input_on_enter(void* context) {
    POCSAGPagerApp* app = context;
    const bool flex = app->tx_config.protocol == PagerTxProtocolFlex;
    number_input_set_header_text(
        app->number_input, flex ? "FLEX short capcode" : "Destination RIC");
    number_input_set_result_callback(
        app->number_input,
        pocsag_pager_scene_tx_ric_input_callback,
        app,
        flex ? app->tx_config.ric[pocsag_pager_tx_ric_phase(app)] : app->tx_config.ric[0],
        // Zero is allowed so a phase can be cleared.
        0,
        flex ? FLEX_TX_SHORT_CAPCODE_MAX : PCSG_TX_RIC_MAX);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewNumberInput);
}

bool pocsag_pager_scene_tx_ric_input_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    if(event.type == SceneManagerEventTypeCustom &&
       event.event == PCSGCustomEventTxRicInputDone) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void pocsag_pager_scene_tx_ric_input_on_exit(void* context) {
    UNUSED(context);
}
