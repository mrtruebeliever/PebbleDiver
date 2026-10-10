# Changelog

## 1.2.0

- **Underwater sounds.** The chiptune square-wave blips are replaced by sounds
  that belong under water: a glassy two-voice "ting" for a pearl, a real
  bubble for an air bubble, a dull knock for a collision (both samples
  synthesised by `tools/gen_sfx.py`), and a slow falling line with rising
  bubbles when the air runs out. New sounds where there were none: a dark
  two-note motif when a shark appears, a metallic clang for a sword parry
  (it used to play the shop's purchase sound), a twinkle when the magic Pebble
  appears, a three-voice shimmer when it is found, and a bright climb instead
  of the drowning line on a new depth record. Low oxygen now repeats a soft
  double beep every two seconds while the tank stays nearly empty. Uses
  `speaker_play_tracks()` for the layered sounds.
- **Follows the watch's speaker mute.** With the speaker muted in the watch
  settings, the game plays silent (as in Quiet Time) and the Sound row in
  Settings reads "Muted".
- **The backlight follows the water (Pebble Time 2).** While diving, the
  colour backlight runs from pale cyan at the surface to deep blue at 250 m,
  and flashes red for a moment on a collision. Back on land (title, shop,
  pause, game over) it is the watch's own colour again.

## 1.1.0

- **Sharks.** A new hazard that actively swims toward your depth instead of
  drifting past — starts appearing from 30 m down, one at a time. Get too
  close without defending yourself and it costs you like any other collision.
- **Sword**, a fourth shop upgrade (5 levels, same cost curve as the rest):
  with a sword equipped, a tap of **UP** or a tap on the screen while a shark
  is in range fends it off and pays out pearls; without one, dodging is your
  only option.
- Touch (Pebble Time 2): tap the screen to fend off a shark in range, in
  addition to UP.

## 1.0.0

- Initial release: a one-button dive game for Pebble Time 2. Hold **SELECT**
  to swim up, hold **DOWN** to dive faster, release to sink.
- **Oxygen meter** drains continuously; colliding with rocks or jellyfish
  costs 6 seconds, air bubbles refill 6 seconds, and a guaranteed-bubble
  "pity" system keeps a dry spell from ever running unwinnably long.
- **Pearls** are the currency, collected while diving and persisted between
  runs.
- **Shop** with 3 upgrades × 5 levels each: Oxygen tank (+8 s per level),
  Fins (faster swimming), Lamp (attracts nearby pearls). Costs scale
  15 / 40 / 90 / 160 / 250 pearls per level.
- Water darkens and current speed/density increase with depth for a real
  sense of descent.
- **Endgame**: once every upgrade is maxed, a color-shifting magic Pebble
  starts appearing from 200 m down — catch it for a victory screen and a
  trophy on the title screen ever after.
- Localized in English (default/fallback), Nederlands, Français, Deutsch and
  Español, following the watch's system language.
- Auto-pauses on incoming notifications, and locks input for 1 second after
  game over so a reflexive button press can't accidentally start a new run.
