#include "../pocsag_pager_app_i.h"

typedef enum {
    PCSGTxConfigProtocol,
    PCSGTxConfigFrequency,
    PCSGTxConfigRic,
    PCSGTxConfigFunction,
    PCSGTxConfigBaud,
    PCSGTxConfigPolarity,
    PCSGTxConfigCorrection,
    PCSGTxConfigParams,
    PCSGTxConfigMessage,
    PCSGTxConfigReview,
} PCSGTxConfigIndex;

static const uint16_t tx_baud_values[] = {512, 1200, 2400};
static const char* const tx_baud_text[] = {"512", "1200", "2400"};
static const char* const tx_protocol_text[] = {"POCSAG", "FLEX"};
static const char* const tx_function_text[] = {"0", "1", "2", "3"};
static const char* const tx_polarity_text[] = {"Standard", "Inverted"};
/*
 * Four-level is last and only offered when the firmware can shift the radio's
 * frequency offset mid-burst, so on firmware without that the row simply stops at two
 * choices rather than the app refusing to load.
 */
static const char* const tx_flex_mode_text[] = {
    "1600/2FSK", "3200/2FSK", "3200/4FSK", "6400/4FSK"};

static uint8_t pocsag_pager_scene_tx_flex_modes(void) {
    return flex_tx_four_level_supported() ? COUNT_OF(tx_flex_mode_text) : 2U;
}

static uint8_t pocsag_pager_scene_tx_flex_mode_index(const PocsagTxConfig* config) {
    if(config->flex_6400) return 3U;
    if(config->flex_4level) return 2U;
    return config->flex_3200 ? 1U : 0U;
}
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

// Clear unsupported destinations instead of silently sending to another address.
static void pocsag_pager_scene_tx_clamp_address(POCSAGPagerApp* app) {
    if(app->tx_config.protocol != PagerTxProtocolFlex) {
        if(app->tx_config.ric[0] > PCSG_TX_RIC_MAX) app->tx_config.ric[0] = 0U;
        return;
    }
    for(uint8_t i = 0; i < 4U; i++) {
        uint32_t* capcode = &app->tx_config.ric[i];
        // Zero means the phase sends nothing, so it is left alone.
        if(*capcode == 0U) continue;
        if(!flex_tx_capcode_valid(*capcode)) *capcode = 0U;
    }
}

/*
 * FLEX gets one capcode per phase and left and right step between them, the
 * row reading "416297/A". What the letter means is for the Phases row in FLEX
 * params to say.
 *
 * Under CAP it is a readout: the capcode carries its phase in its own address
 * bits and is filed under the one it names. Under Group it is the setting:
 * pick the phase, then enter the capcode that goes out on it. Group exists
 * because the embedding rule is an allocation convention rather than
 * something the air carries - which rule a subscriber was provisioned under
 * lives in its CAPCODE type letter, and that is never transmitted (TI SPRA193
 * section 2.9.3). Types U-X assign phases outright with no rule at all, the
 * form the spec recommends wherever group addresses are in use, and observed
 * traffic bears that out: capcodes turn up on phases CAP does not predict.
 *
 * An empty phase shows as "-/B" and sends nothing. Only the phases the
 * current mode carries can be stepped to, so 1600/2 offers A alone and
 * 6400/4 all four - under CAP a capcode naming a phase this mode does not
 * carry is kept but cannot go out, which is the rule working as written: a
 * single-phase pager on phase C is not reachable on a one-phase channel.
 *
 * POCSAG has no phases, so it keeps the single RIC it always had.
 */
static void pocsag_pager_scene_tx_show_address(POCSAGPagerApp* app) {
    VariableItem* item = variable_item_list_get(app->variable_item_list, PCSGTxConfigRic);
    if(!item) return;
    const bool flex = app->tx_config.protocol == PagerTxProtocolFlex;
    variable_item_set_item_label(item, flex ? "Capcode:" : "RIC:");

    char text[20];
    if(!flex) {
        variable_item_set_values_count(item, 1U);
        variable_item_set_current_value_index(item, 0);
        snprintf(text, sizeof(text), "%lu", app->tx_config.ric[0]);
        variable_item_set_current_value_text(item, text);
        return;
    }

    uint8_t order[4];
    const uint8_t phases = flex_tx_mode_phases(&app->tx_config, order);
    if(app->tx_ric_slot >= phases) app->tx_ric_slot = 0U;
    const uint8_t phase = order[app->tx_ric_slot];
    variable_item_set_values_count(item, phases);
    variable_item_set_current_value_index(item, app->tx_ric_slot);
    const uint32_t capcode = app->tx_config.ric[phase];
    if(capcode) {
        snprintf(text, sizeof(text), "%lu/%c", capcode, 'A' + phase);
    } else {
        snprintf(text, sizeof(text), "-/%c", 'A' + phase);
    }
    variable_item_set_current_value_text(item, text);
}

static void pocsag_pager_scene_tx_set_address(VariableItem* item) {
    POCSAGPagerApp* app = variable_item_get_context(item);
    app->tx_ric_slot = variable_item_get_current_value_index(item);
    pocsag_pager_scene_tx_show_address(app);
}

/*
 * The rows that can be taken away share a skeleton: find the row, and if it is
 * not available collapse it to the single value `disabled` names and report it
 * as handled. A NULL `disabled` means it is the caller's to fill, with `count`
 * values to choose from.
 */
static VariableItem* pocsag_pager_scene_tx_row(
    POCSAGPagerApp* app,
    uint8_t index,
    uint8_t count,
    const char* disabled) {
    VariableItem* item = variable_item_list_get(app->variable_item_list, index);
    if(!item) return NULL;
    if(!disabled) {
        variable_item_set_values_count(item, count);
        return item;
    }
    variable_item_set_values_count(item, 1U);
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, disabled);
    return NULL;
}

// The clock words and everything else FLEX-only live in their own scene, so
// this row is a door rather than a setting.
static void pocsag_pager_scene_tx_show_params(POCSAGPagerApp* app) {
    const bool flex = app->tx_config.protocol == PagerTxProtocolFlex;
    VariableItem* item =
        pocsag_pager_scene_tx_row(app, PCSGTxConfigParams, 1U, flex ? NULL : "N/A");
    if(!item) return;
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, ">");
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
    const bool flex = app->tx_config.protocol == PagerTxProtocolFlex;
    if(!flex && pager_tx_mode_is_beacon(app->tx_config.message_mode)) {
        app->tx_config.message_mode = PagerTxMessageTyped;
    }
    // Never taken away: POCSAG keeps the first two choices.
    VariableItem* item = pocsag_pager_scene_tx_row(
        app,
        PCSGTxConfigMessage,
        flex ? PagerTxMessageModeCount : PagerTxMessageBeacon,
        NULL);
    if(!item) return;
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
        baud, flex ? pocsag_pager_scene_tx_flex_modes() : COUNT_OF(tx_baud_values));
    if(flex) {
        uint8_t mode_index = pocsag_pager_scene_tx_flex_mode_index(&app->tx_config);
        // A saved four-level choice on firmware that cannot do it falls back.
        if(mode_index >= pocsag_pager_scene_tx_flex_modes()) {
            mode_index = 1U;
            app->tx_config.flex_3200 = true;
            app->tx_config.flex_4level = false;
            app->tx_config.flex_6400 = false;
        }
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
    // The beacon modes and the FLEX params are FLEX only, so leaving FLEX has
    // to drop them.
    pocsag_pager_scene_tx_show_message(app);
    pocsag_pager_scene_tx_show_params(app);
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
        uint8_t mode_index = variable_item_get_current_value_index(item);
        if(mode_index >= pocsag_pager_scene_tx_flex_modes()) mode_index = 1U;
        app->tx_config.flex_3200 = mode_index != 0U;
        app->tx_config.flex_4level = mode_index >= 2U;
        app->tx_config.flex_6400 = mode_index == 3U;
        variable_item_set_current_value_text(item, tx_flex_mode_text[mode_index]);
        // The mode decides which phases exist, so the capcode row's entries
        // change with it. Without this the row keeps the count it was given
        // when the scene opened and will not step to the new phases until the
        // scene is left and entered again.
        pocsag_pager_scene_tx_show_address(app);
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
    variable_item_list_add(
        app->variable_item_list, "RIC:", 1, pocsag_pager_scene_tx_set_address, app);
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

    variable_item_list_add(app->variable_item_list, "FLEX params:", 1, NULL, app);
    pocsag_pager_scene_tx_show_params(app);

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
#ifdef PCSG_TX_APP
    /*
     * This is the transmit app's first scene, so Back would drop the user at
     * the launcher. Hand back to the receiver instead, which is where they came
     * from. If it is not installed, fall through and exit as normal.
     */
    if(event.type == SceneManagerEventTypeBack && pcsg_app_handover(app, PCSG_APP_RX_PATH)) {
        return true;
    }
#endif
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
    if(event.event == PCSGTxConfigParams) {
        if(app->tx_config.protocol == PagerTxProtocolFlex) {
            scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneTxParams);
        }
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
