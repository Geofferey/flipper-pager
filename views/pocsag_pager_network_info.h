#pragma once

#include <gui/view.h>
#include "../helpers/pocsag_pager_types.h"
#include "../helpers/pocsag_pager_event.h"

typedef struct PCSGNetworkInfo PCSGNetworkInfo;

typedef void (*PCSGNetworkInfoCallback)(PCSGCustomEvent event, void* context);

/**
 * Everything the screen shows, already rendered to text. An empty string is
 * not drawn, so a field the owner has nothing for simply leaves its place
 * empty rather than needing a flag of its own.
 *
 * The screen is built on the app thread and handed over whole: the decoder's
 * words and the settings are read there, never from the draw callback.
 */
typedef struct {
    /** Top row: zone and daylight flag on the left, roaming on the right. */
    char zone[24];
    char roam[8];
    /** Centre. Big digits, so `clock` carries only digits and colons. */
    char clock[16];
    bool have_clock;
    /** Under it: the date with the clock's source beside it, as
     *  "1994-11-17 | NET". Centred under the clock, so the two read as one.
     *  Sized for the longest the compiler can prove, not the longest real
     *  date, so composing the two needs no truncation warning. */
    char date[28];
    /** Identity and frame rows along the bottom. */
    char ssid1[20];
    char ssid2[16];
    char frame[16];
    char msgs[12];
    char ext_sec[16];
} PCSGNetworkInfoFields;

/** Select, for the owner to act on. The view holds no settings of its own. */
void pcsg_view_network_info_set_callback(
    PCSGNetworkInfo* pcsg_network_info,
    PCSGNetworkInfoCallback callback,
    void* context);

/** Replace everything on screen at once. */
void pcsg_view_network_info_update(
    PCSGNetworkInfo* pcsg_network_info,
    const PCSGNetworkInfoFields* fields);

PCSGNetworkInfo* pcsg_view_network_info_alloc(void);

void pcsg_view_network_info_free(PCSGNetworkInfo* pcsg_network_info);

View* pcsg_view_network_info_get_view(PCSGNetworkInfo* pcsg_network_info);
