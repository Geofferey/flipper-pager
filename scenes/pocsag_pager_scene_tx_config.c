#include "../pocsag_pager_app_i.h"

typedef enum {
    PCSGTxConfigProtocol,
    PCSGTxConfigFrequency,
    PCSGTxConfigRic,
    PCSGTxConfigFunction,
    PCSGTxConfigBaud,
    PCSGTxConfigPolarity,
    PCSGTxConfigCorrection,
    PCSGTxConfigClock,
    PCSGTxConfigMessage,
    PCSGTxConfigReview,
} PCSGTxConfigIndex;

static const uint16_t tx_baud_values[] = {512, 1200, 2400};
static const char* const tx_baud_text[] = {"512", "1200", "2400"};
static const char* const tx_protocol_text[] = {"POCSAG", "FLEX"};
static const char* const tx_function_text[] = {"0", "1", "2", "3"};
static const char* const tx_polarity_text[] = {"Standard", "Inverted"};
static const char* const tx_flex_mode_text[] = {"1600/2FSK", "3200/2FSK"};
// Indexed by the stored bit mask, so the order follows PCSG_TX_CLOCK_*.
static const char* const tx_clock_text[] = {"Off", "Time", "Date", "Time+Date"};

#define PCSG_FREQ_CORR_COUNT                                                   \
    ((PCSG_FREQ_CORRECTION_LIST_MAX_HZ * 2) / PCSG_FREQ_CORRECTION_STEP_HZ + 1)

static uint8_t pocsag_pager_scene_tx_frequency_index(POCSAGPagerApp* app) {
    for(uint8_t i = 0; i < subghz_setting_get_frequency_count(app->setting); i++) {
        if(subghz_setting_get_frequency(app->setting, i) == app->tx_config.frequency) return i;
    }
    return subghz_setting_get_frequency_default_index(app->setting);
}

static uint8_t pocsag_pager_scene_tx_baud_index(uint16_t baud) {
    for(uint8_t i = 0; i < COUNT_OF(tx_baud_values); i++) {
        if(tx_baud_values[i] == baud) return i;
    }
    return 1;
}

// A typed value can sit past what the arrows reach, so clamp rather than
// discard: the row still displays the real value, it just stops stepping.
static uint8_t pocsag_pager_scene_tx_correction_index(int32_t correction_hz) {
    if(correction_hz > PCSG_FREQ_CORRECTION_LIST_MAX_HZ) {
        correction_hz = PCSG_FREQ_CORRECTION_LIST_MAX_HZ;
    } else if(correction_hz < -PCSG_FREQ_CORRECTION_LIST_MAX_HZ) {
        correction_hz = -PCSG_FREQ_CORRECTION_LIST_MAX_HZ;
    }
    const int32_t offset = correction_hz + PCSG_FREQ_CORRECTION_LIST_MAX_HZ;
    return (uint8_t)((offset + PCSG_FREQ_CORRECTION_STEP_HZ / 2) / PCSG_FREQ_CORRECTION_STEP_HZ);
}

static void pocsag_pager_scene_tx_show_correction(VariableItem* item, int32_t correction_hz) {
    if(correction_hz == 0) {
        variable_item_set_current_value_text(item, "Off");
        return;
    }

    char text[16];
    const uint32_t magnitude = correction_hz < 0 ? -correction_hz : correction_hz;
    snprintf(
        text,
        sizeof(text),
        "%c%lu.%01lu kHz",
        correction_hz < 0 ? '-' : '+',
        magnitude / 1000U,
        (magnitude % 1000U) / 100U);
    variable_item_set_current_value_text(item, text);
}

static void pocsag_pager_scene_tx_show_frequency(VariableItem* item, uint32_t hz) {
    char text[12];
    snprintf(text, sizeof(text), "%lu.%04lu", hz / 1000000U, (hz % 1000000U) / 100U);
    variable_item_set_current_value_text(item, text);
}

// Left and right step through the preset channels; OK on the row types any
// frequency, so on_enter must show the stored value rather than calling this,
// which would snap a typed one back onto the list.
static void pocsag_pager_scene_tx_set_frequency(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->tx_config.frequency = subghz_setting_get_frequency(app->setting, index);
    pocsag_pager_scene_tx_show_frequency(item, app->tx_config.frequency);
}

// FLEX short capcodes occupy their own range, so the address has to be moved
// into it when the protocol changes and when the scene opens on it.
static void pocsag_pager_scene_tx_clamp_address(POCSAGPagerApp* app) {
    if(app->tx_config.protocol != PagerTxProtocolFlex) return;
    if(app->tx_config.ric < FLEX_TX_SHORT_CAPCODE_MIN) {
        app->tx_config.ric = FLEX_TX_SHORT_CAPCODE_MIN;
    } else if(app->tx_config.ric > FLEX_TX_SHORT_CAPCODE_MAX) {
        app->tx_config.ric = FLEX_TX_SHORT_CAPCODE_MAX;
    }
}

static void pocsag_pager_scene_tx_show_address(POCSAGPagerApp* app) {
    VariableItem* item =
        variable_item_list_get(app->variable_item_list, PCSGTxConfigRic);
    if(!item) return;
    variable_item_set_item_label(
        item, app->tx_config.protocol == PagerTxProtocolFlex ? "Capcode:" : "RIC:");
    char text[20];
    snprintf(text, sizeof(text), "%lu", app->tx_config.ric);
    variable_item_set_current_value_text(item, text);
}

// Only FLEX carries BIWs, so POCSAG has nothing to choose here.
static void pocsag_pager_scene_tx_show_clock(POCSAGPagerApp* app) {
    VariableItem* item = variable_item_list_get(app->variable_item_list, PCSGTxConfigClock);
    if(!item) return;
    const bool flex = app->tx_config.protocol == PagerTxProtocolFlex;
    variable_item_set_values_count(item, flex ? PCSG_TX_CLOCK_COUNT : 1U);
    if(!flex) {
        variable_item_set_current_value_index(item, 0);
        variable_item_set_current_value_text(item, "N/A");
        return;
    }
    if(app->tx_config.clock_biws > PCSG_TX_CLOCK_BOTH) {
        app->tx_config.clock_biws = PCSG_TX_CLOCK_BOTH;
    }
    variable_item_set_current_value_index(item, app->tx_config.clock_biws);
    variable_item_set_current_value_text(item, tx_clock_text[app->tx_config.clock_biws]);
}

static void pocsag_pager_scene_tx_set_clock(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    if(app->tx_config.protocol != PagerTxProtocolFlex) {
        variable_item_set_current_value_index(item, 0);
        variable_item_set_current_value_text(item, "N/A");
        return;
    }
    app->tx_config.clock_biws = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, tx_clock_text[app->tx_config.clock_biws]);
}

/*
 * The message row doubles as a mode picker: left and right choose between the
 * typed message, SEQ, which sends a numbered test page and counts the number
 * up on every send, Beacon, which repeats sync and the clock BIWs with no
 * pages behind them, and Beacon + Seq, which is that beacon carrying the
 * numbered page once a minute. The last two need the FLEX clock BIWs, so
 * POCSAG stops where they start.
 */
static void pocsag_pager_scene_tx_show_message(POCSAGPagerApp* app) {
    VariableItem* item = variable_item_list_get(app->variable_item_list, PCSGTxConfigMessage);
    if(!item) return;
    const bool flex = app->tx_config.protocol == PagerTxProtocolFlex;
    if(!flex && pager_tx_mode_is_beacon(app->tx_config.message_mode)) {
        app->tx_config.message_mode = PagerTxMessageTyped;
    }

    variable_item_set_values_count(
        item, flex ? PagerTxMessageModeCount : PagerTxMessageBeacon);
    variable_item_set_current_value_index(item, app->tx_config.message_mode);

    char text[20];
    switch(app->tx_config.message_mode) {
    case PagerTxMessageBeacon:
        strlcpy(text, "Beacon", sizeof(text));
        break;
    case PagerTxMessageBeaconSeq:
        strlcpy(text, "Bcn+Seq", sizeof(text));
        break;
    case PagerTxMessageSequence:
        snprintf(text, sizeof(text), "SEQ %04u", app->tx_config.sequence_number);
        break;
    default: {
        const size_t length = strlen(app->tx_config.message);
        if(length == 0U) {
            strlcpy(text, "Tone only", sizeof(text));
        } else {
            snprintf(text, sizeof(text), "%u chars", (unsigned)length);
        }
        break;
    }
    }
    variable_item_set_current_value_text(item, text);
}

static void pocsag_pager_scene_tx_set_message(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    app->tx_config.message_mode = variable_item_get_current_value_index(item);
    pocsag_pager_scene_tx_show_message(app);
}

/*
 * Label, choice count and value of the address, function and baud rows all
 * depend on the protocol. on_enter adds those three as placeholders and calls
 * this, so the rules live here only, rather than once here and once there.
 */
static void pocsag_pager_scene_tx_show_protocol_options(POCSAGPagerApp* app) {
    const bool flex = app->tx_config.protocol == PagerTxProtocolFlex;
    pocsag_pager_scene_tx_show_address(app);

    VariableItem* function =
        variable_item_list_get(app->variable_item_list, PCSGTxConfigFunction);
    VariableItem* baud = variable_item_list_get(app->variable_item_list, PCSGTxConfigBaud);
    if(!function || !baud) return;
    variable_item_set_values_count(function, flex ? 1U : COUNT_OF(tx_function_text));
    variable_item_set_current_value_index(function, flex ? 0U : app->tx_config.function);
    variable_item_set_current_value_text(
        function, flex ? "N/A" : tx_function_text[app->tx_config.function]);

    variable_item_set_item_label(baud, flex ? "Mode:" : "Baud:");
    variable_item_set_values_count(
        baud, flex ? COUNT_OF(tx_flex_mode_text) : COUNT_OF(tx_baud_values));
    if(flex) {
        const uint8_t mode_index = app->tx_config.flex_3200 ? 1U : 0U;
        variable_item_set_current_value_index(baud, mode_index);
        variable_item_set_current_value_text(baud, tx_flex_mode_text[mode_index]);
    } else {
        const uint8_t baud_index = pocsag_pager_scene_tx_baud_index(app->tx_config.baud);
        variable_item_set_current_value_index(baud, baud_index);
        variable_item_set_current_value_text(baud, tx_baud_text[baud_index]);
    }
}

static void pocsag_pager_scene_tx_set_protocol(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    app->tx_config.protocol = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, tx_protocol_text[app->tx_config.protocol]);
    pocsag_pager_scene_tx_clamp_address(app);
    pocsag_pager_scene_tx_show_protocol_options(app);
    // The beacon modes and the clock BIWs are FLEX only, so leaving FLEX has
    // to drop them.
    pocsag_pager_scene_tx_show_message(app);
    pocsag_pager_scene_tx_show_clock(app);
}

static void pocsag_pager_scene_tx_set_function(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    if(app->tx_config.protocol == PagerTxProtocolFlex) {
        variable_item_set_current_value_index(item, 0);
        variable_item_set_current_value_text(item, "N/A");
        return;
    }
    app->tx_config.function = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, tx_function_text[app->tx_config.function]);
}

static void pocsag_pager_scene_tx_set_baud(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    if(app->tx_config.protocol == PagerTxProtocolFlex) {
        const uint8_t mode_index = variable_item_get_current_value_index(item);
        app->tx_config.flex_3200 = mode_index != 0U;
        variable_item_set_current_value_text(item, tx_flex_mode_text[mode_index]);
        return;
    }
    const uint8_t index = variable_item_get_current_value_index(item);
    app->tx_config.baud = tx_baud_values[index];
    variable_item_set_current_value_text(item, tx_baud_text[index]);
}

static void pocsag_pager_scene_tx_set_polarity(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    app->tx_config.inverted = variable_item_get_current_value_index(item) != 0;
    variable_item_set_current_value_text(
        item, tx_polarity_text[app->tx_config.inverted ? 1U : 0U]);
}

static void pocsag_pager_scene_tx_set_correction(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->filter.frequency_correction_hz =
        -PCSG_FREQ_CORRECTION_LIST_MAX_HZ + index * PCSG_FREQ_CORRECTION_STEP_HZ;
    pocsag_pager_scene_tx_show_correction(item, app->filter.frequency_correction_hz);
}

static void pocsag_pager_scene_tx_enter_callback(void* context, uint32_t index) {
    POCSAGPagerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void pocsag_pager_scene_tx_config_on_enter(void* context) {
    POCSAGPagerApp* app = context;
    VariableItem* item;

    pocsag_pager_scene_tx_clamp_address(app);

    item = variable_item_list_add(
        app->variable_item_list,
        "Protocol:",
        PagerTxProtocolCount,
        pocsag_pager_scene_tx_set_protocol,
        app);
    variable_item_set_current_value_index(item, app->tx_config.protocol);
    variable_item_set_current_value_text(item, tx_protocol_text[app->tx_config.protocol]);

    item = variable_item_list_add(
        app->variable_item_list,
        "Frequency:",
        subghz_setting_get_frequency_count(app->setting),
        pocsag_pager_scene_tx_set_frequency,
        app);
    variable_item_set_current_value_index(item, pocsag_pager_scene_tx_frequency_index(app));
    pocsag_pager_scene_tx_show_frequency(item, app->tx_config.frequency);

    // Placeholders: show_protocol_options() below sets the label, choice count
    // and value of all three according to the protocol.
    variable_item_list_add(app->variable_item_list, "RIC:", 1, NULL, app);
    variable_item_list_add(
        app->variable_item_list,
        "Function:",
        COUNT_OF(tx_function_text),
        pocsag_pager_scene_tx_set_function,
        app);
    variable_item_list_add(
        app->variable_item_list,
        "Baud:",
        COUNT_OF(tx_baud_values),
        pocsag_pager_scene_tx_set_baud,
        app);
    pocsag_pager_scene_tx_show_protocol_options(app);

    item = variable_item_list_add(
        app->variable_item_list,
        "Polarity:",
        COUNT_OF(tx_polarity_text),
        pocsag_pager_scene_tx_set_polarity,
        app);
    const uint8_t polarity_index = app->tx_config.inverted ? 1U : 0U;
    variable_item_set_current_value_index(item, polarity_index);
    variable_item_set_current_value_text(item, tx_polarity_text[polarity_index]);

    item = variable_item_list_add(
        app->variable_item_list,
        "Correction:",
        PCSG_FREQ_CORR_COUNT,
        pocsag_pager_scene_tx_set_correction,
        app);
    const uint8_t correction_index =
        pocsag_pager_scene_tx_correction_index(app->filter.frequency_correction_hz);
    variable_item_set_current_value_index(item, correction_index);
    // Show what is stored, not the index: a typed value need not be on the grid.
    pocsag_pager_scene_tx_show_correction(item, app->filter.frequency_correction_hz);

    variable_item_list_add(
        app->variable_item_list,
        "Clock:",
        PCSG_TX_CLOCK_COUNT,
        pocsag_pager_scene_tx_set_clock,
        app);
    pocsag_pager_scene_tx_show_clock(app);

    variable_item_list_add(
        app->variable_item_list, "Message:", 1, pocsag_pager_scene_tx_set_message, app);
    pocsag_pager_scene_tx_show_message(app);

    item = variable_item_list_add(app->variable_item_list, "Review & send", 1, NULL, app);
    variable_item_set_current_value_text(item, ">");

    variable_item_list_set_enter_callback(
        app->variable_item_list, pocsag_pager_scene_tx_enter_callback, app);
    variable_item_list_set_selected_item(
        app->variable_item_list, app->tx_config_selected_index);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewVariableItemList);
}

bool pocsag_pager_scene_tx_config_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == PCSGTxConfigFrequency) {
        scene_manager_set_scene_state(
            app->scene_manager, POCSAGPagerSceneCapcodeInput, PCSGNumberFieldTxFrequency);
        scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneCapcodeInput);
        return true;
    }
    if(event.event == PCSGTxConfigRic) {
        scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneTxRicInput);
        return true;
    }
    if(event.event == PCSGTxConfigCorrection) {
        scene_manager_set_scene_state(
            app->scene_manager, POCSAGPagerSceneCapcodeInput, PCSGNumberFieldFreqCorrection);
        scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneCapcodeInput);
        return true;
    }
    if(event.event == PCSGTxConfigMessage) {
        // Only the typed mode has anything to type: the rest build their own.
        if(app->tx_config.message_mode == PagerTxMessageTyped) {
            scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneTxMessageInput);
        }
        return true;
    }
    if(event.event == PCSGTxConfigReview) {
        // POCSAG cannot fragment, so it rejects what FLEX would have split.
        // "%04u" is as wide as the digits it prints, so the format is the body.
        const size_t length =
            app->tx_config.message_mode == PagerTxMessageTyped ?
                strlen(app->tx_config.message) :
            app->tx_config.message_mode == PagerTxMessageSequence ?
                strlen(PCSG_TX_SEQUENCE_FORMAT) :
                0U;
        if(app->tx_config.protocol == PagerTxProtocolPocsag &&
           length > PCSG_TX_POCSAG_MAX_MESSAGE_LENGTH) {
            notification_message(app->notifications, &sequence_error);
            return true;
        }
        // An empty message is allowed: it sends a tone-only page.
        scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneTxConfirm);
        return true;
    }
    return false;
}

void pocsag_pager_scene_tx_config_on_exit(void* context) {
    POCSAGPagerApp* app = context;
    app->tx_config_selected_index =
        variable_item_list_get_selected_item_index(app->variable_item_list);
    variable_item_list_reset(app->variable_item_list);
    pcsg_settings_save(app);
}
