#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <toolbox/level_duration.h>

/*
 * Shared entry buffer. FLEX reaches the full length by splitting the message
 * into fragments; POCSAG has no fragmentation and is bounded by the three
 * batches its encoder emits, so it carries its own lower ceiling.
 */
#define PCSG_TX_MAX_MESSAGE_LENGTH        240U
#define PCSG_TX_POCSAG_MAX_MESSAGE_LENGTH 80U
#define PCSG_TX_RIC_MAX                   0x1FFFFFU

/*
 * Clock BIWs a FLEX transmission carries, one bit each, so the stored value is
 * also the Clock row's index. The zone word rides with the time: it carries
 * the standard/daylight flag the hour is in. Three words is the ceiling -- BIW1
 * holds the address offset in two bits, so word 4 is as far out as the address
 * can start -- which is exactly time, date and zone.
 */
#define PCSG_TX_CLOCK_TIME  1U
#define PCSG_TX_CLOCK_DATE  2U
#define PCSG_TX_CLOCK_BOTH  3U
#define PCSG_TX_CLOCK_COUNT 4U

/** Body of a Sequence page; the counter is four digits, so it wraps at 9999. */
#define PCSG_TX_SEQUENCE_FORMAT "THIS IS A TEST PERIODIC PAGE SEQUENTIAL NUMBER %04u"
#define PCSG_TX_SEQUENCE_MAX    9999U
#define PCSG_TX_SEQUENCE_CHARS  64U

typedef enum {
    PagerTxProtocolPocsag,
    PagerTxProtocolFlex,
    PagerTxProtocolCount,
} PagerTxProtocol;

/*
 * What a transmission carries. The order is the order of the Message row's
 * choices, so the row's index is the mode, and the FLEX-only ones come last,
 * which is what lets POCSAG offer the same row two choices shorter.
 */
typedef enum {
    PagerTxMessageTyped,
    PagerTxMessageSequence,
    PagerTxMessageBeacon,
    PagerTxMessageBeaconSeq,
    PagerTxMessageModeCount,
} PagerTxMessageMode;

/** Repeats until Back, and needs the FLEX clock BIWs, so POCSAG has neither. */
static inline bool pager_tx_mode_is_beacon(PagerTxMessageMode mode) {
    return mode >= PagerTxMessageBeacon;
}

/** Carries the numbered periodic page, on a button press or once a minute. */
static inline bool pager_tx_mode_is_sequence(PagerTxMessageMode mode) {
    return mode == PagerTxMessageSequence || mode == PagerTxMessageBeaconSeq;
}

typedef struct {
    PagerTxProtocol protocol;
    uint32_t frequency;
    uint32_t ric;
    uint16_t baud;
    uint8_t function;
    bool inverted;
    /** FLEX only: 3200/2-level instead of 1600/2-level. */
    bool flex_3200;
    /** FLEX only: which clock BIWs to send, PCSG_TX_CLOCK_* as a bit each. */
    uint8_t clock_biws;
    /** Which body to send. Not saved. */
    PagerTxMessageMode message_mode;
    /** Sequence mode: the number this page carries. Not saved. */
    uint16_t sequence_number;
    char message[PCSG_TX_MAX_MESSAGE_LENGTH + 1U];
} PocsagTxConfig;

typedef struct PocsagTxEncoder PocsagTxEncoder;

PocsagTxEncoder* pocsag_tx_encoder_alloc(void);
void pocsag_tx_encoder_free(PocsagTxEncoder* encoder);

/** Build one complete, batch-padded alphanumeric POCSAG transmission. */
bool pocsag_tx_encoder_prepare(PocsagTxEncoder* encoder, const PocsagTxConfig* config);

/** Callback consumed by the Sub-GHz asynchronous transmitter. */
LevelDuration pocsag_tx_encoder_yield(void* context);

/** Known-good asynchronous 2-FSK preset; the waveform timer supplies the baud. */
uint8_t* pocsag_tx_get_preset(uint16_t baud);
