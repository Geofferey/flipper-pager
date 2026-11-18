# Flipper Pager

POCSAG and FLEX pager receiver and transmitter for the Flipper Zero CC1101 radio.
The receiver (`flipper_pager.fap`) and transmitter (`flipper_pager_tx.fap`) are
separate apps built from this tree. Install both in `/ext/apps/Sub-GHz` to use
the Send messages handover between them.

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
  It can also send an alphanumeric or tone-only FLEX page to short or long
  capcodes. Select the
  protocol, frequency and polarity, enter up to 80 POCSAG or 240 FLEX characters, review the
  complete page, then explicitly press Send. POCSAG supports functions 0-3 and
  512/1200/2400 baud. FLEX supports 1600/2FSK, 3200/2FSK, 3200/4FSK and
  6400/4FSK with short capcodes 1-1933312 and long address set 1-2 capcodes
  2101249-1075843072. Other long-address sets are not supported. Zero clears a
  phase; addresses in the gap between the two ranges are rejected. Each phase
  can carry a short or long capcode independently, and the full capcodes are
  retained when switching between the RX and TX apps. **Phases**, in FLEX
  params, picks how the
  phase a capcode goes out on is decided. `CAP` applies the standard frame and
  phase embedding rule, `Phase = (Integer(Addr/4)) Modulo 4`, filing each
  capcode under the phase its own address names. `Group` assigns phases by
  hand: left and right on the Capcode row choose the phase the entry fills.
  Use `Group` where the rule does not hold, which the air cannot tell you -
  the rule is how operators hand out numbers, and whether a given subscriber
  was provisioned under it lives in its CAPCODE type letter, which is never
  transmitted. Types U-X assign phases outright with no rule at all, the form
  the spec recommends wherever group addresses are in use. Read the phase off
  the receiver and match it. Switching to `CAP` re-files the capcodes already
  entered, and where two of them name the same phase the first is kept.
  **FLEX params** holds everything that describes the transmission rather than
  the page, `Phases` above among them. It shares its row with `Function`, which
  is POCSAG-only, so whichever of the two the current protocol has no use for
  is not shown at all rather than sitting there reading `N/A`. Like
  `Review & send`, it carries a `>` at the right of its row, in the same column
  as the arrows on the rows that step through values, and right opens it as OK
  does. A row that does step through values keeps right for stepping, even
  where OK also opens something on it, so the arrow means one thing throughout.
  `Clock` picks which clock BIWs go out: `Off`, `Time`, `Date` or
  `Time+Date`. `UTC` and `DST` are the zone and the standard/daylight flag, and
  `ExtSec` the accurate-seconds trim, 0-7 in steps of 0.9375 s, left at 0
  because the frame grid is already clock-synchronized. Those three are fields
  of the one zone word, which rides with the time, so all three read `No zone`
  and send nothing when `Clock` is `Off` or `Date`.

  The rest need changing only to imitate a particular network. `SSID1` sends
  the system identification word
  naming `Local ID` (0-511) and `Cov zone` (0-31); `SSID2` sends the one naming
  `Country` (0-1023). `Roaming` sets the frame information word's roaming flag
  and switches both SSID words on, since the flag means nothing without an
  identified system. `Msg #` is the message number carried in the alphanumeric
  header: `Auto` derives it from the clock as before, and a fixed 0-63 is sent
  as set and counted on once the page has gone out.

  `ERS` is the lead-in duration ahead of a transmission, 0.24 s to 2.88 s in
  steps of 60 ms. The two modes lead in differently: a page with the ERS code,
  a beacon with the 37.5 Hz square wave captured from the network. `Default`
  leaves each at the length it was measured or chosen at - 1.44 s of ERS ahead
  of a page, 100 ms of square wave ahead of a beacon or Bcn+Seq - and any other
  value is that duration whichever mode is sending. A scaled beacon preamble is
  anchored at its end, so however long it runs it meets sync 1 on the same
  phase of the square wave the capture does.

  BIW1 carries the address offset in two bits, so at most **three** block
  information words fit ahead of the address. With three or fewer enabled they
  all go out, in the order SSID1, Time, Date, Zone, SSID2. Past that the time
  is pinned, so a receiver meeting a single frame can still set its clock, and
  the rest take turns by frame number rather than the tail of the list never
  being sent at all.

  Four-level modes are offered only
  when the firmware exports `furi_hal_subghz_write_reg`; the symbol is resolved
  at runtime so the same app can still load on older firmware. These modes
  use the internal CC1101, with a thread driving GPIO and frequency-offset
  register writes. FLEX sends one acquisition burst, splitting long pages
  across consecutive frames, with the retrieval flag set. Use
  Standard polarity unless the target requires an inverted signal. Optional TX
  correction offsets the carrier from -15.0 to +15.0 kHz in 0.5 kHz steps and
  defaults to Off. This is intended for licensed operators testing their own
  pager infrastructure; the app does not bypass firmware region restrictions.
- **Settings** is the same configuration, available before receiving: frequency,
  modulation, `Auto RX`, logging, FLEX time sync and DST, the frequency
  correction, the debug rows, and three doors into groups of their own.
  `Filtering` holds everything that decides which messages reach the list -
  per-kind FLEX page visibility, duplicates, encrypted and partial messages, the
  capcode, RIC and frame filters with their ranges, and the FLEX phase.
  `Alerting` holds how a message announces itself and what the radio sounds
  like - the alert and its tone, the receive and decode LEDs, and the signal
  sound with its volume, pitch and spread. `HUD options` is the clock and the
  Network screen. The receive activity blink can be disabled or set to
  cyan, blue, green, yellow, magenta, red or white, and keeps going for as long
  as the radio is listening, whichever screen is open; the successful-decode
  green blink can be disabled independently. Settings are saved to
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

**Auto RX** starts receiving as the app opens instead of waiting at the
menu. The menu is still behind it, so Back from the receiver reaches it as on
any other run; it is simply never drawn.

**Cap. Filter** and **RIC Filter** offer `Off`, `Exact`, `Range`, `Alert` and
`AlertRng`. The first three decide what is shown. The two alert modes hide
nothing: every message still arrives and is still listed, and only the
addresses they name raise an alert - which is usually the want, knowing when
one address is paged without going deaf to the rest. What an alert is remains
the `Alert` and `Alert Tone` rows' to say, so `Alert: Off` is silent whatever
is set here. `Frame Filter` has no alert modes: a frame is not an address.

Holding OK on a message in the list sets its address to `Alert`. It used to set
`Exact`, which hid every other address - rarely what is wanted from a list of
what is on the air.

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

**Network info** is reached with right from the live message list, as settings
are with left. It shows what the network has said about itself, with its clock
as the centre piece: the zone and the offset in force, the date, the local ID
and coverage zone from the SSID1 word, the country code from SSID2, the
standard/daylight flag beside the zone as `UTC-8 | DST` - divider and all shown
only when daylight is advertised, the offset beside it already being the one in
force, and **struck through when the `DST` setting is overriding it**, which is
the one case where a network advertises daylight and the hour on show is not
daylight - the roaming flag from the frame information word, the frame and cycle, the count of
messages held this session, and the accurate-seconds trim.

The date carries which clock the hour came from beside it, as
`1994-11-17 | NET` - the same shape the zone row uses for its daylight flag,
and centred under the clock so the two read as one. **OK swaps between them**:
`RTC` shows the Flipper's own
time and date whatever the HUD options are set to, which is also the one reading
that needs no reception, so it works with nothing on the air. The override lasts
while the screen is open and is forgotten on the way out - it is a look at the
other clock, not a change of settings, and it leaves the receiver's status-bar
clock alone. `RTC` also appears without pressing anything whenever the hour is
the Flipper's already, which `Hour: Hybrid` does until a clock word arrives.
`Source` in the HUD options hides the indicator; OK still swaps the clock over,
it just stops saying which one is on show.

The message count is
the same one the status bar carries, so the two always agree - including that it
falls as well as rises, the history being a rolling window. A field reads `-` until a word carrying it arrives, so
a network that never sends one is told apart from one that sends it as zero.
The saved-log list has no network behind it, so right does nothing there.

**HUD options**, in Settings, decides what the clock is made of and what the
Network screen shows. `Day/Month`, `Year`, `Hour` and `Minute` each take
their value from the `Network` or from the Flipper (`Local`); `Hour` also
offers `Hybrid`, the network's once a clock word has arrived and the Flipper's
until then, which is the default. The year is chosen apart from the day and
month because that is the part the observed networks get wrong: both send
today's day and month, one with the year field wrapped to 1994 and the other
with a date stale by years. `Year: Local` with `Day/Month: Network` reads the
network's date and the Flipper's year. `Year era` decides how the network's own
year field is read - see [the year era](#the-year-era) - and does nothing when
the year is taken locally.
`Offset` shifts the displayed hour by up to twelve hours either
way, for a network whose hour is simply wrong. `Freq switch` decides what
becomes of the reading when the channel changes from the settings menu:
`Discard` throws it away, as the app always did, and `Keep` carries it over,
each field standing until the new network sends a word that replaces it - so a
channel that never sends a country code leaves the last one showing. Useful
across channels of one operator, misleading across operators, so it is yours to
pick. An input overrun still clears the reading whichever is set, the decoder
having lost its place in the frame grid. `Ticker` sets
whether the status
bar rotates between the frequency and the clock, holds one of them, or stays
off, and `Ticker secs` how long it holds each. The rest show or hide the
individual Network screen fields. All of it is display only: the log keeps
timestamping from the Flipper's own clock whatever is set here. The one clock
setting that does reach the RTC is `DST`, which lives with `Time sync` rather
than here.

A reading stays once taken. The clock keeps running from the last trusted
frame, and the zone, date and system words hold until the network replaces
them, rather than being dropped when nothing new arrives. A frame recovered by
Weak Decode is ignored for timing instead of clearing what earlier clean frames
established. Minutes and seconds come from where the frame sits in the cycle,
so they run from the first frame onwards; only the hour needs a clock word, and
it reads `--:MM:SS` until one arrives. Setting the RTC still requires a frame
from the last five seconds - a stale reading is fine to show and not to sync
from.

The date is shown as the network sends it, with no attempt to resolve an era
the format does not carry. The date is its own word and says nothing about the
time of day: a commercial channel was observed carrying an accurate time of day
and a 2007 date, so a date that looks wrong is the network's and not a decoding
fault.

The field shown as `ExtSec` is the spec's **Accurate Seconds**, bits 18-20 of
the system message word, and the standard/daylight flag `L` is bit 16. The
receiver read that flag at bit 20 until the TI tables were read properly: bit 20
is the top bit of Accurate Seconds, so the old reading turned a trim of 4 or more
into a standard-time hour. That happened to give the right time on every
transmitter to hand, which kept it alive for a while - see
[DST](#dst-and-why-the-receiver-overrides-the-network) - but it is not the flag
and is no longer applied. Accurate Seconds says how much to add to the
*coarse* seconds of the time word, in 1/64-minute (0.9375 s) steps, trimming
those 7.5 s steps to within a second. This app never reads the coarse seconds -
continuous frame timing supplies the minutes and seconds - so there is nothing
for the trim to refine, and it is displayed rather than applied. The name
`ExtSec` is Mayhem's, kept for parity with the transmitter most people will test
against.

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
continuous frame timing supplies the minutes and seconds.

### The year era

The year arrives as five bits counting from 1994, which TI table 2-5 defines as
modulo arithmetic: 1994-2025, then 2026-2057, and so on. Nothing on the air says
which window is meant, so `Year era` in the HUD options decides how to read it.

| | year field 0 | field 13 | field 21 |
|---|---|---|---|
| `Off` (default) | 1994 | 2007 | 2015 |
| `On` | **2026** | 2007 | 2015 |

`Off` takes the field at face value and never names a year past 2025. `On` adds
whole 32-year eras, up to **the latest era the Flipper's own clock says has
begun** - not the nearest one. That distinction is the whole difficulty: a
network frozen at 2007 sits in an era that has begun, so it still reads as 2007,
where the nearest era would call it a future 2039 and be confidently wrong.

Both readings are wanted because observed networks disagree about what they are
doing. One is frozen at a genuinely stale 2007. One sends today's day and month
with the year wrapped to 1994, which is 2026 an era on, and only `On` recovers
it. A third sends 2015, the calendar twin of 2026, which both readings report as
2015 - correct, because 2015 is what is on the wire, and the twin is chosen
precisely so that every weekday still lands right.

`On` leans on the Flipper's own clock being roughly right, which `Off` does not,
so it is off by default.

### DST, and why the receiver overrides the network

`Settings -> DST:` is **daylight saving where you are**, not on the network. It
sits beside `Time sync` because it reaches the RTC and not only the display.

`Off` and `On` are the two answers to "is daylight saving in force here", and
each corrects the network that disagrees with it. `Disabled` corrects nothing.

| | network advertises daylight | network advertises standard |
|---|---|---|
| `Off` | an hour fast: **take one off** | agreed, left alone |
| `On` | agreed, left alone | an hour slow: **add one** |
| `Disabled` | left alone | left alone |

Default is `Off`, which is also the standing value where daylight saving is
never observed; `Disabled` is the one for a network whose flag cannot be
trusted. The zone offset moves with the hour, so the label and the digits
always agree about which offset is in force.

The reason the receiver gets the last word is that **the network knows which
kind of time it is sending, and only the receiver knows whether that is the
season**. `L` at word bit 16 says which - set for standard, clear for daylight -
and observed networks are honest about it. What they cannot know is whether
their own answer is still current.

Two production networks, measured in a Pacific winter against NTP:

| | `ExtSec` | date sent | `L` says | hour | honest |
|---|---|---|---|---|---|
| network A | 0 | 2015-11-17 | standard | matches NTP | yes |
| network B | 5 | 2007-04-03 | daylight | an hour fast | yes |

Both flags are correct. Network A is well run: its date is today's, with 2015
being 2026's calendar twin, the same substitution this app's own transmitter
makes. Network B's date is frozen in **April** 2007 - inside daylight saving
season - so its terminal applies DST, transmits daylight time, and says so
accurately. Its clock is live while its date is not. Nothing is lying; a stale
date is driving correct logic to an answer that stopped being true in November.

So `DST: Off` corrects network B and leaves network A alone, from the flag
alone. Come summer `DST: On` is the mirror of it: network B's daylight time is
right by then and is left alone, and network A is corrected until it switches
over - which is the case `Off` cannot express, and the reason `On` adds an hour
rather than simply doing nothing.

One transmitter this cannot satisfy is Mayhem's FLEX TX with DST enabled. It
sends `L` from `pmem::dst_enabled()` - whether daylight saving is *configured* -
rather than `dst_in_range`, whether it is in *force*, so in midwinter it
advertises daylight while transmitting standard time. That is a malformed
transmission, and `DST: Off` will take an hour off it. Leave DST off on the
PortaPack when using it as a reference, or set `DST: Disabled` here, which
takes every transmitter exactly as sent.

An earlier build gated this on `AccSec >= 4` instead - the accurate-seconds
field, which the decoder misread as the flag for several releases. On these
three transmitters it happened to land correctly every time (0, 5, 0 against
"no, yes, no"), but the correlation was incidental: network B is simply a
neglected site that also sends a nonzero trim. The flag carries the real signal,
so the trim is reported and never applied.

The hour and the zone offset always move together or not at all: a `UTC-8` label
over a clock still reading `-7` is worse than either answer on its own. Time
sync takes the same corrected hour the screen shows, so Full sync can never
write an hour the HUD did not display.

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

The system information words, the roaming flag and the remaining frame
parameters were ported from that same Mayhem `flex_tx` app, whose
`FlexParamsView` is the model for this app's **FLEX params** screen. Its
`flex_biw_ssid1` (word format `000`, coverage zone at bit 7 and local ID at
bit 12) and `flex_biw_ssid2` (format `111`, country code at bit 11) supply the
two SSID layouts; its `flex_fiw` supplies the roaming flag at bit 15; its
`flex_biw_sysinfo` call supplies the accurate-seconds position, bits 18-20 of
the same word that already carried this app's zone and daylight flag; and its
`on_roaming_changed` is why enabling roaming enables both SSID words. The
three-slot ceiling and the word order `SSID1, Time, Date, Zone, SSID2` are
Mayhem's `build_frame`, so a frame with three or fewer of them is word for
word what Mayhem sends. Rotating the remainder by frame number when more are
enabled, and pinning the time while doing so, is this app's own, as is the
message number's advance after a page rather than Mayhem's advance after a
transmission. No source code was copied.

Long address set 1-2 encoding follows the conversion and address assignments
in TI's [SPRA193](https://www.ti.com/lit/pdf/spra193), sections 2.9.4-2.9.6,
and the inverse already used by this tree's `protocols/flex.c`. The receiver
also supplies the reference for the second vector slot: the alphanumeric
header moves there, while tone-only pages use five BCD fill digits. Fixed
address-word fixtures check the encoder independently of the RX round trip.

The four-level transmitter combines GPIO-controlled 2-FSK with per-symbol
`FSCTRL0` writes. Register scaling is documented in the
[Texas Instruments CC1101 datasheet](https://www.ti.com/lit/ds/symlink/cc1101.pdf)
(FSCTRL0 and DEVIATN). This register-driven approach needs RF validation;
the register definitions alone do not establish settling performance at 6400 bps.
Mode markers and A/B/C/D phase ordering follow this app's `protocols/flex.c`.
At 6400/4, alternating symbols carry A/B and C/D at 3200 symbols/s; each
configured phase carries the page. Sync 1 and FIW remain at 1600 symbols/s. Timing uses cycle
deadlines to preserve the 312.5 us symbol period and 1.875 s frame duration.

`python tests/test_flex_tx.py` compiles the actual encoder and symbol thread
with host hardware shims, then checks all four modes using the actual receiver.
It covers sync acquisition, FIW, phase ordering, both polarities, 1-240 character
pages, tone pages, fragments, beacons, short/long address boundaries, mixed
phases, all clock-word combinations, exact timing, counter wrap and cancellation.
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
  and its companion [spra193](https://www.ti.com/lit/pdf/spra193). Section 2.5.3
  and table 2-7 are the authority for the system message word used here: the
  `S` Accurate Seconds, `L` daylight and `Z` zone definitions below are quoted
  from it, and table 2-8 is the zone table in `pcsg_zone_quarters`.

Both number the word as four check bits, the three `f` format bits, then `s0`
to `s13`. Taken as word bits, that puts the date's year at 7, day at 12 and
month at 17; the time's hour at 7, minute at 12 and coarse seconds at 18; and
the system message's subfield at 7, zone at 11, daylight flag `L` at 16 and
accurate seconds at 18. PortaPack Mayhem's `flex_tx` encoder and the baseband
parse in its `proc_flex.cpp` independently use those same positions, which is
what confirmed the reading of the two tables.

Three details follow from the datasheets rather than from any implementation:

- The year field counts from 1994 and TI table 2-5 defines it as "the year with
  modulo arithmetic ... representing 1994 through 2025, 2026 through 2057, etc".
  Nothing on the air names which era is in force, so the reading is a choice,
  and `Year era` in the HUD options makes it - see
  [the year era](#the-year-era). On transmit a later year goes out as its
  calendar twin, the
  year in that window with the same leap status and the same weekday for
  1 January, so every date in it still lands on the right day - 2026 is sent as
  2015. Mayhem's `flex_equiv_year()` does the same and reports the substitution
  on screen.
- `L` **set** means the transmitted hour is local standard time, clear means
  daylight. The zone code is always the standard-time offset, which is why the
  table has codes for -3:30 and +9:30 but none for daylight in Newfoundland or
  Adelaide. `L` describes the transmitted time; it does not ask a receiver to
  adjust it. The spec is explicit that "the time transmitted is the local time
  for the transmitted time zone", so by the spec the hour is used as sent. It is
  used as sent here too, unless a network advertises daylight time out of season
  - see [DST](#dst-and-why-the-receiver-overrides-the-network). All 31 zone
  codes in TI table 2-8 were checked against `pcsg_zone_quarters` and agree.
- The three bits at 18-20 are **Accurate Seconds**, not a security or zone
  field: "how much time should be added to the coarse seconds in 1/64-of-a-
  minute increments", 0 to 6.5625 s, refining the time word's 7.5 s coarse
  steps to within a second. This app derives seconds from the frame grid and
  ignores the coarse field, so the trim has nothing to refine here. It is shown
  as `ExtSec` after Mayhem, which named it for "extended seconds".
- The time refers to the leading edge of the first bit of sync 1 of frame 0 of
  the current cycle, so the minute is the cycle's own four-minute boundary and
  a pager adds frame * 1.875 s itself. Mayhem's
  [PR #3368](https://github.com/portapack-mayhem/mayhem-firmware/pull/3368)
  reaches the same conclusion from ARIB STD-43A.

**spra183 defines the system message word twice, incompatibly**, which is where
the long-running "is the flag at 16 or at 20" question comes from. Table 2-7, in
the signal-structure chapter, breaks `s4`-`s13` into a five-bit `Z`, the `L`
flag, a don't-care, and three bits of `S`. Table 3-22, in the decoder chapter,
gives the same word as a single **ten-bit `z` field over `s4`-`s13`** with a
four-bit `A` replacing the `010X` subtype selector - no `L` and no `S` at all.

Table 2-7 is the authority: chapter 3 describes what TI's decoder device reports
over its serial interface, and it defers explicitly, "see section 2.5, FLEX
local time and date, for detailed information about these block information
words". Table 3-22 is an abbreviation that loses the subfields, not a rival
definition of the air format. The tables are also not carefully proofed - table
2-6 names the coarse seconds `S5 S4 S3` where table 3-22 names the same three
bits `S2 S1 S0`, and table 2-6 prints a lowercase `m3` where `M3` belongs.

But Table 3-22 is exactly the ten-bit field Mayhem's FLEX RX calls `biw_v2`, and
it reads `(biw_v2 >> 9) & 1` - `s13`, word bit 20, the top bit of that `z`
field. So reading the flag at bit 20 is not a mistake anyone invented from
traffic; it is what Table 3-22 leads to.

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
