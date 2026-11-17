#include "pocsag_pager_app_i.h"
#include <datetime/datetime.h>
#include <furi_hal_rtc.h>

#define PCSG_TIME_SYNC_MAX_AGE_MS 5000U
#define PCSG_TIME_SYNC_MIN_ADJUST_S 2
#define PCSG_GPS_UTC_OFFSET_S 18U

/*
 * Where one frame's timing puts the network clock: the return is seconds into
 * the UTC hour, and `hour`, when asked for, the network's own local hour. That
 * hour only means anything once the clock words have arrived.
 *
 * The RTC sync and the receiver's status bar both need this, so it lives here
 * rather than in each of them.
 */
static uint32_t pcsg_time_frame_second(
    const FlexFrameTiming* timing,
    uint32_t age_ms,
    uint8_t* hour,
    int8_t dst_hours) {
    // FIW publication marks this frame's start; elapsed time includes reception.
    const uint32_t gps_seconds =
        ((uint32_t)timing->cycle * 240000U + (uint32_t)timing->frame * 1875U + age_ms + 500U) /
        1000U;
    const uint32_t gps_second = gps_seconds % 3600U;
    if(hour) {
        // 23, 24 or 25: the wrap and the daylight correction in one term, so
        // the sum stays positive whichever way the correction goes.
        const uint32_t adjust = (uint32_t)(24 + dst_hours);
        *hour = (timing->hour + gps_seconds / 3600U + adjust -
                 (gps_second < PCSG_GPS_UTC_OFFSET_S)) %
                24U;
    }
    return (gps_second + 3600U - PCSG_GPS_UTC_OFFSET_S) % 3600U;
}

/*
 * Hours to move this frame's clock and zone offset by, reconciling what the
 * network advertises against whether daylight saving is in force here.
 *
 * The network knows which kind of time it is sending; only the receiver knows
 * whether that is the season, so the setting is the authority and each option
 * corrects the network that disagrees with it. A network already in step is
 * left alone, and Disabled leaves every network alone.
 *
 * The hour and the zone offset move together or not at all: a UTC-8 label over
 * a clock still reading -7 is worse than either answer on its own.
 */
static int8_t pcsg_time_dst_hours(const POCSAGPagerApp* app, const FlexFrameTiming* timing) {
    // The zone word carries the flag, so without one nothing is advertised.
    if(timing->zone >= 32U) return 0;
    const bool advertises_daylight = !timing->standard_time;
    switch(app->filter.dst) {
    case PCSGDstOff:
        return advertises_daylight ? -1 : 0;
    case PCSGDstOn:
        return advertises_daylight ? 0 : 1;
    default:
        return 0;
    }
}

/*
 * Common gate: FLEX reception with frame timing to extrapolate from. The clock
 * is free running once seeded - the frame gives the position, the Flipper's own
 * tick carries it forward - so how old the last frame may be depends on what
 * the answer is for. Setting the RTC from a stale reading would be wrong, so
 * that caps it; showing one is not, so that does not. Zero means no cap.
 */
static bool pcsg_time_frame(
    POCSAGPagerApp* app,
    FlexFrameTiming* timing,
    uint32_t* age_ms,
    uint32_t max_age_ms) {
    if(app->txrx->txrx_state != PCSGTxRxStateRx) return false;
    if(strncmp(furi_string_get_cstr(app->txrx->preset->name), "FLEX", 4) != 0) return false;
    if(!flex_sampler_get_frame_timing(app->txrx->flex_sampler, timing)) return false;
    const uint32_t age_ticks = furi_get_tick() - timing->tick;
    if(max_age_ms != 0U && age_ticks > furi_ms_to_ticks(max_age_ms)) return false;
    // 64-bit: uncapped, the multiply overflows a uint32 at about 71 minutes.
    *age_ms = (uint32_t)((uint64_t)age_ticks * 1000ULL / furi_kernel_get_tick_frequency());
    return true;
}

/*
 * Zone offsets in quarter hours, indexed by the zone code, from TI's TMS320FLEX
 * table 2-8 (see README.md). Code 16 is reserved and reads as GMT. Quarter
 * hours because every zone FLEX can name is a whole number of them, so the
 * whole table is 32 bytes.
 */
static const int8_t pcsg_zone_quarters[32] = {
    0,  4,   8,   12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  14,  18,  22,
    0,  23,  26,  38,  -14, -44, -40, -36, -32, -28, -24, -20, -16, -12, -8,  -4,
};

/*
 * The same codes in UTC order, which is not code order: the codes run +0 to
 * +12, then the fractional zones, then -11 to -1. The transmitter's row steps
 * through this so it reads like a map; everything else works in codes. Keeping
 * the order as 31 bytes beside the offsets costs a great deal less than a
 * second table of labels, which is what this replaced - the labels are just
 * the offsets formatted, so none of them need storing.
 */
static const uint8_t pcsg_zone_order[PCSG_ZONE_COUNT] = {
    21, 22, 23, 24, 25, 26, 27, 28, 20, 29, 30, 31, 0, 1,  2,  3,
    13, 4,  14, 5,  15, 17, 6,  18, 7,  8,  9,  19, 10, 11, 12,
};

int16_t pcsg_zone_minutes(uint8_t code) {
    return code < 32U ? (int16_t)pcsg_zone_quarters[code] * 15 : 0;
}

uint8_t pcsg_zone_code(uint8_t index) {
    return index < PCSG_ZONE_COUNT ? pcsg_zone_order[index] : pcsg_zone_order[PCSG_ZONE_UTC0];
}

uint8_t pcsg_zone_index(uint8_t code) {
    for(uint8_t i = 0; i < PCSG_ZONE_COUNT; i++) {
        if(pcsg_zone_order[i] == code) return i;
    }
    // Code 16 is reserved, so it is the one value that is not in the order.
    return PCSG_ZONE_UTC0;
}

void pcsg_zone_format(int16_t minutes, char* out, size_t size) {
    const uint16_t magnitude = (uint16_t)(minutes < 0 ? -minutes : minutes);
    const char sign = minutes < 0 ? '-' : '+';
    if(magnitude % 60U) {
        snprintf(out, size, "UTC%c%u:%02u", sign, magnitude / 60U, magnitude % 60U);
    } else {
        snprintf(out, size, "UTC%c%u", sign, magnitude / 60U);
    }
}

static bool
    pcsg_time_zone_minutes(const FlexFrameTiming* timing, int16_t* minutes, int8_t dst_hours) {
    // The code as transmitted, moved by whatever the clock itself moves, so
    // the label and the digits agree about which offset is in force.
    *minutes = pcsg_zone_minutes(timing->zone) + (int16_t)dst_hours * 60;
    return timing->zone < 32U;
}

bool pcsg_time_network(POCSAGPagerApp* app, PCSGNetworkTime* out) {
    furi_assert(app);
    furi_assert(out);
    FlexFrameTiming timing;
    uint32_t age_ms;
    // No cap: a reading already taken stays good, and the clock runs on from
    // it. It used to be dropped five seconds after the last frame.
    if(!pcsg_time_frame(app, &timing, &age_ms, 0U)) return false;
    /*
     * Minutes and seconds come from where the frame sits in the cycle, so they
     * are known as soon as any frame is, with no clock word involved. Only the
     * hour needs one, and only the hour waits for it.
     */
    const int8_t dst_hours = pcsg_time_dst_hours(app, &timing);
    const uint32_t sec = pcsg_time_frame_second(&timing, age_ms, &out->hour, dst_hours);
    out->minute = sec / 60U;
    out->second = sec % 60U;
    out->have_hour = timing.have_hour;
    out->have_zone = pcsg_time_zone_minutes(&timing, &out->zone_minutes, dst_hours);
    out->dst_hours = dst_hours;
    out->standard_time = timing.standard_time;

    /*
     * The HUD settings decide where each part actually comes from. The
     * network's clock is the point of this, but two observed networks are
     * wrong about it, so the Flipper's own can stand in for any part.
     */
    DateTime local;
    furi_hal_rtc_get_datetime(&local);
    if(app->filter.hud_minute == PCSGHudSourceLocal) {
        out->minute = local.minute;
        out->second = local.second;
    }
    const bool local_hour = app->filter.hud_hour == PCSGHudHourLocal ||
                            (app->filter.hud_hour == PCSGHudHourHybrid && !out->have_hour);
    out->hour_local = local_hour;
    if(local_hour) {
        out->hour = local.hour;
        // Taken from the Flipper, so it is known, whatever the network sent.
        out->have_hour = true;
    }
    if(out->have_hour && app->filter.hud_offset_hours != 0) {
        out->hour = (uint8_t)(((int16_t)out->hour + app->filter.hud_offset_hours + 24) % 24);
    }
    return true;
}

// Adapted from Mayhem FLEX RX's network_hour/update_rtc; keep the RTC date:
// https://github.com/Geofferey/mayhem-firmware/blob/67345f64389694e45d1873587a0f052c4baed8e5/firmware/application/external/flex_rx/ui_flex_rx.cpp
void pcsg_time_sync(POCSAGPagerApp* app) {
    if(app->filter.time_sync == PCSGTimeSyncOff) return;

    FlexFrameTiming timing;
    uint32_t age_ms;
    // Setting the RTC does need a fresh frame, so this one keeps the cap.
    if(!pcsg_time_frame(app, &timing, &age_ms, PCSG_TIME_SYNC_MAX_AGE_MS)) return;
    if(timing.sequence == app->time_sync_sequence) return;
    app->time_sync_sequence = timing.sequence;
    if(timing.chase) return;

    const bool full = app->filter.time_sync == PCSGTimeSyncFull && timing.clock_valid;
    uint8_t network_hour;
    // The same correction the screen gets: Full sync would otherwise write an
    // hour the HUD never showed.
    const int32_t second = (int32_t)pcsg_time_frame_second(
        &timing, age_ms, &network_hour, pcsg_time_dst_hours(app, &timing));

    DateTime target;
    furi_hal_rtc_get_datetime(&target);
    int32_t adjustment = second - ((int32_t)target.minute * 60 + target.second);
    if(full) {
        adjustment += ((int32_t)network_hour - target.hour) * 3600;
        target.hour = network_hour;
    } else {
        if(adjustment > 1800) adjustment -= 3600;
        if(adjustment < -1800) adjustment += 3600;
    }
    if(adjustment > -PCSG_TIME_SYNC_MIN_ADJUST_S &&
       adjustment < PCSG_TIME_SYNC_MIN_ADJUST_S) return;
    if(full) {
        target.minute = second / 60;
        target.second = second % 60;
    } else {
        // Preserve the existing MM:SS adjustment, including hour/date carry.
        const int64_t stamp = (int64_t)datetime_datetime_to_timestamp(&target) + adjustment;
        if(stamp < 0 || stamp > UINT32_MAX) return;
        datetime_timestamp_to_datetime((uint32_t)stamp, &target);
    }
    furi_hal_rtc_set_datetime(&target);
}
