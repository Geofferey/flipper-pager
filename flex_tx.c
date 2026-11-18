#include "flex_tx.h"

#include <furi.h>
#include <furi_hal.h>
#include <flipper_application/api_hashtable/api_hashtable.h>
#include <flipper_application/elf/elf_api_interface.h>

/*
 * furi_hal_subghz_write_reg(), resolved by name instead of linked against. An
 * unresolved import stops the whole app loading, so asking the firmware's own
 * symbol table at runtime is what lets one build serve both firmwares.
 */
typedef void (*FlexTxWriteReg)(uint8_t reg, uint8_t value);

// Exported by the firmware but not declared in any header it ships, so apps
// that want the symbol table declare it themselves, as the plugin examples do.
extern const ElfApiInterface* const firmware_api_interface;

static FlexTxWriteReg flex_tx_write_reg(void) {
    static FlexTxWriteReg resolved = NULL;
    static bool looked_up = false;
    if(!looked_up) {
        looked_up = true;
        Elf32_Addr address = 0;
        if(elf_resolve_from_hashtable(
               firmware_api_interface,
               elf_symbolname_hash("furi_hal_subghz_write_reg"),
               &address)) {
            resolved = (FlexTxWriteReg)address;
        }
    }
    return resolved;
}

bool flex_tx_four_level_supported(void) {
    return flex_tx_write_reg() != NULL;
}

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
 * two bursts and two frames this replaced. The ERS row sets it, so what is
 * sized here is the longest it can be asked for.
 */
#define FLEX_TX_ERS_CYCLE_BYTES 12U
// The frame proper, 3000 bits at 1600 baud = exactly 1.875 s. The flush tail
// is sent once after the last frame, not per frame, so consecutive frames stay
// on the network's frame grid.
#define FLEX_TX_FRAME_BYTES     375U
#define FLEX_TX_SYNC1_BYTES     14U
#define FLEX_TX_ERS_BYTES       (PCSG_TX_ERS_CYCLES_MAX * FLEX_TX_ERS_CYCLE_BYTES)
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
 * Where that captured preamble ends, which is the part worth keeping when the
 * ERS row asks for a different length: what the phase got right is how the
 * square wave runs into sync1, so a longer or shorter one is anchored at its
 * end and grows backwards. Phase repeats every 128 bits - 128 * 3 / 64 is an
 * even 6 - so the start is taken modulo that.
 */
#define FLEX_TX_TEST_PREAMBLE_END \
    (FLEX_TX_TEST_PREAMBLE_PHASE + FLEX_TX_TEST_PREAMBLE_BITS)
#define FLEX_TX_TEST_PREAMBLE_PERIOD 128U
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
#define FLEX_TX_HEAD_BYTES (FLEX_TX_SYNC1_BYTES + FLEX_TX_FIW_BYTES)

/*
 * What each FLEX mode is, in one place. A block is 160 ms in every one of them,
 * so the block size, the symbol period and the bits a symbol carries are not
 * independent: bytes * 8 / bits is the symbol count, and that times the symbol
 * period comes to 160 ms each time.
 *
 *   mode      marker  symbols/s  bits/symbol  block    payload
 *   1600/2    0x870C       1600            1  32 B   1600 bps, phase A
 *   3200/2    0x7B18       3200            1  64 B   3200 bps, phases A C
 *   3200/4    0xB068       1600            2  64 B   3200 bps, phases A B
 *   6400/4    0xDEA0       3200            2 128 B   6400 bps, phases A B C D
 *
 * Sync 2 is repeated so it occupies the same 25 ms whatever the rate, which is
 * one copy per 1600 bps of payload. Markers come from flex_modes[] in
 * protocols/flex.c, so the transmitter and this app's receiver cannot drift
 * apart on them.
 */
typedef struct {
    uint16_t marker;
    uint16_t symbol_half_us;
    uint8_t bits;
    uint8_t sync2_copies;
    uint16_t block_bytes;
    /*
     * Which phase supplies each successive on-air bit, and how many of them a
     * block position consumes. The phases a mode carries and the order they go
     * out in are the same fact, so one list covers both: 1600/2 is phase A
     * alone, 3200/2 alternates A and C, 3200/4 pairs A and B into one symbol,
     * and 6400/4 sends A and B then C and D. A block is 32 bytes per phase.
     */
    uint8_t order[4];
    uint8_t phases;
} FlexTxMode;

static FlexTxMode flex_tx_mode(const PocsagTxConfig* config) {
    if(config->flex_6400) return (FlexTxMode){0xDEA0U, 625U, 2U, 4U, 128U, {0, 1, 2, 3}, 4U};
    if(config->flex_4level) return (FlexTxMode){0xB068U, 1250U, 2U, 2U, 64U, {0, 1}, 2U};
    if(config->flex_3200) return (FlexTxMode){0x7B18U, 625U, 1U, 2U, 64U, {0, 2}, 2U};
    return (FlexTxMode){0x870CU, 1250U, 1U, 1U, 32U, {0}, 1U};
}

/*
 * The standard frame and phase embedding rule, TI SPRA193 section 2.9.2:
 * "bits 2 and 3 in order, specify phases 00, 01, 10, or 11 for phase 0,1,2,3
 * (a,b,c,d)", or equivalently Phase = (Integer(Addr/4)) Modulo 4.
 *
 * It is an allocation rule, not a decode rule: the operator picks a phase and
 * hands out a number that carries it, so sequential assignment spreads the
 * subscriber population over the phases on its own. Whether a given capcode
 * was handed out that way is recorded in its CAPCODE type letter, which is
 * never transmitted, so this holds only where the operator says it does. The
 * Phases row is where that is said.
 */
uint8_t flex_tx_capcode_slot(uint32_t capcode) {
    return (uint8_t)((capcode >> 2) & 3U);
}

static bool flex_tx_capcode_is_long(uint32_t capcode) {
    return capcode > FLEX_TX_SHORT_CAPCODE_MAX;
}

bool flex_tx_capcode_valid(uint32_t capcode) {
    if(capcode >= FLEX_TX_SHORT_CAPCODE_MIN && capcode <= FLEX_TX_SHORT_CAPCODE_MAX) {
        return true;
    }
    return capcode >= FLEX_TX_LONG_CAPCODE_MIN && capcode <= FLEX_TX_LONG_CAPCODE_MAX;
}

uint8_t flex_tx_mode_phases(const PocsagTxConfig* config, uint8_t order[4]) {
    const FlexTxMode mode = flex_tx_mode(config);
    for(uint8_t i = 0; i < mode.phases; i++) order[i] = mode.order[i];
    return mode.phases;
}

static uint16_t flex_tx_frame_bytes(const FlexTxMode* mode) {
    return (uint16_t)(FLEX_TX_HEAD_BYTES + FLEX_TX_SYNC2_BYTES * mode->sync2_copies +
                      11U * mode->block_bytes);
}

// The largest a frame gets, which is what the buffer has to hold: 6400/4.
#define FLEX_TX_MAX_FRAME_BYTES \
    (FLEX_TX_HEAD_BYTES + FLEX_TX_SYNC2_BYTES * 4U + 11U * 128U)

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
    (FLEX_TX_ERS_BYTES + FLEX_TX_MAX_FRAGMENTS * FLEX_TX_MAX_FRAME_BYTES)

// Where the periodic page sits: frame 7 of every 32. 32 frames is 60.000 s.
#define FLEX_TX_SEQ_FRAME_GROUP 32U
#define FLEX_TX_SEQ_FRAME       7U

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
    FlexTxMode mode;
    uint16_t frame_bytes;
    // Built here rather than on the stack: four phases of 88 words is 1.4 kB,
    // and the writer needs all of them at once.
    uint32_t phase_words[4][FLEX_TX_PHASE_WORDS];
    uint8_t half_us_remainder;
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

// Bit 15 is the roaming flag, which marks the frame as carried by a system a
// subscriber is visiting rather than its home one.
static uint32_t flex_tx_fiw(uint8_t cycle, uint8_t frame, bool roaming) {
    uint32_t word = ((uint32_t)(cycle & 0x0FU) << 4) |
                    ((uint32_t)(frame & 0x7FU) << 8) | ((uint32_t)roaming << 15);
    return flex_tx_encode_word(flex_tx_checksum(word));
}

static uint32_t flex_tx_biw1(uint8_t address_start, uint8_t vector_start) {
    uint32_t word = ((uint32_t)(address_start - 1U) & 0x03U) << 8;
    word |= ((uint32_t)vector_start & 0x3FU) << 10;
    return flex_tx_encode_word(flex_tx_checksum(word));
}

/*
 * Clock BIWs: word format 001 is the date, 010 the time and 101 the system
 * message carrying the zone. Field layouts are from the FLEX decoder
 * datasheets cited in README.md. flex_decode_clock() in protocols/flex.c reads
 * the hour and the zone and takes minutes and seconds from frame timing
 * instead.
 *
 * The time anchors to frame 0 of the cycle rather than to the frame carrying
 * it: a pager adds frame * 1.875 s itself. So the minute is the cycle's own
 * four-minute boundary, and the seconds field stays zero. Fifteen cycles make
 * an hour exactly, so a cycle never straddles one and the hour holds for every
 * frame in it.
 */
static uint32_t flex_tx_biw_time(uint8_t hour, uint8_t minute) {
    uint32_t word = (2U << 4) | ((uint32_t)(hour & 31U) << 7) |
                    ((uint32_t)(minute & 63U) << 12);
    return flex_tx_encode_word(flex_tx_checksum(word));
}

static uint32_t flex_tx_biw_date(uint8_t year_field, uint8_t month, uint8_t day) {
    uint32_t word = (1U << 4) | ((uint32_t)(year_field & 31U) << 7) |
                    ((uint32_t)(day & 31U) << 12) | ((uint32_t)(month & 15U) << 17);
    return flex_tx_encode_word(flex_tx_checksum(word));
}

/*
 * The system message word, whose s3 to s0 subfield is 0101. 0100 would also
 * claim an Operator Messaging Address in the frame, which this never sends.
 * Above that sit z4 to z0, the time zone, then L for standard against
 * daylight, an unused bit, and S2 to S0, an accurate-seconds trim left at zero
 * because the frame grid is already clock-synchronised.
 *
 * The zone is always the standard-time offset, whatever the season, so L is
 * what moves. That is also why the zone table has no code for daylight in
 * Newfoundland or Adelaide: it never needs one.
 */
static uint32_t flex_tx_biw_zone(uint8_t utc_code, bool dst, uint8_t ext_sec) {
    uint32_t word = (5U << 4) | (5U << 7) | ((uint32_t)(utc_code & 31U) << 11);
    // L is word bit 16: set is standard time, clear is daylight.
    if(!dst) word |= 1UL << 16;
    // S2 to S0, bits 18-20: the seconds trim, zero when the frame grid is
    // already clock-synchronised, which it is unless a network says otherwise.
    word |= ((uint32_t)(ext_sec & 7U)) << 18;
    return flex_tx_encode_word(flex_tx_checksum(word));
}

/*
 * System identification, which says where the transmission comes from rather
 * than when. SSID1 is word format 000, carrying the coverage zone and the
 * local identity; SSID2 is 111, carrying the country code and a traffic
 * management flag this never sets. Field positions follow Mayhem's
 * flex_biw_ssid1() and flex_biw_ssid2(), cited in README.md.
 */
static uint32_t flex_tx_biw_ssid1(uint16_t local_id, uint8_t coverage_zone) {
    const uint32_t word = ((uint32_t)(coverage_zone & 31U) << 7) |
                          ((uint32_t)(local_id & 0x1FFU) << 12);
    return flex_tx_encode_word(flex_tx_checksum(word));
}

static uint32_t flex_tx_biw_ssid2(uint16_t country_code) {
    const uint32_t word = (7U << 4) | ((uint32_t)(country_code & 0x3FFU) << 11);
    return flex_tx_encode_word(flex_tx_checksum(word));
}

/*
 * Five bits counting from 1994, so the field names a year in 1994-2025 and
 * nothing past it. The era is not carried anywhere, so a receiver cannot
 * resolve one, and wrapping the count would simply name a year in that window
 * at random - 2026 would arrive as 1994.
 *
 * A later year is therefore sent as its calendar twin: the year in the window
 * with the same leap status and the same weekday for 1 January, so every date
 * in it still lands on the day it really falls on. 2026 goes out as 2015.
 * PortaPack Mayhem's flex_equiv_year() does the same and says so on screen.
 */
static bool flex_tx_year_is_leap(uint16_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || (year % 400U == 0U);
}

static uint8_t flex_tx_year_jan1_dow(uint16_t year) {
    return (uint8_t)((year + year / 4U - year / 100U + year / 400U + 1U) % 7U);
}

static uint8_t flex_tx_year_field(uint16_t year) {
    if(year >= 1994U && year <= 2025U) return (uint8_t)(year - 1994U);
    const bool leap = flex_tx_year_is_leap(year);
    const uint8_t dow = flex_tx_year_jan1_dow(year);
    // Fourteen combinations of leap and weekday exist and a 32 year window
    // holds every one, so the search always finds a twin.
    for(uint16_t y = 2025U; y >= 1994U; y--) {
        if(flex_tx_year_is_leap(y) == leap && flex_tx_year_jan1_dow(y) == dow) {
            return (uint8_t)(y - 1994U);
        }
    }
    return (uint8_t)((year - 1994U) & 0x1FU);
}

// Where the frame lands in the hour once it is actually on the air, and the
// date it lands on.
static void flex_tx_clock(
    uint8_t* cycle,
    uint8_t* frame,
    uint8_t* hour,
    DateTime* on_air,
    uint32_t pre_fiw_ms) {
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
    if(hours >= 24U) {
        // The lead carried it past midnight, so the date goes with it.
        datetime_timestamp_to_datetime(
            datetime_datetime_to_timestamp(&datetime) + 86400U, &datetime);
    }

    *cycle = ms / FLEX_TX_CYCLE_MS;
    *frame = (ms % FLEX_TX_CYCLE_MS) / FLEX_TX_FRAME_MS;
    *hour = hours % 24U;
    *on_air = datetime;
}

/*
 * A long address is two words. Inverting what flex_decode_phase() in
 * protocols/flex.c reads back:
 *
 *   capcode = first + ((second ^ 0x1FFFFF) << 15) + 0x1F9000
 *
 * Only some first words mark an address as long, and taking the low fifteen
 * bits for it always lands below 0x8001, which is one of those ranges - so the
 * split picks itself. A zero word is what a receiver treats as an empty slot,
 * so when the low bits come to zero the word borrows 0x8000 from the high part
 * instead, which is still inside the same range.
 */
static void flex_tx_long_address(uint32_t capcode, uint32_t* first, uint32_t* second) {
    const uint32_t value = capcode - 0x1F9000UL;
    uint32_t low = value & 0x7FFFUL;
    uint32_t high = value >> 15;
    if(low == 0U) {
        low = 0x8000UL;
        high--;
    }
    *first = flex_tx_encode_word(low);
    *second = flex_tx_encode_word((high ^ 0x001FFFFFUL) & FLEX_TX_DATA_MASK);
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
    const uint32_t start =
        (FLEX_TX_TEST_PREAMBLE_END + FLEX_TX_TEST_PREAMBLE_PERIOD -
         (total_bits % FLEX_TX_TEST_PREAMBLE_PERIOD)) %
        FLEX_TX_TEST_PREAMBLE_PERIOD;
    uint8_t byte = 0;
    for(uint16_t i = 0; i < total_bits; i++) {
        // 37.5 Hz at 1600 baud: half a period is 64/3 bits.
        const uint32_t phase = (uint32_t)i + start;
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
 * One block of every mode. Each phase the mode carries is interleaved on its
 * own, then their bits go out in the mode's order, one phase's bit after
 * another for each position in the block. That is all the difference between
 * the modes: phase A alone gives 32 bytes, A and C or A and B give 64, and all
 * four give 128, which is what block_bytes says.
 *
 * Replaces a writer per mode. They only ever differed in which phases they
 * emitted and in what order, and the ones with spare phases filled them with
 * the interleaver's idle pattern - which this does too, by being handed phases
 * that carry nothing but their own block information word.
 */
static void flex_tx_write_block(
    uint8_t** output,
    const uint32_t phase_words[4][FLEX_TX_PHASE_WORDS],
    uint8_t block,
    const FlexTxMode* mode) {
    uint8_t woven[4][32];
    for(uint8_t p = 0; p < mode->phases; p++) {
        uint8_t* cursor = woven[p];
        flex_tx_write_interleaved(&cursor, &phase_words[mode->order[p]][block * 8U]);
    }

    uint8_t byte = 0;
    uint8_t filled = 0;
    for(uint8_t i = 0; i < 32U; i++) {
        for(uint8_t bit = 0; bit < 8U; bit++) {
            for(uint8_t p = 0; p < mode->phases; p++) {
                byte = (uint8_t)((byte << 1) | ((woven[p][i] >> (7U - bit)) & 1U));
                if(++filled == 8U) {
                    *(*output)++ = byte;
                    byte = 0;
                    filled = 0;
                }
            }
        }
    }
}

typedef struct {
    uint8_t cycle;
    uint8_t frame;
    uint8_t hour;
    uint8_t year_field;
    uint8_t month;
    uint8_t day;
    uint8_t message_number;
    uint8_t fragment;
    bool continuation;
    uint8_t signature;
    const char* text;
    size_t length;
    // Sync and the clock, addressing nobody. A burst can mix idle frames with
    // a page, so it is a property of the frame rather than of the mode.
    bool idle;
} FlexTxFrameSpec;

// Steps to the next frame, carrying into the cycle and the hour.
static void flex_tx_clock_advance(FlexTxFrameSpec* spec) {
    if(++spec->frame < 128U) return;
    spec->frame = 0U;
    if(++spec->cycle < 15U) return;
    spec->cycle = 0U;
    spec->hour = (spec->hour + 1U) % 24U;
}

/*
 * The block information words that precede the address, in Mayhem's order:
 * BIW1 carries the address offset in two bits, so word 4 is as far out as the
 * address can start and only three of these fit. Three or fewer enabled is
 * therefore word for word what Mayhem's build_frame() sends.
 */
typedef enum {
    FlexTxBiwSsid1,
    FlexTxBiwTime,
    FlexTxBiwDate,
    FlexTxBiwZone,
    FlexTxBiwSsid2,
    FlexTxBiwCount,
} FlexTxBiwKind;

#define FLEX_TX_BIW_SLOTS 3U

/*
 * Which of them this frame carries. Past three the time is pinned, because a
 * receiver meeting a single frame still has to be able to set its clock, and
 * the rest take turns by frame number so that none is starved - a network
 * spreads its system words over the cycle the same way. The order above is
 * kept whatever the frame, so only the choice rotates, not the layout.
 */
static void flex_tx_biw_select(
    const PocsagTxConfig* config,
    uint8_t frame,
    bool chosen[FlexTxBiwCount]) {
    bool enabled[FlexTxBiwCount];
    enabled[FlexTxBiwSsid1] = config->send_ssid1;
    enabled[FlexTxBiwTime] = (config->clock_biws & PCSG_TX_CLOCK_TIME) != 0U;
    enabled[FlexTxBiwDate] = (config->clock_biws & PCSG_TX_CLOCK_DATE) != 0U;
    // The zone says which offset the hour is in, so it goes where the time does.
    enabled[FlexTxBiwZone] = enabled[FlexTxBiwTime];
    enabled[FlexTxBiwSsid2] = config->send_ssid2;

    uint8_t count = 0U;
    for(uint8_t i = 0; i < FlexTxBiwCount; i++) {
        chosen[i] = false;
        if(enabled[i]) count++;
    }
    if(count <= FLEX_TX_BIW_SLOTS) {
        memcpy(chosen, enabled, sizeof(enabled));
        return;
    }

    uint8_t slots = FLEX_TX_BIW_SLOTS;
    if(enabled[FlexTxBiwTime]) {
        chosen[FlexTxBiwTime] = true;
        enabled[FlexTxBiwTime] = false;
        slots--;
    }
    uint8_t pool[FlexTxBiwCount];
    uint8_t pool_count = 0U;
    for(uint8_t i = 0; i < FlexTxBiwCount; i++) {
        if(enabled[i]) pool[pool_count++] = i;
    }
    // More contenders than slots is what got us here, so the walk always has
    // somewhere to go and cannot pick the same word twice.
    for(uint8_t i = 0; i < slots; i++) {
        chosen[pool[(frame + i) % pool_count]] = true;
    }
}

static uint8_t flex_tx_biw_words(
    const PocsagTxConfig* config,
    const FlexTxFrameSpec* spec,
    uint32_t words[FLEX_TX_BIW_SLOTS]) {
    bool chosen[FlexTxBiwCount];
    flex_tx_biw_select(config, spec->frame, chosen);

    uint8_t count = 0U;
    for(uint8_t i = 0; i < FlexTxBiwCount && count < FLEX_TX_BIW_SLOTS; i++) {
        if(!chosen[i]) continue;
        switch(i) {
        case FlexTxBiwSsid1:
            words[count++] = flex_tx_biw_ssid1(config->local_id, config->coverage_zone);
            break;
        case FlexTxBiwTime:
            words[count++] = flex_tx_biw_time(spec->hour, (uint8_t)(spec->cycle * 4U));
            break;
        case FlexTxBiwDate:
            words[count++] = flex_tx_biw_date(spec->year_field, spec->month, spec->day);
            break;
        case FlexTxBiwZone:
            words[count++] =
                flex_tx_biw_zone(config->utc_code, config->dst, config->ext_sec);
            break;
        default:
            words[count++] = flex_tx_biw_ssid2(config->country_code);
            break;
        }
    }
    return count;
}

static bool flex_tx_write_frame(
    uint8_t** output,
    FlexTxEncoder* encoder,
    const PocsagTxConfig* config,
    const FlexTxFrameSpec* spec,
    bool retrieval) {
    static const uint8_t sync1[] = {
        0xAA, 0xAA, 0xAA, 0xAA, 0x78, 0xF3, 0x59,
        0x39, 0x55, 0x55, 0x87, 0x0C, 0xA6, 0xC6,
    };
    static const uint8_t sync2[] = {0xAE, 0xD8, 0x45, 0x12, 0x7B};
    const FlexTxMode mode = flex_tx_mode(config);

    uint8_t* const sync1_out = *output;
    for(size_t i = 0; i < COUNT_OF(sync1); i++) *(*output)++ = sync1[i];
    /*
     * The mode marker, in place of 1600/2's 0x870C: bytes 10 and 11 carry it
     * and bytes 4 and 5 its inverse. A receiver reads this before anything
     * else, so it must describe both the symbol rate and the number of levels.
     * Codes from flex_modes[] in protocols/flex.c.
     */
    sync1_out[10] = (uint8_t)(mode.marker >> 8);
    sync1_out[11] = (uint8_t)mode.marker;
    sync1_out[4] = (uint8_t)~sync1_out[10];
    sync1_out[5] = (uint8_t)~sync1_out[11];
    flex_tx_write_word(output, flex_tx_fiw(spec->cycle, spec->frame, config->roaming));
    for(uint8_t pass = 0; pass < mode.sync2_copies; pass++) {
        for(size_t i = 0; i < COUNT_OF(sync2); i++) *(*output)++ = sync2[i];
    }

    const bool idle = spec->idle;
    /*
     * Encoded per phase rather than once for all of them: with per-phase
     * pages each carries its own text, and a tone-only page is then a
     * property of the phase too - one address can be sent a message while
     * another beside it is only beeped.
     */
    uint32_t message_words[32];
    uint8_t message_word_count = 0;
    bool tone_only = !idle && spec->length == 0U;
    if(!idle && !tone_only && !config->per_phase_text) {
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

    uint32_t(*phase_words)[FLEX_TX_PHASE_WORDS] = encoder->phase_words;
    for(uint8_t p = 0; p < 4U; p++) {
        for(uint8_t i = 0; i < FLEX_TX_PHASE_WORDS; i++) {
            phase_words[p][i] = (i & 1U) ? 0x00000000U : 0xFFFFFFFFU;
        }
    }
    // Every carried phase with a capcode set gets the page addressed to it;
    // the rest carry sync and the clock and address nobody.
    /*
     * The block information words come first and BIW1 points the address past
     * however many of them there are. An idle frame starts the address field
     * where the vector field starts, which is an empty one: sync and the
     * clock, carrying no pages at all.
     */
    uint32_t biw_words[FLEX_TX_BIW_SLOTS];
    const uint8_t biw_count = flex_tx_biw_words(config, spec, biw_words);

    const uint8_t address_start = (uint8_t)(1U + biw_count);
    for(uint8_t p = 0; p < mode.phases; p++) {
        const uint8_t index = mode.order[p];
        const uint32_t capcode = config->ric[index];

        if(config->per_phase_text && !idle) {
            const char* text = config->page_text[index];
            const uint8_t length = (uint8_t)strnlen(text, PCSG_TX_PAGE_CHARS);
            tone_only = length == 0U;
            message_word_count = 0;
            if(!tone_only) {
                message_word_count = flex_tx_encode_alpha(
                    text,
                    length,
                    spec->fragment,
                    spec->continuation,
                    flex_tx_signature(text, length),
                    spec->message_number,
                    retrieval,
                    message_words,
                    COUNT_OF(message_words));
                // Longer than one frame's phase will hold.
                if(message_word_count == 0U) return false;
            }
        }
        // Every phase on the air gets its own block information word and the
        // clock, so a pager listening on any of them finds a valid frame.
        const bool carries = !idle && flex_tx_capcode_valid(capcode);
        // Long addresses consume two address and two vector slots per phase.
        const bool long_address = carries && flex_tx_capcode_is_long(capcode);
        const uint8_t words = long_address ? 2U : 1U;
        const uint8_t vector_start = carries ? (uint8_t)(address_start + words) : address_start;
        const uint8_t message_start = (uint8_t)(vector_start + words);
        phase_words[index][0] = flex_tx_biw1(address_start, vector_start);
        for(uint8_t i = 0; i < biw_count; i++) {
            phase_words[index][1U + i] = biw_words[i];
        }
        if(!carries) continue;
        if(long_address) {
            flex_tx_long_address(
                capcode,
                &phase_words[index][address_start],
                &phase_words[index][address_start + 1U]);
        } else {
            phase_words[index][address_start] = flex_tx_short_address(capcode);
        }
        const uint32_t vector = tone_only ?
                                    flex_tx_tone_vector() :
                                    flex_tx_alpha_vector(message_start, message_word_count);
        phase_words[index][vector_start] = vector;
        // The second vector slot holds the alpha header (counted in length),
        // or five BCD fill digits for tone-only pages. See protocols/flex.c.
        uint8_t first = 0U;
        if(long_address) {
            phase_words[index][vector_start + 1U] =
                tone_only ? flex_tx_encode_word(0xCCCCCU) : message_words[0];
            first = tone_only ? 0U : 1U;
        }
        for(uint8_t i = first; i < message_word_count; i++) {
            phase_words[index][message_start + i - first] = message_words[i];
        }
    }

    for(uint8_t block = 0; block < 11U; block++) {
        flex_tx_write_block(output, phase_words, block, &mode);
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

    const bool beacon = pager_tx_mode_is_beacon(config->message_mode);
    const size_t message_length = strnlen(config->message, sizeof(config->message));
    // An empty message is a tone-only page, not a refusal.
    if(config->protocol != PagerTxProtocolFlex ||
       message_length > PCSG_TX_MAX_MESSAGE_LENGTH) {
        return false;
    }
    // At least one phase this mode carries has to have somewhere to send to.
    {
        uint8_t order[4];
        const uint8_t phases = flex_tx_mode_phases(config, order);
        bool any = beacon;
        for(uint8_t i = 0; i < phases && !any; i++) {
            any = flex_tx_capcode_valid(config->ric[order[i]]);
        }
        if(!any) return false;
    }

    // Auto keeps what the app did before the row existed: a number off the
    // clock, so two transmissions a moment apart do not look like one message
    // repeated. A set number is sent as it stands and the app counts it on.
    uint8_t message_number = config->msg_number;
    if(message_number > PCSG_TX_MSG_NUMBER_MAX) {
        DateTime datetime;
        furi_hal_rtc_get_datetime(&datetime);
        message_number = (datetime.second + datetime.minute * 7U + datetime.hour) & 0x3FU;
        if(message_number == 0U) message_number = 1U;
    }

    // The cycle and frame are the clock, so they come from it rather than the
    // capcode: a frame number that disagreed with the clock words would be a
    // page timestamped to a slot it was not sent in.
    FlexTxFrameSpec spec = {
        .message_number = message_number,
        .signature = flex_tx_signature(config->message, message_length),
    };
    /*
     * The ERS row is a lead-in duration, and the two modes lead in differently:
     * a page with the ERS code, a beacon with the square wave the network was
     * captured sending. Default leaves each at the length it was measured or
     * chosen at - 24 cycles ahead of a page, 100 ms ahead of a beacon - and a
     * set value scales whichever one this burst uses.
     */
    const uint8_t ers_cycles = config->ers_cycles > PCSG_TX_ERS_CYCLES_MAX ?
                                   0U :
                                   config->ers_cycles;
    const uint8_t page_cycles = ers_cycles ? ers_cycles : PCSG_TX_ERS_CYCLES_DEFAULT;
    const size_t lead_bytes =
        beacon ? (ers_cycles ? (size_t)ers_cycles * FLEX_TX_ERS_CYCLE_BYTES :
                               FLEX_TX_TEST_PREAMBLE_BYTES) :
                 (size_t)page_cycles * FLEX_TX_ERS_CYCLE_BYTES;
    DateTime on_air;
    flex_tx_clock(
        &spec.cycle, &spec.frame, &spec.hour, &on_air, FLEX_TX_PRE_FIW_MS(lead_bytes));
    spec.year_field = flex_tx_year_field(on_air.year);
    spec.month = on_air.month;
    spec.day = on_air.day;

    /*
     * A beacon burst is a single idle frame, except that Beacon + Seq carries
     * the periodic page in frame 7 of every 32, which is once every 60.000 s.
     * The measured network sends that page as the middle frame of frames 6, 7
     * and 8 keyed back to back: one preamble and one tail around 5.625 s of
     * frames, holding the air through the odd slot the cadence leaves quiet.
     * Bursts step two slots at a time, so exactly one of 6 and 7 falls in each
     * group of 32 whichever parity the beacon started on.
     */
    uint8_t frames;
    uint8_t page_frame = 0U;
    if(beacon) {
        const uint8_t slot = spec.frame % FLEX_TX_SEQ_FRAME_GROUP;
        const bool page = config->message_mode == PagerTxMessageBeaconSeq &&
                          (slot == FLEX_TX_SEQ_FRAME - 1U || slot == FLEX_TX_SEQ_FRAME);
        frames = page ? 3U : 1U;
        page_frame = page ? 1U : frames;
    } else if(config->per_phase_text) {
        // Each phase carries its own page, so there is no one length to split
        // on: they would have to run to different numbers of frames.
        frames = 1U;
    } else {
        // A tone-only page is one frame with nothing to split.
        frames = (message_length == 0U) ?
                     1U :
                     (uint8_t)((message_length + FLEX_TX_FRAGMENT_CHARS - 1U) /
                               FLEX_TX_FRAGMENT_CHARS);
    }
    if(frames > FLEX_TX_MAX_FRAGMENTS) return false;

    /*
     * One acquisition burst and one frame. The page carries the retrieval
     * flag set, marking it as the original delivery rather than a repeat,
     * which is what makes a single transmission a complete message: networks
     * send a page again for reliability, and the flag is how a receiver tells
     * a repeat apart, so repeating is not required of a sender.
     */
    uint8_t* output = encoder->bytes;
    if(beacon) {
        flex_tx_write_preamble(&output, (uint16_t)(lead_bytes * 8U));
    } else {
        flex_tx_write_ers(&output, page_cycles);
    }
    encoder->frame_base = lead_bytes;
    for(uint8_t i = 0; i < frames; i++) {
        spec.idle = beacon && i != page_frame;
        // A beacon's page is a whole message in one frame, so it reads from
        // the start of the text wherever in the burst that frame sits.
        const uint8_t part = beacon ? 0U : i;
        const size_t offset = (size_t)part * FLEX_TX_FRAGMENT_CHARS;
        const size_t remaining = message_length - offset;
        spec.fragment = (part == 0U) ? 3U : (uint8_t)((part - 1U) % 3U);
        spec.continuation = !beacon && (uint8_t)(part + 1U) < frames;
        spec.text = config->message + offset;
        spec.length = remaining < FLEX_TX_FRAGMENT_CHARS ? remaining : FLEX_TX_FRAGMENT_CHARS;
        if(!flex_tx_write_frame(&output, encoder, config, &spec, true)) return false;
        flex_tx_clock_advance(&spec);
    }
    // The tail is a tone, not bits, so it is emitted by the yield.
    encoder->tail_index = 0;
    encoder->frames = frames;
    encoder->byte_count = output - encoder->bytes;
    furi_check(encoder->byte_count <= sizeof(encoder->bytes));

    encoder->bit_index = 0;
    encoder->mode = flex_tx_mode(config);
    encoder->frame_bytes = flex_tx_frame_bytes(&encoder->mode);
    encoder->half_us_remainder = 0U;
    encoder->inverted = config->inverted;
    // The async timer starts high and drops a leading low run, which would
    // cost the first burst bit. Only inverted polarity now starts low.
    encoder->lead_in_pending = config->inverted;
    encoder->tail_pending = true;
    return true;
}

uint8_t flex_tx_encoder_frames(const FlexTxEncoder* encoder) {
    furi_assert(encoder);
    return encoder->frames;
}

static bool flex_tx_wire_bit(const FlexTxEncoder* encoder, size_t bit_index) {
    return (encoder->bytes[bit_index / 8U] >> (7U - (bit_index % 8U))) & 1U;
}

// Frames are a fixed size, so where a bit sits in one decides its rate. The
// tail is slow again: the decoder is back to hunting sync by then.
static bool flex_tx_bit_is_payload(const FlexTxEncoder* encoder, size_t bit_index) {
    const size_t byte = bit_index / 8U;
    if(byte < encoder->frame_base) return false;
    const size_t into_frames = byte - encoder->frame_base;
    // Past the last frame is the flush tail, which is slow again.
    if(into_frames >= (size_t)encoder->frames * encoder->frame_bytes) return false;
    return (into_frames % encoder->frame_bytes) >= FLEX_TX_HEAD_BYTES;
}

static bool flex_tx_bit_is_fast(const FlexTxEncoder* encoder, size_t bit_index) {
    return encoder->mode.symbol_half_us == 625U && flex_tx_bit_is_payload(encoder, bit_index);
}

static uint32_t flex_tx_bit_period(bool fast) {
    return 1000000U / (fast ? FLEX_TX_FAST_BAUD : FLEX_TX_BAUD);
}

/*
 * Four-level transmission.
 *
 * The CC1101 has no four-level mode, so the four deviations are built from a
 * two-level modulator plus the synthesiser's frequency offset. DEVIATN is set
 * to the midpoint of the two FLEX levels and FSCTRL0 steps the carrier by half
 * their difference, which puts the data pin in charge of the sign and one
 * register bit in charge of inner against outer:
 *
 *   DEVIATN  M=0 E=1  = (26 MHz / 2^17) * 8 * 2 = 3173.8 Hz
 *   FSCTRL0  +/-1 LSB = 26 MHz / 2^14          = 1586.9 Hz
 *   together                                   = +/-4760.7 and +/-1586.9 Hz
 *
 * against FLEX's +/-4800 and +/-1600, so under one percent on both. It is real
 * FSK, not a dithered approximation, so the occupied bandwidth is the same as
 * any other FLEX transmitter - which matters on 12.5 kHz channels.
 *
 * The decoder's own definitions give the mapping. flex_read_data_symbol() in
 * protocols/flex.c takes bit_a = symbol > 1 and bit_b = symbol is 1 or 2, so
 * symbols 0..3 run from the lowest deviation up. Inverting that:
 *
 *   bit_a bit_b  symbol  pin   FSCTRL0
 *     0     0      0     low    -1      -4760.7 Hz
 *     0     1      1     low    +1      -1586.9 Hz
 *     1     1      2     high   -1      +1586.9 Hz
 *     1     0      3     high   +1      +4760.7 Hz
 *
 * which is pin = bit_a and FSCTRL0 positive when bit_a differs from bit_b. A
 * two-level bit is bit_b = 0, so the head rides the outer levels and sync1
 * looks exactly as it does in any other mode.
 *
 * This cannot run from the async transmitter: its callback is the DMA
 * interrupt, and writing a register means taking the SPI bus. So the symbols
 * are clocked out by a thread of our own instead.
 */
#define FLEX_TX_CC1101_FSCTRL0 0x0CU
#define FLEX_TX_CC1101_DEVIATN 0x15U
#define FLEX_TX_DEVIATN_MID    0x10U // DEVIATION_E = 1, DEVIATION_M = 0
// What pocsag_tx_preset in pocsag_tx.c sets DEVIATN to, restored after a
// four-level burst. FSCTRL0 is in no preset at all, so unless it is put back
// here nothing clears it and the next transmission starts off frequency.
#define FLEX_TX_DEVIATN_PRESET 0x13U

typedef struct {
    FuriThread* thread;
    FlexTxEncoder* encoder;
    const GpioPin* data;
    volatile bool running;
    volatile bool finished;
} FlexTxFourLevel;

static FlexTxFourLevel flex_tx_four_level = {0};

static int32_t flex_tx_four_level_thread(void* context) {
    FlexTxFourLevel* state = context;
    const FlexTxWriteReg write_reg = flex_tx_write_reg();
    FlexTxEncoder* encoder = state->encoder;
    const size_t total_bits = encoder->byte_count * 8U;

    /*
     * There is no sub-millisecond sleep, so the wait between symbols is a spin
     * and this thread owns the processor for the whole burst. Highest rather
     * than Isr: Isr sits above the kernel's own deferred work, which is no
     * place to spend two seconds. Everything else, the display and the
     * notification service included, stops until the burst ends.
     */
    furi_thread_set_current_priority(FuriThreadPriorityHighest);

    // DEVIATN and the starting offset are set by _start() before this thread
    // exists: the carrier is already up by then, so leaving them until the
    // thread is scheduled put the first symbols out at the preset's deviation.
    uint8_t offset = 0U;
    size_t bit = 0;
    uint16_t tail = 0U;
    const uint16_t tail_halves = FLEX_TX_TAIL_800_HALVES + FLEX_TX_TAIL_750_HALVES;
    const uint32_t ticks_per_us = furi_hal_cortex_instructions_per_microsecond();
    FuriHalCortexTimer timer = furi_hal_cortex_timer_get(0U);
    while(state->running && (bit < total_bits || tail <= tail_halves)) {
        bool bit_a;
        bool bit_b = false;
        uint16_t half_us = 1250U;
        if(bit < total_bits) {
            const bool payload = flex_tx_bit_is_payload(encoder, bit);
            bit_a = flex_tx_wire_bit(encoder, bit);
            const uint8_t bits = payload ? encoder->mode.bits : 1U;
            if(bits == 2U) bit_b = flex_tx_wire_bit(encoder, bit + 1U);
            if(payload) half_us = encoder->mode.symbol_half_us;
            bit += bits;
        } else {
            bit_a = (tail & 1U) == 0U;
            if(tail == tail_halves) bit_a = true;
            if(tail >= FLEX_TX_TAIL_800_HALVES && tail < tail_halves) {
                half_us = FLEX_TX_TAIL_750_US * 2U;
            }
            tail++;
        }

        const bool pin = encoder->inverted ? !bit_a : bit_a;
        const uint8_t want = (pin != bit_b) ? 0x01U : 0xFFU;
        if(want != offset) {
            offset = want;
            write_reg(FLEX_TX_CC1101_FSCTRL0, offset);
        }
        furi_hal_gpio_write(state->data, pin);

        // Absolute cycle deadlines retain 312.5 us and exclude loop overhead.
        timer.value = ticks_per_us * half_us / 2U;
        furi_hal_cortex_timer_wait(timer);
        timer.start += timer.value;
    }

    write_reg(FLEX_TX_CC1101_FSCTRL0, 0x00U);
    state->finished = true;
    return 0;
}

// Undo everything the burst changed, so what runs next - another mode, the
// receiver, any other app - gets the radio the preset describes.
static void flex_tx_four_level_restore(void) {
    const FlexTxWriteReg write_reg = flex_tx_write_reg();
    if(!write_reg) return;
    write_reg(FLEX_TX_CC1101_FSCTRL0, 0x00U);
    write_reg(FLEX_TX_CC1101_DEVIATN, FLEX_TX_DEVIATN_PRESET);
}

bool flex_tx_four_level_start(FlexTxEncoder* encoder, const GpioPin* data) {
    furi_assert(encoder);
    const FlexTxWriteReg write_reg = flex_tx_write_reg();
    if(!write_reg || !data || flex_tx_four_level.thread || encoder->mode.bits != 2U ||
       !encoder->byte_count) {
        return false;
    }

    // Synchronously, because the carrier is already up: left to the thread
    // these land only once it is scheduled, and the symbols clocked out in
    // between go at the preset's deviation with whatever offset was there.
    write_reg(FLEX_TX_CC1101_DEVIATN, FLEX_TX_DEVIATN_MID);
    write_reg(FLEX_TX_CC1101_FSCTRL0, 0x00U);

    flex_tx_four_level.encoder = encoder;
    flex_tx_four_level.data = data;
    flex_tx_four_level.running = true;
    flex_tx_four_level.finished = false;
    flex_tx_four_level.thread = furi_thread_alloc_ex(
        "FlexTx4", 1024, flex_tx_four_level_thread, &flex_tx_four_level);
    if(!flex_tx_four_level.thread) {
        flex_tx_four_level_restore();
        return false;
    }
    furi_thread_start(flex_tx_four_level.thread);
    return true;
}

bool flex_tx_four_level_complete(void) {
    return flex_tx_four_level.thread && flex_tx_four_level.finished;
}

void flex_tx_four_level_stop(void) {
    if(!flex_tx_four_level.thread) return;
    flex_tx_four_level.running = false;
    furi_thread_join(flex_tx_four_level.thread);
    furi_thread_free(flex_tx_four_level.thread);
    flex_tx_four_level.thread = NULL;
    // The thread clears the offset on its way out, but not when it was torn
    // down early, and never the deviation. This is the one path every burst
    // leaves by, so the radio is put back here.
    flex_tx_four_level_restore();
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
    const uint32_t half_us = (fast ? 625U : 1250U) * run_bits + encoder->half_us_remainder;
    encoder->half_us_remainder = half_us & 1U;
    return level_duration_make(async_level, half_us / 2U);
}
