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
- **A page is sent once, not twice.** Airtime halves. Press send again to repeat, and each copy is marked an original.
- **A longer acquisition burst** — 1.44 s of runway instead of 480 ms, so a receiver meeting a single transmission cold has enough of it to recover the bit clock.
- **Type an arbitrary TX frequency.** OK on the frequency row opens number entry; left and right still step through the channel list. The radio decides what it will tune, so a value outside its bands leaves the old one alone.
- **Per-shot correction widened to ±15 kHz, in 0.1 kHz steps.** 10 kHz was short of what a crystal error reaches at 930 MHz, and 0.5 kHz steps were too coarse to centre a FLEX carrier. Left and right cover ±12.7 kHz; OK on the row types any value in range.
- **The message text is remembered** across leaving and reopening the app. Be aware it is stored in plain text in the app's `settings.txt`.

Transmit only where you are licensed and authorised.

## Receive

- **Type an arbitrary RX frequency**, the same way as the transmit row. The displayed frequency gains two decimals, which the channels needed anyway — they carry 100 Hz detail.
- **Multi-part messages assemble far more often.** A part whose page checksum failed used to be excluded from joining, so a message only came together if *every* part passed — which is why three-part and longer messages so often failed to join with all parts received. A damaged part is now used anyway; the result stays marked partial and the whole-message signature still has to reconcile.
- **Frequency hopping is gone.** It scanned the Sub-GHz hopper list while receiving and never earned its special cases.
- **Automatic frequency correction is gone.** The closed loop that drove the ppm value from the demodulator never worked properly. Manual correction is untouched.

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

The packaged `.fap` is **118,952 bytes**. Resident, the app is **52,181 bytes** — text 52,024, data 54, bss 103 — about where 2.0 left it despite everything above, because frequency hopping and automatic correction went out at the same time. The transmit buffer grows to 1,425 bytes for a full-length FLEX message, but that is heap held only for the length of a transmission.

## Building

The app lives at the root of this repository, so it drops straight into a firmware tree as an external app. It is built and tested against Momentum.

If you are packaging it into a firmware, `flipper_pager.txt` is published next to the built `.fap` automatically — include it in your assets so the About screen shows the full credits rather than a link.

## Credits

Built on work by **@xMasterX & @Shmuma**, with protocol improvements by **@htotoo** and FLEX decoding by **@Geofferey**. FLEX decoding is based on **multimon-ng** (Craig Shelley, Bruce Quinton, Jason Lingohr and contributors) and **PortaPack Mayhem** FLEX RX/TX (@timelf123, @VasylSamoilov). POCSAG encoding follows ITU-R M.584-2, cross-checked against **DAPNET UniPager**, **MMDVMHost** and the **faithanalog** POCSAG encoder. Icons by **@Svaarich**.
