#pragma once

#include "pocsag_tx.h"

#include <furi_hal_gpio.h>

#define FLEX_TX_SHORT_CAPCODE_MIN 1U
#define FLEX_TX_SHORT_CAPCODE_MAX 1933312U
/** Long address set 1-2, supported by the receiver's capcode conversion. */
#define FLEX_TX_LONG_CAPCODE_MIN  2101249U
#define FLEX_TX_LONG_CAPCODE_MAX  1075843072U
#define FLEX_TX_BAUD              1600U
#define FLEX_TX_FAST_BAUD         3200U
/** A frame is 3000 bits at 1600 baud, so a slot on the grid is exact. */
#define FLEX_TX_FRAME_MS          1875U

typedef struct FlexTxEncoder FlexTxEncoder;

FlexTxEncoder* flex_tx_encoder_alloc(void);
void flex_tx_encoder_free(FlexTxEncoder* encoder);

/** Build a FLEX burst in the selected mode for short or long-address pages. */
bool flex_tx_encoder_prepare(FlexTxEncoder* encoder, const PocsagTxConfig* config);

/** Frames the prepared burst holds the air for, which is what decides when the
 *  next beacon burst is due. Valid until the encoder is freed. */
uint8_t flex_tx_encoder_frames(const FlexTxEncoder* encoder);

/*
 * Four-level FLEX needs the radio's frequency offset shifted between symbols, which
 * means writing one CC1101 register while it is transmitting. Firmware that
 * exports furi_hal_subghz_write_reg can do that; older firmware cannot, and
 * the symbol is looked up by name rather than linked, so this app still loads
 * there - it just does not offer the mode. See README.md.
 */
bool flex_tx_four_level_supported(void);

/** Phases this configuration's mode carries, in the order they go out. */
uint8_t flex_tx_mode_phases(const PocsagTxConfig* config, uint8_t order[4]);

/** Supported destination address; zero disables a phase and is not an address. */
bool flex_tx_capcode_valid(uint32_t capcode);

/** Clocks a prepared burst out as four-level symbols on `data`, from a thread
 *  of its own because the register writes need the SPI bus. The radio must
 *  already be in transmit. */
bool flex_tx_four_level_start(FlexTxEncoder* encoder, const GpioPin* data);
bool flex_tx_four_level_complete(void);
void flex_tx_four_level_stop(void);

/** Callback consumed by the Sub-GHz asynchronous transmitter. */
LevelDuration flex_tx_encoder_yield(void* context);
