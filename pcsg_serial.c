#include "pcsg_serial.h"
#include "pocsag_pager_app_i.h"

#include <furi_hal_usb.h>
#include <furi_hal_usb_cdc.h>
#include <datetime/datetime.h>

#define TAG "PCSGSerial"

// Interface 0 is the console's. Taking 1 leaves the CLI and qFlipper alone.
#define PCSG_SERIAL_IF 1U

#define PCSG_SERIAL_LINE_MAX 288U
#define PCSG_SERIAL_OUT_MAX  192U

#define PCSG_SERIAL_FLAG_RX   (1UL << 0)
#define PCSG_SERIAL_FLAG_TX   (1UL << 1)
#define PCSG_SERIAL_FLAG_STOP (1UL << 2)
#define PCSG_SERIAL_FLAG_ALL \
    (PCSG_SERIAL_FLAG_RX | PCSG_SERIAL_FLAG_TX | PCSG_SERIAL_FLAG_STOP)

struct PCSGSerial {
    POCSAGPagerApp* app;
    FuriThread* thread;
    FuriHalUsbInterface* usb_restore;
    volatile bool connected;
    volatile uint32_t commands;
    // Assembled command, and the last one answered for the screen.
    char line[PCSG_SERIAL_LINE_MAX];
    size_t length;
    bool truncated;
    char last[64];
};

/* ------------------------------------------------------------------ output */

static void pcsg_serial_write(PCSGSerial* serial, const char* text) {
    // Waiting is per-thread, so the link itself is not needed here; the
    // parameter keeps every output call reading the same way.
    UNUSED(serial);
    size_t remaining = strlen(text);
    const uint8_t* cursor = (const uint8_t*)text;
    while(remaining) {
        const uint16_t chunk =
            remaining > CDC_DATA_SZ ? (uint16_t)CDC_DATA_SZ : (uint16_t)remaining;
        furi_thread_flags_clear(PCSG_SERIAL_FLAG_TX);
        furi_hal_cdc_send(PCSG_SERIAL_IF, (uint8_t*)cursor, chunk);
        cursor += chunk;
        remaining -= chunk;
        /*
         * Every packet, including the last: the endpoint carries one at a
         * time, and a reply is several of these calls in a row - the three
         * lines of a status, say - so stopping only between the pieces of one
         * string lets the next call overwrite a packet still going out. That
         * lost a whole line on the first run against hardware.
         */
        furi_thread_flags_wait(PCSG_SERIAL_FLAG_TX, FuriFlagWaitAny, 100);
    }
}

static void pcsg_serial_printf(PCSGSerial* serial, const char* format, ...) {
    char buffer[PCSG_SERIAL_OUT_MAX];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    pcsg_serial_write(serial, buffer);
}

static void pcsg_serial_ok(PCSGSerial* serial) {
    pcsg_serial_write(serial, "OK\r\n");
}

static void pcsg_serial_err(PCSGSerial* serial, int code, const char* text) {
    pcsg_serial_printf(serial, "ERR %d %s\r\n", code, text);
}

/* ----------------------------------------------------------------- parsing */

// Case-insensitive match of a whole token.
static bool pcsg_serial_is(const char* text, const char* name) {
    while(*name) {
        char a = *text++;
        if(a >= 'a' && a <= 'z') a = (char)(a - 32);
        if(a != *name++) return false;
    }
    return *text == '\0';
}

// Case-insensitive prefix match; on success `rest` points past it.
static bool pcsg_serial_starts(const char* text, const char* prefix, const char** rest) {
    while(*prefix) {
        char a = *text++;
        if(a >= 'a' && a <= 'z') a = (char)(a - 32);
        if(a != *prefix++) return false;
    }
    if(rest) *rest = text;
    return true;
}

/*
 * One comma-separated argument, unquoted in place. Returns the start of the
 * next argument, or NULL after the last. A quoted argument keeps any commas
 * inside it, which is what lets a message carry them.
 */
static char* pcsg_serial_arg(char* text, char** value) {
    if(!text) {
        *value = NULL;
        return NULL;
    }
    while(*text == ' ') text++;
    if(*text == '"') {
        *value = ++text;
        char* out = text;
        while(*text && *text != '"') {
            if(*text == '\\' && text[1]) text++;
            *out++ = *text++;
        }
        const bool closed = *text == '"';
        if(*text) text++;
        *out = '\0';
        if(!closed) return NULL;
        while(*text == ' ') text++;
        return *text == ',' ? text + 1 : NULL;
    }
    *value = text;
    char* comma = strchr(text, ',');
    if(comma) {
        *comma = '\0';
        return comma + 1;
    }
    return NULL;
}

static bool pcsg_serial_number(const char* text, uint32_t* out, uint32_t max) {
    if(!text || !*text) return false;
    uint32_t value = 0;
    for(; *text; text++) {
        if(*text < '0' || *text > '9') return false;
        value = value * 10U + (uint32_t)(*text - '0');
        if(value > max) return false;
    }
    *out = value;
    return true;
}

/* ---------------------------------------------------------------- commands */

// 1600/2, 3200/2, 3200/4 and 6400/4, as the Mode row offers them.
static uint8_t pcsg_serial_mode_index(const PocsagTxConfig* config) {
    if(config->flex_6400) return 3U;
    if(config->flex_4level) return 2U;
    return config->flex_3200 ? 1U : 0U;
}

static void pcsg_serial_set_mode(PocsagTxConfig* config, uint8_t index) {
    config->flex_3200 = index != 0U;
    config->flex_4level = index >= 2U;
    config->flex_6400 = index == 3U;
}

// Fixed-width digits, advancing the cursor. Hand-read rather than scanned:
// one sscanf costs this app several kilobytes of formatted-input code.
static bool pcsg_serial_digits(const char** text, uint8_t count, uint32_t* out) {
    uint32_t value = 0;
    for(uint8_t i = 0; i < count; i++) {
        const char c = *(*text)++;
        if(c < '0' || c > '9') return false;
        value = value * 10U + (uint32_t)(c - '0');
    }
    *out = value;
    return true;
}

static bool pcsg_serial_set_time(POCSAGPagerApp* app, const char* text) {
    // "YYYY-MM-DDThh:mm:ss", the T interchangeable with a space. The seconds
    // are taken as they come; the transmitter rounds to its own frame grid.
    if(!text) return false;
    uint32_t year, month, day, hour, minute, second;
    if(!pcsg_serial_digits(&text, 4, &year) || *text++ != '-') return false;
    if(!pcsg_serial_digits(&text, 2, &month) || *text++ != '-') return false;
    if(!pcsg_serial_digits(&text, 2, &day) || !*text++) return false;
    if(!pcsg_serial_digits(&text, 2, &hour) || *text++ != ':') return false;
    if(!pcsg_serial_digits(&text, 2, &minute) || *text++ != ':') return false;
    if(!pcsg_serial_digits(&text, 2, &second) || *text) return false;

    if(year < 2000U || year > 2099U || month < 1U || month > 12U || day < 1U || day > 31U) {
        return false;
    }
    if(hour > 23U || minute > 59U || second > 59U) return false;

    DateTime value = {
        .year = (uint16_t)year,
        .month = (uint8_t)month,
        .day = (uint8_t)day,
        .hour = (uint8_t)hour,
        .minute = (uint8_t)minute,
        .second = (uint8_t)second,
    };
    if(!datetime_validate_datetime(&value)) return false;
    // The weekday is not transmitted but the RTC wants it consistent.
    const uint32_t stamp = datetime_datetime_to_timestamp(&value);
    datetime_timestamp_to_datetime(stamp, &value);
    furi_hal_rtc_set_datetime(&value);
    UNUSED(app);
    return true;
}

static void pcsg_serial_status(PCSGSerial* serial) {
    const PocsagTxConfig* config = &serial->app->tx_config;
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    static const char* const modes[] = {"1600/2", "3200/2", "3200/4", "6400/4"};
    pcsg_serial_printf(
        serial,
        "+STATUS: freq=%lu mode=%s phase=%s clock=%u zone=%u dst=%u extsec=%u\r\n",
        (unsigned long)config->frequency,
        modes[pcsg_serial_mode_index(config)],
        config->phase_mode == PagerTxPhaseCap ? "CAP" : "GROUP",
        config->clock_biws,
        config->utc_code,
        config->dst ? 1U : 0U,
        config->ext_sec);
    pcsg_serial_printf(
        serial,
        "+STATUS: cap=%lu,%lu,%lu,%lu ssid1=%u/%u ssid2=%u roam=%u\r\n",
        (unsigned long)config->ric[0],
        (unsigned long)config->ric[1],
        (unsigned long)config->ric[2],
        (unsigned long)config->ric[3],
        config->send_ssid1 ? config->local_id : 0U,
        config->send_ssid1 ? config->coverage_zone : 0U,
        config->send_ssid2 ? config->country_code : 0U,
        config->roaming ? 1U : 0U);
    pcsg_serial_printf(
        serial,
        "+STATUS: time=%04u-%02u-%02uT%02u:%02u:%02u busy=%u\r\n",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second,
        serial->app->txrx->txrx_state == PCSGTxRxStateTx ? 1U : 0U);
}

// Hand the current configuration to the transmitter and let it run.
static void pcsg_serial_send(PCSGSerial* serial) {
    POCSAGPagerApp* app = serial->app;
    if(app->txrx->txrx_state == PCSGTxRxStateTx) {
        pcsg_serial_err(serial, 5, "busy");
        return;
    }
    const PCSGTxStartStatus status = pcsg_tx_start(app);
    if(status != PCSGTxStartOk) {
        pcsg_serial_printf(serial, "ERR 6 tx refused (%d)\r\n", (int)status);
        return;
    }
    pcsg_serial_ok(serial);
}

static void pcsg_serial_execute(PCSGSerial* serial, char* line) {
    POCSAGPagerApp* app = serial->app;
    PocsagTxConfig* config = &app->tx_config;
    const char* rest = NULL;
    char* args = NULL;
    uint32_t value = 0;

    strlcpy(serial->last, line, sizeof(serial->last));
    serial->commands++;

    if(pcsg_serial_is(line, "AT")) {
        pcsg_serial_ok(serial);
        return;
    }
    if(!pcsg_serial_starts(line, "AT+", &rest)) {
        pcsg_serial_err(serial, 1, "expected AT");
        return;
    }

    // Queries first: every one of them ends in '?' and takes no argument.
    if(pcsg_serial_is(rest, "VER?")) {
        pcsg_serial_write(serial, "+VER: flextx 1\r\n");
        pcsg_serial_ok(serial);
        return;
    }
    if(pcsg_serial_is(rest, "STATUS?")) {
        pcsg_serial_status(serial);
        pcsg_serial_ok(serial);
        return;
    }
    if(pcsg_serial_is(rest, "FREQ?")) {
        pcsg_serial_printf(serial, "+FREQ: %lu\r\n", (unsigned long)config->frequency);
        pcsg_serial_ok(serial);
        return;
    }

    // Everything else is NAME=arg[,arg...].
    char* equals = strchr((char*)rest, '=');
    if(!equals) {
        pcsg_serial_err(serial, 2, "unknown command");
        return;
    }
    *equals = '\0';
    args = equals + 1;

    char* first = NULL;
    char* next = pcsg_serial_arg(args, &first);

    if(pcsg_serial_is(rest, "FREQ")) {
        if(!pcsg_serial_number(first, &value, PCSG_FREQUENCY_MAX_HZ) ||
           value < PCSG_FREQUENCY_MIN_HZ) {
            pcsg_serial_err(serial, 3, "frequency out of range");
            return;
        }
        config->frequency = value;
    } else if(pcsg_serial_is(rest, "MODE")) {
        if(!pcsg_serial_number(first, &value, 3U)) {
            pcsg_serial_err(serial, 3, "mode 0..3");
            return;
        }
        pcsg_serial_set_mode(config, (uint8_t)value);
    } else if(pcsg_serial_is(rest, "PHASE")) {
        if(pcsg_serial_is(first, "CAP")) {
            config->phase_mode = PagerTxPhaseCap;
        } else if(pcsg_serial_is(first, "GROUP")) {
            config->phase_mode = PagerTxPhaseGroup;
        } else {
            pcsg_serial_err(serial, 3, "phase CAP or GROUP");
            return;
        }
    } else if(pcsg_serial_is(rest, "POL")) {
        if(pcsg_serial_is(first, "NORM")) {
            config->inverted = false;
        } else if(pcsg_serial_is(first, "INV")) {
            config->inverted = true;
        } else {
            pcsg_serial_err(serial, 3, "polarity NORM or INV");
            return;
        }
    } else if(pcsg_serial_is(rest, "CLOCK")) {
        if(!pcsg_serial_number(first, &value, PCSG_TX_CLOCK_BOTH)) {
            pcsg_serial_err(serial, 3, "clock 0..3");
            return;
        }
        config->clock_biws = (uint8_t)value;
    } else if(pcsg_serial_is(rest, "ZONE")) {
        if(!pcsg_serial_number(first, &value, 31U)) {
            pcsg_serial_err(serial, 3, "zone 0..31");
            return;
        }
        config->utc_code = (uint8_t)value;
    } else if(pcsg_serial_is(rest, "DST")) {
        if(!pcsg_serial_number(first, &value, 1U)) {
            pcsg_serial_err(serial, 3, "dst 0 or 1");
            return;
        }
        config->dst = value != 0U;
    } else if(pcsg_serial_is(rest, "EXTSEC")) {
        if(!pcsg_serial_number(first, &value, PCSG_TX_EXT_SEC_MAX)) {
            pcsg_serial_err(serial, 3, "extsec out of range");
            return;
        }
        config->ext_sec = (uint8_t)value;
    } else if(pcsg_serial_is(rest, "SSID1")) {
        uint32_t coverage = 0;
        char* second = NULL;
        pcsg_serial_arg(next, &second);
        if(!pcsg_serial_number(first, &value, PCSG_TX_LOCAL_ID_MAX) ||
           !pcsg_serial_number(second, &coverage, PCSG_TX_COVERAGE_MAX)) {
            pcsg_serial_err(serial, 3, "ssid1 <localid>,<coverage>");
            return;
        }
        config->local_id = (uint16_t)value;
        config->coverage_zone = (uint8_t)coverage;
        config->send_ssid1 = true;
    } else if(pcsg_serial_is(rest, "SSID2")) {
        if(!pcsg_serial_number(first, &value, PCSG_TX_COUNTRY_MAX)) {
            pcsg_serial_err(serial, 3, "country out of range");
            return;
        }
        config->country_code = (uint16_t)value;
        config->send_ssid2 = true;
    } else if(pcsg_serial_is(rest, "ROAM")) {
        if(!pcsg_serial_number(first, &value, 1U)) {
            pcsg_serial_err(serial, 3, "roam 0 or 1");
            return;
        }
        config->roaming = value != 0U;
    } else if(pcsg_serial_is(rest, "TIME")) {
        if(!pcsg_serial_set_time(app, first)) {
            pcsg_serial_err(serial, 3, "time YYYY-MM-DDThh:mm:ss");
            return;
        }
    } else if(pcsg_serial_is(rest, "CAP")) {
        // Up to four, one per phase; the ones left out are cleared.
        uint32_t caps[4] = {0};
        char* text = first;
        for(size_t i = 0; i < 4U && text; i++) {
            if(!pcsg_serial_number(text, &caps[i], 0xFFFFFFFFU)) {
                pcsg_serial_err(serial, 3, "capcode not a number");
                return;
            }
            if(caps[i] && !flex_tx_capcode_valid(caps[i])) {
                pcsg_serial_err(serial, 4, "capcode out of range");
                return;
            }
            next = pcsg_serial_arg(next, &text);
        }
        for(size_t i = 0; i < 4U; i++) config->ric[i] = caps[i];
    } else if(pcsg_serial_is(rest, "MSG")) {
        if(!first) {
            pcsg_serial_err(serial, 3, "msg \"text\"");
            return;
        }
        strlcpy(config->message, first, sizeof(config->message));
        config->message_mode = PagerTxMessageTyped;
        pcsg_serial_send(serial);
        return;
    } else if(pcsg_serial_is(rest, "SEND")) {
        pcsg_serial_send(serial);
        return;
    } else if(pcsg_serial_is(rest, "ABORT")) {
        pcsg_tx_stop(app);
    } else {
        pcsg_serial_err(serial, 2, "unknown command");
        return;
    }

    pcsg_serial_ok(serial);
}

/* ------------------------------------------------------------------ thread */

static void pcsg_serial_feed(PCSGSerial* serial, const uint8_t* data, size_t length) {
    for(size_t i = 0; i < length; i++) {
        const char c = (char)data[i];
        if(c == '\r' || c == '\n') {
            if(serial->truncated) {
                pcsg_serial_err(serial, 7, "line too long");
            } else if(serial->length) {
                serial->line[serial->length] = '\0';
                pcsg_serial_execute(serial, serial->line);
            }
            serial->length = 0;
            serial->truncated = false;
            continue;
        }
        if(serial->length + 1U >= sizeof(serial->line)) {
            serial->truncated = true;
            continue;
        }
        serial->line[serial->length++] = c;
    }
}

static int32_t pcsg_serial_thread(void* context) {
    PCSGSerial* serial = context;
    uint8_t buffer[CDC_DATA_SZ];

    pcsg_serial_write(serial, "\r\n+READY: flextx 1\r\n");

    while(true) {
        const uint32_t flags =
            furi_thread_flags_wait(PCSG_SERIAL_FLAG_ALL, FuriFlagWaitAny, FuriWaitForever);
        if(flags & FuriFlagError) continue;
        if(flags & PCSG_SERIAL_FLAG_STOP) break;
        if(!(flags & PCSG_SERIAL_FLAG_RX)) continue;

        int32_t read;
        do {
            read = furi_hal_cdc_receive(PCSG_SERIAL_IF, buffer, sizeof(buffer));
            if(read > 0) pcsg_serial_feed(serial, buffer, (size_t)read);
        } while(read > 0);
    }
    return 0;
}

/* ---------------------------------------------------------------- lifetime */

static void pcsg_serial_rx_callback(void* context) {
    PCSGSerial* serial = context;
    furi_thread_flags_set(furi_thread_get_id(serial->thread), PCSG_SERIAL_FLAG_RX);
}

static void pcsg_serial_tx_callback(void* context) {
    PCSGSerial* serial = context;
    furi_thread_flags_set(furi_thread_get_id(serial->thread), PCSG_SERIAL_FLAG_TX);
}

static void pcsg_serial_state_callback(void* context, CdcState state) {
    PCSGSerial* serial = context;
    serial->connected = state == CdcStateConnected;
}

static CdcCallbacks pcsg_serial_callbacks = {
    .tx_ep_callback = pcsg_serial_tx_callback,
    .rx_ep_callback = pcsg_serial_rx_callback,
    .state_callback = pcsg_serial_state_callback,
    .ctrl_line_callback = NULL,
    .config_callback = NULL,
};

PCSGSerial* pcsg_serial_alloc(POCSAGPagerApp* app) {
    furi_assert(app);
    PCSGSerial* serial = malloc(sizeof(PCSGSerial));
    memset(serial, 0, sizeof(PCSGSerial));
    serial->app = app;

    serial->thread = furi_thread_alloc_ex("PagerSerial", 2048, pcsg_serial_thread, serial);
    furi_thread_start(serial->thread);

    // Two interfaces, so the console keeps the one it had.
    serial->usb_restore = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_hal_usb_set_config(&usb_cdc_dual, NULL);
    furi_hal_cdc_set_callbacks(PCSG_SERIAL_IF, &pcsg_serial_callbacks, serial);
    return serial;
}

void pcsg_serial_free(PCSGSerial* serial) {
    furi_assert(serial);
    furi_hal_cdc_set_callbacks(PCSG_SERIAL_IF, NULL, NULL);
    furi_thread_flags_set(furi_thread_get_id(serial->thread), PCSG_SERIAL_FLAG_STOP);
    furi_thread_join(serial->thread);
    furi_thread_free(serial->thread);
    furi_hal_usb_set_config(serial->usb_restore, NULL);
    free(serial);
}

bool pcsg_serial_connected(const PCSGSerial* serial) {
    furi_assert(serial);
    return serial->connected;
}

uint32_t pcsg_serial_commands(const PCSGSerial* serial) {
    furi_assert(serial);
    return serial->commands;
}

const char* pcsg_serial_last(const PCSGSerial* serial) {
    furi_assert(serial);
    return serial->last;
}
