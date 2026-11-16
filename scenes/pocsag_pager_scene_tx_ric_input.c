#include "../pocsag_pager_app_i.h"

// Which phase the open entry is editing.
static uint8_t pocsag_pager_tx_ric_phase(POCSAGPagerApp* app) {
    uint8_t order[4];
    const uint8_t phases = flex_tx_mode_phases(&app->tx_config, order);
    return order[app->tx_ric_slot < phases ? app->tx_ric_slot : 0U];
}

static void pocsag_pager_scene_tx_ric_input_callback(void* context, int32_t number) {
    POCSAGPagerApp* app = context;
    if(app->tx_config.protocol != PagerTxProtocolFlex) {
        app->tx_config.ric[0] = number;
    } else {
        if(number != 0 && !flex_tx_capcode_valid((uint32_t)number)) {
            view_dispatcher_send_custom_event(
                app->view_dispatcher, PCSGCustomEventTxRicInputInvalid);
            return;
        }
        // The capcode goes on the phase that was selected. Which phase a
        // subscriber listens on is provisioning, not arithmetic on the
        // address - see the Capcode row in pocsag_pager_scene_tx_config.c.
        app->tx_config.ric[pocsag_pager_tx_ric_phase(app)] = number;
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, PCSGCustomEventTxRicInputDone);
}

void pocsag_pager_scene_tx_ric_input_on_enter(void* context) {
    POCSAGPagerApp* app = context;
    const bool flex = app->tx_config.protocol == PagerTxProtocolFlex;
    // Naming the phase says where the capcode is going, since the entry fills
    // the slot that was selected rather than one derived from the number.
    char header[28];
    if(flex) {
        snprintf(
            header,
            sizeof(header),
            "Phase %c capcode (0 clears)",
            'A' + pocsag_pager_tx_ric_phase(app));
    }
    number_input_set_header_text(app->number_input, flex ? header : "Destination RIC");
    number_input_set_result_callback(
        app->number_input,
        pocsag_pager_scene_tx_ric_input_callback,
        app,
        flex ? app->tx_config.ric[pocsag_pager_tx_ric_phase(app)] : app->tx_config.ric[0],
        // Zero is allowed so a phase can be cleared.
        0,
        flex ? FLEX_TX_LONG_CAPCODE_MAX : PCSG_TX_RIC_MAX);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewNumberInput);
}

bool pocsag_pager_scene_tx_ric_input_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    if(event.type == SceneManagerEventTypeCustom &&
       event.event == PCSGCustomEventTxRicInputInvalid) {
        number_input_set_header_text(app->number_input, "Invalid FLEX capcode");
        notification_message(app->notifications, &sequence_error);
        return true;
    }
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
