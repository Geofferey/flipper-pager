#include "flex_tx.h"

#include <furi.h>
#include <furi_hal.h>

#define FLEX_TX_BCH_POLYNOMIAL 0x769U
#define FLEX_TX_DATA_MASK      0x1FFFFFU
#define FLEX_TX_PHASE_WORDS    88U
/*
 * Acquisition runway ahead of the frame, in 12-byte cycles of alternating
 * bits and the ERS code. At FLEX_TX_BAUD each cycle is 96 bits, so this is
 * 1.44 s, and a transmission is 667 bytes or about 3.3 s in all.
 *
 * A receiver on a live FLEX channel is already locked to the network's clock
 * and needs almost none of this; one coming cold to a single transmission has
 * to recover the bit clock from scratch, and since the page is now sent once
 * there is no second burst to fall back on. Still shorter overall than the
 * two bursts and two frames this replaced. Lower it if the airtime matters
 * more than the margin.
 */
#define FLEX_TX_ERS_CYCLES     24U
// The frame proper, 3000 bits at 1600 baud = exactly 1.875 s. The flush tail
// is sent once after the last frame, not per frame, so consecutive frames stay
// on the network's frame grid.
#define FLEX_TX_FRAME_BYTES    375U
#define FLEX_TX_SYNC1_BYTES    14U
#define FLEX_TX_ERS_BYTES      (FLEX_TX_ERS_CYCLES * 12U)
/*
 * The test beacon's preamble, taken off the air from the FLEX network at
 * 929.4625 MHz: a 37.5 Hz square wave at full deviation, far below the 800 Hz
 * of sync1's bit sync, held about 98 ms ahead of every burst and bit-identical
 * each time. Half a period is 64/3 bits at 1600 baud, so the runs it produces
 * alternate 21 and 22 bits, as the captured ones do. 160 bits is 100 ms and
 * lands byte-aligned.
 *
 * A page keeps the longer ERS burst instead: a receiver may meet a single page
 * cold, where the beacon repeats forever.
 */
#define FLEX_TX_TEST_PREAMBLE_BITS  160U
#define FLEX_TX_TEST_PREAMBLE_BYTES (FLEX_TX_TEST_PREAMBLE_BITS / 8U)
// Starting phase within the square wave. 20 is what makes these 160 bits come
// out byte-for-byte as captured, ending on a low run into sync1.
#define FLEX_TX_TEST_PREAMBLE_PHASE 20U
/*
 * 3200/2-level keeps the burst, sync1 and the FIW at 1600 -- a receiver has to
 * read the mode out of sync1 before it can know any other rate -- and runs
 * everything after them at twice that. sync2 is a fixed 25 ms, so it costs
 * twice the bits, and the data carries phases A and C instead of A alone.
 */
#define FLEX_TX_FIW_BYTES   4U
#define FLEX_TX_SYNC2_BYTES 5U
/*
 * Flush tail, as the network at 929.4625 MHz sends it: 800 Hz, then a step
 * DOWN to 750 Hz, then the carrier drops. Measured over 36 bursts, the step
 * sits 74.32 ms past the end of the frame (sd 0.03 ms) and the 750 Hz run is
 * 31.6 ms median, 21-41 ms spread.
 *
 * 750 Hz is not a 1600 baud bit pattern -- that would be 2.133 bits per
 * cycle. Every captured half cycle measured 736-769 Hz, never the 400/800 Hz
 * pair a bit-quantised approximation produces, which is 800 Hz warbling at
 * 100 Hz rather than a tone. So the tail is emitted as half cycles of its own
 * instead of as bits in the buffer: the async timer takes arbitrary
 * microsecond runs, so an exact half period costs nothing.
 */
#define FLEX_TX_TAIL_800_HALVES 119U
#define FLEX_TX_TAIL_750_HALVES 48U
#define FLEX_TX_TAIL_800_US     625U
#define FLEX_TX_TAIL_750_US     667U
#define FLEX_TX_HEAD_BYTES  (FLEX_TX_SYNC1_BYTES + FLEX_TX_FIW_BYTES)
#define FLEX_TX_FAST_FRAME_BYTES \
    (FLEX_TX_HEAD_BYTES + FLEX_TX_SYNC2_BYTES * 2U + 11U * 64U)

/*
 * A message longer than one page is split into fragments, each sent as its own
 * frame in the same transmission, one acquisition burst covering them all. The
 * frames are consecutive, as a network would send them, so the frame numbers
 * step with the clock rather than repeating.
 *
 * Fragments are numbered 3 then 0, 1, 2, 0, ... and every one but the last
 * sets continuation; the receiver joins them per capcode and message number.
 */
#define FLEX_TX_MAX_FRAGMENTS  3U
#define FLEX_TX_FRAGMENT_CHARS (PCSG_TX_MAX_MESSAGE_LENGTH / FLEX_TX_MAX_FRAGMENTS)
#define FLEX_TX_BUFFER_BYTES \
    (FLEX_TX_ERS_BYTES + FLEX_TX_MAX_FRAGMENTS * FLEX_TX_FAST_FRAME_BYTES)

/*
 * Clock carried by the frame. Time of hour is not a field: FLEX derives it
 * from where the frame sits in the cycle, so the FIW's cycle and frame numbers
 * are the minutes and seconds, and only the hour needs a word of its own.
 *
 * Everything ahead of the FIW -- the acquisition burst and sync1 -- is airtime
 * that passes before a receiver reads the clock, so it is added on, or the
 * page would arrive describing when the buffer was built.
 *
 * FLEX runs on GPS time; the receiver takes the offset back off again. Keep in
 * step with PCSG_GPS_UTC_OFFSET_S in pocsag_pager_time.c.
 */
#define FLEX_TX_PRE_FIW_MS(ers_bytes) \
    ((((ers_bytes) + FLEX_TX_SYNC1_BYTES) * 8U * 1000U) / FLEX_TX_BAUD)
#define FLEX_TX_GPS_UTC_OFFSET_S 18U
#define FLEX_TX_FRAME_MS         1875U
#define FLEX_TX_CYCLE_MS         240000U
#define FLEX_TX_HOUR_MS          3600000U

struct FlexTxEncoder {
    uint8_t bytes[FLEX_TX_BUFFER_BYTES];
    size_t byte_count;
    size_t bit_index;
    // Where the frames start, the acquisition burst being of either length.
    size_t frame_base;
    uint8_t frames;
    // Half cycles of the tone tail already emitted.
    uint16_t tail_index;
    bool fast;
    bool inverted;
    bool lead_in_pending;
    bool tail_pending;
};

/*
 * FLEX framing is implemented from the word layouts already consumed by the
 * local decoder and cross-checked against PortaPack Mayhem's FLEX TX. See the
 * exact references in README.md.
 */
static uint32_t flex_tx_reverse_bits(uint32_t value) {
    value = ((value >> 1) & 0x55555555U) | ((value & 0x55555555U) << 1);
    value = ((value >> 2) & 0x33333333U) | ((value & 0x33333333U) << 2);
    value = ((value >> 4) & 0x0F0F0F0FU) | ((value & 0x0F0F0F0FU) << 4);
    value = ((value >> 8) & 0x00FF00FFU) | ((value & 0x00FF00FFU) << 8);
    return (value >> 16) | (value << 16);
}

static uint32_t flex_tx_encode_word(uint32_t data21) {
    const uint32_t reversed = flex_tx_reverse_bits(data21) >> 11;
    uint32_t remainder = reversed << 10;
    for(int8_t bit = 30; bit >= 10; bit--) {
        if(remainder & (1UL << bit)) {
            remainder ^= FLEX_TX_BCH_POLYNOMIAL << (bit - 10);
        }
    }

    uint32_t codeword = ((reversed << 10) | (remainder & 0x3FFU)) << 1;
    if(__builtin_popcount(codeword) & 1U) codeword |= 1U;
    return codeword;
}

static uint32_t flex_tx_checksum(uint32_t data21) {
    uint32_t sum = (data21 & 0x0FU) + ((data21 >> 4) & 0x0FU) +
                   ((data21 >> 8) & 0x0FU) + ((data21 >> 12) & 0x0FU) +
                   ((data21 >> 16) & 0x0FU) + ((data21 >> 20) & 0x01U);
    return (data21 & ~0x0FU) | ((0x0FU - (sum & 0x0FU)) & 0x0FU);
}

static uint32_t flex_tx_fiw(uint8_t cycle, uint8_t frame) {
    uint32_t word = ((uint32_t)(cycle & 0x0FU) << 4) |
                    ((uint32_t)(frame & 0x7FU) << 8);
    return flex_tx_encode_word(flex_tx_checksum(word));
}

static uint32_t flex_tx_biw1(uint8_t address_start, uint8_t vector_start) {
    uint32_t word = ((uint32_t)(address_start - 1U) & 0x03U) << 8;
    word |= ((uint32_t)vector_start & 0x3FU) << 10;
    return flex_tx_encode_word(flex_tx_checksum(word));
}

/*
 * Clock BIWs, mirroring flex_decode_clock() in protocols/flex.c: type 2 holds
 * the hour, type 5 subfield 4 the zone, and the decoder needs both before it
 * will treat the clock as usable. Fields neither side reads are left zero.
 */
static uint32_t flex_tx_biw_time(uint8_t hour) {
    uint32_t word = (2U << 4) | ((uint32_t)(hour & 31U) << 7);
    return flex_tx_encode_word(flex_tx_checksum(word));
}

static uint32_t flex_tx_biw_zone(void) {
    // Bit 20 clear is daylight, which is what the hour word above is in.
    uint32_t word = (5U << 4) | (4U << 7);
    return flex_tx_encode_word(flex_tx_checksum(word));
}

// Where the frame lands in the hour once it is actually on the air.
static void flex_tx_clock(uint8_t* cycle, uint8_t* frame, uint8_t* hour, uint32_t pre_fiw_ms) {
    DateTime datetime;
    furi_hal_rtc_get_datetime(&datetime);

    uint32_t ms = ((uint32_t)datetime.minute * 60U + datetime.second +
                   FLEX_TX_GPS_UTC_OFFSET_S) *
                      1000U +
                  pre_fiw_ms + (FLEX_TX_FRAME_MS / 2U);
    uint32_t hours = datetime.hour;
    if(ms >= FLEX_TX_HOUR_MS) {
        ms -= FLEX_TX_HOUR_MS;
        hours++;
    }

    *cycle = ms / FLEX_TX_CYCLE_MS;
    *frame = (ms % FLEX_TX_CYCLE_MS) / FLEX_TX_FRAME_MS;
    *hour = hours % 24U;
}

static uint32_t flex_tx_short_address(uint32_t capcode) {
    return flex_tx_encode_word((capcode + 0x8000U) & FLEX_TX_DATA_MASK);
}

/*
 * A tone-only page is carried entirely by its vector word: bits 7-8 clear and
 * the three BCD nibbles above them all 0xC, the fill code. That is what
 * Mayhem's FLEX RX requires before it calls a page TONE, in
 * FlexProcessor::decode_phase -- t == 0 && d == 0xCCC. Bits 7-8 set instead
 * select the source-digit form there, which reads back as "SRC 0".
 * https://github.com/portapack-mayhem/mayhem-firmware/blob/next/firmware/baseband/proc_flex.cpp
 */
static uint32_t flex_tx_tone_vector(void) {
    const uint32_t word = (2U << 4) | (0xCCCU << 9);
    return flex_tx_encode_word(flex_tx_checksum(word));
}

static uint32_t flex_tx_alpha_vector(uint8_t message_start, uint8_t message_words) {
    uint32_t word = 5U << 4;
    word |= ((uint32_t)message_start & 0x7FU) << 7;
    word |= ((uint32_t)message_words & 0x7FU) << 14;
    return flex_tx_encode_word(flex_tx_checksum(word));
}

/*
 * Signature over the whole message, not this fragment: the receiver sums the
 * characters of every fragment it joins and checks the total against the one
 * value carried in fragment 3. Fill characters are excluded, as there too.
 */
static uint8_t flex_tx_signature(const char* message, size_t length) {
    uint32_t sum = 0;
    for(size_t i = 0; i < length; i++) {
        const uint8_t value = (uint8_t)message[i] & 0x7FU;
        if(value != 0x03U) sum += value;
    }
    return (~sum) & 0x7FU;
}

static uint8_t flex_tx_encode_alpha(
    const char* message,
    size_t length,
    uint8_t fragment,
    bool continuation,
    uint8_t signature,
    uint8_t message_number,
    bool retrieval,
    uint32_t* output,
    uint8_t capacity) {
    if(capacity < 2U) return 0;

    uint8_t count = 0;
    output[count++] = ((uint32_t)(fragment & 0x03U) << 11) |
                      ((uint32_t)continuation << 10) |
                      ((uint32_t)(message_number & 0x3FU) << 13) |
                      ((uint32_t)retrieval << 19);

    // Fragment 3 spends the first slot of its first data word on the
    // signature; a continuation fragment carries a character there instead.
    size_t character = 0;
    uint32_t word = (fragment == 3U) ? (uint32_t)(signature & 0x7FU) : 0U;
    uint8_t slot = (fragment == 3U) ? 1U : 0U;
    while(count < capacity) {
        while(slot < 3U) {
            const uint8_t value =
                character < length ? (uint8_t)message[character++] & 0x7FU : 0x03U;
            word |= (uint32_t)value << (slot * 7U);
            slot++;
        }
        output[count++] = word;
        if(character >= length) break;
        word = 0;
        slot = 0;
    }
    if(character < length) return 0;

    uint32_t checksum = 0;
    for(uint8_t i = 0; i < count; i++) {
        checksum += output[i] & 0xFFU;
        checksum += (output[i] >> 8) & 0xFFU;
        checksum += (output[i] >> 16) & 0x1FU;
    }
    output[0] |= (~checksum) & 0x3FFU;

    for(uint8_t i = 0; i < count; i++) output[i] = flex_tx_encode_word(output[i]);
    return count;
}

static void flex_tx_write_word(uint8_t** output, uint32_t word) {
    *(*output)++ = (word >> 24) & 0xFFU;
    *(*output)++ = (word >> 16) & 0xFFU;
    *(*output)++ = (word >> 8) & 0xFFU;
    *(*output)++ = word & 0xFFU;
}

static void flex_tx_write_preamble(uint8_t** output, uint16_t total_bits) {
    uint8_t byte = 0;
    for(uint16_t i = 0; i < total_bits; i++) {
        // 37.5 Hz at 1600 baud: half a period is 64/3 bits.
        const uint32_t phase = (uint32_t)i + FLEX_TX_TEST_PREAMBLE_PHASE;
        byte = (uint8_t)((byte << 1) | ((phase * 3U / 64U) & 1U));
        if((i % 8U) == 7U) {
            *(*output)++ = byte;
            byte = 0;
        }
    }
}

static void flex_tx_write_ers(uint8_t** output, uint8_t cycles) {
    static const uint8_t ers_code[] = {0xCB, 0x20, 0x59, 0x39};
    for(uint8_t cycle = 0; cycle < cycles; cycle++) {
        *(*output)++ = 0xAA;
        *(*output)++ = 0xAA;
        for(size_t i = 0; i < COUNT_OF(ers_code); i++) *(*output)++ = ers_code[i];
        *(*output)++ = 0x55;
        *(*output)++ = 0x55;
        for(size_t i = 0; i < COUNT_OF(ers_code); i++) *(*output)++ = ~ers_code[i];
    }
}

static void flex_tx_write_interleaved(uint8_t** output, const uint32_t words[8]) {
    for(uint8_t bit = 0; bit < 32U; bit++) {
        uint8_t byte = 0;
        for(uint8_t word = 0; word < 8U; word++) {
            byte |= ((words[word] >> (31U - bit)) & 1U) << (7U - word);
        }
        *(*output)++ = byte;
    }
}

/*
 * At 3200/2-level the frame carries phases A and C, one bit of each in turn.
 * The page rides in A; C is left idle, and idle through the block interleaver
 * is the alternating pattern, so its bit is simply the bit position's parity.
 */
static void flex_tx_write_woven(uint8_t** output, const uint32_t words[8]) {
    uint8_t block[32];
    uint8_t* cursor = block;
    flex_tx_write_interleaved(&cursor, words);
    for(uint8_t i = 0; i < sizeof(block); i++) {
        uint16_t woven = 0;
        for(uint8_t bit = 0; bit < 8U; bit++) {
            const uint16_t a = (block[i] >> (7U - bit)) & 1U;
            woven = (uint16_t)((woven << 2) | (a << 1) | ((bit & 1U) ? 0U : 1U));
        }
        *(*output)++ = (uint8_t)(woven >> 8);
        *(*output)++ = (uint8_t)woven;
    }
}

typedef struct {
    uint8_t cycle;
    uint8_t frame;
    uint8_t hour;
    uint8_t message_number;
    uint8_t fragment;
    bool continuation;
    uint8_t signature;
    const char* text;
    size_t length;
} FlexTxFrameSpec;

// Steps to the next frame, carrying into the cycle and the hour.
static void flex_tx_clock_advance(FlexTxFrameSpec* spec) {
    if(++spec->frame < 128U) return;
    spec->frame = 0U;
    if(++spec->cycle < 15U) return;
    spec->cycle = 0U;
    spec->hour = (spec->hour + 1U) % 24U;
}

static bool flex_tx_write_frame(
    uint8_t** output,
    const PocsagTxConfig* config,
    const FlexTxFrameSpec* spec,
    bool retrieval) {
    static const uint8_t sync1[] = {
        0xAA, 0xAA, 0xAA, 0xAA, 0x78, 0xF3, 0x59,
        0x39, 0x55, 0x55, 0x87, 0x0C, 0xA6, 0xC6,
    };
    static const uint8_t sync2[] = {0xAE, 0xD8, 0x45, 0x12, 0x7B};
    const bool fast = config->flex_3200;

    uint8_t* const sync1_out = *output;
    for(size_t i = 0; i < COUNT_OF(sync1); i++) *(*output)++ = sync1[i];
    if(fast) {
        // Mode marker 0x7B18 and its inverse, in place of 1600/2's 0x870C.
        sync1_out[4] = 0x84;
        sync1_out[5] = 0xE7;
        sync1_out[10] = 0x7B;
        sync1_out[11] = 0x18;
    }
    flex_tx_write_word(output, flex_tx_fiw(spec->cycle, spec->frame));
    for(uint8_t pass = 0; pass < (fast ? 2U : 1U); pass++) {
        for(size_t i = 0; i < COUNT_OF(sync2); i++) *(*output)++ = sync2[i];
    }

    const bool test = config->message_mode == PagerTxMessageTestBeacon;
    const bool tone_only = !test && spec->length == 0U;
    uint32_t message_words[32];
    uint8_t message_word_count = 0;
    if(!test && !tone_only) {
        message_word_count = flex_tx_encode_alpha(
            spec->text,
            spec->length,
            spec->fragment,
            spec->continuation,
            spec->signature,
            spec->message_number,
            retrieval,
            message_words,
            COUNT_OF(message_words));
        if(message_word_count == 0U) return false;
    }

    uint32_t frame_words[FLEX_TX_PHASE_WORDS];
    for(uint8_t i = 0; i < FLEX_TX_PHASE_WORDS; i++) {
        frame_words[i] = (i & 1U) ? 0x00000000U : 0xFFFFFFFFU;
    }
    // Words 1 and 2 are the clock; BIW1 points the address past them. A test
    // beacon starts the address field where the vector field starts, which is
    // an empty one: sync and the clock, carrying no pages at all.
    const uint8_t address_start = 3U;
    const uint8_t vector_start = test ? address_start : 4U;
    const uint8_t message_start = 5U;
    frame_words[0] = flex_tx_biw1(address_start, vector_start);
    frame_words[1] = flex_tx_biw_time(spec->hour);
    frame_words[2] = flex_tx_biw_zone();
    if(!test) {
        frame_words[address_start] = flex_tx_short_address(config->ric);
        frame_words[vector_start] = tone_only ?
                                        flex_tx_tone_vector() :
                                        flex_tx_alpha_vector(message_start, message_word_count);
        for(uint8_t i = 0; i < message_word_count; i++) {
            frame_words[message_start + i] = message_words[i];
        }
    }

    for(uint8_t block = 0; block < 11U; block++) {
        if(fast) {
            flex_tx_write_woven(output, &frame_words[block * 8U]);
        } else {
            flex_tx_write_interleaved(output, &frame_words[block * 8U]);
        }
    }
    return true;
}

FlexTxEncoder* flex_tx_encoder_alloc(void) {
    return calloc(1, sizeof(FlexTxEncoder));
}

void flex_tx_encoder_free(FlexTxEncoder* encoder) {
    free(encoder);
}

bool flex_tx_encoder_prepare(FlexTxEncoder* encoder, const PocsagTxConfig* config) {
    furi_assert(encoder);
    furi_assert(config);

    const bool test = config->message_mode == PagerTxMessageTestBeacon;
    const size_t message_length = strnlen(config->message, sizeof(config->message));
    // An empty message is a tone-only page, not a refusal.
    if(config->protocol != PagerTxProtocolFlex ||
       message_length > PCSG_TX_MAX_MESSAGE_LENGTH || config->ric < FLEX_TX_SHORT_CAPCODE_MIN ||
       config->ric > FLEX_TX_SHORT_CAPCODE_MAX) {
        return false;
    }

    DateTime datetime;
    furi_hal_rtc_get_datetime(&datetime);
    uint8_t message_number =
        (datetime.second + datetime.minute * 7U + datetime.hour) & 0x3FU;
    if(message_number == 0U) message_number = 1U;

    // The cycle and frame are the clock, so they come from it rather than the
    // capcode: a frame number that disagreed with the clock words would be a
    // page timestamped to a slot it was not sent in.
    FlexTxFrameSpec spec = {
        .message_number = message_number,
        .signature = flex_tx_signature(config->message, message_length),
    };
    const size_t lead_bytes = test ? FLEX_TX_TEST_PREAMBLE_BYTES : FLEX_TX_ERS_BYTES;
    flex_tx_clock(&spec.cycle, &spec.frame, &spec.hour, FLEX_TX_PRE_FIW_MS(lead_bytes));

    // A tone-only page and a test beacon are one frame with nothing to split.
    const uint8_t fragments =
        (test || message_length == 0U) ?
            1U :
            (uint8_t)((message_length + FLEX_TX_FRAGMENT_CHARS - 1U) / FLEX_TX_FRAGMENT_CHARS);
    if(fragments > FLEX_TX_MAX_FRAGMENTS) return false;

    /*
     * One acquisition burst and one frame. The page carries the retrieval
     * flag set, marking it as the original delivery rather than a repeat,
     * which is what makes a single transmission a complete message: networks
     * send a page again for reliability, and the flag is how a receiver tells
     * a repeat apart, so repeating is not required of a sender.
     */
    uint8_t* output = encoder->bytes;
    if(test) {
        flex_tx_write_preamble(&output, FLEX_TX_TEST_PREAMBLE_BITS);
    } else {
        flex_tx_write_ers(&output, FLEX_TX_ERS_CYCLES);
    }
    encoder->frame_base = lead_bytes;
    for(uint8_t i = 0; i < fragments; i++) {
        const size_t offset = (size_t)i * FLEX_TX_FRAGMENT_CHARS;
        const size_t remaining = message_length - offset;
        spec.fragment = (i == 0U) ? 3U : (uint8_t)((i - 1U) % 3U);
        spec.continuation = (uint8_t)(i + 1U) < fragments;
        spec.text = config->message + offset;
        spec.length = remaining < FLEX_TX_FRAGMENT_CHARS ? remaining : FLEX_TX_FRAGMENT_CHARS;
        if(!flex_tx_write_frame(&output, config, &spec, true)) return false;
        flex_tx_clock_advance(&spec);
    }
    // The tail is a tone, not bits, so it is emitted by the yield.
    encoder->tail_index = 0;
    encoder->frames = fragments;
    encoder->byte_count = output - encoder->bytes;
    furi_check(encoder->byte_count <= sizeof(encoder->bytes));

    encoder->bit_index = 0;
    encoder->fast = config->flex_3200;
    encoder->inverted = config->inverted;
    // The async timer starts high and drops a leading low run, which would
    // cost the first burst bit. Only inverted polarity now starts low.
    encoder->lead_in_pending = config->inverted;
    encoder->tail_pending = true;
    return true;
}

static bool flex_tx_wire_bit(const FlexTxEncoder* encoder, size_t bit_index) {
    return (encoder->bytes[bit_index / 8U] >> (7U - (bit_index % 8U))) & 1U;
}

// Frames are a fixed size, so where a bit sits in one decides its rate. The
// tail is slow again: the decoder is back to hunting sync by then.
static bool flex_tx_bit_is_fast(const FlexTxEncoder* encoder, size_t bit_index) {
    if(!encoder->fast) return false;
    const size_t byte = bit_index / 8U;
    if(byte < encoder->frame_base) return false;
    const size_t into_frames = byte - encoder->frame_base;
    // Past the last frame is the flush tail, which is slow again.
    if(into_frames >= (size_t)encoder->frames * FLEX_TX_FAST_FRAME_BYTES) return false;
    return (into_frames % FLEX_TX_FAST_FRAME_BYTES) >= FLEX_TX_HEAD_BYTES;
}

static uint32_t flex_tx_bit_period(bool fast) {
    return 1000000U / (fast ? FLEX_TX_FAST_BAUD : FLEX_TX_BAUD);
}

LevelDuration flex_tx_encoder_yield(void* context) {
    furi_assert(context);
    FlexTxEncoder* encoder = context;
    if(encoder->lead_in_pending) {
        encoder->lead_in_pending = false;
        return level_duration_make(true, flex_tx_bit_period(false));
    }

    const size_t total_bits = encoder->byte_count * 8U;
    if(encoder->bit_index >= total_bits) {
        // 800 Hz, then a true 750 Hz, one half cycle per call. The frame's
        // last idle bit is low, so starting high keeps the alternation going.
        const uint16_t halves = FLEX_TX_TAIL_800_HALVES + FLEX_TX_TAIL_750_HALVES;
        if(encoder->tail_index < halves) {
            const bool level = (encoder->tail_index & 1U) == 0U;
            const uint32_t us = encoder->tail_index < FLEX_TX_TAIL_800_HALVES ?
                                    FLEX_TX_TAIL_800_US :
                                    FLEX_TX_TAIL_750_US;
            encoder->tail_index++;
            return level_duration_make(encoder->inverted ? !level : level, us);
        }
        if(encoder->tail_pending) {
            encoder->tail_pending = false;
            return level_duration_make(!encoder->inverted, flex_tx_bit_period(false));
        }
        return level_duration_reset();
    }

    // A run cannot straddle the rate change, so it ends there as well.
    const bool wire_bit = flex_tx_wire_bit(encoder, encoder->bit_index);
    const bool fast = flex_tx_bit_is_fast(encoder, encoder->bit_index);
    size_t run_bits = 1U;
    while(encoder->bit_index + run_bits < total_bits &&
          flex_tx_wire_bit(encoder, encoder->bit_index + run_bits) == wire_bit &&
          flex_tx_bit_is_fast(encoder, encoder->bit_index + run_bits) == fast) {
        run_bits++;
    }
    encoder->bit_index += run_bits;

    /*
     * Unlike POCSAG, FLEX bit 1 is the positive-deviation tone, and CC1101
     * asynchronous input 1 already selects positive deviation, so standard
     * polarity passes the bit straight through. This had POCSAG's inversion,
     * which made the two settings mean each other's labels.
     */
    const bool async_level = encoder->inverted ? !wire_bit : wire_bit;
    return level_duration_make(async_level, flex_tx_bit_period(fast) * run_bits);
}
