# Flipper Pager

POCSAG and FLEX pager receiver, plus a POCSAG transmitter, for the Flipper Zero CC1101 radio.

Protocol implementation made by @Shmuma, 512 and 2400 speeds support by @htotoo.
Plugin based on [Weather Station](https://github.com/flipperdevices/flipperzero-firmware/tree/dev/applications/plugins/weather_station) from OFW.
Icons by @Svaarich.

## Disclaimer

This app is provided as is, without warranty, for education, research,
receiving traffic you are authorized to receive, and sending pages to systems
you own or are authorized to use. You are responsible for how you use it.

**Frequency range.** The CC1101 is rated for 300-348, 387-464 and
779-928 MHz. The US FLEX paging bands (929-932 MHz) are outside that range
and only work with the firmware's extended frequency range enabled. Operation
there is out of specification: sensitivity and stability are not
guaranteed, and the extended range is unlocked at your own risk. Transmit only
on frequencies where your device is permitted and you are licensed or otherwise
authorized. The app keeps the firmware's regional transmit checks in place.

**Intercepting pager traffic.** Pager messages are communications between
other people. In the US, intercepting the contents of display pager messages
is restricted by the Electronic Communications Privacy Act (18 U.S.C. 2511),
and divulging or using intercepted radio communications by 47 U.S.C. 605;
other countries have similar laws. Check what applies where you are before
decoding anything.

**Sensitive personal information.** Decoded messages and logs can contain
personal and sensitive information, including names, phone numbers,
addresses and medical or emergency details. Do not share, publish or upload
received messages, screenshots or log files. Prefer Temp logging, which keeps
nothing after the app exits. If you use Save, protect the SD card and delete
logs you no longer need. Deleting a file does not reliably erase it from flash
storage.

## Supported signals

- POCSAG 512, 1200 and 2400
- FLEX 1600/2, 3200/2, 3200/4 and 6400/4, with short and long (two-word) addresses
- FLEX alphanumeric, numeric, tone and binary pages; secure pages are shown as alphanumeric

Built-in radio presets:

- `POCSAG` (default) for POCSAG: 58 kHz filter, no carrier-sense squelch
- `POCSAG-W` for POCSAG at 900 MHz, with a 116 kHz filter for crystal offset
- `FM95`, the original POCSAG preset (270 kHz filter with carrier sense)
- `FLEX` for two-level FLEX (1600/2 and 3200/2), about +/-4.76 kHz deviation
- `FLEX4-3200` and `FLEX4-6400` for four-level FLEX. Both decode every FLEX
  rate; they differ only in the CC1101 data-rate setting.

The `FLEX4` presets read the CC1101 4-FSK hard decisions (RX_HARD_DATA) from
the GDO0 and GDO1 pins, sampled at 25.6 kHz. GDO1 shares the SPI MISO line, so
RSSI display is off while they are active. External
modules must expose GDO1 on MISO, as standard CC1101 modules do.

## Using the app

- **Receive messages** lists messages as they arrive; OK opens one (Up/Down
  scroll long messages). Hold OK on a message to enable an exact filter for
  its FLEX CAP or POCSAG RIC. Double-press Back to jump to the newest message;
  hold Back to end the receive session. Left opens the receiver Config.
- **Review messages** opens a saved session log from `/ext/pager/logs` using
  the same row layout, scrolling and message view as the live list. OK opens
  a message; double-press Back jumps to the newest message, and hold Back exits
  the log. The status bar shows the session time and message count. Config and
  address filtering are available only during live reception.
- **Send messages** composes an alphanumeric POCSAG page for a destination RIC.
  It can also send an alphanumeric FLEX page to a short capcode. Select the
  protocol, frequency and polarity, enter up to 80 POCSAG or 240 FLEX characters, review the
  complete page, then explicitly press Send. POCSAG supports functions 0-3 and
  512/1200/2400 baud. FLEX supports 1600/2FSK, 3200/2FSK, 3200/4FSK and
  6400/4FSK with short capcodes 1-1933312. Four-level modes are offered only
  when the firmware exports `furi_hal_subghz_write_reg`; the symbol is resolved
  at runtime so the same app can still load on older firmware. These modes
  use the internal CC1101, with a thread driving GPIO and frequency-offset
  register writes. FLEX sends one acquisition burst, splitting long pages
  across consecutive frames, with the retrieval flag set. Use
  Standard polarity unless the target requires an inverted signal. Optional TX
  correction offsets the carrier from -15.0 to +15.0 kHz in 0.5 kHz steps and
  defaults to Off. This is intended for licensed operators testing their own
  pager infrastructure; the app does not bypass firmware region restrictions.
- **Settings** is the same configuration, available before receiving:
  frequency, modulation, per-kind FLEX page visibility, duplicates,
  encrypted messages, logging, FLEX time sync, receive/decode LED indicators
  and a capcode filter. The receive activity blink can be disabled or set to
  cyan, blue, green, yellow, magenta, red or white; the successful-decode green
  blink can be disabled independently. Settings are saved to
  `/ext/apps_data/flipper_pager/settings.txt` and restored at startup.

**Alert Tone** offers `Default` (the existing short beep) and `Classic`.
Classic follows the user's [pager tone reference](https://youtu.be/IVyItS3sZLo):
the audio measures approximately 3200 Hz, with 875 ms on and 125 ms off.
Each alert plays three beeps (2.875 seconds total). It synthesizes the measured
pitch and cadence; the Flipper speaker's timbre can differ from the recording.
A lazily allocated UI event-loop timer controls the long notes without holding
up the notification service for their duration.

Classic works with `Alert: Sound` or `Both`; Both adds the existing brief
vibration at the start. It plays at most once every five seconds, including
while a message is open. Messages arriving during the cooldown do not build
an alert backlog. `Default`, `Vibro`, and `Off` retain their existing behavior.
The tones use the firmware notification service and respect its sound
settings and speaker ownership; Signal Sound can occupy the speaker.
No backlight notification is sent. Older settings files use Default.

**Time sync** offers `Off` (default), `MM:SS`, and `HH:MM:SS`.
`MM:SS` keeps the existing nearest-hour correction, including hour/date carry
at a boundary. `HH:MM:SS` uses that correction until validated time and
standard/daylight-time BIWs arrive, then sets the network's local hour too,
preserving the current date when setting the full time. Both modes continue
correcting during reception, including while a message is open; they are not
limited to one correction per session. Existing saved `On` settings become
`MM:SS`.

Minutes and seconds come from fresh, BCH-validated cycle/frame timing with
the 18-second GPS-to-UTC offset removed. Corrections smaller than two seconds
are skipped. Soft-recovered FIWs and clock BIWs are excluded from RTC control.
Full-time setting requires the network to broadcast both the hour and the
standard/daylight flag; it does not set the date or a system timezone.

Receiver settings and the transmitter protocol, frequency, address, function,
baud, polarity, correction and message text are saved on a clean app exit.
The message is stored in plain text in that file, like the rest of it.

Reception and error correction:

- POCSAG applies BCH(31,21) plus even-parity validation to every codeword,
  correcting any one- or two-bit error. Frame sync tolerates two bad bits, and
  the decoder measures the transmitter's bit period from the preamble.
- An uncorrectable POCSAG message word is shown as `?` characters while later
  words stay aligned and remain decodable.
- FLEX applies BCH to every phase word. Optional **Weak Decode** uses the
  sampled bit confidence to recover additional codewords and FIWs; pages that
  depend on a soft recovery are marked `W`.

FLEX decoding features:

- Every page in every phase of a frame is decoded.
- Long messages that the network splits into fragments are joined into one
  entry (`S<parts>J` in the header); missing parts are marked `[...]`.
- The alphanumeric header checksum is verified; a page that fails it is
  marked `?`.
- Pages with uncorrectable words are repaired from later copies when the
  network retransmits them (marked `R`).
- Alphanumeric, Numbers, Tone only and Binary each hide or show that kind of
  FLEX page independently. POCSAG is not filtered by kind; it has only the RIC
  filter.
- Duplicates can hide repeated content, show every page, or use **CAPS** mode.
  Hide displays only the first copy regardless of capcode. CAPS shows copies
  addressed to different capcodes but hides true retransmits to the same cap.
  Show displays every copy. Duplicate copies remain available to repair/ECC
  before this display filter runs, and every copy remains in the SD log.
- Messages that look like encrypted payloads can be hidden. This includes
  FLEX secure pages and wholly opaque Base64, Base64url, hexadecimal or mixed
  printable payloads; readable messages containing only an opaque field remain
  visible. A locally inserted `[...]` missing-fragment marker does not let an
  otherwise matching payload evade the filter. Short Base64-like padded
  fragments are only classified when the decoder has already marked them as
  suspect continuations.

## Logging

Messages are written to the SD card as they arrive. A hidden SD-side index
tracks the entries admitted to the live viewer, while RAM caches only the four
rows currently on screen. Scrolling loads older rows and full messages on
demand, so live-list memory use does not grow with session length. Full-message
buffers are released when the message is closed. Display filters only control
which messages enter the live list; the session log retains every decoded
message, including duplicates, encrypted payloads and partial pages.

- **Temp** (default): one hidden file, `/ext/pager/.session.tmp`, overwritten
  each receive session. It is overwritten with zeros and deleted when a new
  session starts, when the app exits, and at the next start after a crash.
  This is best effort: SD card wear levelling can keep old data in cells the
  file system no longer uses.
- **Save**: one log per receive session in `/ext/pager/logs`, named by start
  time and frequency, e.g. `2026-10-25_04-48-13_929.6125.txt`. One line per
  message: time, frequency, protocol, header and message, tab-separated.

The debug **Stats Log** option writes counter-only FLEX4 snapshots to
`/ext/pager/stats.txt` every 30 seconds and on normal exit, overwriting the
file when the next enabled session starts. It records no capcodes or message
text and can be used while the pager is unplugged. Each snapshot also records
current free heap, the session minimum, and the largest contiguous free block
as `heap free/min/block` so exhaustion and fragmentation can be distinguished.

Logs can contain other people's personal information; see the
[Disclaimer](#disclaimer).

## Frequencies

On first start the app creates `/ext/pager/settings.txt` (a standard Flipper
Sub-GHz setting file) with the standard Sub-GHz frequencies plus these paging
channels:

POCSAG: 439.9875 (DAPNET), 448.425, 465.970 and 466.075 MHz (German networks),
from [Wikipedia](https://en.wikipedia.org/wiki/POCSAG) and
[DAPNET](https://en.wikipedia.org/wiki/DAPNET).

US FLEX: 929.3625, 929.3875, 929.4625 (test channel), 929.5375, 929.5875, 929.6125,
929.6625, 929.9375, 931.0625, 931.2125, 931.3375, 931.8625 and 931.9375 MHz,
from the [RadioReference paging wiki](https://wiki.radioreference.com/index.php/Paging)
and the [Signal Identification Wiki](https://sigidwiki.com/wiki/FLEX).

They are above the CC1101's 928 MHz rating and only appear if the extended
frequency range is enabled (see the [Disclaimer](#disclaimer)). An existing `settings.txt` is never overwritten;
edit it to add your own `Frequency:` lines (in Hz, e.g. `Frequency: 929612500`),
or delete it to get the defaults back. Custom frequencies appear at the end of
the frequency list. See the Flipper
[setting_user.example](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/main/subghz/resources/subghz/assets/setting_user.example)
for the file format. The DAPNET POCSAG frequency 439.9875 MHz is the default.

## References for the FLEX decoder

The FLEX decoder was written for this app, using these projects as references:

- **[multimon-ng](https://github.com/EliasOenal/multimon-ng)**, `demod_flex.c`,
  `demod_flex_next.c` and `bch.c` (GPL-3.0-or-later). The protocol structure
  follows it: sync
  words and the mode table, frame information word decoding, block
  deinterleaving into phases A-D, BCH(31,21) error correction (derived from
  its BCH implementation), block information, address and vector word
  parsing, page types, alphanumeric parsing including the signature slot
  and fragment flags, BCD numeric and tone pages, the long-address range
  test and the extra data word that long numeric and tone pages carry, and
  binary pages shown as hex like its `parse_unknown`. The long-address
  capcode formula is PDW's, as quoted in multimon-ng. Its FLEX decoder
  fragment stream key, sequence tracking and whole-message signature checks
  were also compared while hardening this app's fragment joining. It
  credits Craig Shelley, Jason Lingohr, Bruce Quinton, Ramon Smits, Rob0101,
  bierviltje and bertinholland, building on GNU Radio code (Free Software
  Foundation).
- **[PortaPack Mayhem](https://github.com/portapack-mayhem/mayhem-firmware)**.
  Its FLEX receiver (`proc_flex.cpp`) informed the four-level approach of
  tracking symbol phase and taking the majority of samples near each symbol
  centre. Its FLEX transmitter (`flex_encode_alpha` in the `flex_tx` app) was
  the reference for the alphanumeric header word: the checksum, message
  number and retrieval flag (which marks retransmitted copies), and the
  placement of the header in the second vector slot for long addresses.
  Mayhem FLEX transmissions were also used for testing. Mayhem's FLEX
  receiver is by @timelf123 (Tim Elfelt) and @VasylSamoilov, and its FLEX
  transmitter by @VasylSamoilov (from the project's git history).

FLEX decoding for this app by @Geofferey.

Written for this app: the CC1101 GDO sampling, the symbol clock recovery
(a fractional-phase DPLL that follows the 1600 to 3200 baud switch),
repair from multiple copies (including a two-copy BCH search),
fragment joining, duplicate and encrypted detection, and SD logging.

## References for FLEX time sync

The cycle/frame clock uses the FLEX timing described in the
[Hewlett-Packard Journal](https://vtda.org/pubs/HP_Journal/HP_Journal_1998-02.pdf):
15 cycles per hour, 128 frames per four-minute cycle and 1.875 seconds per
frame, synchronized to GPS time. The GPS-to-UTC conversion uses the current
18-second offset documented by the
[U.S. Coast Guard Navigation Center](https://www.navcen.uscg.gov/sites/default/files/pdf/gps/geninfo/2020SPSPerformanceStandardFINAL.pdf).
Hour and standard/daylight handling are adapted from Mayhem FLEX RX's
[`network_hour`, `update_rtc`, and BIW handling](https://github.com/Geofferey/mayhem-firmware/blob/67345f64389694e45d1873587a0f052c4baed8e5/firmware/application/external/flex_rx/ui_flex_rx.cpp).
The time BIW supplies the hour; SysInfo subtype 4/5 supplies the
standard/daylight flag. The BIWs' coarse/accurate seconds are not used;
continuous frame timing supplies the minutes and seconds. The receiver reads no
date BIW, so a date this app transmits is not one it displays.

Known discrepancy: the receiver reads that standard/daylight flag at word bit
20, which the decoder datasheets referenced under the transmitter define as the
top bit of the accurate-seconds field, the flag itself being bit 16. So the
receiver does not see the flag the transmitter sends, and a network sending an
accurate-seconds trim reads to it as standard time.

## References for the FLEX transmitter

The transmitter uses the FLEX word layouts already exercised by the
receiver and was cross-checked against PortaPack Mayhem's
[`flex_tx` encoder](https://github.com/portapack-mayhem/mayhem-firmware/blob/next/firmware/application/external/flex_tx/ui_flex_tx.cpp).
That reference covers Sync 1/2, FIW and BIW fields, short addresses,
alphanumeric vectors and checksums, BCH(31,21), block interleaving, ERS and the
retrieval-flag change between original and repeated pages. The radio deviation
and bitstream polarity use the same Mayhem
[`proc_fsk` implementation](https://github.com/portapack-mayhem/mayhem-firmware/blob/next/firmware/baseband/proc_fsk.cpp)
already referenced by the POCSAG transmitter.

The four-level transmitter combines GPIO-controlled 2-FSK with per-symbol
`FSCTRL0` writes. Register scaling is documented in the
[Texas Instruments CC1101 datasheet](https://www.ti.com/lit/ds/symlink/cc1101.pdf)
(FSCTRL0 and DEVIATN). This register-driven approach needs RF validation;
the register definitions alone do not establish settling performance at 6400 bps.
Mode markers and A/B/C/D phase ordering follow this app's `protocols/flex.c`.
At 6400/4, alternating symbols carry A/B and C/D at 3200 symbols/s; only A
carries the page. Sync 1 and FIW remain at 1600 symbols/s. Timing uses cycle
deadlines to preserve the 312.5 us symbol period and 1.875 s frame duration.

`python tests/test_flex_tx.py` compiles the actual encoder and symbol thread
with host hardware shims, then checks all four modes using the actual receiver.
It covers sync acquisition, FIW, phase ordering, both polarities, 1-240 character
pages, tone pages, fragments, beacons, exact timing, counter wrap and cancellation.
Use a GCC/Clang host compiler with sanitizers, or set `CC=cl` in a Visual Studio
developer environment for AddressSanitizer. These checks do not exercise RF
settling or interrupt jitter on a Flipper; 6400/4 still needs an on-device test.

## References for the FLEX clock words

The date, time and system-message BIWs are implemented from two FLEX decoder
datasheets that document the fields bit by bit and agree with each other
throughout, both evidently written from the Motorola specification:

- Philips **[PCD5008 FLEX Pager
  Decoder](https://datasheet.octopart.com/PCD5008H-NXP-Semiconductors-datasheet-180940953.pdf)**
  product specification, 1998, section 8.7.9 and tables 49 to 53.
- Texas Instruments **[TMS320FLEX design
  manual](https://www.ti.com/lit/pdf/spra183)** section 2.5, tables 2-5 to 2-8,
  and its companion [spra193](https://www.ti.com/lit/pdf/spra193).

Both number the word as four check bits, the three `f` format bits, then `s0`
to `s13`. Taken as word bits, that puts the date's year at 7, day at 12 and
month at 17; the time's hour at 7, minute at 12 and coarse seconds at 18; and
the system message's subfield at 7, zone at 11, daylight flag `L` at 16 and
accurate seconds at 18. PortaPack Mayhem's `flex_tx` encoder and the baseband
parse in its `proc_flex.cpp` independently use those same positions, which is
what confirmed the reading of the two tables.

Three details follow from the datasheets rather than from any implementation:

- The year is **modulo 32** arithmetic from 1994, so the field covers 1994 to
  2025, then 2026 to 2057, and a receiver resolves the era. Mayhem maps a year
  past 2025 onto an equivalent one instead; this does not.
- `L` **set** means the transmitted hour is local standard time, clear means
  daylight. The zone code is always the standard-time offset, which is why the
  table has codes for -3:30 and +9:30 but none for daylight in Newfoundland or
  Adelaide.
- The time refers to the leading edge of the first bit of sync 1 of frame 0 of
  the current cycle, so the minute is the cycle's own four-minute boundary and
  a pager adds frame * 1.875 s itself. Mayhem's
  [PR #3368](https://github.com/portapack-mayhem/mayhem-firmware/pull/3368)
  reaches the same conclusion from ARIB STD-43A.

The 32 zone codes are TI's table 2-8, which `options_tz` and `flex_tz_table` in
Mayhem both match: code 16 is reserved, and there is no code for GMT-12 or for
anything past GMT+12. Subfield 0101 is the Additional Time Instruction; 0100
would also claim an Operator Messaging Address in the frame, which this never
sends.

None of this is verified against on-air traffic, since neither network captured
for this work sends any clock BIW at all.

## References for the POCSAG transmitter

The encoder was independently implemented for this app from
[ITU-R M.584-2](https://www.itu.int/rec/R-REC-M.584-2-199711-I/en), including
the 576-bit alternating preamble, 8-frame/16-codeword batch layout,
BCH(31,21), even parity, address frame placement and LSB-first 7-bit message
packing. Its output structure and padding conventions were cross-checked
against [DAPNET UniPager](https://github.com/DAPNET/UniPager) and
[MMDVMHost](https://github.com/g4klx/MMDVMHost/blob/master/POCSAGControl.cpp),
then checked against the independently written
[faithanalog POCSAG encoder](https://github.com/faithanalog/pocsag-encoder/blob/master/pocsag.c).
The RF polarity, 576-bit preamble and +/-4.5 kHz reference were compared with
[PortaPack Mayhem's POCSAG transmitter](https://github.com/portapack-mayhem/mayhem-firmware/blob/next/firmware/application/external/pocsag_tx/ui_pocsag_tx.cpp),
its [POCSAG encoder](https://github.com/portapack-mayhem/mayhem-firmware/blob/next/firmware/common/pocsag.cpp),
and its [FSK baseband implementation](https://github.com/portapack-mayhem/mayhem-firmware/blob/next/firmware/baseband/proc_fsk.cpp).
No source code was copied. The fixed 38.4 kBaud asynchronous sampler and the
CC1101 deviation step use the programming rules from the
[Texas Instruments CC1101 data sheet](https://www.ti.com/lit/ds/symlink/cc1101.pdf);
the waveform timer independently supplies the selected 512/1200/2400 baud.

All contributions are welcome!
