#include "../pocsag_pager_app_i.h"

// Number entry for the capcode and frame filters. The scene state selects
// the field being edited.

/*
 * A frequency is the one field here that a range cannot validate: the radio
 * decides, and a rejected value must leave the old one alone rather than be
 * clamped into a band the hardware will not tune. Shared so that every
 * frequency entry in the app asks the same question.
 */
static bool pocsag_pager_scene_capcode_input_is_frequency(uint32_t field) {
#ifdef PCSG_TX_APP
    return field == PCSGNumberFieldTxFrequency;
#else
    return field == PCSGNumberFieldRxFrequency;
#endif
}

// Round onto the arrow step, so typing and stepping cannot disagree and the
// saved value stays one the settings loader accepts.
static int32_t pocsag_pager_scene_capcode_input_round(int32_t hz) {
    const int32_t step = PCSG_FREQ_CORRECTION_STEP_HZ;
    const int32_t half = step / 2;
    return hz < 0 ? -(((-hz) + half) / step) * step : ((hz + half) / step) * step;
}

static uint32_t* pocsag_pager_scene_capcode_input_field(POCSAGPagerApp* app) {
    switch(scene_manager_get_scene_state(app->scene_manager, POCSAGPagerSceneCapcodeInput)) {
#ifdef PCSG_TX_APP
    case PCSGNumberFieldTxFrequency:
    default:
        return &app->tx_config.frequency;
#else
    case PCSGNumberFieldRxFrequency:
        return &app->txrx->preset->frequency;
    case PCSGNumberFieldCapcodeMax:
        return &app->filter.capcode_max;
    case PCSGNumberFieldFrameMin:
        return &app->filter.frame_min;
    case PCSGNumberFieldFrameMax:
        return &app->filter.frame_max;
    case PCSGNumberFieldRicMin:
        return &app->filter.ric_min;
    case PCSGNumberFieldRicMax:
        return &app->filter.ric_max;
#endif
    case PCSGNumberFieldFreqCorrection:
        return (uint32_t*)&app->filter.frequency_correction_hz;
#ifndef PCSG_TX_APP
    default:
        return &app->filter.capcode_min;
#endif
    }
}

static void pocsag_pager_scene_capcode_input_callback(void* context, int32_t number) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    const uint32_t field =
        scene_manager_get_scene_state(app->scene_manager, POCSAGPagerSceneCapcodeInput);

    if(pocsag_pager_scene_capcode_input_is_frequency(field) &&
       !subghz_devices_is_frequency_valid(app->txrx->radio_device, (uint32_t)number)) {
        // Outside every band the radio will tune: keep what was there.
        notification_message(app->notifications, &sequence_error);
    } else {
        if(field == PCSGNumberFieldFreqCorrection) {
            number = pocsag_pager_scene_capcode_input_round(number);
        }
        *pocsag_pager_scene_capcode_input_field(app) = number;
#ifndef PCSG_TX_APP
        // Retune on returning to the receiver, as changing the channel does.
        if(field == PCSGNumberFieldRxFrequency) app->txrx->rx_restart = true;
#endif
    }

    view_dispatcher_send_custom_event(app->view_dispatcher, PCSGCustomEventSceneCapcodeInputDone);
}

void pocsag_pager_scene_capcode_input_on_enter(void* context) {
    POCSAGPagerApp* app = context;
    const uint32_t field =
        scene_manager_get_scene_state(app->scene_manager, POCSAGPagerSceneCapcodeInput);
#ifndef PCSG_TX_APP
    const bool frame = field == PCSGNumberFieldFrameMin || field == PCSGNumberFieldFrameMax;
    const bool ric = field == PCSGNumberFieldRicMin || field == PCSGNumberFieldRicMax;
    const PCSGCapcodeFilter mode = frame ? app->filter.frame_filter :
                                   ric   ? app->filter.ric_filter :
                                           app->filter.capcode_filter;
    const bool range = mode == PCSGCapcodeFilterRange;
#endif

    const char* header;
    int32_t min = 0;
    int32_t max = PCSG_CAPCODE_MAX;
    switch(field) {
#ifdef PCSG_TX_APP
    case PCSGNumberFieldTxFrequency:
    default:
        header = "TX frequency, Hz";
#else
    case PCSGNumberFieldRxFrequency:
        header = "RX frequency, Hz";
#endif
        min = PCSG_FREQUENCY_MIN_HZ;
        max = PCSG_FREQUENCY_MAX_HZ;
        break;
    case PCSGNumberFieldFreqCorrection:
        // Signed: + raises a low carrier, - lowers a high one.
        header = "Correction, Hz +/-";
        min = PCSG_FREQ_CORRECTION_MIN_HZ;
        max = PCSG_FREQ_CORRECTION_MAX_HZ;
        break;
#ifndef PCSG_TX_APP
    case PCSGNumberFieldCapcodeMax:
        header = "Capcode max";
        break;
    case PCSGNumberFieldFrameMin:
        header = range ? "Frame min (0-127)" : "Frame (0-127)";
        max = PCSG_FRAME_MAX;
        break;
    case PCSGNumberFieldFrameMax:
        header = "Frame max (0-127)";
        max = PCSG_FRAME_MAX;
        break;
    case PCSGNumberFieldRicMin:
        header = range ? "RIC min" : "RIC";
        max = PCSG_RIC_MAX;
        break;
    case PCSGNumberFieldRicMax:
        header = "RIC max";
        max = PCSG_RIC_MAX;
        break;
    default:
        header = range ? "Capcode min" : "Capcode";
        break;
#endif
    }

    number_input_set_header_text(app->number_input, header);
    number_input_set_result_callback(
        app->number_input,
        pocsag_pager_scene_capcode_input_callback,
        app,
        (int32_t)*pocsag_pager_scene_capcode_input_field(app),
        min,
        max);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewNumberInput);
}

bool pocsag_pager_scene_capcode_input_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    if(event.type == SceneManagerEventTypeCustom &&
       event.event == PCSGCustomEventSceneCapcodeInputDone) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void pocsag_pager_scene_capcode_input_on_exit(void* context) {
    UNUSED(context);
}
