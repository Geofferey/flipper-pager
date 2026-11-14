# Flipper Pager v2.1

First release from the app's own repository. FLEX transmissions now carry the network clock and can run to 240 characters, and the app has its own identity: its own appid, its own storage, its own icon.

## ⚠️ Upgrading from 2.0 — names and paths changed

This release does not migrate anything. Nothing is deleted either, so the old files stay where they are if you want to copy them across.

- **The app is `flipper_pager`.** Install `flipper_pager.fap`. The old `pocsag_pager` can stay installed alongside it — different appid, different icon, different storage, no conflict.
- **Data moved from `/ext/pocsag/` to `/ext/pager/`** — `settings.txt` (your frequency and preset list), `logs/`, `captures/`, `stats.txt`. Copy them over if you want them; the app creates the new folder empty on first start.
- **App settings moved** from `/ext/apps_data/pocsag_pager/settings.txt` to `/ext/apps_data/flipper_pager/settings.txt`. Copy that file across to keep your filters, alerts, LED colours and saved TX message, or just set them again.
- **`ABOUT.txt` is now `flipper_pager.txt`**, and belongs at `/ext/pager/flipper_pager.txt`. Without it the About screen shows the repository link instead of the credits.

## Transmit

- **FLEX now carries the network clock.** Every transmission includes the time words a receiver needs to set its own clock, so a Flipper receiving one syncs to it the way it syncs to a real network. The time sent is this Flipper's own clock, so it is only as right as the RTC.
- **FLEX messages up to 240 characters.** Anything over 80 is split into fragments automatically and sent as consecutive frames in a single transmission; the receiver joins them back into one message. A full 240 characters is about 7.1 s on air against 3.3 s for a short page.
- **POCSAG stays at 80 characters** — it has no fragmentation, and three batches is what its encoder emits. A longer message is refused with the error tone when you open the review screen, rather than failing silently at send.
- **FLEX 3200/2FSK as well as 1600/2FSK.** Pick it on the Mode row. The acquisition burst, sync1 and the frame info word stay at 1600 — a receiver has to read the mode out of sync1 before it can know any other rate — and everything after runs at double, carrying phases A and C where 1600 carries A alone. Frames are a fixed 1.875 s in every FLEX mode, so this does not shorten a short page; it doubles what one frame holds. 1600/2FSK remains the default and is the mode every FLEX receiver must support.
- **A TEST beacon on the message row.** Left and right on the message row pick between the typed message and **TEST**, which transmits sync and the clock BIWs with no pages behind them, repeating until you press Back. It carries no acquisition burst, since it repeats, so a burst is a frame and little else. Each one is built fresh, so its cycle, frame and hour words describe the slot it actually goes out in — which makes it a clock source you control for testing a receiver's time sync. FLEX only.
- **An empty message sends a tone-only page.** Leave the message blank and the Review row goes through instead of refusing. POCSAG sends the address word alone with the function bits picking the alert; FLEX sends a type 2 vector with the BCD fill code, which reads back as a tone page rather than a source digit.
- **A page is sent once, not twice.** Airtime halves. Press send again to repeat, and each copy is marked an original.
- **A longer acquisition burst** — 1.44 s of runway instead of 480 ms, so a receiver meeting a single transmission cold has enough of it to recover the bit clock.
- **Type an arbitrary TX frequency.** OK on the frequency row opens number entry; left and right still step through the channel list. The radio decides what it will tune, so a value outside its bands leaves the old one alone.
- **One frequency correction, in Hz, shared by transmit and receive.** The ppm setting is gone and the Hz correction takes its place on the receiver config screen as **Correction**, still global to the app and reachable from the transmit screen too. It runs to ±15 kHz in 0.1 kHz steps — 10 kHz fell short of what a crystal error reaches at 930 MHz, and 0.5 kHz was too coarse to centre a FLEX carrier. Left and right cover ±12.7 kHz; OK on the row types any value in range. Being hertz rather than a ratio, it stops being right once the band changes.
- **The message text is remembered** across leaving and reopening the app. Be aware it is stored in plain text in the app's `settings.txt`.

Transmit only where you are licensed and authorised.

## Receive

- **Type an arbitrary RX frequency**, the same way as the transmit row. The displayed frequency gains two decimals, which the channels needed anyway — they carry 100 Hz detail.
- **Multi-part messages assemble far more often.** A part whose page checksum failed used to be excluded from joining, so a message only came together if *every* part passed — which is why three-part and longer messages so often failed to join with all parts received. A damaged part is now used anyway; the result stays marked partial and the whole-message signature still has to reconcile.
- **Frequency hopping is gone.** It scanned the Sub-GHz hopper list while receiving and never earned its special cases.
- **Automatic frequency correction is gone.** The closed loop that drove the correction from the demodulator never worked properly. Manual correction is untouched.

## Saved logs and review

- **Address search covers the whole log.** A saved session now keeps an index beside it as `<stamp>.idx`, so stepping through one capcode or RIC reads fixed-size records instead of parsing log lines — no longer bounded to the nearest 256 entries. Logs recorded by older builds fall back to the old bounded scan.
- **Left and right step through one address while reading.** Up and down still walk the list in order. Works in the live viewer and in saved logs.

## Fixes

- **`strip_fap.py` removed.** It stripped `.symtab`, `.strtab` and the legacy `.rel` tables, which renumbers every section after them — and the loader resolves section-relative relocations by section index, so every one of them landed on the wrong base. The produced `.fap` would not load correctly. It never saved any RAM either; those sections are not allocated.

## Installing

1. Copy **`flipper_pager.fap`** to `/ext/apps/Sub-GHz/`
2. Copy **`flipper_pager.txt`** to `/ext/pager/`

The app runs without step 2; only the About screen is affected.

## Size

The packaged `.fap` is **119,984 bytes**. Resident, the app is **52,781 bytes** — text 52,624, data 54, bss 103 — about where 2.0 left it despite everything above, because frequency hopping and automatic correction went out at the same time. The transmit buffer is 2,496 bytes at its largest, enough for three 3200/2FSK frames, and that is heap held only for the length of a transmission.

## Building

The app lives at the root of this repository, so it drops straight into a firmware tree as an external app. It is built and tested against Momentum.

If you are packaging it into a firmware, `flipper_pager.txt` is published next to the built `.fap` automatically — include it in your assets so the About screen shows the full credits rather than a link.

## Credits

Built on work by **@xMasterX & @Shmuma**, with protocol improvements by **@htotoo** and FLEX decoding by **@Geofferey**. FLEX decoding is based on **multimon-ng** (Craig Shelley, Bruce Quinton, Jason Lingohr and contributors) and **PortaPack Mayhem** FLEX RX/TX (@timelf123, @VasylSamoilov). POCSAG encoding follows ITU-R M.584-2, cross-checked against **DAPNET UniPager**, **MMDVMHost** and the **faithanalog** POCSAG encoder. Icons by **@Svaarich**.
