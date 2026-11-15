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
 * choices, so the row's index is the mode; the FLEX-only one is last, which is
 * what lets POCSAG offer the same row one choice shorter.
 */
typedef enum {
    PagerTxMessageTyped,
    PagerTxMessageSequence,
    PagerTxMessageTestBeacon,
    PagerTxMessageModeCount,
} PagerTxMessageMode;

typedef struct {
    PagerTxProtocol protocol;
    uint32_t frequency;
    uint32_t ric;
    uint16_t baud;
    uint8_t function;
    bool inverted;
    /** FLEX only: 3200/2-level instead of 1600/2-level. */
    bool flex_3200;
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
