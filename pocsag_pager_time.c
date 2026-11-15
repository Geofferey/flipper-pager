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
static uint32_t
    pcsg_time_frame_second(const FlexFrameTiming* timing, uint32_t age_ms, uint8_t* hour) {
    // FIW publication marks this frame's start; elapsed time includes reception.
    const uint32_t gps_seconds =
        ((uint32_t)timing->cycle * 240000U + (uint32_t)timing->frame * 1875U + age_ms + 500U) /
        1000U;
    const uint32_t gps_second = gps_seconds % 3600U;
    if(hour) {
        // BIW hour uses daylight time year-round; set L0 means standard time.
        *hour = (timing->hour + gps_seconds / 3600U + 24U - timing->standard_time -
                 (gps_second < PCSG_GPS_UTC_OFFSET_S)) %
                24U;
    }
    return (gps_second + 3600U - PCSG_GPS_UTC_OFFSET_S) % 3600U;
}

// Common gate: FLEX reception with frame timing recent enough to extrapolate.
static bool pcsg_time_frame(POCSAGPagerApp* app, FlexFrameTiming* timing, uint32_t* age_ms) {
    if(app->txrx->txrx_state != PCSGTxRxStateRx) return false;
    if(strncmp(furi_string_get_cstr(app->txrx->preset->name), "FLEX", 4) != 0) return false;
    if(!flex_sampler_get_frame_timing(app->txrx->flex_sampler, timing)) return false;
    const uint32_t age_ticks = furi_get_tick() - timing->tick;
    if(age_ticks > furi_ms_to_ticks(PCSG_TIME_SYNC_MAX_AGE_MS)) return false;
    // The five-second limit above is at most 5000 ticks on Flipper.
    *age_ms = age_ticks * 1000U / furi_kernel_get_tick_frequency();
    return true;
}

/*
 * Zone offsets in quarter hours, indexed by the zone code, from TI's TMS320FLEX
 * table 2-8 (see README.md). Code 16 is reserved and reads as GMT.
 */
static const int8_t pcsg_time_zone_quarters[32] = {
    0,  4,   8,   12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  14,  18,  22,
    0,  23,  26,  38,  -14, -44, -40, -36, -32, -28, -24, -20, -16, -12, -8,  -4,
};

static bool pcsg_time_zone_minutes(const FlexFrameTiming* timing, int16_t* minutes) {
    if(timing->zone >= 32U) return false;
    int16_t offset = (int16_t)pcsg_time_zone_quarters[timing->zone] * 15;
    // The networks name the zone's daylight offset and leave the code alone all
    // year, so standard time is an hour back from it. Same correction the hour
    // gets, and the same one Geofferey's Mayhem flex_rx fork applies.
    if(timing->standard_time) offset -= 60;
    *minutes = offset;
    return true;
}

bool pcsg_time_network(POCSAGPagerApp* app, PCSGNetworkTime* out) {
    furi_assert(app);
    furi_assert(out);
    FlexFrameTiming timing;
    uint32_t age_ms;
    if(!pcsg_time_frame(app, &timing, &age_ms)) return false;
    // Without the clock words there is no hour to show, only a frame position.
    if(timing.chase || !timing.clock_valid) return false;
    const uint32_t sec = pcsg_time_frame_second(&timing, age_ms, &out->hour);
    out->minute = sec / 60U;
    out->second = sec % 60U;
    out->have_zone = pcsg_time_zone_minutes(&timing, &out->zone_minutes);
    return true;
}

// Adapted from Mayhem FLEX RX's network_hour/update_rtc; keep the RTC date:
// https://github.com/Geofferey/mayhem-firmware/blob/67345f64389694e45d1873587a0f052c4baed8e5/firmware/application/external/flex_rx/ui_flex_rx.cpp
void pcsg_time_sync(POCSAGPagerApp* app) {
    if(app->filter.time_sync == PCSGTimeSyncOff) return;

    FlexFrameTiming timing;
    uint32_t age_ms;
    if(!pcsg_time_frame(app, &timing, &age_ms)) return;
    if(timing.sequence == app->time_sync_sequence) return;
    app->time_sync_sequence = timing.sequence;
    if(timing.chase) return;

    const bool full = app->filter.time_sync == PCSGTimeSyncFull && timing.clock_valid;
    uint8_t network_hour;
    const int32_t second = (int32_t)pcsg_time_frame_second(&timing, age_ms, &network_hour);

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
