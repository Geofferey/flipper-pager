#include "../pocsag_pager_app_i.h"

/*
 * What the network has said about itself, reached with right from the live
 * list. The clock is the centre of it because that is the one field a network
 * sends constantly; the identity words arrive in a frame here and there, so
 * the rest fills in around it as they turn up and reads "-" until then.
 *
 * Rebuilt on the tick rather than drawn by a view of its own: the clock has to
 * run, and the widget already places text where this wants it.
 */
#define PCSG_NETWORK_TICK_MS 500U

// Centre of the screen, and the rows above and below the clock.
#define PCSG_NETWORK_MID_X   64
#define PCSG_NETWORK_TOP_Y   9
#define PCSG_NETWORK_CLOCK_Y 34
#define PCSG_NETWORK_DATE_Y  45
#define PCSG_NETWORK_ROW1_Y  55
#define PCSG_NETWORK_ROW2_Y  64
// Inset from either edge for the fields that flank the centre.
#define PCSG_NETWORK_LEFT_X  2
#define PCSG_NETWORK_RIGHT_X 126

static void pocsag_pager_scene_network_info_add(
    POCSAGPagerApp* app,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Font font,
    const char* text) {
    widget_add_string_element(app->widget, x, y, horizontal, AlignBottom, font, text);
}

/*
 * The five bit year field is an absolute index into 1994-2025, not modulo 32
 * arithmetic rolling on into a later era: nothing on the air names an era, so
 * a receiver cannot resolve one. A transmitter with a later year sends that
 * year's calendar twin instead, which is what this app's transmitter does.
 *
 * This did resolve an era, against the Flipper's own clock, which turned a
 * network sending 2007 into 2039. Networks really do send years that far back.
 * The date is its own word, nothing to do with the time of day: a network can
 * carry an accurate time and a date years out, and one does.
 */
static uint16_t pocsag_pager_scene_network_info_year(uint8_t field) {
    return 1994U + (field & 31U);
}

static void pocsag_pager_scene_network_info_draw(POCSAGPagerApp* app) {
    widget_reset(app->widget);

    // Each field says for itself whether it has been heard, so whether any of
    // them has is not asked here: the zeroed struct reads as nothing heard.
    FlexSystemInfo info = {0};
    if(app->txrx->flex_sampler) flex_sampler_get_system_info(app->txrx->flex_sampler, &info);

    PCSGNetworkTime now;
    const bool have_clock = pcsg_time_network(app, &now);

    char text[24];

    // Zone on the left of the top row, the offset the clock is actually on.
    if(app->filter.hud_show_zone) {
        if(have_clock && now.have_zone) {
            pcsg_zone_format(now.zone_minutes, text, sizeof(text));
        } else {
            strlcpy(text, "No zone", sizeof(text));
        }
        pocsag_pager_scene_network_info_add(
            app, PCSG_NETWORK_LEFT_X, PCSG_NETWORK_TOP_Y, AlignLeft, FontSecondary, text);
    }

    // Roaming on the right of it: a word about the network, not about the time.
    if(info.have_roaming && app->filter.hud_show_roaming) {
        pocsag_pager_scene_network_info_add(
            app,
            PCSG_NETWORK_RIGHT_X,
            PCSG_NETWORK_TOP_Y,
            AlignRight,
            FontSecondary,
            info.roaming ? "ROAM" : "HOME");
    }

    // The centre piece. Big numbers carry digits and the colon, nothing else,
    // so the no-clock case has to be said in the smaller font.
    if(have_clock) {
        // Minutes and seconds come from the frame grid, so they run from the
        // first frame onwards; the hour stays "--" until a clock word arrives.
        if(now.have_hour) {
            snprintf(text, sizeof(text), "%02u:%02u:%02u", now.hour, now.minute, now.second);
        } else {
            snprintf(text, sizeof(text), "--:%02u:%02u", now.minute, now.second);
        }
        pocsag_pager_scene_network_info_add(
            app, PCSG_NETWORK_MID_X, PCSG_NETWORK_CLOCK_Y, AlignCenter, FontBigNumbers, text);
    } else {
        pocsag_pager_scene_network_info_add(
            app,
            PCSG_NETWORK_MID_X,
            PCSG_NETWORK_CLOCK_Y - 8,
            AlignCenter,
            FontPrimary,
            "Waiting for");
        pocsag_pager_scene_network_info_add(
            app,
            PCSG_NETWORK_MID_X,
            PCSG_NETWORK_CLOCK_Y + 2,
            AlignCenter,
            FontPrimary,
            "the network");
    }

    /*
     * Date: the network's, or the Flipper's. Two observed networks send one
     * that is not today's - a stale 2007 on one, today's day and month with
     * the year field wrapped to 1994 on another - so Local is a real choice
     * rather than a fallback.
     */
    if(app->filter.hud_show_date) {
        DateTime local;
        furi_hal_rtc_get_datetime(&local);

        // Year and day/month are chosen apart, so they are built apart: the
        // networks seen so far get one right and the other wrong.
        char year_text[8];
        if(app->filter.hud_year == PCSGHudSourceLocal) {
            snprintf(year_text, sizeof(year_text), "%u", local.year);
        } else if(info.have_date) {
            snprintf(
                year_text,
                sizeof(year_text),
                "%u",
                pocsag_pager_scene_network_info_year(info.year_field));
        } else {
            strlcpy(year_text, "????", sizeof(year_text));
        }

        char day_text[8];
        if(app->filter.hud_date == PCSGHudSourceLocal) {
            snprintf(day_text, sizeof(day_text), "%02u-%02u", local.month, local.day);
        } else if(info.have_date) {
            snprintf(day_text, sizeof(day_text), "%02u-%02u", info.month, info.day);
        } else {
            // Escaped: "??-" would otherwise be read as the trigraph for "~".
            strlcpy(day_text, "\?\?-\?\?", sizeof(day_text));
        }

        if(year_text[0] == '?' && day_text[0] == '?') {
            strlcpy(text, "no date", sizeof(text));
        } else {
            snprintf(text, sizeof(text), "%s-%s", year_text, day_text);
        }
        pocsag_pager_scene_network_info_add(
            app, PCSG_NETWORK_MID_X, PCSG_NETWORK_DATE_Y, AlignCenter, FontSecondary, text);
    }

    // Who is transmitting: SSID1 on the left, SSID2 on the right.
    if(app->filter.hud_show_ssid1) {
        if(info.have_ssid1) {
            snprintf(text, sizeof(text), "LID %u/%u", info.local_id, info.coverage_zone);
        } else {
            strlcpy(text, "LID -", sizeof(text));
        }
        pocsag_pager_scene_network_info_add(
            app, PCSG_NETWORK_LEFT_X, PCSG_NETWORK_ROW1_Y, AlignLeft, FontSecondary, text);
    }

    if(app->filter.hud_show_ssid2) {
        if(info.have_ssid2) {
            snprintf(text, sizeof(text), "CC %u", info.country_code);
        } else {
            strlcpy(text, "CC -", sizeof(text));
        }
        pocsag_pager_scene_network_info_add(
            app, PCSG_NETWORK_RIGHT_X, PCSG_NETWORK_ROW1_Y, AlignRight, FontSecondary, text);
    }

    // Where the frame grid is, and the seconds trim riding with the zone.
    if(app->filter.hud_show_frame) {
        FlexFrameTiming timing;
        if(app->txrx->flex_sampler &&
           flex_sampler_get_frame_timing(app->txrx->flex_sampler, &timing)) {
            snprintf(text, sizeof(text), "F%u/C%u", timing.frame, timing.cycle);
        } else {
            strlcpy(text, "F-/C-", sizeof(text));
        }
        pocsag_pager_scene_network_info_add(
            app, PCSG_NETWORK_LEFT_X, PCSG_NETWORK_ROW2_Y, AlignLeft, FontSecondary, text);
    }

    /*
     * Messages held this session, between the frame and the trim. The same
     * count the status bar carries, so the two always agree - which means it
     * falls as well as rises, the history being a rolling window.
     */
    if(app->filter.hud_show_msg_count) {
        snprintf(text, sizeof(text), "Msg %u", pcsg_history_get_item(app->txrx->history));
        pocsag_pager_scene_network_info_add(
            app, PCSG_NETWORK_MID_X, PCSG_NETWORK_ROW2_Y, AlignCenter, FontSecondary, text);
    }

    if(app->filter.hud_show_ext_sec) {
        if(info.have_ext_sec) {
            snprintf(text, sizeof(text), "ExtSec %u", info.ext_sec);
        } else {
            strlcpy(text, "ExtSec -", sizeof(text));
        }
        pocsag_pager_scene_network_info_add(
            app, PCSG_NETWORK_RIGHT_X, PCSG_NETWORK_ROW2_Y, AlignRight, FontSecondary, text);
    }
}

void pocsag_pager_scene_network_info_on_enter(void* context) {
    POCSAGPagerApp* app = context;
    scene_manager_set_scene_state(
        app->scene_manager, POCSAGPagerSceneNetworkInfo, furi_get_tick());
    pocsag_pager_scene_network_info_draw(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewWidget);
}

bool pocsag_pager_scene_network_info_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    if(event.type != SceneManagerEventTypeTick) return false;
    /*
     * The app ticks ten times a second and a rebuild frees and reallocates
     * every element on the screen, which is far more than a clock showing
     * seconds needs. The scene state carries when it was last redrawn.
     */
    const uint32_t now = furi_get_tick();
    const uint32_t last =
        scene_manager_get_scene_state(app->scene_manager, POCSAGPagerSceneNetworkInfo);
    if(now - last < furi_ms_to_ticks(PCSG_NETWORK_TICK_MS)) return true;
    scene_manager_set_scene_state(app->scene_manager, POCSAGPagerSceneNetworkInfo, now);
    pocsag_pager_scene_network_info_draw(app);
    return true;
}

void pocsag_pager_scene_network_info_on_exit(void* context) {
    POCSAGPagerApp* app = context;
    widget_reset(app->widget);
}
