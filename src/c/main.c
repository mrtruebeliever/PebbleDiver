#include <pebble.h>

// ---------------------------------------------------------------------------
// PebbleDiver — one-button dive game for Pebble Time 2 (emery)
// Hold SELECT to swim up, DOWN to dive, release to sink. Dodge rocks &
// jellyfish, collect pearls, spend them on upgrades between runs.
// ---------------------------------------------------------------------------

#define TICK_MS 33            // ~30 fps
#define FP 8                  // 8.8 fixed point
#define HUD_H 26

#define DIVER_X 46
#define DIVER_R 8             // collision radius

#define MAX_ENT 14
#define MAX_PART 10

#define MAX_LEVEL 5
#define BASE_O2_MS 30000
#define O2_PER_TANK_MS 8000
#define O2_BUBBLE_MS 6000
#define O2_HIT_MS 6000

// Persist keys
#define PK_PEARLS 1
#define PK_BEST 2
#define PK_TANK 3
#define PK_FINS 4
#define PK_LAMP 5
#define PK_FOUND 6
#define PK_BACKLIGHT 7      // legacy bool; superseded by PK_BL_MODE
#define PK_VIBRATION 8
#define PK_SOUND 9          // legacy on/off bool; superseded by PK_SOUND_VOL
#define PK_BL_MODE 10
#define PK_SOUND_VOL 11
#define PK_SWORD 12

#define BL_TIMEOUT_MS 30000

// The magic Pebble first appears at this depth (fully upgraded divers only)
#define PEBBLE_MIN_M 200
#define PEBBLE_RETRY_M 40

// Shark: an active hazard that homes in on the diver's depth instead of
// drifting past like a rock or jelly. Starting values, to be calibrated on
// real hardware.
#define SHARK_MIN_DEPTH_M    30   // sharks start appearing once the water gets this deep
#define SHARK_SPAWN_PCT      8    // % chance a spawn roll picks a shark, when eligible
// px/tick it closes in on the diver's y while approaching. Must stay below the
// diver's own slowest sustained climb (~1.2 px/tick, unupgraded, vmax_up=310fp
// -> 310/256) or the chase is literally unwinnable -- the diver can never
// out-climb a homing target that corrects faster than it can move.
#define SHARK_HOME_SPEED     1
#define SHARK_STRIKE_RANGE   55   // px either side of the diver: the parry-or-dodge window

typedef enum { ST_TITLE, ST_PLAYING, ST_PAUSED, ST_GAMEOVER, ST_VICTORY, ST_SHOP, ST_SETTINGS } GameState;
typedef enum { ENT_NONE, ENT_ROCK, ENT_JELLY, ENT_PEARL, ENT_BUBBLE, ENT_PEBBLE, ENT_SHARK } EntType;
// Backlight: off (system default), timeout (lit while interacting, then dims),
// or always (forced 100% on). Mirrors Switchback's 3-state model.
typedef enum { BL_OFF, BL_TIMED, BL_ALWAYS, NUM_BL_MODES } BacklightMode;

typedef struct {
  EntType type;
  int32_t x_fp;      // fixed-point x (px<<FP)
  int16_t y;         // px (jelly: current, wobbles around base_y)
  int16_t base_y;
  int16_t r;         // collision radius
  uint16_t phase;    // jelly wobble phase
} Entity;

typedef struct {
  bool alive;
  int16_t x, y;
  int8_t life;
} Particle;

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;

static GameState s_state = ST_TITLE;
static bool s_thrust;
static bool s_dive;                // DOWN held: swim down actively
static bool s_go_lock;             // brief input lock on the game-over screen
static int32_t s_diver_y_fp, s_diver_vy_fp;
static int32_t s_oxygen_ms, s_max_oxygen_ms;
static int32_t s_dist_px;          // total scrolled px this run
static int32_t s_spawn_px;         // px until next spawn
static int32_t s_px_since_bubble;  // px scrolled since last bubble spawn
static int16_t s_invuln;           // ticks of invulnerability left
static bool s_o2_warned;
static int32_t s_o2_beep_ms;       // countdown to the next low-oxygen beep
#define O2_BEEP_EVERY_MS 2000
static uint32_t s_tick;

static Entity s_ents[MAX_ENT];
static Particle s_parts[MAX_PART];

// bank / progress
static int32_t s_pearls, s_run_pearls;
static int32_t s_best_m;
static int8_t s_tank, s_fins, s_lamp, s_sword;
static bool s_new_record;
static bool s_found;               // magic Pebble found at least once
static bool s_pebble_active;       // magic Pebble currently in the water
static int32_t s_next_pebble_m;    // depth at which the next one appears

// shop
static int8_t s_shop_sel;
static int8_t s_shop_flash;        // "not enough pearls" flash ticks
static const int32_t SHOP_COST[MAX_LEVEL] = {15, 40, 90, 160, 250};

// settings
static int8_t s_bl_mode = BL_OFF;  // BacklightMode
static bool s_vibration_on = true;
static int8_t s_sound_vol = 1;     // 0=Off 1=Soft 2=Normal 3=Loud (chiptune SFX)
static int8_t s_settings_sel;      // settings-menu row
static bool s_reset_armed;         // "Reset save" needs a confirming second press
static AppTimer *s_backlight_timer;

static GFont s_f14, s_f18, s_f18b, s_f24b, s_f28b;

// ---------------------------------------------------------------------------
// Localization — device language via i18n_get_system_locale(), English
// fallback. Strings are UTF-8; Pebble system fonts cover Latin-1.
// ---------------------------------------------------------------------------

typedef enum { L_EN, L_NL, L_FR, L_DE, L_ES, NUM_LANGS } Lang;

enum {
  S_TITLE, S_HINT_DIVE, S_HINT_SHOP, S_STATS,
  S_O2_OUT, S_DEPTH, S_RUN_PEARLS, S_NEW_RECORD, S_HINT_RETRY,
  S_SHOP_TITLE, S_ITEM_TANK, S_ITEM_FINS, S_ITEM_LAMP, S_ITEM_SWORD,
  S_DESC_TANK, S_DESC_FINS, S_DESC_LAMP, S_DESC_SWORD, S_SHOP_HINT,
  S_PAUSED, S_HINT_RESUME,
  S_WIN_TITLE, S_LEGEND,
  S_HINT_SETTINGS, S_SETTINGS_TITLE, S_OPT_BACKLIGHT, S_OPT_VIBRATION,
  S_ON, S_OFF, S_SETTINGS_HINT,
  S_OPT_SOUND, S_OPT_RESET, S_RESET_CONFIRM, S_BL_TIMED, S_BL_ALWAYS,
  S_HINT_CONTROLS,
  S_SND_SOFT, S_SND_NORMAL, S_SND_LOUD, S_SND_MUTED,
  NUM_STRS
};

static const char *TR[NUM_LANGS][NUM_STRS] = {
  [L_EN] = {
    "PebbleDiver", "SELECT · dive", "DOWN · shop",
    "Best: %ld m  ·  Pearls: %ld",
    "Out of air!", "Depth: %ld m", "Pearls: +%ld", "New record!",
    "SELECT · again",
    "Shop", "Oxygen tank", "Fins", "Lamp", "Sword",
    "+8s oxygen", "swim faster", "attracts pearls", "fight back the shark",
    "SELECT · buy    BACK · close",
    "Paused", "SELECT · resume",
    "You found the magic Pebble!",
    "The magic Pebble waits deep below...",
    "UP · settings", "Settings", "Backlight", "Vibration",
    "On", "Off", "UP/DOWN · move   SELECT · toggle   BACK · close",
    "Sound", "Reset save", "Press again to erase", "Timeout", "Always",
    "SELECT up · DOWN dive",
    "Soft", "Normal", "Loud", "Muted",
  },
  [L_NL] = {
    "PebbleDiver", "SELECT · duiken", "OMLAAG · winkel",
    "Record: %ld m  ·  Parels: %ld",
    "Zuurstof op!", "Diepte: %ld m", "Parels: +%ld", "Nieuw record!",
    "SELECT · opnieuw",
    "Winkel", "Zuurstoftank", "Vinnen", "Lamp", "Zwaard",
    "+8s zuurstof", "sneller zwemmen", "trekt parels aan", "sla de haai terug",
    "SELECT · kopen    TERUG · sluiten",
    "Gepauzeerd", "SELECT · verder",
    "Je vond de magische Pebble!",
    "De magische Pebble wacht in de diepte...",
    "OMHOOG · instellingen", "Instellingen", "Verlichting", "Trilling",
    "Aan", "Uit", "OMHOOG/OMLAAG · kiezen   SELECT · wisselen   TERUG · sluiten",
    "Geluid", "Wissen", "Nogmaals om te wissen", "Time-out", "Altijd",
    "SELECT omhoog · OMLAAG duik",
    "Zacht", "Normaal", "Luid", "Gedempt",
  },
  [L_FR] = {
    "PebbleDiver", "SELECT · plonger", "BAS · boutique",
    "Record : %ld m  ·  Perles : %ld",
    "Plus d'air !", "Prof. : %ld m", "Perles : +%ld", "Nouveau record !",
    "SELECT · rejouer",
    "Boutique", "Bouteille d'O2", "Palmes", "Lampe", "Épée",
    "+8 s d'oxygène", "nage plus vite", "attire les perles", "repousse le requin",
    "SELECT · acheter   RETOUR · fermer",
    "Pause", "SELECT · reprendre",
    "Pebble magique trouvé !",
    "Le Pebble magique attend au fond...",
    "HAUT · réglages", "Réglages", "Éclairage", "Vibration",
    "Activé", "Désactivé", "HAUT/BAS · choisir   SELECT · basculer   RETOUR · fermer",
    "Son", "Effacer", "Encore pour effacer", "Minuteur", "Toujours",
    "SELECT monter · BAS plonger",
    "Doux", "Normal", "Fort", "Muet",
  },
  [L_DE] = {
    "PebbleDiver", "SELECT · tauchen", "RUNTER · Laden",
    "Rekord: %ld m  ·  Perlen: %ld",
    "Keine Luft!", "Tiefe: %ld m", "Perlen: +%ld", "Neuer Rekord!",
    "SELECT · nochmal",
    "Laden", "Sauerstofftank", "Flossen", "Lampe", "Schwert",
    "+8s Sauerstoff", "schneller schwimmen", "zieht Perlen an", "wehr den Hai ab",
    "SELECT · kaufen  ZURÜCK · schließen",
    "Pausiert", "SELECT · weiter",
    "Magischer Pebble gefunden!",
    "Der magische Pebble wartet tief unten...",
    "HOCH · Einstellungen", "Einstellungen", "Beleuchtung", "Vibration",
    "An", "Aus", "HOCH/RUNTER · wählen  SELECT · umschalten  ZURÜCK · schließen",
    "Ton", "Löschen", "Nochmal zum Löschen", "Zeitlimit", "Immer",
    "SELECT hoch · RUNTER tauchen",
    "Leise", "Normal", "Laut", "Stumm",
  },
  [L_ES] = {
    "PebbleDiver", "SELECT · bucear", "ABAJO · tienda",
    "Récord: %ld m  ·  Perlas: %ld",
    "¡Sin aire!", "Prof.: %ld m", "Perlas: +%ld", "¡Nuevo récord!",
    "SELECT · otra vez",
    "Tienda", "Tanque de O2", "Aletas", "Lámpara", "Espada",
    "+8 s de oxígeno", "nada más rápido", "atrae las perlas", "rechaza al tiburón",
    "SELECT · comprar   ATRÁS · cerrar",
    "Pausa", "SELECT · seguir",
    "¡Pebble mágico encontrado!",
    "El Pebble mágico espera en el fondo...",
    "ARRIBA · ajustes", "Ajustes", "Luz", "Vibración",
    "Activado", "Desactivado", "ARRIBA/ABAJO · elegir   SELECT · cambiar   ATRÁS · cerrar",
    "Sonido", "Borrar", "Otra vez para borrar", "Con tiempo", "Siempre",
    "SELECT sube · ABAJO bucea",
    "Bajo", "Normal", "Alto", "Silencio",
  },
};

static const char **s_tr = TR[L_EN];
#define S(id) s_tr[id]

static void pick_language(void) {
  const char *loc = i18n_get_system_locale();
  if (!loc) return;
  if (strncmp(loc, "nl", 2) == 0) s_tr = TR[L_NL];
  else if (strncmp(loc, "fr", 2) == 0) s_tr = TR[L_FR];
  else if (strncmp(loc, "de", 2) == 0) s_tr = TR[L_DE];
  else if (strncmp(loc, "es", 2) == 0) s_tr = TR[L_ES];
  else s_tr = TR[L_EN];
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int32_t depth_m(void) { return s_dist_px / 12; }

static GColor water_color(int32_t m) {
  if (m < 40) return GColorVividCerulean;
  if (m < 90) return GColorBlueMoon;
  if (m < 150) return GColorCobaltBlue;
  if (m < 220) return GColorBlue;
  return GColorDukeBlue;
}

static void save_progress(void) {
  persist_write_int(PK_PEARLS, s_pearls);
  persist_write_int(PK_BEST, s_best_m);
  persist_write_int(PK_TANK, s_tank);
  persist_write_int(PK_FINS, s_fins);
  persist_write_int(PK_LAMP, s_lamp);
  persist_write_int(PK_SWORD, s_sword);
  persist_write_bool(PK_FOUND, s_found);
}

static void load_progress(void) {
  s_pearls = persist_read_int(PK_PEARLS);
  s_best_m = persist_read_int(PK_BEST);
  s_tank = persist_read_int(PK_TANK);
  s_fins = persist_read_int(PK_FINS);
  s_lamp = persist_read_int(PK_LAMP);
  s_sword = persist_read_int(PK_SWORD);
  s_found = persist_read_bool(PK_FOUND);
  // Migrate the old on/off backlight bool to the 3-state mode on first run.
  if (persist_exists(PK_BL_MODE)) {
    s_bl_mode = persist_read_int(PK_BL_MODE);
  } else {
    s_bl_mode = persist_read_bool(PK_BACKLIGHT) ? BL_TIMED : BL_OFF;
  }
  if (s_bl_mode < 0 || s_bl_mode >= NUM_BL_MODES) s_bl_mode = BL_OFF;
  s_vibration_on = persist_exists(PK_VIBRATION) ? persist_read_bool(PK_VIBRATION) : true;
  // Migrate the old on/off sound bool to the 4-level volume. It only ever said
  // "on", never "loud", so it lands on the same Soft the fresh install gets.
  if (persist_exists(PK_SOUND_VOL)) s_sound_vol = persist_read_int(PK_SOUND_VOL);
  else if (persist_exists(PK_SOUND)) s_sound_vol = persist_read_bool(PK_SOUND) ? 1 : 0;
  else s_sound_vol = 1;
  if (s_sound_vol < 0 || s_sound_vol > 3) s_sound_vol = 1;
}

static void vibe_short(void)  { if (s_vibration_on) vibes_short_pulse(); }
static void vibe_long(void)   { if (s_vibration_on) vibes_long_pulse(); }
static void vibe_double(void) { if (s_vibration_on) vibes_double_pulse(); }

// ---------------------------------------------------------------------------
// Sound effects (PT2 speaker), all gated by the Sound setting. The game is under
// water, so the sounds are too: soft sine and triangle voices, a real bubble and
// a dull knock instead of chiptune square waves. The bubble and the knock are
// samples synthesised by tools/gen_sfx.py (resources/sfx/); the rest are notes.
// Several effects stack voices with speaker_play_tracks() -- a pearl's "ting"
// is a note plus a quieter fifth above it, which a single waveform can't do.
// MIDI notes: 60 = C4. Velocity 0 = the call's volume; a nonzero velocity per
// note is how the effects shape their own fade.
// ---------------------------------------------------------------------------
#define SFX_VOL 80
#define SFX_N(a) (sizeof(a) / sizeof((a)[0]))
#define SI  SpeakerWaveformSine
#define SQ  SpeakerWaveformSquare
#define TR_ SpeakerWaveformTriangle
#define SAW SpeakerWaveformSawtooth
#define REST(ms) {0, SI, ms, 0, 0}

// Samples: loaded from resources once (sfx_load), played pitch-shifted. The
// waveform field of a sample track's notes is ignored. Base note = the pitch
// the sample was rendered at, so a note of SAMPLE_BASE plays it unshifted.
#define SAMPLE_BASE 72
static SpeakerSample s_smp_bubble = { NULL, 0, SpeakerPcmFormat_16kHz_8bit, SAMPLE_BASE, false };
static SpeakerSample s_smp_thud   = { NULL, 0, SpeakerPcmFormat_16kHz_8bit, SAMPLE_BASE, false };

// Pearl: a glassy "ting" -- B6 fading out, with a quiet F#7 shimmer on top.
static const SpeakerNote PEARL_A[] = {{95, SI, 30, 120, 0}, {95, SI, 40, 70, 0}, {95, SI, 70, 30, 0}};
static const SpeakerNote PEARL_B[] = {{102, SI, 25, 55, 0}, {102, SI, 45, 25, 0}};
// Air bubble: two blubs, the second higher, like a bubble wobbling up.
static const SpeakerNote BUBBLE_N[] = {{72, SI, 90, 110, 0}, {79, SI, 100, 95, 0}};
// Collision: a dull knock on rock or a jellyfish, not an electric buzz.
static const SpeakerNote THUD_N[] = {{72, SI, 160, 127, 0}};
// Low oxygen: a soft double beep, repeated while the tank stays nearly empty.
static const SpeakerNote LOW_O2[] = {{81, TR_, 60, 110, 0}, REST(70), {81, TR_, 60, 110, 0}};
// Drowned: a slow falling line while the last air bubbles up past it.
static const SpeakerNote DROWN_A[] = {{76, TR_, 200, 110, 0}, {72, TR_, 200, 100, 0},
                                      {69, TR_, 200, 90, 0}, {64, TR_, 260, 80, 0},
                                      {64, TR_, 200, 35, 0}};
static const SpeakerNote DROWN_B[] = {REST(80), {84, SI, 90, 80, 0}, REST(160), {88, SI, 90, 70, 0},
                                      REST(200), {91, SI, 90, 55, 0}, REST(180), {96, SI, 90, 40, 0}};
// New depth record (instead of the drowning line): a bright climb.
static const SpeakerNote RECORD_A[] = {{72, TR_, 80, 100, 0}, {76, TR_, 80, 100, 0},
                                       {79, TR_, 80, 100, 0}, {84, TR_, 260, 110, 0}, {84, TR_, 200, 40, 0}};
static const SpeakerNote RECORD_B[] = {REST(240), {88, SI, 80, 50, 0}, {91, SI, 260, 45, 0}};
// A shark closes in: two dark notes a semitone apart, closing in faster.
static const SpeakerNote SHARK[] = {{64, SAW, 240, 100, 0}, {65, SAW, 240, 100, 0}, REST(160),
                                    {64, SAW, 150, 110, 0}, {65, SAW, 150, 110, 0},
                                    {64, SAW, 110, 120, 0}, {65, SAW, 110, 120, 0}};
// Sword parry: two square notes a semitone apart beat against each other,
// which is what makes metal ring.
static const SpeakerNote CLANG_A[] = {{88, SQ, 40, 110, 0}, {88, SQ, 60, 55, 0}, {88, SQ, 80, 20, 0}};
static const SpeakerNote CLANG_B[] = {{89, SQ, 40, 90, 0}, {89, SQ, 60, 45, 0}, {89, SQ, 80, 15, 0}};
// The magic Pebble appears: a quick high twinkle.
static const SpeakerNote TWINKLE[] = {{96, SI, 35, 70, 0}, {100, SI, 35, 70, 0}, {103, SI, 35, 70, 0},
                                      {108, SI, 90, 50, 0}};
// The magic Pebble found: a shimmering arpeggio in three voices.
static const SpeakerNote WIN_A[] = {{84, SI, 90, 110, 0}, {88, SI, 90, 110, 0}, {91, SI, 90, 110, 0},
                                    {96, SI, 360, 110, 0}, {96, SI, 300, 40, 0}};
static const SpeakerNote WIN_B[] = {REST(45), {72, TR_, 90, 80, 0}, {76, TR_, 90, 80, 0},
                                    {79, TR_, 90, 80, 0}, {84, TR_, 560, 70, 0}};
static const SpeakerNote WIN_C[] = {REST(360), {103, SI, 30, 45, 0}, REST(60), {108, SI, 30, 40, 0},
                                    REST(90), {105, SI, 30, 35, 0}, REST(120), {110, SI, 30, 30, 0}};
// Shop: confirm and refuse.
static const SpeakerNote SFX_BUY[]  = {{79, TR_, 60, 0, 0}, {84, TR_, 130, 0, 0}};
static const SpeakerNote SFX_DENY[] = {{55, SQ, 90, 0, 0}, {51, SQ, 150, 0, 0}};

#define TRACK(notes)          { notes, SFX_N(notes), NULL }
#define SAMPLE_TRACK(n, smp)  { n, SFX_N(n), &(smp) }

// Master volume per Sound setting (Off/Soft/Normal/Loud). Each SFX keeps its own
// tuned base loudness; sv() scales it by the chosen level, so Loud == the
// original tuning and lower levels attenuate everything proportionally.
static const uint8_t SND_VOL[4] = {0, 35, 70, 100};
static uint8_t sv(uint8_t base) { return (uint16_t)base * SND_VOL[s_sound_vol & 3] / 100; }

// Quiet Time is the watch saying "not now" — a game blipping through it is
// exactly what the user turned it on to stop. The setting stays where it was;
// this only silences the speaker while the mode is active.
// Quiet Time and the watch's own mute (Settings → Sounds & Haptics) both win
// over the in-game Sound level.
static bool sfx_enabled(void) { return s_sound_vol && !quiet_time_is_active() && !speaker_is_muted(); }

static void sfx_play(const SpeakerNote *n, uint32_t c) { if (sfx_enabled()) speaker_play_notes(n, c, sv(SFX_VOL)); }
static void sfx_tracks(const SpeakerTrack *t, uint32_t c) {
  if (sfx_enabled()) speaker_play_tracks(t, c, sv(SFX_VOL));
}

// A sample whose resource failed to load plays as nothing rather than garbage.
static void load_sample(SpeakerSample *smp, uint32_t resource_id) {
  ResHandle h = resource_get_handle(resource_id);
  size_t size = resource_size(h);
  uint8_t *buf = malloc(size);
  if (!buf) return;
  smp->num_bytes = resource_load(h, buf, size);
  smp->data = buf;
}

// Loaded once and never freed: a sound can still be playing from a buffer when
// the app closes, and the system reclaims the app's heap after it stops it.
static void sfx_load(void) {
  load_sample(&s_smp_bubble, RESOURCE_ID_SFX_BUBBLE);
  load_sample(&s_smp_thud, RESOURCE_ID_SFX_THUD);
}

static void sfx_sample(const SpeakerNote *n, uint32_t c, SpeakerSample *smp) {
  if (!smp->data) return;
  SpeakerTrack t = { n, c, smp };
  sfx_tracks(&t, 1);
}

static void sfx_pearl(void) {
  static const SpeakerTrack t[] = { TRACK(PEARL_A), TRACK(PEARL_B) };
  sfx_tracks(t, SFX_N(t));
}
static void sfx_bubble(void) { sfx_sample(BUBBLE_N, SFX_N(BUBBLE_N), &s_smp_bubble); }
static void sfx_hit(void)    { sfx_sample(THUD_N, SFX_N(THUD_N), &s_smp_thud); }
static void sfx_low_o2(void) { sfx_play(LOW_O2, SFX_N(LOW_O2)); }
static void sfx_drown(void) {
  static const SpeakerTrack t[] = { TRACK(DROWN_A), TRACK(DROWN_B) };
  sfx_tracks(t, SFX_N(t));
}
static void sfx_record(void) {
  static const SpeakerTrack t[] = { TRACK(RECORD_A), TRACK(RECORD_B) };
  sfx_tracks(t, SFX_N(t));
}
static void sfx_shark(void)   { sfx_play(SHARK, SFX_N(SHARK)); }
static void sfx_clang(void) {
  static const SpeakerTrack t[] = { TRACK(CLANG_A), TRACK(CLANG_B) };
  sfx_tracks(t, SFX_N(t));
}
static void sfx_twinkle(void) { sfx_play(TWINKLE, SFX_N(TWINKLE)); }
static void sfx_win(void) {
  static const SpeakerTrack t[] = { TRACK(WIN_A), TRACK(WIN_B), TRACK(WIN_C) };
  sfx_tracks(t, SFX_N(t));
}
static void sfx_buy(void)    { sfx_play(SFX_BUY,  SFX_N(SFX_BUY)); }
static void sfx_deny(void)   { sfx_play(SFX_DENY, SFX_N(SFX_DENY)); }

// Backlight. ALWAYS forces the light on until we release it; TIMED keeps it lit
// while the player interacts and auto-dims after BL_TIMEOUT_MS of stillness;
// OFF hands back to the watch's normal auto-dim.
static void backlight_timeout(void *data) {
  s_backlight_timer = NULL;
  light_enable(false);
}

// Push the auto-off further out (or start it). No-op unless in TIMED mode.
static void backlight_touch(void) {
  if (s_bl_mode != BL_TIMED) return;
  light_enable(true);
  if (s_backlight_timer) {
    app_timer_reschedule(s_backlight_timer, BL_TIMEOUT_MS);
  } else {
    s_backlight_timer = app_timer_register(BL_TIMEOUT_MS, backlight_timeout, NULL);
  }
}

static void backlight_stop_timer(void) {
  if (s_backlight_timer) {
    app_timer_cancel(s_backlight_timer);
    s_backlight_timer = NULL;
  }
}

static void apply_backlight(void) {
  backlight_stop_timer();
  switch (s_bl_mode) {
    case BL_ALWAYS: light_enable(true); break;
    case BL_TIMED:  backlight_touch(); break;
    default:        light_enable(false); break;
  }
}

// Colour backlight (Time 2; a no-op elsewhere). While diving the light follows
// the water, pale cyan at the surface to deep blue by 250 m, and flashes red
// for a moment on a hit. Off the dive it hands back to the user's own colour.
#define TINT_SHALLOW 0xB4F0FFu
#define TINT_DEEP    0x1E3CFFu
#define TINT_HIT     0xFF2814u
#define TINT_DEEP_M  250
#define TINT_STEPS   25        // one step per 10 m
#define HIT_FLASH_TICKS 9      // ~300 ms
static int16_t s_hit_flash;    // ticks of red left
static int s_tint = -1;        // last applied: -1 system, -2 hit, else depth step

static uint32_t lerp_rgb(uint32_t a, uint32_t b, int step, int steps) {
  uint32_t out = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    int32_t ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
    out |= (uint32_t)(ca + (cb - ca) * step / steps) << shift;
  }
  return out;
}

// Called from every redraw: each state change and every game frame passes
// through layer_update, and the cached step keeps the LED driver out of it
// unless the colour actually changes.
static void apply_tint(void) {
  int want;
  if (s_state != ST_PLAYING) want = -1;
  else if (s_hit_flash > 0)  want = -2;
  else {
    int32_t m = depth_m();
    want = (int)((m > TINT_DEEP_M ? TINT_DEEP_M : m) * TINT_STEPS / TINT_DEEP_M);
  }
  if (want == s_tint) return;
  s_tint = want;
  if (want == -1)      light_set_system_color();
  else if (want == -2) light_set_color_rgb888(TINT_HIT);
  else                 light_set_color_rgb888(lerp_rgb(TINT_SHALLOW, TINT_DEEP, want, TINT_STEPS));
}

static bool all_upgrades_max(void) {
  return s_tank >= MAX_LEVEL && s_fins >= MAX_LEVEL && s_lamp >= MAX_LEVEL
      && s_sword >= MAX_LEVEL;
}

// A shark is dangerous (collides) and parryable only within this corridor
// around the diver; outside it, it's either still closing in or already spent.
static bool shark_in_range(const Entity *e) {
  int16_t ex = e->x_fp >> FP;
  int16_t d = ex - DIVER_X;
  if (d < 0) d = -d;
  return d <= SHARK_STRIKE_RANGE;
}

static bool shark_active(void) {
  for (int i = 0; i < MAX_ENT; i++) {
    if (s_ents[i].type == ENT_SHARK) return true;
  }
  return false;
}

static void stop_timer(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
}

// ---------------------------------------------------------------------------
// Run lifecycle
// ---------------------------------------------------------------------------

static void game_tick(void *data);

static void start_run(void) {
  GRect b = layer_get_bounds(s_layer);
  s_diver_y_fp = ((HUD_H + (b.size.h - HUD_H) / 2) << FP);
  s_diver_vy_fp = 0;
  s_max_oxygen_ms = BASE_O2_MS + s_tank * O2_PER_TANK_MS;
  s_oxygen_ms = s_max_oxygen_ms;
  s_dist_px = 0;
  s_spawn_px = 90;
  s_px_since_bubble = 0;
  s_run_pearls = 0;
  s_invuln = 45;
  s_hit_flash = 0;
  s_o2_warned = false;
  s_thrust = false;
  s_dive = false;
  s_new_record = false;
  s_pebble_active = false;
  s_next_pebble_m = PEBBLE_MIN_M;
  for (int i = 0; i < MAX_ENT; i++) s_ents[i].type = ENT_NONE;
  for (int i = 0; i < MAX_PART; i++) s_parts[i].alive = false;
  s_state = ST_PLAYING;
  stop_timer();
  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  layer_mark_dirty(s_layer);
}

static void go_unlock(void *data) {
  s_go_lock = false;
  layer_mark_dirty(s_layer);
}

static void finish_run(void) {
  stop_timer();
  s_thrust = false;
  s_dive = false;
  s_pearls += s_run_pearls;
  int32_t m = depth_m();
  if (m > s_best_m) {
    s_best_m = m;
    s_new_record = true;
  }
  save_progress();
}

static void end_run(bool drowned) {
  finish_run();
  s_state = drowned ? ST_GAMEOVER : ST_TITLE;
  if (drowned) {
    vibe_long();
    // A new depth record is the news worth hearing, even when the air ran out.
    if (s_new_record) sfx_record(); else sfx_drown();
    s_go_lock = true;
    app_timer_register(1000, go_unlock, NULL);
  }
  layer_mark_dirty(s_layer);
}

static void win_run(void) {
  s_found = true;
  finish_run();
  s_state = ST_VICTORY;
  vibe_double();
  sfx_win();
  s_go_lock = true;
  app_timer_register(1000, go_unlock, NULL);
  layer_mark_dirty(s_layer);
}

// ---------------------------------------------------------------------------
// Spawning & update
// ---------------------------------------------------------------------------

static void spawn_entity(int16_t screen_w, int16_t screen_h) {
  Entity *e = NULL;
  for (int i = 0; i < MAX_ENT; i++) {
    if (s_ents[i].type == ENT_NONE) { e = &s_ents[i]; break; }
  }
  if (!e) return;

  // Pity system: guarantee a bubble if none spawned for a while,
  // sooner when oxygen is running low
  bool low_o2 = s_oxygen_ms < s_max_oxygen_ms / 3;
  if (s_px_since_bubble > (low_o2 ? 260 : 540)) {
    e->type = ENT_BUBBLE;
  } else {
    int roll = rand() % 100;
    // Sharks start appearing once the water is deep enough, one at a time —
    // a distinct encounter rather than just another entity in the rotation.
    if (depth_m() >= SHARK_MIN_DEPTH_M && !shark_active() && roll < SHARK_SPAWN_PCT) {
      e->type = ENT_SHARK;
      sfx_shark();
    } else if (roll < 35) e->type = ENT_ROCK;
    else if (roll < 60) e->type = ENT_JELLY;
    else if (roll < 85) e->type = ENT_PEARL;
    else e->type = ENT_BUBBLE;
  }
  if (e->type == ENT_BUBBLE) s_px_since_bubble = 0;

  int16_t top = HUD_H + 14;
  int16_t bot = screen_h - 12;
  e->base_y = top + rand() % (bot - top);
  e->y = e->base_y;
  e->x_fp = (screen_w + 16) << FP;
  e->phase = rand() % TRIG_MAX_ANGLE;
  switch (e->type) {
    case ENT_ROCK:  e->r = 9 + rand() % 5; break;
    case ENT_JELLY: e->r = 7; break;
    case ENT_PEARL: e->r = 4; break;
    case ENT_SHARK: e->r = 8; break;
    default:        e->r = 5; break;
  }
}

static void spawn_magic_pebble(int16_t screen_w, int16_t screen_h) {
  for (int i = 0; i < MAX_ENT; i++) {
    if (s_ents[i].type == ENT_NONE) {
      Entity *e = &s_ents[i];
      e->type = ENT_PEBBLE;
      int16_t top = HUD_H + 24;
      int16_t bot = screen_h - 24;
      e->base_y = top + rand() % (bot - top);
      e->y = e->base_y;
      e->x_fp = (screen_w + 16) << FP;
      e->phase = rand() % TRIG_MAX_ANGLE;
      e->r = 7;
      s_pebble_active = true;
      sfx_twinkle();
      return;
    }
  }
}

static void spawn_particle(int16_t x, int16_t y) {
  for (int i = 0; i < MAX_PART; i++) {
    if (!s_parts[i].alive) {
      s_parts[i].alive = true;
      s_parts[i].x = x;
      s_parts[i].y = y;
      s_parts[i].life = 24 + rand() % 10;
      return;
    }
  }
}

// Fends off a shark that's within striking range — a tap (UP or the screen)
// timed while it's close. Without a sword this is a no-op: the only defense
// is swimming out of the way, same as any other hazard.
static void try_parry(void) {
  if (s_state != ST_PLAYING || s_sword <= 0) return;
  for (int i = 0; i < MAX_ENT; i++) {
    Entity *e = &s_ents[i];
    if (e->type != ENT_SHARK || !shark_in_range(e)) continue;
    int16_t ex = e->x_fp >> FP;
    s_run_pearls += 2 + s_sword;   // higher sword levels pay off the parry more
    spawn_particle(ex, e->y);
    spawn_particle(ex - 6, e->y - 4);
    e->type = ENT_NONE;
    backlight_touch();
    vibe_short();
    sfx_clang();
    return;   // only one shark is ever active at a time
  }
}

static void game_tick(void *data) {
  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  s_tick++;
  GRect b = layer_get_bounds(s_layer);

  // Oxygen
  s_oxygen_ms -= TICK_MS;
  if (s_oxygen_ms <= 0) {
    s_oxygen_ms = 0;
    end_run(true);
    return;
  }
  // Low oxygen: one buzz when it starts, then a soft double beep every couple
  // of seconds for as long as the tank stays nearly empty.
  if (!s_o2_warned && s_oxygen_ms < 10000) {
    s_o2_warned = true;
    s_o2_beep_ms = 0;
    vibe_short();
  }
  if (s_o2_warned) {
    if (s_o2_beep_ms <= 0) { sfx_low_o2(); s_o2_beep_ms = O2_BEEP_EVERY_MS; }
    s_o2_beep_ms -= TICK_MS;
  }
  if (s_o2_warned && s_oxygen_ms >= 12000) s_o2_warned = false;

  // Scroll speed: 1.5 px/t base, up to 4 px/t with depth
  int32_t speed_fp = 384 + depth_m() * 3;
  if (speed_fp > 1024) speed_fp = 1024;
  s_dist_px += speed_fp >> FP;
  s_px_since_bubble += speed_fp >> FP;

  // Diver physics — extra accel when reversing direction, so the diver
  // turns around quickly instead of drifting past pearls
  if (s_thrust) {
    int32_t a = 30 + s_fins * 5;
    if (s_diver_vy_fp > 0) a += a / 2;
    s_diver_vy_fp -= a;
    if (s_tick % 4 == 0) spawn_particle(DIVER_X - 12, (s_diver_y_fp >> FP));
  } else if (s_dive) {
    int32_t a = 44 + s_fins * 6;
    if (s_diver_vy_fp < 0) a += a / 2;
    s_diver_vy_fp += a;
  } else {
    int32_t g = 24 + s_fins * 4;
    if (s_diver_vy_fp < 0) g += g / 2;
    s_diver_vy_fp += g;
  }
  int32_t vmax_up = 310 + s_fins * 25;
  int32_t vmax_down = (s_dive ? 470 : 350) + s_fins * 20;
  if (s_diver_vy_fp < -vmax_up) s_diver_vy_fp = -vmax_up;
  if (s_diver_vy_fp > vmax_down) s_diver_vy_fp = vmax_down;
  s_diver_y_fp += s_diver_vy_fp;

  int32_t y_min = (HUD_H + 10) << FP;
  int32_t y_max = (b.size.h - 9) << FP;
  if (s_diver_y_fp < y_min) { s_diver_y_fp = y_min; s_diver_vy_fp = 0; }
  if (s_diver_y_fp > y_max) { s_diver_y_fp = y_max; s_diver_vy_fp = 0; }

  int16_t dy = s_diver_y_fp >> FP;

  // Entities
  int32_t magnet_r = s_lamp * 9;
  for (int i = 0; i < MAX_ENT; i++) {
    Entity *e = &s_ents[i];
    if (e->type == ENT_NONE) continue;
    e->x_fp -= speed_fp;
    if (e->x_fp < -(24 << FP)) {
      if (e->type == ENT_PEBBLE) {
        // Missed it — it resurfaces a bit deeper
        s_pebble_active = false;
        s_next_pebble_m = depth_m() + PEBBLE_RETRY_M;
      }
      e->type = ENT_NONE;
      continue;
    }
    int16_t ex = e->x_fp >> FP;

    if (e->type == ENT_JELLY) {
      e->phase = (e->phase + 900) % TRIG_MAX_ANGLE;
      e->y = e->base_y + (sin_lookup(e->phase) * 12 / TRIG_MAX_RATIO);
    } else if (e->type == ENT_PEBBLE) {
      e->phase = (e->phase + 700) % TRIG_MAX_ANGLE;
      e->y = e->base_y + (sin_lookup(e->phase) * 5 / TRIG_MAX_RATIO);
    } else if (e->type == ENT_SHARK) {
      // Actively closes in on the diver's depth while it's still approaching
      // — the "aanloop" that gives a fair beat to react. Once it has entered
      // the strike range it commits to that trajectory (stops homing), so
      // the last stretch is a plain positional dodge, same as a rock or
      // jelly, instead of an inescapable magnet the whole way in.
      e->phase = (e->phase + 1400) % TRIG_MAX_ANGLE;   // tail wag, draw-only
      if (!shark_in_range(e)) {
        if (dy > e->y + SHARK_HOME_SPEED) e->y += SHARK_HOME_SPEED;
        else if (dy < e->y - SHARK_HOME_SPEED) e->y -= SHARK_HOME_SPEED;
        else e->y = dy;
        e->base_y = e->y;
      }
    }

    // Lamp magnet pulls collectibles toward the diver
    if (magnet_r > 0 &&
        (e->type == ENT_PEARL || e->type == ENT_BUBBLE || e->type == ENT_PEBBLE)) {
      int32_t ddx = DIVER_X - ex, ddy = dy - e->y;
      if (ddx * ddx + ddy * ddy < magnet_r * magnet_r) {
        e->x_fp += (ddx > 0 ? 2 : -2) << FP;
        int16_t step = (ddy > 0 ? 2 : (ddy < 0 ? -2 : 0));
        e->y += step;
        e->base_y += step;  // wobblers recompute y from base_y each tick
      }
    }

    // Collision
    int32_t cdx = DIVER_X - ex, cdy = dy - e->y;
    int32_t rr = DIVER_R + e->r;
    if (cdx * cdx + cdy * cdy < rr * rr) {
      if (e->type == ENT_PEBBLE) {
        win_run();
        return;
      } else if (e->type == ENT_PEARL) {
        s_run_pearls++;
        sfx_pearl();
        e->type = ENT_NONE;
      } else if (e->type == ENT_BUBBLE) {
        s_oxygen_ms += O2_BUBBLE_MS;
        if (s_oxygen_ms > s_max_oxygen_ms) s_oxygen_ms = s_max_oxygen_ms;
        sfx_bubble();
        e->type = ENT_NONE;
      } else if (s_invuln == 0) {
        s_oxygen_ms -= O2_HIT_MS;
        s_invuln = 45;
        s_hit_flash = HIT_FLASH_TICKS;
        vibe_short();
        sfx_hit();
        if (s_oxygen_ms <= 0) {
          s_oxygen_ms = 0;
          end_run(true);
          return;
        }
      }
    }
  }
  if (s_invuln > 0) s_invuln--;
  if (s_hit_flash > 0) s_hit_flash--;

  // The magic Pebble appears once every upgrade is maxed out
  if (all_upgrades_max() && !s_pebble_active && depth_m() >= s_next_pebble_m) {
    spawn_magic_pebble(b.size.w, b.size.h);
  }

  // Spawning
  s_spawn_px -= speed_fp >> FP;
  if (s_spawn_px <= 0) {
    spawn_entity(b.size.w, b.size.h);
    int32_t shrink = depth_m() / 8;
    if (shrink > 25) shrink = 25;
    s_spawn_px = 55 + rand() % 40 - shrink;
  }

  // Particles
  for (int i = 0; i < MAX_PART; i++) {
    if (!s_parts[i].alive) continue;
    s_parts[i].y -= 1;
    s_parts[i].x -= (speed_fp >> FP);
    if (--s_parts[i].life <= 0 || s_parts[i].y < HUD_H) s_parts[i].alive = false;
  }

  layer_mark_dirty(s_layer);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void draw_diver(GContext *ctx, int16_t x, int16_t y) {
  // Blink while invulnerable
  if (s_state == ST_PLAYING && s_invuln > 0 && (s_tick / 3) % 2 == 0) return;

  // Fins (back)
  graphics_context_set_fill_color(ctx, GColorWindsorTan);
  graphics_fill_rect(ctx, GRect(x - 14, y - 3, 6, 4), 1, GCornersLeft);
  // Air tank
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(x - 8, y - 9, 11, 4), 2, GCornersAll);
  // Body
  graphics_context_set_fill_color(ctx, GColorOrange);
  graphics_fill_rect(ctx, GRect(x - 9, y - 5, 16, 10), 3, GCornersAll);
  // Helmet (brass diving helmet)
  graphics_context_set_fill_color(ctx, GColorBrass);
  graphics_fill_circle(ctx, GPoint(x + 8, y - 3), 5);
  // Visor
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_circle(ctx, GPoint(x + 9, y - 3), 2);
}

static GColor magic_color(uint32_t t) {
  switch (t % 4) {
    case 0: return GColorMagenta;
    case 1: return GColorCyan;
    case 2: return GColorYellow;
    default: return GColorSpringBud;
  }
}

static void draw_magic_pebble(GContext *ctx, GPoint p, int16_t r) {
  graphics_context_set_fill_color(ctx, magic_color(s_tick / 4));
  graphics_fill_circle(ctx, p, r);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  if ((s_tick / 6) % 2 == 0) graphics_draw_circle(ctx, p, r + 2);
  graphics_draw_pixel(ctx, GPoint(p.x - r / 2, p.y - r / 2));
  graphics_draw_pixel(ctx, GPoint(p.x + r / 2, p.y + r / 3));
}

static void draw_entity(GContext *ctx, Entity *e, GColor water) {
  int16_t x = e->x_fp >> FP;
  switch (e->type) {
    case ENT_ROCK:
      graphics_context_set_fill_color(ctx, GColorDarkGray);
      graphics_fill_circle(ctx, GPoint(x, e->y), e->r);
      graphics_fill_circle(ctx, GPoint(x + e->r / 2, e->y + 2), e->r - 3);
      // Two-tone on purpose. A dark-grey rock against the 90-150 m water
      // (CobaltBlue) measures 1.02:1 -- the thing that ends your run is
      // invisible exactly where the scroll is fastest. No single flat colour
      // fixes it, because the water spans #00AAFF to #0000AA; but where the
      // light outline is weakest (the shallow cyan band) the dark body is at
      // its strongest, so the pair reads at every depth.
      graphics_context_set_stroke_color(ctx, GColorLightGray);
      graphics_draw_circle(ctx, GPoint(x, e->y), e->r);
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_circle(ctx, GPoint(x - e->r / 3, e->y - e->r / 3), 2);
      break;
    case ENT_JELLY: {
      graphics_context_set_fill_color(ctx, GColorShockingPink);
      graphics_fill_circle(ctx, GPoint(x, e->y), 7);
      graphics_context_set_fill_color(ctx, water);
      graphics_fill_rect(ctx, GRect(x - 8, e->y + 1, 17, 8), 0, GCornerNone);
      graphics_context_set_stroke_color(ctx, GColorRichBrilliantLavender);
      for (int t = -1; t <= 1; t++) {
        int16_t tx = x + t * 4;
        int16_t sway = (sin_lookup((e->phase + t * 9000) % TRIG_MAX_ANGLE) * 2 / TRIG_MAX_RATIO);
        graphics_draw_line(ctx, GPoint(tx, e->y + 1), GPoint(tx + sway, e->y + 7));
      }
      break;
    }
    case ENT_PEARL:
      graphics_context_set_stroke_color(ctx, GColorLightGray);
      graphics_draw_circle(ctx, GPoint(x, e->y), 4);
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, GPoint(x, e->y), 3);
      break;
    case ENT_BUBBLE:
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_circle(ctx, GPoint(x, e->y), 5);
      graphics_draw_pixel(ctx, GPoint(x - 2, e->y - 2));
      break;
    case ENT_PEBBLE:
      draw_magic_pebble(ctx, GPoint(x, e->y), 6);
      break;
    case ENT_SHARK: {
      // Grey while closing in; flips to red (plus a ring) once it's in the
      // strike-or-parry corridor — the readable cue for "act now". Body is a
      // wide, short oval (not a full circle) with pointed nose/tail and a
      // triangular dorsal fin, so it reads as a small shark rather than a
      // coloured ball. Travels right-to-left, so the nose points left.
      bool danger = shark_in_range(e);
      GColor body = danger ? GColorRed : GColorDarkGray;
      int16_t r = e->r;
      int16_t wag = sin_lookup(e->phase) * 3 / TRIG_MAX_RATIO;
      GPoint center = GPoint(x, e->y);

      graphics_context_set_fill_color(ctx, body);
      graphics_fill_rect(ctx, GRect(x - r - 2, e->y - r / 2 - 1, 2 * r + 4, r + 2), 4, GCornersAll);

      GPathInfo nose_info = {
        .num_points = 3,
        .points = (GPoint[]){ {x - r - 7, e->y}, {x - r + 1, e->y - 4}, {x - r + 1, e->y + 4} }
      };
      GPath *nose = gpath_create(&nose_info);
      gpath_draw_filled(ctx, nose);
      gpath_destroy(nose);

      GPathInfo tail_info = {
        .num_points = 3,
        .points = (GPoint[]){ {x + r - 1, e->y - 4}, {x + r - 1, e->y + 4}, {x + r + 7, e->y + wag} }
      };
      GPath *tail = gpath_create(&tail_info);
      gpath_draw_filled(ctx, tail);
      gpath_destroy(tail);

      GPathInfo fin_info = {
        .num_points = 3,
        .points = (GPoint[]){ {x - 3, e->y - r / 2 - 1}, {x + 3, e->y - r / 2 - 1}, {x, e->y - r - 6} }
      };
      GPath *fin = gpath_create(&fin_info);
      gpath_draw_filled(ctx, fin);
      gpath_destroy(fin);

      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, GPoint(x - r / 2, e->y - 1), 2);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_circle(ctx, GPoint(x - r / 2, e->y - 1), 1);

      if (danger) {
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_draw_circle(ctx, center, r + 5);
      }
      break;
    }
    default:
      break;
  }
}

static void draw_hud(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, HUD_H), 0, GCornerNone);

  char buf[16];
  graphics_context_set_text_color(ctx, GColorWhite);

  // Depth
  snprintf(buf, sizeof(buf), "%ldm", (long)depth_m());
  graphics_draw_text(ctx, buf, s_f18b, GRect(4, 0, 56, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Oxygen bar
  GRect bar = GRect(64, 9, 66, 9);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_rect(ctx, bar);
  int32_t frac_w = (bar.size.w - 2) * s_oxygen_ms / s_max_oxygen_ms;
  GColor oc = GColorGreen;
  if (s_oxygen_ms < s_max_oxygen_ms / 4) oc = GColorRed;
  else if (s_oxygen_ms < s_max_oxygen_ms / 2) oc = GColorChromeYellow;
  if (s_oxygen_ms < 10000 && (s_tick / 8) % 2 == 0) oc = GColorDarkCandyAppleRed;
  graphics_context_set_fill_color(ctx, oc);
  graphics_fill_rect(ctx, GRect(bar.origin.x + 1, bar.origin.y + 1, frac_w, bar.size.h - 2),
                     0, GCornerNone);

  // Pearls
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(b.size.w - 56, 13), 4);
  snprintf(buf, sizeof(buf), "%ld", (long)s_run_pearls);
  graphics_draw_text(ctx, buf, s_f18b, GRect(b.size.w - 48, 0, 44, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void draw_game(GContext *ctx, GRect b) {
  GColor water = water_color(depth_m());
  graphics_context_set_fill_color(ctx, water);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Surface shimmer near the top of the dive
  if (depth_m() < 10) {
    graphics_context_set_fill_color(ctx, GColorCeleste);
    graphics_fill_rect(ctx, GRect(0, HUD_H, b.size.w, 3), 0, GCornerNone);
  }

  for (int i = 0; i < MAX_PART; i++) {
    if (!s_parts[i].alive) continue;
    graphics_context_set_stroke_color(ctx, GColorCeleste);
    graphics_draw_circle(ctx, GPoint(s_parts[i].x, s_parts[i].y), s_parts[i].life > 12 ? 2 : 1);
  }

  for (int i = 0; i < MAX_ENT; i++) {
    if (s_ents[i].type != ENT_NONE) draw_entity(ctx, &s_ents[i], water);
  }

  draw_diver(ctx, DIVER_X, s_diver_y_fp >> FP);
  draw_hud(ctx, b);

  // First-run onboarding: a control hint just under the HUD for the opening
  // stretch of a player's very first dive (no record yet), then it fades out.
  if (s_best_m == 0 && depth_m() < 12) {
    GRect box = GRect(6, HUD_H + 4, b.size.w - 12, 22);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, box, 4, GCornersAll);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, S(S_HINT_CONTROLS), s_f14,
                       GRect(box.origin.x + 2, box.origin.y + 2, box.size.w - 4, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void draw_center_text(GContext *ctx, const char *text, GFont font,
                             int16_t y, int16_t h, GRect b, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, GRect(4, y, b.size.w - 8, h),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// Replace a " · " / "  ·  " separator (space(s) + U+00B7 + space(s)) with a
// single newline, trimming the spaces on both sides, so a stat line becomes two
// clean centred lines — lets us use a bigger font without clipping the width.
static void split_middot(char *s) {
  for (char *p = s; p[0]; p++) {
    if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xB7) {
      char *a = p;                              // walk back over leading spaces
      while (a > s && a[-1] == ' ') a--;
      char *b2 = p + 2;                         // walk forward over trailing spaces
      while (*b2 == ' ') b2++;
      *a = '\n';
      memmove(a + 1, b2, strlen(b2) + 1);
      return;
    }
  }
}

static void draw_title(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorCeleste);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, 4), 0, GCornerNone);

  draw_center_text(ctx, S(S_TITLE), s_f28b, 10, 44, b, GColorWhite);

  char buf[48];
  // Record depth + pearl bank just under the title, split onto two lines.
  snprintf(buf, sizeof(buf), S(S_STATS), (long)s_best_m, (long)s_pearls);
  split_middot(buf);
  draw_center_text(ctx, buf, s_f18b, 46, 42, b, GColorWhite);

  draw_diver(ctx, b.size.w / 2, 108);
  graphics_context_set_stroke_color(ctx, GColorCeleste);
  graphics_draw_circle(ctx, GPoint(b.size.w / 2 + 18, 95), 2);
  graphics_draw_circle(ctx, GPoint(b.size.w / 2 + 24, 87), 3);
  // Trophy: the diver carries the magic Pebble once it has been found
  if (s_found) draw_magic_pebble(ctx, GPoint(b.size.w / 2 - 28, 108), 5);

  draw_center_text(ctx, S(S_HINT_DIVE), s_f18b, 130, 24, b, GColorWhite);
  draw_center_text(ctx, S(S_HINT_SHOP), s_f18b, 152, 24, b, GColorWhite);
  draw_center_text(ctx, S(S_HINT_SETTINGS), s_f18b, 176, 22, b, GColorWhite);
}

static void draw_gameover(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  draw_center_text(ctx, S(S_O2_OUT), s_f28b, 16, 36, b, GColorRed);

  char buf[40];
  snprintf(buf, sizeof(buf), S(S_DEPTH), (long)depth_m());
  draw_center_text(ctx, buf, s_f24b, 62, 30, b, GColorWhite);
  snprintf(buf, sizeof(buf), S(S_RUN_PEARLS), (long)s_run_pearls);
  draw_center_text(ctx, buf, s_f24b, 92, 30, b, GColorPastelYellow);

  if (s_new_record) {
    draw_center_text(ctx, S(S_NEW_RECORD), s_f18b, 124, 24, b, GColorGreen);
  }

  // Hints appear once the brief input lock has expired
  if (!s_go_lock) {
    draw_center_text(ctx, S(S_HINT_RETRY), s_f18b, b.size.h - 52, 24, b, GColorWhite);
    draw_center_text(ctx, S(S_HINT_SHOP), s_f18b, b.size.h - 30, 24, b, GColorLightGray);
  }
}

static void draw_shop(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  char buf[40];
  GColor head = s_shop_flash ? GColorRed : GColorWhite;
  graphics_context_set_text_color(ctx, head);
  graphics_draw_text(ctx, S(S_SHOP_TITLE), s_f24b, GRect(8, 2, 110, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  // Pearl balance top-right
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(b.size.w - 56, 18), 4);
  snprintf(buf, sizeof(buf), "%ld", (long)s_pearls);
  graphics_context_set_text_color(ctx, head);
  graphics_draw_text(ctx, buf, s_f18b, GRect(b.size.w - 48, 7, 44, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  const char *names[4] = {S(S_ITEM_TANK), S(S_ITEM_FINS), S(S_ITEM_LAMP), S(S_ITEM_SWORD)};
  const char *descs[4] = {S(S_DESC_TANK), S(S_DESC_FINS), S(S_DESC_LAMP), S(S_DESC_SWORD)};
  int8_t levels[4] = {s_tank, s_fins, s_lamp, s_sword};

  for (int i = 0; i < 4; i++) {
    int16_t y = 32 + i * 42;
    GRect card = GRect(6, y, b.size.w - 12, 38);
    graphics_context_set_fill_color(ctx, i == s_shop_sel ? GColorCobaltBlue : GColorDukeBlue);
    graphics_fill_rect(ctx, card, 4, GCornersAll);
    if (i == s_shop_sel) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_round_rect(ctx, card, 4);
    }

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, names[i], s_f18b, GRect(card.origin.x + 6, y + 1, 136, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, GColorCeleste);
    graphics_draw_text(ctx, descs[i], s_f14, GRect(card.origin.x + 6, y + 17, 130, 14),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // Level pips
    for (int p = 0; p < MAX_LEVEL; p++) {
      GRect pip = GRect(card.origin.x + 6 + p * 10, y + 30, 6, 6);
      if (p < levels[i]) {
        graphics_context_set_fill_color(ctx, GColorGreen);
        graphics_fill_rect(ctx, pip, 1, GCornersAll);
      } else {
        graphics_context_set_stroke_color(ctx, GColorLightGray);
        graphics_draw_rect(ctx, pip);
      }
    }

    // Price / MAX
    graphics_context_set_text_color(ctx, GColorPastelYellow);
    if (levels[i] >= MAX_LEVEL) {
      snprintf(buf, sizeof(buf), "MAX");
    } else {
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, GPoint(card.origin.x + card.size.w - 46, y + 17), 3);
      snprintf(buf, sizeof(buf), "%ld", (long)SHOP_COST[(int)levels[i]]);
    }
    graphics_draw_text(ctx, buf, s_f18b,
                       GRect(card.origin.x + card.size.w - 40, y + 8, 36, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  // Once everything is bought, the shop whispers about the legend instead
  draw_center_text(ctx, all_upgrades_max() ? S(S_LEGEND) : S(S_SHOP_HINT), s_f14,
                   b.size.h - 30, 28, b,
                   all_upgrades_max() ? GColorCeleste : GColorLightGray);
}

static void draw_victory(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  draw_magic_pebble(ctx, GPoint(b.size.w / 2, 34), 11);
  draw_center_text(ctx, S(S_WIN_TITLE), s_f24b, 52, 64, b, GColorPastelYellow);

  char buf[40];
  snprintf(buf, sizeof(buf), S(S_DEPTH), (long)depth_m());
  draw_center_text(ctx, buf, s_f24b, 116, 30, b, GColorWhite);
  snprintf(buf, sizeof(buf), S(S_RUN_PEARLS), (long)s_run_pearls);
  draw_center_text(ctx, buf, s_f24b, 144, 30, b, GColorWhite);

  if (!s_go_lock) {
    draw_center_text(ctx, S(S_HINT_RETRY), s_f18b, b.size.h - 30, 24, b, GColorWhite);
  }
}

#define NUM_SETTINGS 4

static void draw_settings(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, S(S_SETTINGS_TITLE), s_f24b, GRect(8, 4, b.size.w - 16, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  const char *labels[NUM_SETTINGS] = {
    S(S_OPT_BACKLIGHT), S(S_OPT_VIBRATION), S(S_OPT_SOUND), S(S_OPT_RESET) };

  for (int i = 0; i < NUM_SETTINGS; i++) {
    int16_t y = 40 + i * 34;
    GRect row = GRect(6, y, b.size.w - 12, 30);
    if (i == s_settings_sel) {
      graphics_context_set_fill_color(ctx, GColorCobaltBlue);
      graphics_fill_rect(ctx, row, 4, GCornersAll);
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_round_rect(ctx, row, 4);
    }

    // Reset row: when armed, the whole row becomes a red confirm prompt. The red
    // goes in the bar rather than the text — red on the cobalt selection bar was
    // the lowest-contrast pair on screen, in the one state that must be read.
    if (i == 3 && s_reset_armed) {
      graphics_context_set_fill_color(ctx, GColorRed);
      graphics_fill_rect(ctx, row, 4, GCornersAll);
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_round_rect(ctx, row, 4);
      graphics_context_set_text_color(ctx, GColorWhite);
      graphics_draw_text(ctx, S(S_RESET_CONFIRM), s_f18, GRect(row.origin.x + 8, y + 4, row.size.w - 16, 22),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      continue;
    }

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, labels[i], s_f18b, GRect(row.origin.x + 8, y + 4, row.size.w - 76, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    if (i == 3) continue;   // Reset has no value column

    const char *vt;
    bool active;
    if (i == 0) {   // Backlight: 3-state
      vt = (s_bl_mode == BL_ALWAYS) ? S(S_BL_ALWAYS)
         : (s_bl_mode == BL_TIMED) ? S(S_BL_TIMED) : S(S_OFF);
      active = (s_bl_mode != BL_OFF);
    } else if (i == 1) {
      vt = s_vibration_on ? S(S_ON) : S(S_OFF);
      active = s_vibration_on;
    } else {   // Sound: 4-level volume
      vt = s_sound_vol == 0 ? S(S_OFF) : speaker_is_muted() ? S(S_SND_MUTED)
         : s_sound_vol == 1 ? S(S_SND_SOFT)
         : s_sound_vol == 2 ? S(S_SND_NORMAL) : S(S_SND_LOUD);
      active = s_sound_vol > 0 && !speaker_is_muted();
    }
    graphics_context_set_text_color(ctx, active ? GColorGreen : GColorLightGray);
    graphics_draw_text(ctx, vt, s_f14, GRect(row.origin.x + row.size.w - 68, y + 6, 62, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }

  draw_center_text(ctx, S(S_SETTINGS_HINT), s_f14, b.size.h - 38, 36, b, GColorLightGray);
}

static void layer_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  apply_tint();
  switch (s_state) {
    case ST_PLAYING:
      draw_game(ctx, b);
      break;
    case ST_PAUSED:
      draw_game(ctx, b);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_rect(ctx, GRect(0, b.size.h / 2 - 26, b.size.w, 52), 0, GCornerNone);
      draw_center_text(ctx, S(S_PAUSED), s_f24b, b.size.h / 2 - 24, 30, b, GColorWhite);
      draw_center_text(ctx, S(S_HINT_RESUME), s_f14, b.size.h / 2 + 4, 18, b, GColorLightGray);
      break;
    case ST_TITLE:
      draw_title(ctx, b);
      break;
    case ST_GAMEOVER:
      draw_gameover(ctx, b);
      break;
    case ST_VICTORY:
      draw_victory(ctx, b);
      break;
    case ST_SHOP:
      draw_shop(ctx, b);
      break;
    case ST_SETTINGS:
      draw_settings(ctx, b);
      break;
  }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static void shop_flash_end(void *data) {
  s_shop_flash = 0;
  layer_mark_dirty(s_layer);
}

static void try_buy(void) {
  int8_t *lvl[4] = {&s_tank, &s_fins, &s_lamp, &s_sword};
  int8_t cur = *lvl[(int)s_shop_sel];
  if (cur >= MAX_LEVEL) return;
  int32_t cost = SHOP_COST[(int)cur];
  if (s_pearls < cost) {
    s_shop_flash = 1;
    app_timer_register(600, shop_flash_end, NULL);
    vibe_double();
    sfx_deny();
    layer_mark_dirty(s_layer);
    return;
  }
  s_pearls -= cost;
  (*lvl[(int)s_shop_sel])++;
  save_progress();
  vibe_short();
  sfx_buy();
  layer_mark_dirty(s_layer);
}

// Wipe run progress — pearls, best depth, upgrades and the found-Pebble trophy —
// while leaving the player's settings untouched.
static void reset_save(void) {
  s_pearls = 0;
  s_best_m = 0;
  s_tank = 0; s_fins = 0; s_lamp = 0; s_sword = 0;
  s_found = false;
  s_new_record = false;
  save_progress();
  vibe_short();
  sfx_deny();
}

static void toggle_current_setting(void) {
  switch (s_settings_sel) {
    case 0:
      s_bl_mode = (s_bl_mode + 1) % NUM_BL_MODES;   // Off -> Timeout -> Always
      apply_backlight();
      persist_write_int(PK_BL_MODE, s_bl_mode);
      break;
    case 1:
      s_vibration_on = !s_vibration_on;
      persist_write_bool(PK_VIBRATION, s_vibration_on);
      if (s_vibration_on) vibe_short();   // sample buzz when turning it on
      break;
    case 2:
      s_sound_vol = (s_sound_vol + 1) % 4;   // Off -> Soft -> Normal -> Loud
      persist_write_int(PK_SOUND_VOL, s_sound_vol);
      if (s_sound_vol) sfx_pearl();   // sample blip at the new level
      break;
    case 3:
      if (s_reset_armed) { reset_save(); s_reset_armed = false; }
      else s_reset_armed = true;   // first press arms; a second confirms
      break;
  }
  layer_mark_dirty(s_layer);
}

static void select_raw_down(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  switch (s_state) {
    case ST_PLAYING:
      s_thrust = true;
      break;
    case ST_PAUSED:
      s_state = ST_PLAYING;
      s_thrust = true;
      stop_timer();
      s_timer = app_timer_register(TICK_MS, game_tick, NULL);
      break;
    case ST_GAMEOVER:
    case ST_VICTORY:
      if (s_go_lock) break;
      start_run();
      s_thrust = true;   // SELECT is still held: keep swimming, don't sink
      break;
    case ST_TITLE:
      start_run();
      s_thrust = true;
      break;
    case ST_SHOP:
      try_buy();
      break;
    case ST_SETTINGS:
      toggle_current_setting();
      break;
  }
}

static void select_raw_up(ClickRecognizerRef rec, void *ctx) {
  s_thrust = false;
}

static void up_click(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  switch (s_state) {
    case ST_PLAYING:
      try_parry();
      break;
    case ST_SHOP:
      if (s_shop_sel > 0) {
        s_shop_sel--;
        layer_mark_dirty(s_layer);
      }
      break;
    case ST_TITLE:
      s_settings_sel = 0;
      s_reset_armed = false;
      s_state = ST_SETTINGS;
      layer_mark_dirty(s_layer);
      break;
    case ST_SETTINGS:
      if (s_settings_sel > 0) {
        s_settings_sel--;
        s_reset_armed = false;
        layer_mark_dirty(s_layer);
      }
      break;
    default:
      break;
  }
}

static void down_raw_down(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  switch (s_state) {
    case ST_PLAYING:
      s_dive = true;
      break;
    case ST_GAMEOVER:
    case ST_VICTORY:
      if (s_go_lock) break;
      /* fallthrough */
    case ST_TITLE:
      s_shop_sel = 0;
      s_state = ST_SHOP;
      layer_mark_dirty(s_layer);
      break;
    case ST_SHOP:
      if (s_shop_sel < 3) {
        s_shop_sel++;
        layer_mark_dirty(s_layer);
      }
      break;
    case ST_SETTINGS:
      if (s_settings_sel < NUM_SETTINGS - 1) {
        s_settings_sel++;
        s_reset_armed = false;
        layer_mark_dirty(s_layer);
      }
      break;
    default:
      break;
  }
}

static void down_raw_up(ClickRecognizerRef rec, void *ctx) {
  s_dive = false;
}

static void back_click(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  switch (s_state) {
    case ST_PLAYING:
      s_state = ST_PAUSED;
      s_thrust = false;
      s_dive = false;
      stop_timer();
      layer_mark_dirty(s_layer);
      break;
    case ST_PAUSED:
      end_run(false);  // bail out, keep collected pearls
      break;
    case ST_SETTINGS:
      s_reset_armed = false;
      s_state = ST_TITLE;
      layer_mark_dirty(s_layer);
      break;
    case ST_GAMEOVER:
    case ST_VICTORY:
    case ST_SHOP:
      s_state = ST_TITLE;
      layer_mark_dirty(s_layer);
      break;
    case ST_TITLE:
      window_stack_pop(true);
      break;
  }
}

// Touch (Pebble Time 2): tapping the screen fends off a shark in range, same
// as UP — SELECT/DOWN are already busy swimming, so touch is a second way to
// reach the same action rather than a full gesture set.
static void touch_handler(const TouchEvent *event, void *ctx) {
  if (event->type == TouchEvent_Touchdown) {
    try_parry();
  }
}

static void click_config(void *ctx) {
  window_raw_click_subscribe(BUTTON_ID_SELECT, select_raw_down, select_raw_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, down_raw_down, down_raw_up, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------

static void focus_handler(bool in_focus) {
  if (!in_focus && s_state == ST_PLAYING) {
    s_state = ST_PAUSED;
    s_thrust = false;
    s_dive = false;
    stop_timer();
    layer_mark_dirty(s_layer);
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, layer_update);
  layer_add_child(root, s_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_layer);
}

static void init(void) {
  srand(time(NULL));
  pick_language();
  load_progress();
  sfx_load();
  apply_backlight();

  s_f14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  // Non-bold 18 exists for the armed-reset confirm: bold 18 would not fit
  // NL "Nogmaals om te wissen" in the row, and f14 made the one state that
  // must be read the smallest text on the screen.
  s_f18 = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  s_f18b = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_f24b = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_f28b = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  app_focus_service_subscribe(focus_handler);
  touch_service_subscribe(touch_handler, NULL);
  window_stack_push(s_window, true);
}

static void deinit(void) {
  backlight_stop_timer();
  light_enable(false);
  stop_timer();
  touch_service_unsubscribe();
  app_focus_service_unsubscribe();
  save_progress();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
