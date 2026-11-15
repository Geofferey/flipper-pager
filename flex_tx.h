#pragma once

#include "pocsag_tx.h"

#define FLEX_TX_SHORT_CAPCODE_MIN 1U
#define FLEX_TX_SHORT_CAPCODE_MAX 1933312U
#define FLEX_TX_BAUD              1600U
#define FLEX_TX_FAST_BAUD         3200U
/** A frame is 3000 bits at 1600 baud, so a slot on the grid is exact. */
#define FLEX_TX_FRAME_MS          1875U

typedef struct FlexTxEncoder FlexTxEncoder;

FlexTxEncoder* flex_tx_encoder_alloc(void);
void flex_tx_encoder_free(FlexTxEncoder* encoder);

/** Build one complete 1600/2FSK FLEX frame for a short-address alpha page. */
bool flex_tx_encoder_prepare(FlexTxEncoder* encoder, const PocsagTxConfig* config);

/** Frames the prepared burst holds the air for, which is what decides when the
 *  next beacon burst is due. Valid until the encoder is freed. */
uint8_t flex_tx_encoder_frames(const FlexTxEncoder* encoder);

/** Callback consumed by the Sub-GHz asynchronous transmitter. */
LevelDuration flex_tx_encoder_yield(void* context);
