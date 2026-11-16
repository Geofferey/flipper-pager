#pragma once

#include <stdatomic.h>
#include <furi/core/event_loop_timer.h>

#include "helpers/pocsag_pager_types.h"
#include "helpers/radio_device_loader.h"

#include "scenes/pocsag_pager_scene.h"
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/number_input.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>
#include "views/pocsag_pager_receiver.h"
#include "views/pocsag_pager_receiver_info.h"
#include "pocsag_pager_history.h"
#include "flex_sampler.h"
#include "pocsag_pager_settings.h"
#include "pocsag_pager_log.h"
#include "pocsag_tx.h"
#include "flex_tx.h"

#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/registry.h>
#include <lib/subghz/devices/devices.h>

typedef struct POCSAGPagerApp POCSAGPagerApp;

struct POCSAGPagerTxRx {
    SubGhzWorker* worker;

    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzRadioPreset* preset;
    PCSGHistory* history;
    FlexSampler* flex_sampler;
    uint16_t idx_menu_chosen;
    PCSGTxRxState txrx_state;
    PCSGRxKeyState rx_key_state;
    // Set when Config changes frequency or preset; otherwise the receiver
    // keeps running when it is re-entered.
    bool rx_restart;
    // The speaker is held for the signal sound while receiving.
    bool speaker_on;

    const SubGhzDevice* radio_device;
};

typedef struct POCSAGPagerTxRx POCSAGPagerTxRx;

struct POCSAGPagerApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    POCSAGPagerTxRx* txrx;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    VariableItemList* variable_item_list;
    Submenu* submenu;
    Widget* widget;
    NumberInput* number_input;
    TextInput* text_input;
    PCSGReceiver* pcsg_receiver;
    PCSGReceiverInfo* pcsg_receiver_info;
    PCSGLock lock;
    SubGhzSetting* setting;
    PCSGFilterSettings filter;
    uint8_t config_selected_index;
    // Config opened from the main menu rather than the receiver.
    bool config_standalone;
    uint32_t time_sync_sequence;
    FuriEventLoopTimer* classic_alert_timer;
    uint32_t classic_alert_tick;
    atomic_bool classic_alert_pending;
    bool classic_alert_started;
    bool classic_alert_sounding;
    uint8_t classic_alert_remaining;

    PocsagTxEncoder* tx_encoder;
    FlexTxEncoder* flex_tx_encoder;
    PocsagTxConfig tx_config;
    uint8_t tx_config_selected_index;
    // Which phase entry the capcode row is showing, as an index into
    // the phases the current mode carries.
    uint8_t tx_ric_slot;
    // Tick the current beacon burst was scheduled for. It advances by one
    // beacon period at a time rather than from "now", so the 100 ms scene tick
    // cannot accumulate into a drifting cadence. Unsigned differences keep the
    // comparisons right across a tick counter wrap.
    // Tick the receiver's status bar started its cycle on, so it always opens
    // on the frequency line rather than wherever a free-running clock landed.
    uint32_t rx_bar_tick;
    // Last tick the network clock actually read, so a reading can be held
    // across the gap a Weak Decode frame opens in the decoder's clock.
    uint32_t rx_clock_tick;
    uint32_t tx_burst_tick;
    // Frames that burst carries, taken while its encoder still exists. It sets
    // the period, and more than one frame means it carried the periodic page.
    uint8_t tx_burst_frames;
    // Transmission in flight is four-level, so it is driven by its own thread
    // rather than the asynchronous transmitter, and stops differently.
    bool tx_four_level;
    bool tx_pause_pending;

    // Review Messages
    PCSGLog* log;
    FuriString* log_path;
    uint16_t review_idx;
};

void pcsg_preset_init(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size);
void pcsg_get_frequency_modulation(
    POCSAGPagerApp* app,
    FuriString* frequency,
    FuriString* modulation);
bool pcsg_preset_is_flex4(const char* preset_name);
/*
 * Receiving and transmitting are two apps, and they hand over by queueing each
 * other with the loader and then exiting: the loader starts the queued one once
 * we are gone, so only one is ever resident. False if the other .fap is not
 * installed, in which case nothing is queued and the caller should stay put -
 * exiting anyway would leave the user with neither.
 */
#define PCSG_APP_RX_PATH "/ext/apps/Sub-GHz/flipper_pager.fap"
#define PCSG_APP_TX_PATH "/ext/apps/Sub-GHz/flipper_pager_tx.fap"
bool pcsg_app_handover(POCSAGPagerApp* app, const char* path);

void pcsg_time_sync(POCSAGPagerApp* app);

/** The FLEX network's own clock: its time of day, and the offset its zone code
 *  and standard/daylight flag put it at. */
typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    int16_t zone_minutes;
    bool have_zone;
} PCSGNetworkTime;

/** False until the network's clock words arrive, or while they are stale. */
bool pcsg_time_network(POCSAGPagerApp* app, PCSGNetworkTime* out);

/*
 * FLEX time zones, shared by the transmitter's UTC row and the receiver's
 * status bar. 31 of the 32 codes name a zone; code 16 is reserved. The offsets
 * are the zone's own, so a receiver still has to apply the standard/daylight
 * flag itself.
 */
#define PCSG_ZONE_COUNT 31U
/** Where UTC+0 sits in the ordering, and where an unknown code lands. */
#define PCSG_ZONE_UTC0 12U
/** Minutes from GMT for a zone code; 0 for anything that is not one. */
int16_t pcsg_zone_minutes(uint8_t code);
/** Zone code at a position in UTC order, for stepping through them. */
uint8_t pcsg_zone_code(uint8_t index);
/** Where a code sits in that order. */
uint8_t pcsg_zone_index(uint8_t code);
/** Writes an offset as "UTC-8" or "UTC+5:45". */
void pcsg_zone_format(int16_t minutes, char* out, size_t size);
bool pcsg_classic_alert_request(POCSAGPagerApp* app);
void pcsg_classic_alert_tick(POCSAGPagerApp* app);
void pcsg_classic_alert_reset(POCSAGPagerApp* app);
void pcsg_classic_alert_free(POCSAGPagerApp* app);
void pcsg_begin(POCSAGPagerApp* app, uint8_t* preset_data);
uint32_t pcsg_rx(POCSAGPagerApp* app, uint32_t frequency);
void pcsg_idle(POCSAGPagerApp* app);
void pcsg_rx_end(POCSAGPagerApp* app);
void pcsg_sleep(POCSAGPagerApp* app);

typedef enum {
    PCSGTxStartOk,
    PCSGTxStartInvalid,
    PCSGTxStartRestricted,
} PCSGTxStartStatus;

PCSGTxStartStatus pcsg_tx_start(POCSAGPagerApp* app);
bool pcsg_tx_complete(POCSAGPagerApp* app);
void pcsg_tx_stop(POCSAGPagerApp* app);
