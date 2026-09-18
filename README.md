# Time Bridge for Pebble

Timebridge is a Rebble-compatible native Pebble watchapp. It puts the device's current local time above a selected world time.
The select button opens a Pebble-native launcher with **Recent**, **Timezones**
and **Settings**. The interaction is designed for a 144 × 168 rectangular
Pebble display (Pebble, Pebble Time, and Pebble Time 2).

## Included behavior

- Two time cards with a central, two-way swap indicator.
- A short date below each time; the default uses a two-letter month, e.g.
  `Se 18, 2026`.
- Timezone labels use the requested format, e.g. `IST (+5:30)`, in both
  Timezones and Recent. The picker covers every current global UTC offset,
  including 30- and 45-minute offsets.
- The five most recently chosen zones are saved on the watch.
- Pebble's system MenuLayer provides the native scrolling, bounce animation,
  selection highlight, and title/subtitle rows without custom glyphs.
- Dark/light theme, date visibility, a picker with 15 common international
  date layouts, and 12/24-hour
  time all persist across launches.
- The PebbleKit JS companion resolves the selected IANA zone's live offset,
  including daylight saving time. The C app falls back to a built-in standard
  offset if the phone companion is unavailable.

## Build

Install the Rebble/Pebble SDK toolchain, then from this folder run:

```sh
pebble build
pebble install --emulator basalt
```

`wscript` is the required build definition. It compiles `src/c/**/*.c` for
each configured platform and bundles `src/pkjs/index.js` into the final PBW.

The project uses the current `package.json` app manifest and declares the
rectangular platforms: `aplite`, `basalt`, `diorite`, `emery`, and `flint`.
The face centers itself on both the 144×168 classic displays and the larger
Pebble 2 displays.
