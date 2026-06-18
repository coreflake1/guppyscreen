# Beeps & Songs (the Buzzer)

Your Ender-3 V3 KE has a small buzzer on the board. Stock firmware only ever used it for one fixed,
loud beep. OpenKE drives it with **hardware PWM** instead, so it can play **any pitch, any length, and
full melodies** — and it does so with **zero CPU load**, which means it's safe even in the middle of a
print.

There are three things this gives you:

1. A soft **touch click** when you tap the screen (optional).
2. **`M300` beeps** with real pitch and duration (the standard slicer "beep" command).
3. **`PLAY_TUNE` songs** — built-in tunes, plus your own, kept in an editable file.

> **Already running OpenKE and don't see `M300`/`PLAY_TUNE`?** These commands and the default song list
> are installed into your Klipper config, which the on-screen **Update Guppy** button doesn't touch.
> **Re-run the [installer](Installation)** once (it's safe and keeps your config) and they'll appear.
> See [Upgrading](Upgrading) for details. Fresh installs already have everything.

---

## 1. Touch click (screen feedback)

A quiet, low "tick" on every button tap — like a phone's touchscreen feedback.

- **Turn it on/off:** **Settings → Touch Beep**. (Default is **off**.)
- It only clicks on real taps, not when you scroll or drag.
- It's deliberately soft and low — not the old loud beep. Nothing to configure; flip it on and it just
  works.

---

## 2. `M300` — a single beep

The standard slicer/G-code beep command, now with real pitch:

```
M300 S<frequency Hz> P<duration ms>
```

| Example | What you hear |
|---|---|
| `M300 S1000 P200` | 1 kHz tone for 200 ms |
| `M300 S440 P500`  | low A note, half a second |
| `M300 S2000 P80`  | short high blip |
| `BEEP`            | quick confirmation chirp (shortcut for `M300 S2000 P120`) |

Many slicers can emit `M300` on events (e.g. "beep when the print finishes") — now it plays the pitch
and length you actually ask for.

---

## 3. `PLAY_TUNE` — play a song

```
PLAY_TUNE SONG=mario
PLAY_TUNE SONG=zelda
PLAY_TUNE SONG=starwars
PLAY_TUNE SONG=success      # short happy jingle
PLAY_TUNE SONG=error        # short low alert
```

These names come from a plain text file you can edit (see below). Great for a "print complete" or
"print failed" jingle.

### Add your own songs (`songs.conf`)

Songs live in:

```
/usr/data/printer_data/config/songs.conf
```

That's in the same config folder you already see in **Mainsail/Fluidd's file manager**, so you can edit
it right in your browser (or over SSH). The format is one song per line, `name = RTTTL`:

```ini
# Lines starting with # or ; are comments.
mario   = mario:d=4,o=5,b=100:16e6,16e6,32p,16e6,...
success = success:d=4,o=6,b=140:8c,8e,8g,c
tetris  = tetris:d=4,o=5,b=160:e6,8b,8c6,8d6,16e6,16d6,8c6,8b,a,8a,8c6,e6,8d6,8c6,b
```

To add a tune: grab an **RTTTL** string (the old Nokia ringtone format — thousands are online), paste it
in, give it a name, save, then run **`FIRMWARE_RESTART`**. Now `PLAY_TUNE SONG=yourname` plays it. No
recompiling, no reflashing.

Two things worth knowing:

- **Your songs survive updates.** Reinstalling or updating OpenKE only creates `songs.conf` if it's
  missing — it never overwrites the one you've edited.
- **Use normal `#` sharps here.** In `songs.conf` you can paste RTTTL exactly as found online
  (`a#`, `f#`, …). The buzzer player reads this file directly, so the sharps come through fine.

### Playing a one-off without editing the file

```
PLAY_TUNE RTTTL=mytune:d=4,o=5,b=120:c,e,g,8c6
```

For this **inline** form only, there are two rules (because the command passes through Klipper's G-code
parser first):

- **No spaces** in the string.
- Write sharps as **`s`** (e.g. `as`, `cs`, `fs`) — *not* `#`, which G-code treats as a comment.

For anything you'll reuse, just put it in `songs.conf` and use `SONG=` — no rules to remember.

---

## Using it from your slicer

Drop a command in your slicer's **End G-code**, on its own line:

```
PLAY_TUNE SONG=mario
```

It's a normal Klipper command, so the slicer passes it straight through. A few tips:

- Put it at the **very end** of your end-gcode. A song blocks the command queue while it plays — which
  is fine once the print is finished, and that's why end-of-print is the right spot.
- For a quick "done" chirp instead of a whole song, use `M300 S1500 P300` or `BEEP`.
- The slicer's end-gcode only runs on a **successful** finish. To get a sound on *failure*, trigger
  `PLAY_TUNE SONG=error` from a cancel/error macro instead.

> **Don't play long songs mid-print.** `M300` blips are instant and fine anytime, but a full `PLAY_TUNE`
> takes several seconds and the printer waits for it to end. Save songs for idle moments — print done,
> error, startup.

---

## Good to know

- **Some notes are louder than others.** The buzzer is a tiny piezo with a natural resonance around
  2.3 kHz, so volume varies a bit from note to note. The *pitch* is always accurate — only loudness
  changes. That's the physical hardware, not a bug.
- **It's a 1-bit beeper** — think classic 8-bit chiptune, not hi-fi. There's no speaker or audio chip on
  the KE.
- **Safe during prints.** Because it's true hardware PWM, making sound uses essentially no CPU and won't
  disturb a running job.
