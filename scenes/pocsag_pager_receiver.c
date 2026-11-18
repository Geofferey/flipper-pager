#include "../pocsag_pager_app_i.h"
#include "../views/pocsag_pager_receiver.h"
#include "../protocols/flex.h"
#include "../pocsag_pager_led.h"

#define TAG "POCSAGPager"

static const NotificationSequence subghs_sequence_rx = {
    &message_vibro_on,
    &message_note_c6,
    &message_delay_50,
    &message_sound_off,
    &message_vibro_off,

    &message_delay_50,
    NULL,
};

static const NotificationSequence pcsg_sequence_rx_sound = {
    &message_note_c6,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    NULL,
};

static const NotificationSequence pcsg_sequence_rx_vibro = {
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    &message_delay_50,
    NULL,
};

// Message alerts use sound/vibration only; reception never wakes the backlight.
static const NotificationSequence* pocsag_pager_scene_receiver_alert(POCSAGPagerApp* app) {
    switch(app->filter.alert) {
    case PCSGAlertSound:
        return &pcsg_sequence_rx_sound;
    case PCSGAlertVibro:
        return &pcsg_sequence_rx_vibro;
    case PCSGAlertOff:
        return NULL;
    default:
        return &subghs_sequence_rx;
    }
}

static void pocsag_pager_scene_receiver_update_statusbar(void* context) {
    POCSAGPagerApp* app = context;
    char frequency[12];
    char history[12];
    snprintf(
        frequency,
        sizeof(frequency),
        "%03lu.%02lu",
        app->txrx->preset->frequency / 1000000UL % 1000UL,
        app->txrx->preset->frequency / 10000UL % 100UL);
    snprintf(history, sizeof(history), "%lu", pcsg_history_get_total(app->txrx->history));

    const char* preset_name = furi_string_get_cstr(app->txrx->preset->name);
    const char* modulation;
    if(pcsg_preset_is_flex4(preset_name)) {
        modulation = "F4";
    } else if(!strncmp(preset_name, "FLEX", 4)) {
        modulation = "FX";
    } else if(!strcmp(preset_name, "POCSAG-W")) {
        modulation = "PW";
    } else {
        modulation = preset_name;
    }
    char modulation_short[3] = {
        modulation[0], modulation[0] && modulation[1] ? modulation[1] : '\0', '\0'};
    pcsg_view_receiver_add_data_statusbar(
        app->pcsg_receiver, frequency, modulation_short, history);
}

/*
 * The status bar alternates between the frequency line and the network's own
 * clock, ten seconds each, starting on the frequency. The phase comes from the
 * tick the scene opened on, so it is the same whichever way the scene is
 * entered. With no clock words yet there is nothing to show, and the view
 * leaves the frequency line up.
 */
#define PCSG_RX_BAR_PERIOD_MS 10000U
// How long the last reading stays up once the network clock stops arriving.
#define PCSG_RX_CLOCK_HOLD_MS 60000U

static void pocsag_pager_scene_receiver_update_clock(POCSAGPagerApp* app) {
    // The HUD settings decide whether the line rotates at all, and how long it
    // holds each item for. Off and Freq never reach the clock.
    if(app->filter.hud_ticker == PCSGHudTickerOff ||
       app->filter.hud_ticker == PCSGHudTickerFrequency) {
        pcsg_view_receiver_set_clock(app->pcsg_receiver, NULL, false);
        return;
    }
    uint8_t secs = app->filter.hud_ticker_secs;
    if(secs < PCSG_HUD_TICKER_SECS_MIN || secs > PCSG_HUD_TICKER_SECS_MAX) {
        secs = PCSG_HUD_TICKER_SECS_DEFAULT;
    }
    const uint32_t period = furi_ms_to_ticks((uint32_t)secs * 1000U);
    // Clock means hold it there; Rotate alternates with the frequency line.
    const bool show = app->filter.hud_ticker == PCSGHudTickerClock ||
                      ((furi_get_tick() - app->rx_bar_tick) / period) % 2U != 0U;
    if(!show) {
        // The frequency line has the row: nothing to read, nothing to format.
        pcsg_view_receiver_set_clock(app->pcsg_receiver, NULL, false);
        return;
    }

    PCSGNetworkTime now;
    if(pcsg_time_network(app, &now)) {
        char text[24];
        size_t at = 0;
        if(now.have_zone && app->filter.hud_show_zone) {
            pcsg_zone_format(now.zone_minutes, text, sizeof(text));
            at = strlen(text);
            at += snprintf(text + at, sizeof(text) - at, " ");
        }
        // The hour alone waits on a clock word; minutes and seconds do not.
        if(now.have_hour) {
            at += snprintf(text + at, sizeof(text) - at, "%02u", now.hour);
        } else {
            at += snprintf(text + at, sizeof(text) - at, "--");
        }
        snprintf(text + at, sizeof(text) - at, ":%02u:%02u", now.minute, now.second);
        app->rx_clock_tick = furi_get_tick();
        pcsg_view_receiver_set_clock(app->pcsg_receiver, text, show);
        return;
    }

    /*
     * The reading itself no longer goes away on its own: a frame recovered by
     * Weak Decode is ignored rather than allowed to clear the clock, and the
     * clock runs on from the last trusted frame however long ago that was.
     * What is left here is the case with no reading at all behind it - RX
     * stopped, or not on FLEX - where the last one stays up until the hold
     * expires. NULL leaves the text alone and only moves the phase.
     */
    const bool expired = furi_get_tick() - app->rx_clock_tick >
                         furi_ms_to_ticks(PCSG_RX_CLOCK_HOLD_MS);
    pcsg_view_receiver_set_clock(app->pcsg_receiver, expired ? "" : NULL, show);
}

void pocsag_pager_scene_receiver_callback(PCSGCustomEvent event, void* context) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void pocsag_pager_scene_receiver_add_to_history_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    // Both decoders report each message from inside a transmission (FLEX per
    // page, POCSAG per address) and reset themselves when it ends, so the
    // receiver must not be reset here.
    UNUSED(receiver);
    const bool display = pcsg_filter_accepts(&app->filter, decoder_base);

    const PCSGHistoryStateAddKey state =
        pcsg_history_add_to_history(
            app->txrx->history, decoder_base, app->txrx->preset, display);
    const uint16_t evicted = pcsg_history_last_evicted(app->txrx->history);
    if(state == PCSGHistoryStateAddKeyNewDada || state == PCSGHistoryStateAddKeyOverflow ||
       evicted) {
        pcsg_view_receiver_history_changed(
            app->pcsg_receiver, pcsg_history_get_item(app->txrx->history), evicted);
        app->txrx->idx_menu_chosen = app->txrx->idx_menu_chosen > evicted ?
                                         app->txrx->idx_menu_chosen - evicted :
                                         0;

        pocsag_pager_scene_receiver_update_statusbar(app);
    }
    if(state == PCSGHistoryStateAddKeyNewDada) {
        if(app->filter.decode_led) {
            notification_message(
                app->notifications, pcsg_led_blink_sequence(app->filter.decode_led_color));
        }
        /*
         * An address filter in one of its alert modes narrows what alerts
         * without narrowing what arrives, so this is asked separately from
         * the filters above. The decode LED is not gated by it: that reports
         * decoding, not whether the message was one being waited for.
         */
        if(pcsg_filter_alerts(&app->filter, decoder_base)) {
            if(!pcsg_classic_alert_request(app)) {
                const NotificationSequence* alert = pocsag_pager_scene_receiver_alert(app);
                if(alert) notification_message(app->notifications, alert);
            }
        }
        app->txrx->rx_key_state = PCSGRxKeyStateAddKey;
    }
}

static void pocsag_pager_scene_receiver_item_callback(
    void* context,
    uint16_t idx,
    FuriString* label,
    uint8_t* type,
    uint8_t* split) {
    POCSAGPagerApp* app = context;
    pcsg_history_get_text_item_menu(
        app->txrx->history, label, idx, app->filter.preview != PCSGPreviewHide, split);
    *type = pcsg_history_get_type_protocol(app->txrx->history, idx);
}

void pocsag_pager_scene_receiver_on_enter(void* context) {
    POCSAGPagerApp* app = context;

    // A new session keeps the frequency and preset chosen in Config (and
    // restored from settings at startup).
    if(app->txrx->rx_key_state == PCSGRxKeyStateIDLE) {
        pcsg_history_reset(app->txrx->history);
        pcsg_history_set_temp_mode(app->txrx->history, app->filter.log_temp);
        app->txrx->rx_key_state = PCSGRxKeyStateStart;
    }

    pcsg_view_receiver_set_lock(app->pcsg_receiver, app->lock);
    pcsg_view_receiver_set_ext_module_state(
        app->pcsg_receiver, radio_device_loader_is_external(app->txrx->radio_device));

    //Load history to receiver
    pcsg_view_receiver_exit(app->pcsg_receiver);
    pcsg_view_receiver_set_review_mode(app->pcsg_receiver, false);
    pcsg_view_receiver_set_item_callback(
        app->pcsg_receiver, pocsag_pager_scene_receiver_item_callback, app);
    pcsg_view_receiver_set_item_count(app->pcsg_receiver, pcsg_history_get_item(app->txrx->history));
    if(pcsg_history_get_item(app->txrx->history)) app->txrx->rx_key_state = PCSGRxKeyStateAddKey;
    pocsag_pager_scene_receiver_update_statusbar(app);
    // Armed once, not on every entry: re-arming it restarts the cycle on the
    // frequency line, so stepping in and out of messages inside ten seconds
    // meant the clock half never came round at all.
    if(app->rx_bar_tick == 0U) app->rx_bar_tick = furi_get_tick();
    pocsag_pager_scene_receiver_update_clock(app);

    pcsg_view_receiver_set_callback(app->pcsg_receiver, pocsag_pager_scene_receiver_callback, app);
    // Re-read on the way back from settings, which is where it changes.
    pcsg_view_receiver_set_scroll(
        app->pcsg_receiver, app->filter.preview == PCSGPreviewScroll);
    subghz_receiver_set_rx_callback(
        app->txrx->receiver, pocsag_pager_scene_receiver_add_to_history_callback, app);

    // Returning from a message or an unchanged Config keeps the radio running,
    // so a frame (or the rest of a split message) is not lost to a restart.
    if(app->txrx->txrx_state == PCSGTxRxStateRx && app->txrx->rx_restart) {
        pcsg_rx_end(app);
    };
    app->txrx->rx_restart = false;
    if((app->txrx->txrx_state == PCSGTxRxStateIDLE) ||
       (app->txrx->txrx_state == PCSGTxRxStateSleep)) {
        // Start RX
        pcsg_begin(
            app,
            subghz_setting_get_preset_data_by_name(
                app->setting, furi_string_get_cstr(app->txrx->preset->name)));

        pcsg_rx(app, app->txrx->preset->frequency);
    }

    pcsg_view_receiver_set_idx_menu(app->pcsg_receiver, app->txrx->idx_menu_chosen);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewReceiver);
}

bool pocsag_pager_scene_receiver_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case PCSGCustomEventViewReceiverBack: {
            const size_t heap_before = memmgr_get_free_heap();
            // Stop CC1101 Rx
            if(app->txrx->txrx_state == PCSGTxRxStateRx) {
                pcsg_rx_end(app);
                pcsg_idle(app);
            };
            // Frequency and preset stay as chosen for the next session.
            app->txrx->idx_menu_chosen = 0;
            subghz_receiver_set_rx_callback(app->txrx->receiver, NULL, app);

            app->txrx->rx_key_state = PCSGRxKeyStateIDLE;
            // Close the session log and release its index; it is on SD now.
            pcsg_history_reset(app->txrx->history);
            // The decoders outlive a session, so what receiving allocated in
            // them would otherwise stay resident for Review or a transmission.
            // Ordered after the callback is cleared and RX has stopped, so
            // nothing can be decoding into the buffers being released.
            subghz_receiver_reset(app->txrx->receiver);
            flex_sampler_release_session(app->txrx->flex_sampler);
            FURI_LOG_I(
                TAG,
                "RX exit: heap %u -> %u",
                (unsigned)heap_before,
                (unsigned)memmgr_get_free_heap());
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, POCSAGPagerSceneStart);
            consumed = true;
            break;
        }
        case PCSGCustomEventViewReceiverOK:
            app->txrx->idx_menu_chosen = pcsg_view_receiver_get_idx_menu(app->pcsg_receiver);
            scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneReceiverInfo);
            consumed = true;
            break;
        case PCSGCustomEventViewReceiverFilterAddress: {
            const uint16_t index = pcsg_view_receiver_get_idx_menu(app->pcsg_receiver);
            PCSGHistoryAddressType type;
            uint32_t address;
            /*
             * Alert rather than Exact: holding an address used to hide every
             * other one, which is rarely what is wanted from a list of what
             * is on the air. This asks to be told when that address is paged
             * and leaves the rest arriving as before.
             */
            if(pcsg_history_get_address(app->txrx->history, index, &type, &address)) {
                if(type == PCSGHistoryAddressFlexCap && address <= PCSG_CAPCODE_MAX) {
                    app->filter.capcode_filter = PCSGCapcodeFilterAlert;
                    app->filter.capcode_min = address;
                    app->filter.capcode_max = address;
                    pcsg_view_receiver_show_address_filter(app->pcsg_receiver, true, address);
                    pcsg_settings_save(app);
                } else if(type == PCSGHistoryAddressPocsagRic && address <= PCSG_RIC_MAX) {
                    app->filter.ric_filter = PCSGCapcodeFilterAlert;
                    app->filter.ric_min = address;
                    app->filter.ric_max = address;
                    pcsg_view_receiver_show_address_filter(app->pcsg_receiver, false, address);
                    pcsg_settings_save(app);
                }
            }
            consumed = true;
            break;
        }
        case PCSGCustomEventViewReceiverConfig:
            app->config_standalone = false;
            app->txrx->idx_menu_chosen = pcsg_view_receiver_get_idx_menu(app->pcsg_receiver);
            scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneReceiverConfig);
            consumed = true;
            break;
        case PCSGCustomEventViewReceiverNetwork:
            scene_manager_next_scene(app->scene_manager, POCSAGPagerSceneNetworkInfo);
            consumed = true;
            break;
        case PCSGCustomEventViewReceiverOffDisplay:
            notification_message(app->notifications, &sequence_display_backlight_off);
            consumed = true;
            break;
        case PCSGCustomEventViewReceiverUnlock:
            app->lock = PCSGLockOff;
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        pcsg_time_sync(app);
        pocsag_pager_scene_receiver_update_clock(app);
        pcsg_view_receiver_scroll_step(app->pcsg_receiver);
        // Reading RSSI needs SPI, and FLEX4 needs uninterrupted access to both
        // hard-decision GPIOs, one of which shares the SPI MISO pin.
        if(!pcsg_preset_is_flex4(furi_string_get_cstr(app->txrx->preset->name))) {
            pcsg_receiver_rssi(
                app->pcsg_receiver, subghz_devices_get_rssi(app->txrx->radio_device));
        }
        // The receive blink is driven from the app tick, so it keeps going
        // while another scene is open.
    }
    return consumed;
}

void pocsag_pager_scene_receiver_on_exit(void* context) {
    UNUSED(context);
}
