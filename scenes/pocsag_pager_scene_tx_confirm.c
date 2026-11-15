#include "../pocsag_pager_app_i.h"

/*
 * Beacon period, burst start to burst start. A FLEX network measured on air at
 * 929.4625 MHz keys one frame every second slot, so a frame of air is followed
 * by an equal quiet gap and the period is two 1.875 s slots. The gap is
 * whatever is left of the period rather than a constant of its own, so a burst
 * that ran three frames has already used the slot after it and pushes the next
 * burst out to four slots.
 */
static uint32_t pocsag_pager_scene_tx_beacon_period_ms(POCSAGPagerApp* app) {
    return (uint32_t)(app->tx_burst_frames + 1U) * FLEX_TX_FRAME_MS;
}

// The burst in flight decides when the next one is due, so its length is read
// while its encoder is still alive: completion frees it.
static uint8_t pocsag_pager_scene_tx_burst_frames(POCSAGPagerApp* app) {
    return app->flex_tx_encoder ? flex_tx_encoder_frames(app->flex_tx_encoder) : 1U;
}

static void pocsag_pager_scene_tx_sequence_advance(POCSAGPagerApp* app) {
    if(++app->tx_config.sequence_number > PCSG_TX_SEQUENCE_MAX) {
        app->tx_config.sequence_number = 1U;
    }
}

static void pocsag_pager_scene_tx_confirm_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    POCSAGPagerApp* app = context;
    if(result == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, PCSGCustomEventTxSend);
    }
}

// A finished page keeps a button, so sending again costs one press instead of
// a trip back through the config and review screens. The states that cannot
// succeed on a retry pass NULL and leave Back as the only way on.
static void pocsag_pager_scene_tx_confirm_show_result(
    POCSAGPagerApp* app,
    const char* text,
    const char* button) {
    widget_reset(app->widget);
    widget_add_text_box_element(
        app->widget, 4, 18, 120, 32, AlignCenter, AlignCenter, text, false);
    if(button) {
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, button, pocsag_pager_scene_tx_confirm_callback, app);
    }
}

void pocsag_pager_scene_tx_confirm_on_enter(void* context) {
    POCSAGPagerApp* app = context;
    app->tx_pause_pending = false;
    char correction[16];
    if(app->filter.frequency_correction_hz == 0) {
        strlcpy(correction, "Off", sizeof(correction));
    } else {
        const int32_t correction_hz = app->filter.frequency_correction_hz;
        const uint32_t magnitude =
            correction_hz < 0 ? (uint32_t)-correction_hz : (uint32_t)correction_hz;
        snprintf(
            correction,
            sizeof(correction),
            "%c%lu Hz",
            correction_hz < 0 ? '-' : '+',
            (unsigned long)magnitude);
    }
    // An empty message sends a tone-only page, so say so rather than show a gap.
    char sequence[PCSG_TX_SEQUENCE_CHARS];
    const char* body;
    if(app->tx_config.message_mode == PagerTxMessageBeacon) {
        body = "\e#Beacon\e#\nSync and clock BIWs, no pages,\nrepeating";
    } else if(app->tx_config.message_mode == PagerTxMessageBeaconSeq) {
        body = "\e#Beacon + Seq\e#\nSync and clock BIWs, repeating,\n"
               "carrying the sequential page\nonce a minute";
    } else if(app->tx_config.message_mode == PagerTxMessageSequence) {
        snprintf(
            sequence, sizeof(sequence), PCSG_TX_SEQUENCE_FORMAT, app->tx_config.sequence_number);
        body = sequence;
    } else {
        body = app->tx_config.message[0] == '\0' ? "\e#Tone only\e#" : app->tx_config.message;
    }
    FuriString* text;
    if(app->tx_config.protocol == PagerTxProtocolFlex) {
        text = furi_string_alloc_printf(
            "\e#Transmit FLEX?\e#\n"
            "%lu.%04lu MHz | %s\n"
            "Capcode %lu | Clock %s\n"
            "%s polarity | Correction %s\n\n"
            "%s\n\n"
            "Only transmit where authorized.",
            app->tx_config.frequency / 1000000U,
            (app->tx_config.frequency % 1000000U) / 100U,
            app->tx_config.flex_3200 ? "3200/2FSK" : "1600/2FSK",
            app->tx_config.ric,
            app->tx_config.clock_biws == PCSG_TX_CLOCK_BOTH        ? "time+date" :
                app->tx_config.clock_biws == PCSG_TX_CLOCK_TIME    ? "time" :
                app->tx_config.clock_biws == PCSG_TX_CLOCK_DATE    ? "date" :
                                                                     "off",
            app->tx_config.inverted ? "Inverted" : "Standard",
            correction,
            body);
    } else {
        text = furi_string_alloc_printf(
            "\e#Transmit POCSAG?\e#\n"
            "%lu.%04lu MHz | %u baud\n"
            "RIC %lu | Function %u\n"
            "%s polarity | Correction %s\n\n"
            "%s\n\n"
            "Only transmit where authorized.",
            app->tx_config.frequency / 1000000U,
            (app->tx_config.frequency % 1000000U) / 100U,
            app->tx_config.baud,
            app->tx_config.ric,
            app->tx_config.function,
            app->tx_config.inverted ? "Inverted" : "Standard",
            correction,
            body);
    }
    widget_add_text_scroll_element(app->widget, 2, 0, 124, 52, furi_string_get_cstr(text));
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        "Send",
        pocsag_pager_scene_tx_confirm_callback,
        app);
    furi_string_free(text);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewWidget);
}

bool pocsag_pager_scene_tx_confirm_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == PCSGCustomEventTxSend) {
        const PCSGTxStartStatus status = pcsg_tx_start(app);
        if(status == PCSGTxStartOk) {
            app->tx_burst_tick = furi_get_tick();
            app->tx_burst_frames = pocsag_pager_scene_tx_burst_frames(app);
            pocsag_pager_scene_tx_confirm_show_result(
                app,
                app->tx_config.message_mode == PagerTxMessageBeaconSeq ?
                    "\e#Beacon + Seq\e#\n\nPage once a minute\nBack stops" :
                app->tx_config.message_mode == PagerTxMessageBeacon ?
                    "\e#Beacon\e#\n\nSync and clock BIWs\nBack stops" :
                    "\e#Transmitting...\e#\n\nBack cancels",
                NULL);
            notification_message(app->notifications, &sequence_blink_magenta_10);
        } else if(status == PCSGTxStartRestricted) {
            pocsag_pager_scene_tx_confirm_show_result(
                app,
                "\e#Transmission blocked\e#\n\nFrequency is restricted\nby region settings",
                NULL);
            notification_message(app->notifications, &sequence_error);
        } else {
            pocsag_pager_scene_tx_confirm_show_result(
                app, "\e#Cannot transmit\e#\n\nCheck address, message,\nfrequency and mode", NULL);
            notification_message(app->notifications, &sequence_error);
        }
        return true;
    }
    if(event.type == SceneManagerEventTypeTick && app->tx_pause_pending) {
        const uint32_t period =
            furi_ms_to_ticks(pocsag_pager_scene_tx_beacon_period_ms(app));
        if(furi_get_tick() - app->tx_burst_tick < period) return true;
        // Advance the schedule by one period rather than restarting it from
        // now, so the 100 ms tick this is checked on cannot accumulate into a
        // drifting cadence. A burst that overran its slot resyncs instead.
        app->tx_burst_tick += period;
        if(furi_get_tick() - app->tx_burst_tick >= period) {
            app->tx_burst_tick = furi_get_tick();
        }
        app->tx_pause_pending = false;
        if(pcsg_tx_start(app) != PCSGTxStartOk) {
            pocsag_pager_scene_tx_confirm_show_result(
                app, "\e#Cannot transmit\e#\n\nCheck address, message,\nfrequency and mode", NULL);
            notification_message(app->notifications, &sequence_error);
            return true;
        }
        app->tx_burst_frames = pocsag_pager_scene_tx_burst_frames(app);
        return true;
    }
    if(event.type == SceneManagerEventTypeTick && pcsg_tx_complete(app)) {
        pcsg_tx_stop(app);
        /*
         * The beacon runs until Back, a burst every PCSG_TX_BEACON_PAUSE_MS.
         * Each one is built afresh, so the gap does not leave its cycle and
         * frame words describing a slot that has already passed. The screen is
         * left alone so it does not flicker once a burst.
         */
        if(pager_tx_mode_is_beacon(app->tx_config.message_mode)) {
            // A beacon burst longer than one frame is the periodic page, so
            // the number moves on only once a page has actually gone out.
            if(app->tx_burst_frames > 1U) pocsag_pager_scene_tx_sequence_advance(app);
            app->tx_pause_pending = true;
            return true;
        }
        /*
         * Sequence counts up once per page sent, not once per press, so a send
         * that failed or was cancelled does not skip a number. The screen shows
         * the number that just went out and the button the one that follows.
         */
        if(app->tx_config.message_mode == PagerTxMessageSequence) {
            char sent[48];
            char next[16];
            snprintf(
                sent,
                sizeof(sent),
                "\e#Page sent\e#\n\nSequential number %04u",
                app->tx_config.sequence_number);
            pocsag_pager_scene_tx_sequence_advance(app);
            snprintf(next, sizeof(next), "Send %04u", app->tx_config.sequence_number);
            pocsag_pager_scene_tx_confirm_show_result(app, sent, next);
            notification_message(app->notifications, &sequence_success);
            return true;
        }
        pocsag_pager_scene_tx_confirm_show_result(
            app,
            app->tx_config.protocol == PagerTxProtocolFlex ?
                "\e#FLEX page sent\e#" :
                "\e#POCSAG page sent\e#",
            "Send again");
        notification_message(app->notifications, &sequence_success);
        return true;
    }
    return false;
}

void pocsag_pager_scene_tx_confirm_on_exit(void* context) {
    POCSAGPagerApp* app = context;
    // Back during the gap has to stop the beacon as surely as Back mid-burst.
    app->tx_pause_pending = false;
    if(app->txrx->txrx_state == PCSGTxRxStateTx) pcsg_tx_stop(app);
    notification_message(app->notifications, &sequence_blink_stop);
    widget_reset(app->widget);
}
