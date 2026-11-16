#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <lib/subghz/protocols/base.h>

/** Highest FLEX capcode accepted by the capcode filter (NumberInput is int32). */
#define PCSG_CAPCODE_MAX 2147483647U

/** Off / Exact / Range filter, used for capcodes and FLEX frames. */
typedef enum {
    PCSGCapcodeFilterOff,
    PCSGCapcodeFilterExact,
    PCSGCapcodeFilterRange,
    PCSGCapcodeFilterCount,
} PCSGCapcodeFilter;

/** Highest POCSAG RIC (21-bit address). */
#define PCSG_RIC_MAX 2097151U

/** Highest FLEX frame number (128 frames per 4-minute cycle). */
#define PCSG_FRAME_MAX 127U

/** Field edited by the number entry scene (its scene state). */
typedef enum {
    PCSGNumberFieldCapcodeMin,
    PCSGNumberFieldCapcodeMax,
    PCSGNumberFieldFrameMin,
    PCSGNumberFieldFrameMax,
    PCSGNumberFieldRicMin,
    PCSGNumberFieldRicMax,
    /** Crystal correction in Hz, signed, shared by RX and TX. */
    PCSGNumberFieldFreqCorrection,
    /** Tuning frequency in Hz, checked against the radio before it is kept. */
    PCSGNumberFieldTxFrequency,
    PCSGNumberFieldRxFrequency,
    /** SSID words: too many values to step through, so they are typed. */
    PCSGNumberFieldTxLocalId,
    PCSGNumberFieldTxCountry,
} PCSGNumberField;

/*
 * Range offered when typing a frequency. The CC1101 covers several separate
 * bands, which one pair of bounds cannot express, so these only keep the
 * keypad sane: what is actually allowed is decided by
 * subghz_devices_is_frequency_valid() when the value is accepted.
 */
#define PCSG_FREQUENCY_MIN_HZ 300000000
#define PCSG_FREQUENCY_MAX_HZ 950000000

/** FLEX phase to show: all, or one of A-D. */
typedef enum {
    PCSGPhaseFilterAll,
    PCSGPhaseFilterA,
    PCSGPhaseFilterB,
    PCSGPhaseFilterC,
    PCSGPhaseFilterD,
    PCSGPhaseFilterCount,
} PCSGPhaseFilter;

/** FLEX duplicate handling in the live viewer. */
typedef enum {
    PCSGFlexDupsHide,
    PCSGFlexDupsShow,
    /** Show copies to different capcodes, but hide same-cap retransmits. */
    PCSGFlexDupsCaps,
    PCSGFlexDupsCount,
} PCSGFlexDups;

/** Sound/vibration notification for a new message; LED control is separate. */
typedef enum {
    PCSGAlertBoth,
    PCSGAlertSound,
    PCSGAlertVibro,
    PCSGAlertOff,
    PCSGAlertCount,
} PCSGAlert;

typedef enum {
    PCSGAlertToneDefault,
    PCSGAlertToneClassic,
    PCSGAlertToneCount,
} PCSGAlertTone;

/** Shared indicator palette; append colors to preserve saved values. */
typedef enum {
    PCSGLedColorCyan,
    PCSGLedColorBlue,
    PCSGLedColorGreen,
    PCSGLedColorYellow,
    PCSGLedColorMagenta,
    PCSGLedColorRed,
    PCSGLedColorWhite,
    PCSGLedColorAmber,
    PCSGLedColorLime,
    PCSGLedColorTeal,
    PCSGLedColorSky,
    PCSGLedColorViolet,
    PCSGLedColorPink,
    PCSGLedColorWarm,
    PCSGLedColorCount,
} PCSGLedColor;

/** TimeSync values 0/1 retain the old Off/On setting. */
typedef enum {
    PCSGTimeSyncOff,
    PCSGTimeSyncMinuteSecond,
    PCSGTimeSyncFull,
    PCSGTimeSyncCount,
} PCSGTimeSync;

/** Signal sound while receiving; saved as SignalSound (1 was the old "On"). */
typedef enum {
    PCSGSoundOff,
    // The data line drives the speaker directly: the true signal, full volume.
    PCSGSoundRaw,
    // FLEX4 levels play as pitches at the tone volume; other presets use Raw.
    PCSGSoundTone,
    // Like Tone but from the decoder's recovered symbols: clean, no noise.
    PCSGSoundSynth,
    PCSGSoundCount,
} PCSGSound;

typedef enum {
    PCSGToneVolumeLow,
    PCSGToneVolumeMedium,
    PCSGToneVolumeHigh,
    PCSGToneVolumeCount,
} PCSGToneVolume;

/** Tone mode: lowest pitch (300, 600 or 1200 Hz). */
typedef enum {
    PCSGTonePitchLow,
    PCSGTonePitchMedium,
    PCSGTonePitchHigh,
    PCSGTonePitchCount,
} PCSGTonePitch;

/** Tone mode: step between levels, a quarter, half or all of the lowest pitch. */
typedef enum {
    PCSGToneSpreadNarrow,
    PCSGToneSpreadNormal,
    PCSGToneSpreadWide,
    PCSGToneSpreadCount,
} PCSGToneSpread;

/*
 * Where a field of the displayed clock is taken from. The network's own clock
 * is the point of the thing, but a network can be wrong - and two observed
 * ones are - so each part can be taken from the Flipper instead.
 */
typedef enum {
    PCSGHudSourceNetwork,
    PCSGHudSourceLocal,
    PCSGHudSourceCount,
} PCSGHudSource;

/** The hour additionally offers falling back rather than choosing outright. */
typedef enum {
    PCSGHudHourNetwork,
    PCSGHudHourLocal,
    /** The network's once a clock word arrives, the Flipper's until then. */
    PCSGHudHourHybrid,
    PCSGHudHourCount,
} PCSGHudHour;

/*
 * What becomes of the HUD when the channel changes. A new channel is usually
 * a different network, whose zone, date and system words say nothing about the
 * one just left - but they are often the same operator, and seeing a reading
 * survive a hop is worth having.
 */
typedef enum {
    PCSGHudFreqDiscard,
    PCSGHudFreqKeep,
    PCSGHudFreqCount,
} PCSGHudFreqSwitch;

/** What the receiver's status bar line carries. */
typedef enum {
    PCSGHudTickerRotate,
    PCSGHudTickerFrequency,
    PCSGHudTickerClock,
    PCSGHudTickerOff,
    PCSGHudTickerCount,
} PCSGHudTicker;

/** Whole hours, either way, that the HUD clock can be shifted by. */
#define PCSG_HUD_OFFSET_MAX   12
#define PCSG_HUD_OFFSET_COUNT (PCSG_HUD_OFFSET_MAX * 2 + 1)

/** Seconds the status bar holds an item. Two is as fast as it reads. */
#define PCSG_HUD_TICKER_SECS_MIN     2U
#define PCSG_HUD_TICKER_SECS_MAX     60U
#define PCSG_HUD_TICKER_SECS_DEFAULT 10U

typedef struct {
    // Independent visibility per FLEX page kind. POCSAG is not filtered by
    // kind: it has only the RIC filter.
    bool show_alpha;
    bool show_numeric;
    bool show_tone;
    bool show_binary;
    PCSGCapcodeFilter capcode_filter;
    uint32_t capcode_min;
    uint32_t capcode_max;
    PCSGPhaseFilter phase_filter;
    PCSGCapcodeFilter frame_filter;
    uint32_t frame_min;
    uint32_t frame_max;
    // POCSAG only; FLEX capcodes have their own filter.
    PCSGCapcodeFilter ric_filter;
    uint32_t ric_min;
    uint32_t ric_max;
    PCSGFlexDups flex_dups;
    bool hide_encrypted;
    // Discard FLEX messages that could not be fully decoded (off by default).
    bool hide_partial;
    // Log to one temp file destroyed on exit instead of a kept log per session.
    bool log_temp;
    PCSGAlert alert;
    PCSGAlertTone alert_tone;
    bool receive_led;
    PCSGLedColor receive_led_color;
    bool decode_led;
    PCSGLedColor decode_led_color;
    // Sync MM:SS, or set HH:MM:SS once the network hour is known.
    PCSGTimeSync time_sync;
    PCSGSound signal_sound;
    PCSGToneVolume tone_volume;
    PCSGTonePitch tone_pitch;
    PCSGToneSpread tone_spread;
    // Crystal offset compensation applied to both RX and TX tuning, in Hz.
    // Positive raises the carrier, so a radio that runs low needs a positive
    // value.
    int32_t frequency_correction_hz;
    // Record the raw FLEX4 sample stream to SD (debug / offline analysis).
    bool raw_capture;
    // Write counter-only FLEX4 diagnostics to /ext/pager/stats.txt.
    bool stats_log;
    // FLEX4 soft-decision (Chase) recovery of weak words; off by default, and
    // allocates nothing in the decoder unless enabled.
    bool weak_decode;
    /*
     * HUD: where each part of the displayed clock comes from, and what the
     * Network info screen shows. Display only - the log keeps timestamping
     * from the Flipper's own clock whatever is set here, so changing one of
     * these can never rewrite what has already been written down.
     *
     * Two networks have been observed sending a date that is not today's: one
     * a stale 2007, another today's day and month with the year field wrapped
     * to 1994. Local is there for both of them.
     */
    PCSGHudSource hud_date;
    /** Local, Network, or Network falling back to Local until an hour word. */
    PCSGHudHour hud_hour;
    PCSGHudSource hud_minute;
    /** Whole hours added to the displayed clock, for a network that is out. */
    int8_t hud_offset_hours;
    /** Whether changing channel throws the reading away or carries it over. */
    PCSGHudFreqSwitch hud_freq_switch;
    PCSGHudTicker hud_ticker;
    /** How long the status bar holds each item before moving on, in seconds. */
    uint8_t hud_ticker_secs;
    bool hud_show_zone;
    bool hud_show_date;
    bool hud_show_ssid1;
    bool hud_show_ssid2;
    bool hud_show_roaming;
    bool hud_show_frame;
    bool hud_show_ext_sec;
} PCSGFilterSettings;

/*
 * One correction for both RX and TX, added to the frequency handed to the
 * radio; the displayed frequency stays nominal. Wide enough for a crystal
 * error at the top of the range, about 14 ppm reaching 13 kHz at 930 MHz.
 * Being a fixed number of hertz rather than a ratio, it stops being right as
 * soon as the band changes, which is the trade for setting it this finely.
 */
#define PCSG_FREQ_CORRECTION_MIN_HZ  (-15000)
#define PCSG_FREQ_CORRECTION_MAX_HZ  15000
#define PCSG_FREQ_CORRECTION_STEP_HZ 100
/*
 * Arrows cannot span the whole range at that step: variable_item_list_add()
 * counts values in a uint8_t and 30 kHz of them needs 301. They reach this
 * far either side; OK on the row types any value up to the limits above.
 */
#define PCSG_FREQ_CORRECTION_LIST_MAX_HZ 12700

typedef struct POCSAGPagerApp POCSAGPagerApp;

/** Restore receiver and transmitter configuration saved on the SD card. */
void pcsg_settings_load(POCSAGPagerApp* app);

/** Save receiver and transmitter configuration to the SD card. */
void pcsg_settings_save(POCSAGPagerApp* app);

/** Whether a decoded message passes the filters and should enter history. */
bool pcsg_filter_accepts(
    const PCSGFilterSettings* filter,
    SubGhzProtocolDecoderBase* decoder_base);
