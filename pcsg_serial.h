#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct POCSAGPagerApp POCSAGPagerApp;
typedef struct PCSGSerial PCSGSerial;

/**
 * A line-oriented control link for the transmitter, so a program on a PC can
 * drive it: set the channel and the network's identity, hand it the time, and
 * send pages.
 *
 * It takes USB CDC interface 1 and leaves interface 0 to the console, so the
 * Flipper's own CLI and qFlipper keep working while this is open. The host
 * sees a second serial port.
 *
 * Commands are one per line, ending CR, LF or CRLF. Every one is answered
 * with a single `OK` or `ERR <code> <text>`, with any data on `+NAME:` lines
 * before it. Lines beginning `+` that arrive without being asked for are
 * events, as on a modem.
 */
PCSGSerial* pcsg_serial_alloc(POCSAGPagerApp* app);

void pcsg_serial_free(PCSGSerial* serial);

/** Whether the host has the port open, from the CDC control lines. */
bool pcsg_serial_connected(const PCSGSerial* serial);

/** Commands answered since the link was opened, for the screen to show. */
uint32_t pcsg_serial_commands(const PCSGSerial* serial);

/** The last command line seen, for the screen to show. Never NULL. */
const char* pcsg_serial_last(const PCSGSerial* serial);
