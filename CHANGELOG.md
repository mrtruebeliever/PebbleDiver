# Changelog

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
