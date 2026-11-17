#include "../pocsag_pager_app_i.h"

/*
 * What the network has said about itself, reached with right from the live
 * list. The clock is the centre of it because that is the one field a network
 * sends constantly; the identity words arrive in a frame here and there, so
 * the rest fills in around it as they turn up and reads "-" until then.
 *
 * Everything is gathered here, on the app thread, and handed to the view as
 * finished text: the decoder's sequence-locked words and the settings are
 * never read from the draw callback.
 */
#define PCSG_NETWORK_TICK_MS 500U

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
    PCSGNetworkInfoFields f = {0};

    // Each field says for itself whether it has been heard, so whether any of
    // them has is not asked here: the zeroed struct reads as nothing heard.
    FlexSystemInfo info = {0};
    if(app->txrx->flex_sampler) flex_sampler_get_system_info(app->txrx->flex_sampler, &info);

    PCSGNetworkTime now = {0};
    bool have_clock = pcsg_time_network(app, &now);

    DateTime local;
    furi_hal_rtc_get_datetime(&local);

    /*
     * Select forces the Flipper's own clock over whatever the HUD options
     * chose. It lasts while the screen is open and is forgotten on the way
     * out, so it reads as a look at the other clock rather than a change of
     * settings - and the Flipper's clock needs no reception, so it shows even
     * with nothing on the air.
     */
    if(app->network_info_force_rtc) {
        now.hour = local.hour;
        now.minute = local.minute;
        now.second = local.second;
        now.have_hour = true;
        now.hour_local = true;
        have_clock = true;
    }
    f.have_clock = have_clock;

    // Zone on the left of the top row, the offset the clock is actually on,
    // with the daylight flag beside it as "UTC-8 | DST".
    if(app->filter.hud_show_zone) {
        if(have_clock && now.have_zone) {
            char zone[12];
            pcsg_zone_format(now.zone_minutes, zone, sizeof(zone));
            /*
             * Daylight only, divider and all. The offset beside it is already
             * the one in force, so standard time has nothing to add: an
             * indicator always present tells you less than one that appears.
             */
            snprintf(f.zone, sizeof(f.zone), "%s%s", zone, now.standard_time ? "" : " | DST");
        } else {
            strlcpy(f.zone, "No zone", sizeof(f.zone));
        }
    }

    // Roaming on the right of it: a word about the network, not about the time.
    if(info.have_roaming && app->filter.hud_show_roaming) {
        strlcpy(f.roam, info.roaming ? "ROAM" : "HOME", sizeof(f.roam));
    }

    // Minutes and seconds come from the frame grid, so they run from the
    // first frame onwards; the hour stays "--" until a clock word arrives.
    if(have_clock) {
        if(now.have_hour) {
            snprintf(f.clock, sizeof(f.clock), "%02u:%02u:%02u", now.hour, now.minute, now.second);
        } else {
            snprintf(f.clock, sizeof(f.clock), "--:%02u:%02u", now.minute, now.second);
        }
        // Which clock the hour came from, Select or the HUD options having
        // decided it. Beside the date, where the eye already is.
        strlcpy(f.source, now.hour_local ? "RTC" : "NET", sizeof(f.source));
    }

    /*
     * Date: the network's, or the Flipper's. Two observed networks send one
     * that is not today's - a stale 2007 on one, today's day and month with
     * the year field wrapped to 1994 on another - so Local is a real choice
     * rather than a fallback.
     */
    if(app->filter.hud_show_date) {
        // Year and day/month are chosen apart, so they are built apart: the
        // networks seen so far get one right and the other wrong.
        char year_text[8];
        if(app->network_info_force_rtc || app->filter.hud_year == PCSGHudSourceLocal) {
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
        if(app->network_info_force_rtc || app->filter.hud_date == PCSGHudSourceLocal) {
            snprintf(day_text, sizeof(day_text), "%02u-%02u", local.month, local.day);
        } else if(info.have_date) {
            snprintf(day_text, sizeof(day_text), "%02u-%02u", info.month, info.day);
        } else {
            // Escaped: "??-" would otherwise be read as the trigraph for "~".
            strlcpy(day_text, "\?\?-\?\?", sizeof(day_text));
        }

        if(year_text[0] == '?' && day_text[0] == '?') {
            strlcpy(f.date, "no date", sizeof(f.date));
        } else {
            snprintf(f.date, sizeof(f.date), "%s-%s", year_text, day_text);
        }
    }

    // Who is transmitting: SSID1 on the left, SSID2 on the right.
    if(app->filter.hud_show_ssid1) {
        if(info.have_ssid1) {
            snprintf(f.ssid1, sizeof(f.ssid1), "LID %u/%u", info.local_id, info.coverage_zone);
        } else {
            strlcpy(f.ssid1, "LID -", sizeof(f.ssid1));
        }
    }

    if(app->filter.hud_show_ssid2) {
        if(info.have_ssid2) {
            snprintf(f.ssid2, sizeof(f.ssid2), "CC %u", info.country_code);
        } else {
            strlcpy(f.ssid2, "CC -", sizeof(f.ssid2));
        }
    }

    // Where the frame grid is, and the seconds trim riding with the zone.
    if(app->filter.hud_show_frame) {
        FlexFrameTiming timing;
        if(app->txrx->flex_sampler &&
           flex_sampler_get_frame_timing(app->txrx->flex_sampler, &timing)) {
            snprintf(f.frame, sizeof(f.frame), "F%u/C%u", timing.frame, timing.cycle);
        } else {
            strlcpy(f.frame, "F-/C-", sizeof(f.frame));
        }
    }

    /*
     * Messages held this session, between the frame and the trim. The same
     * count the status bar carries, so the two always agree - which means it
     * falls as well as rises, the history being a rolling window.
     */
    if(app->filter.hud_show_msg_count) {
        snprintf(f.msgs, sizeof(f.msgs), "M:%u", pcsg_history_get_item(app->txrx->history));
    }

    if(app->filter.hud_show_ext_sec) {
        if(info.have_ext_sec) {
            snprintf(f.ext_sec, sizeof(f.ext_sec), "ExtSec %u", info.ext_sec);
        } else {
            strlcpy(f.ext_sec, "ExtSec -", sizeof(f.ext_sec));
        }
    }

    pcsg_view_network_info_update(app->pcsg_network_info, &f);
}

static void pocsag_pager_scene_network_info_callback(PCSGCustomEvent event, void* context) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

void pocsag_pager_scene_network_info_on_enter(void* context) {
    POCSAGPagerApp* app = context;
    // Always opens on the network's own clock, whatever Select last chose.
    app->network_info_force_rtc = false;
    pcsg_view_network_info_set_callback(
        app->pcsg_network_info, pocsag_pager_scene_network_info_callback, app);
    scene_manager_set_scene_state(
        app->scene_manager, POCSAGPagerSceneNetworkInfo, furi_get_tick());
    pocsag_pager_scene_network_info_draw(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewNetworkInfo);
}

bool pocsag_pager_scene_network_info_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PCSGCustomEventViewNetworkSource) {
            app->network_info_force_rtc = !app->network_info_force_rtc;
            // Straight away rather than on the next tick: a key press that
            // takes half a second to show reads as a missed press.
            pocsag_pager_scene_network_info_draw(app);
            return true;
        }
        return false;
    }

    if(event.type != SceneManagerEventTypeTick) return false;
    /*
     * The app ticks ten times a second, which is far more than a clock showing
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
    UNUSED(context);
}
