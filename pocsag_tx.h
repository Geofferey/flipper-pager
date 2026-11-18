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
/** What one phase carries in a single frame: the message split three ways. */
#define PCSG_TX_PAGE_CHARS                (PCSG_TX_MAX_MESSAGE_LENGTH / 3U)
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
/** Names for the four combinations, indexed by the mask. Shared by the config
 *  row that picks them and the review screen that reports the choice. */
extern const char* const pcsg_tx_clock_text[PCSG_TX_CLOCK_COUNT];

/** Zone codes are five bits, so 31 is the largest the zone word can carry. */
#define PCSG_TX_UTC_CODE_MAX 31U

/*
 * System information BIWs, which say where the transmission is rather than
 * when. SSID1 carries the local identity and coverage zone, SSID2 the country
 * code; both sit in the same three slots as the clock words, so enabling them
 * puts them in competition with it. Ranges are the widths of their fields.
 */
#define PCSG_TX_LOCAL_ID_MAX 511U
#define PCSG_TX_COVERAGE_MAX 31U
#define PCSG_TX_COUNTRY_MAX  1023U

/** Accurate-seconds trim in the zone word, in steps of 0.9375 s. */
#define PCSG_TX_EXT_SEC_MAX 7U

/** Message number, six bits. One past the top means follow the clock instead. */
#define PCSG_TX_MSG_NUMBER_MAX  63U
#define PCSG_TX_MSG_NUMBER_AUTO 64U

/*
 * Lead-in ahead of a transmission, in 12-byte cycles. Each is 60 ms at 1600
 * baud. Zero is the Default row entry, which leaves each mode at the length it
 * was measured or chosen at - PCSG_TX_ERS_CYCLES_DEFAULT of the ERS code ahead
 * of a page, and the captured square-wave preamble ahead of a beacon - while
 * any other value is that duration whichever mode is sending.
 */
#define PCSG_TX_ERS_CYCLES_MAX     48U
#define PCSG_TX_ERS_CYCLES_DEFAULT 24U
#define PCSG_TX_ERS_CYCLES_STEP    4U

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

/*
 * FLEX only: how the phase a capcode goes out on is decided.
 *
 * Cap applies the standard frame and phase embedding rule, which is how
 * addresses are handed out when an operator lets the number carry the
 * assignment. Group assigns the phase by hand, for the networks that do not
 * use that rule - the spec recommends the explicit CAPCODE types wherever
 * group addresses are in play, and nothing on the air says which was used.
 */
typedef enum {
    PagerTxPhaseCap,
    PagerTxPhaseGroup,
    PagerTxPhaseCount,
} PagerTxPhaseMode;

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
    /*
     * One capcode per FLEX phase, indexed by it. A capcode carries its own
     * phase in bits 2 and 3 of its address, so which entry a capcode belongs
     * in is a property of the capcode, not a choice - a single-phase pager
     * listens only on the phase its address names. POCSAG has no phases and
     * uses the first entry as its RIC.
     */
    uint32_t ric[4];
    uint16_t baud;
    uint8_t function;
    bool inverted;
    /** FLEX only: use a mode above 1600 bps. */
    bool flex_3200;
    /** FLEX only: four-level modulation. */
    bool flex_4level;
    /** FLEX only: 6400 bps as 3200 baud four-level. */
    bool flex_6400;
    /** FLEX only: which clock BIWs to send, PCSG_TX_CLOCK_* as a bit each. */
    uint8_t clock_biws;
    /** FLEX only: the zone word's UTC code, sent with the time BIW. */
    uint8_t utc_code;
    /** FLEX only: whether the hour sent is daylight time rather than standard. */
    bool dst;
    /** FLEX only: whether capcodes name their own phase or are assigned one. */
    uint8_t phase_mode;
    /** FLEX only: accurate-seconds trim carried by the zone word. */
    uint8_t ext_sec;
    /** FLEX only: send the SSID1 word, naming the local identity and zone. */
    bool send_ssid1;
    /** Nine bits and five bits, the widths of their fields in that word. */
    uint16_t local_id;
    uint8_t coverage_zone;
    /** FLEX only: send the SSID2 word, naming the country. Ten bits. */
    bool send_ssid2;
    uint16_t country_code;
    /** FLEX only: roaming flag in the frame information word. */
    bool roaming;
    /** FLEX only: message number, or PCSG_TX_MSG_NUMBER_AUTO to derive it. */
    uint8_t msg_number;
    /** FLEX only: acquisition cycles ahead of a page. */
    uint8_t ers_cycles;
    /** Which body to send. Not saved. */
    PagerTxMessageMode message_mode;
    /** Sequence mode: the number this page carries. Not saved. */
    uint16_t sequence_number;
    char message[PCSG_TX_MAX_MESSAGE_LENGTH + 1U];
    /*
     * Per-phase pages. Ordinarily every phase a mode carries sends `message`
     * to its own capcode in ric[], which is one message to several addresses.
     * With this set, phase n sends page_text[n] instead, so one burst carries
     * a different message to each address - which is what a network's phases
     * are for.
     *
     * One frame, so each page is whatever fits a single fragment; a message
     * long enough to split would need its phases to run to different lengths.
     */
    bool per_phase_text;
    char page_text[4][PCSG_TX_PAGE_CHARS + 1U];
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
