#include "pocsag_pager_app_i.h"

#define TAG "POCSAGPager"
#include <flipper_format/flipper_format_i.h>
#include <loader/loader.h>

static bool pcsg_preset_has_mode_prefix(const char* preset_name, const char* prefix) {
    const size_t prefix_length = strlen(prefix);
    if(strncmp(preset_name, prefix, prefix_length) != 0) return false;
    const char suffix = preset_name[prefix_length];
    return suffix == '\0' || suffix == '-' || suffix == ' ' || suffix == '\t';
}

bool pcsg_preset_is_flex4(const char* preset_name) {
    furi_assert(preset_name);
    return pcsg_preset_has_mode_prefix(preset_name, "FLEX4") ||
           pcsg_preset_has_mode_prefix(preset_name, "FLEX-4");
}

void pcsg_preset_init(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size) {
    furi_assert(context);
    POCSAGPagerApp* app = context;
    furi_string_set(app->txrx->preset->name, preset_name);
    app->txrx->preset->frequency = frequency;
    app->txrx->preset->data = preset_data;
    app->txrx->preset->data_size = preset_data_size;
}

void pcsg_get_frequency_modulation(
    POCSAGPagerApp* app,
    FuriString* frequency,
    FuriString* modulation) {
    furi_assert(app);
    if(frequency != NULL) {
        furi_string_printf(
            frequency,
            "%03ld.%02ld",
            app->txrx->preset->frequency / 1000000 % 1000,
            app->txrx->preset->frequency / 10000 % 100);
    }
    if(modulation != NULL) {
        const char* preset_name = furi_string_get_cstr(app->txrx->preset->name);
        if(pcsg_preset_is_flex4(preset_name)) {
            furi_string_set_str(modulation, "F4");
        } else if(!strncmp(preset_name, "FLEX", 4)) {
            furi_string_set_str(modulation, "FX");
        } else if(!strcmp(preset_name, "POCSAG-W")) {
            furi_string_set_str(modulation, "PW");
        } else {
            furi_string_printf(modulation, "%.2s", preset_name);
        }
    }
}

void pcsg_begin(POCSAGPagerApp* app, uint8_t* preset_data) {
    furi_assert(app);

    subghz_devices_reset(app->txrx->radio_device);
    subghz_devices_idle(app->txrx->radio_device);
    subghz_devices_load_preset(app->txrx->radio_device, FuriHalSubGhzPresetCustom, preset_data);

    // furi_hal_gpio_init(furi_hal_subghz.cc1101_g0_pin, GpioModeInput, GpioPullNo, GpioSpeedLow);
    app->txrx->txrx_state = PCSGTxRxStateIDLE;
}

// Receiving is the receiver app's: the transmit build has no sampler, no
// decoder and no history for any of this to drive.
#ifndef PCSG_TX_APP
uint32_t pcsg_rx(POCSAGPagerApp* app, uint32_t frequency) {
    furi_assert(app);
    if(!subghz_devices_is_frequency_valid(app->txrx->radio_device, frequency)) {
        furi_crash("POCSAGPager: Incorrect RX frequency.");
    }
    furi_assert(
        app->txrx->txrx_state != PCSGTxRxStateRx && app->txrx->txrx_state != PCSGTxRxStateSleep);

    // Compensate the crystal offset so the LO lands on the selected channel.
    // The displayed frequency stays nominal; only the tuning is shifted.
    const int64_t corrected = (int64_t)frequency + app->filter.frequency_correction_hz;
    uint32_t tuned = (corrected > 0 && corrected <= UINT32_MAX) ? (uint32_t)corrected : frequency;
    if(!subghz_devices_is_frequency_valid(app->txrx->radio_device, tuned)) tuned = frequency;

    subghz_devices_idle(app->txrx->radio_device);
    uint32_t value = subghz_devices_set_frequency(app->txrx->radio_device, tuned);

    // Not need. init in subghz_devices_start_async_tx
    // furi_hal_gpio_init(furi_hal_subghz.cc1101_g0_pin, GpioModeInput, GpioPullNo, GpioSpeedLow);

    subghz_devices_flush_rx(app->txrx->radio_device);
    subghz_devices_set_rx(app->txrx->radio_device);

    // Apply the saved setting from a known silent state. This also clears a
    // sampler-owned Synth mode left by an earlier receive session.
    flex_sampler_set_sound(app->txrx->flex_sampler, FlexSamplerSoundOff, NULL, 0.0f, NULL);
    subghz_devices_set_async_mirror_pin(app->txrx->radio_device, NULL);

    // Signal sound. Raw mirrors the received data to the speaker pin, as the
    // Sub-GHz app does; FLEX4 has no async capture to mirror, so its sampler
    // drives the pin. Tone (FLEX4 only) plays each level as a pitch through
    // the speaker's PWM, which has a volume; the async path cannot (its
    // worker thread does not own the speaker) and falls back to Raw.
    const bool flex4 = pcsg_preset_is_flex4(furi_string_get_cstr(app->txrx->preset->name));
    if(app->filter.signal_sound != PCSGSoundOff) {
        static const float volumes[PCSGToneVolumeCount] = {0.1f, 0.4f, 1.0f};
        // Lowest pitch, and the step between levels as a share of it.
        static const float bases[PCSGTonePitchCount] = {300.0f, 600.0f, 1200.0f};
        static const float spreads[PCSGToneSpreadCount] = {0.25f, 0.5f, 1.0f};
        const float base = bases[app->filter.tone_pitch];
        const float step = base * spreads[app->filter.tone_spread];
        const float tones[4] = {base, base + step, base + 2.0f * step, base + 3.0f * step};
        const float volume = volumes[app->filter.tone_volume];

        if(flex4 && app->filter.signal_sound == PCSGSoundSynth) {
            // The sampler thread owns the speaker and the decoder plays tones,
            // so nothing is acquired here.
            flex_sampler_set_sound(
                app->txrx->flex_sampler, FlexSamplerSoundSynth, NULL, volume, tones);
        } else if(furi_hal_speaker_acquire(30)) {
            app->txrx->speaker_on = true;
            if(flex4 && app->filter.signal_sound == PCSGSoundTone) {
                flex_sampler_set_sound(
                    app->txrx->flex_sampler, FlexSamplerSoundTone, NULL, volume, tones);
            } else if(flex4) {
                furi_hal_gpio_init(&gpio_speaker, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
                flex_sampler_set_sound(
                    app->txrx->flex_sampler, FlexSamplerSoundRaw, &gpio_speaker, 0.0f, NULL);
            } else {
                // POCSAG / two-level FLEX: mirror the data line (Raw).
                subghz_devices_set_async_mirror_pin(app->txrx->radio_device, &gpio_speaker);
            }
        }
    }

    /*
     * RX is stopped here, so this discards timing from a previous channel or
     * session only. A new channel is usually a different network, and its
     * zone, date and system words say nothing about this one - but they are
     * often the same operator, and watching a reading survive a hop is the
     * point of keeping it, so the HUD decides.
     */
    if(app->filter.hud_freq_switch == PCSGHudFreqDiscard) {
        flex_sampler_reset_frame_timing(app->txrx->flex_sampler);
    } else {
        // Keeping the reading does not mean keeping the grid: the next frame
        // is another transmitter's, and its cycle number read against the one
        // before it would put the hour on by one.
        flex_sampler_mark_frame_gap(app->txrx->flex_sampler);
    }
    // The sync sequence is restarted either way: it only guards against
    // setting the RTC twice from one frame, and that frame is now behind us.
    app->time_sync_sequence = 0;

    if(flex4) {
        flex_sampler_set_capture(app->txrx->flex_sampler, app->filter.raw_capture);
        flex_sampler_set_stats_log(app->txrx->flex_sampler, app->filter.stats_log);
        flex_sampler_set_freq_correction(
            app->txrx->flex_sampler, app->filter.frequency_correction_hz);
        flex_sampler_set_weak_decode(app->txrx->flex_sampler, app->filter.weak_decode);
        flex_sampler_start(app->txrx->flex_sampler);
    } else {
        subghz_devices_start_async_rx(
            app->txrx->radio_device, subghz_worker_rx_callback, app->txrx->worker);
        subghz_worker_start(app->txrx->worker);
    }
    app->txrx->txrx_state = PCSGTxRxStateRx;
    return value;
}

void pcsg_idle(POCSAGPagerApp* app) {
    furi_assert(app);
    furi_assert(app->txrx->txrx_state != PCSGTxRxStateSleep);
    subghz_devices_idle(app->txrx->radio_device);
    app->txrx->txrx_state = PCSGTxRxStateIDLE;
}

// Long enough that stepping through a row's values with the arrows settles
// before the radio is touched, short enough to feel immediate.
#define PCSG_RX_RESTART_DEBOUNCE_MS 400U

void pcsg_rx_restart_request(POCSAGPagerApp* app) {
    furi_assert(app);
    app->txrx->rx_restart = true;
    app->txrx->rx_restart_due = furi_get_tick() + furi_ms_to_ticks(PCSG_RX_RESTART_DEBOUNCE_MS);
}

void pcsg_rx_restart_poll(POCSAGPagerApp* app) {
    furi_assert(app);
    if(!app->txrx->rx_restart) return;
    // Not while stopped: entering the receiver starts the radio with whatever
    // is set by then, which is the same thing arriving by another road.
    if(app->txrx->txrx_state != PCSGTxRxStateRx) return;
    if((int32_t)(furi_get_tick() - app->txrx->rx_restart_due) < 0) return;

    app->txrx->rx_restart = false;
    pcsg_rx_end(app);
    pcsg_begin(
        app,
        subghz_setting_get_preset_data_by_name(
            app->setting, furi_string_get_cstr(app->txrx->preset->name)));
    pcsg_rx(app, app->txrx->preset->frequency);
}

void pcsg_rx_end(POCSAGPagerApp* app) {
    furi_assert(app);
    furi_assert(app->txrx->txrx_state == PCSGTxRxStateRx);
    if(flex_sampler_is_running(app->txrx->flex_sampler)) {
        flex_sampler_stop(app->txrx->flex_sampler);
    } else if(subghz_worker_is_running(app->txrx->worker)) {
        subghz_worker_stop(app->txrx->worker);
        subghz_devices_stop_async_rx(app->txrx->radio_device);
    }
    pcsg_classic_alert_reset(app);
    // Synth owns and releases the speaker inside the sampler thread, so sound
    // state must be cleared independently of the app-owned speaker flag.
    flex_sampler_set_sound(app->txrx->flex_sampler, FlexSamplerSoundOff, NULL, 0.0f, NULL);
    subghz_devices_set_async_mirror_pin(app->txrx->radio_device, NULL);
    if(app->txrx->speaker_on) {
        if(furi_hal_speaker_is_mine()) {
            furi_hal_speaker_stop();
            furi_hal_speaker_release();
        }
        app->txrx->speaker_on = false;
    }
    subghz_devices_idle(app->txrx->radio_device);
    app->txrx->txrx_state = PCSGTxRxStateIDLE;
}
#endif // !PCSG_TX_APP

bool pcsg_app_handover(POCSAGPagerApp* app, const char* path) {
    furi_assert(app);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool present = storage_file_exists(storage, path);
    furi_record_close(RECORD_STORAGE);
    if(!present) return false;

    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_enqueue_launch(loader, path, NULL, LoaderDeferredLaunchFlagNone);
    furi_record_close(RECORD_LOADER);
    // Queued, not started: it runs once this app has exited.
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

void pcsg_sleep(POCSAGPagerApp* app) {
    furi_assert(app);
    subghz_devices_sleep(app->txrx->radio_device);
    app->txrx->txrx_state = PCSGTxRxStateSleep;
}

// And transmitting is the transmit app's, encoders and all.
#ifdef PCSG_TX_APP
static void pcsg_tx_release_encoder(POCSAGPagerApp* app) {
    pocsag_tx_encoder_free(app->tx_encoder);
    flex_tx_encoder_free(app->flex_tx_encoder);
    app->tx_encoder = NULL;
    app->flex_tx_encoder = NULL;
}

PCSGTxStartStatus pcsg_tx_start(POCSAGPagerApp* app) {
    furi_assert(app);
    // This app never receives, so there is no receive session to end first.
    if(app->txrx->txrx_state == PCSGTxRxStateTx) pcsg_tx_stop(app);
    if(app->txrx->txrx_state == PCSGTxRxStateSleep) return PCSGTxStartInvalid;

    // The same correction the receiver tunes with.
    const int64_t corrected_frequency =
        (int64_t)app->tx_config.frequency + app->filter.frequency_correction_hz;
    if(corrected_frequency <= 0 || corrected_frequency > UINT32_MAX) {
        return PCSGTxStartInvalid;
    }
    const uint32_t tx_frequency = (uint32_t)corrected_frequency;

    const bool flex = app->tx_config.protocol == PagerTxProtocolFlex;
    uint8_t* preset = pocsag_tx_get_preset(flex ? FLEX_TX_BAUD : app->tx_config.baud);
    if(!preset || !subghz_devices_is_frequency_valid(app->txrx->radio_device, tx_frequency)) {
        return PCSGTxStartInvalid;
    }

    /*
     * The sequence modes render their body here rather than into the stored
     * message, so a typed message survives a trip through them and back.
     */
    const PocsagTxConfig* config = &app->tx_config;
    PocsagTxConfig sequence;
    if(pager_tx_mode_is_sequence(config->message_mode)) {
        sequence = *config;
        snprintf(
            sequence.message,
            sizeof(sequence.message),
            PCSG_TX_SEQUENCE_FORMAT,
            config->sequence_number);
        config = &sequence;
    }

    bool prepared;
    if(flex) {
        app->flex_tx_encoder = flex_tx_encoder_alloc();
        prepared =
            app->flex_tx_encoder && flex_tx_encoder_prepare(app->flex_tx_encoder, config);
    } else {
        app->tx_encoder = pocsag_tx_encoder_alloc();
        prepared = app->tx_encoder && pocsag_tx_encoder_prepare(app->tx_encoder, config);
    }
    if(!prepared) {
        pcsg_tx_release_encoder(app);
        return PCSGTxStartInvalid;
    }

    subghz_devices_reset(app->txrx->radio_device);
    subghz_devices_idle(app->txrx->radio_device);
    subghz_devices_load_preset(app->txrx->radio_device, FuriHalSubGhzPresetCustom, preset);
    subghz_devices_set_frequency(app->txrx->radio_device, tx_frequency);

    /*
     * Four-level drives the data pin from a thread instead of the asynchronous
     * transmitter, because each symbol also moves a radio register and the
     * async callback runs in the DMA interrupt where the SPI bus is off
     * limits. The radio is put in transmit here and the pin is ours to drive.
     */
    app->tx_four_level = flex && (config->flex_4level || config->flex_6400);
    if(app->tx_four_level) {
        // The burst starves the notification service, so anything already
        // buzzing would stay on until it ends. Settle it first.
        notification_message_block(app->notifications, &sequence_reset_vibro);
        const GpioPin* data = subghz_devices_get_data_gpio(app->txrx->radio_device);
        furi_hal_gpio_init(data, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
        furi_hal_gpio_write(data, config->inverted);
        if(!furi_hal_subghz_tx() ||
           !flex_tx_four_level_start(app->flex_tx_encoder, data)) {
            subghz_devices_idle(app->txrx->radio_device);
            pcsg_tx_release_encoder(app);
            app->tx_four_level = false;
            return PCSGTxStartRestricted;
        }
        app->txrx->txrx_state = PCSGTxRxStateTx;
        return PCSGTxStartOk;
    }

    FuriHalSubGhzAsyncTxCallback tx_callback =
        flex ? flex_tx_encoder_yield : pocsag_tx_encoder_yield;
    void* tx_context = flex ? (void*)app->flex_tx_encoder : (void*)app->tx_encoder;
    if(!subghz_devices_start_async_tx(app->txrx->radio_device, tx_callback, tx_context)) {
        subghz_devices_idle(app->txrx->radio_device);
        pcsg_tx_release_encoder(app);
        return PCSGTxStartRestricted;
    }

    app->txrx->txrx_state = PCSGTxRxStateTx;
    return PCSGTxStartOk;
}

bool pcsg_tx_complete(POCSAGPagerApp* app) {
    furi_assert(app);
    if(app->txrx->txrx_state != PCSGTxRxStateTx) return false;
    return app->tx_four_level ? flex_tx_four_level_complete() :
                                subghz_devices_is_async_complete_tx(app->txrx->radio_device);
}

void pcsg_tx_stop(POCSAGPagerApp* app) {
    furi_assert(app);
    if(app->txrx->txrx_state != PCSGTxRxStateTx) return;
    if(app->tx_four_level) {
        flex_tx_four_level_stop();
        app->tx_four_level = false;
    } else {
        subghz_devices_stop_async_tx(app->txrx->radio_device);
    }
    subghz_devices_idle(app->txrx->radio_device);
    pcsg_tx_release_encoder(app);
    app->txrx->txrx_state = PCSGTxRxStateIDLE;
}
#endif // PCSG_TX_APP
