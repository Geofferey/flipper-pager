#include "../pocsag_pager_app_i.h"

/** Quiet time between test beacon bursts. */
#define PCSG_TX_BEACON_PAUSE_MS 2000U

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
    const char* body = app->tx_config.test_beacon ?
                           "\e#TEST beacon\e#\nSync and clock BIWs, no pages,\nrepeating" :
                       app->tx_config.message[0] == '\0' ? "\e#Tone only\e#" :
                                                           app->tx_config.message;
    FuriString* text;
    if(app->tx_config.protocol == PagerTxProtocolFlex) {
        text = furi_string_alloc_printf(
            "\e#Transmit FLEX?\e#\n"
            "%lu.%04lu MHz | %s\n"
            "Capcode %lu\n"
            "%s polarity | Correction %s\n\n"
            "%s\n\n"
            "Only transmit where authorized.",
            app->tx_config.frequency / 1000000U,
            (app->tx_config.frequency % 1000000U) / 100U,
            app->tx_config.flex_3200 ? "3200/2FSK" : "1600/2FSK",
            app->tx_config.ric,
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
            pocsag_pager_scene_tx_confirm_show_result(
                app,
                app->tx_config.test_beacon ?
                    "\e#TEST beacon\e#\n\nSync and clock BIWs\nBack stops" :
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
        if(furi_get_tick() - app->tx_pause_tick <
           furi_ms_to_ticks(PCSG_TX_BEACON_PAUSE_MS)) {
            return true;
        }
        app->tx_pause_pending = false;
        if(pcsg_tx_start(app) != PCSGTxStartOk) {
            pocsag_pager_scene_tx_confirm_show_result(
                app, "\e#Cannot transmit\e#\n\nCheck address, message,\nfrequency and mode", NULL);
            notification_message(app->notifications, &sequence_error);
        }
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
        if(app->tx_config.test_beacon) {
            app->tx_pause_tick = furi_get_tick();
            app->tx_pause_pending = true;
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
