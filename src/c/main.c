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

// The magic Pebble first appears at this depth (fully upgraded divers only)
#define PEBBLE_MIN_M 200
#define PEBBLE_RETRY_M 40

typedef enum { ST_TITLE, ST_PLAYING, ST_PAUSED, ST_GAMEOVER, ST_VICTORY, ST_SHOP } GameState;
typedef enum { ENT_NONE, ENT_ROCK, ENT_JELLY, ENT_PEARL, ENT_BUBBLE, ENT_PEBBLE } EntType;

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
static uint32_t s_tick;

static Entity s_ents[MAX_ENT];
static Particle s_parts[MAX_PART];

// bank / progress
static int32_t s_pearls, s_run_pearls;
static int32_t s_best_m;
static int8_t s_tank, s_fins, s_lamp;
static bool s_new_record;
static bool s_found;               // magic Pebble found at least once
static bool s_pebble_active;       // magic Pebble currently in the water
static int32_t s_next_pebble_m;    // depth at which the next one appears

// shop
static int8_t s_shop_sel;
static int8_t s_shop_flash;        // "not enough pearls" flash ticks
static const int32_t SHOP_COST[MAX_LEVEL] = {15, 40, 90, 160, 250};

static GFont s_f14, s_f18b, s_f24b, s_f28b;

// ---------------------------------------------------------------------------
// Localization — device language via i18n_get_system_locale(), English
// fallback. Strings are UTF-8; Pebble system fonts cover Latin-1.
// ---------------------------------------------------------------------------

typedef enum { L_EN, L_NL, L_FR, L_DE, L_ES, NUM_LANGS } Lang;

enum {
  S_TITLE, S_HINT_DIVE, S_HINT_SHOP, S_STATS,
  S_O2_OUT, S_DEPTH, S_RUN_PEARLS, S_NEW_RECORD, S_HINT_RETRY,
  S_SHOP_TITLE, S_ITEM_TANK, S_ITEM_FINS, S_ITEM_LAMP,
  S_DESC_TANK, S_DESC_FINS, S_DESC_LAMP, S_SHOP_HINT,
  S_PAUSED, S_HINT_RESUME,
  S_WIN_TITLE, S_LEGEND,
  NUM_STRS
};

static const char *TR[NUM_LANGS][NUM_STRS] = {
  [L_EN] = {
    "PebbleDiver", "SELECT · dive", "DOWN · shop",
    "Best: %ld m  ·  Pearls: %ld",
    "Out of air!", "Depth: %ld m", "Pearls: +%ld", "New record!",
    "SELECT · again",
    "Shop", "Oxygen tank", "Fins", "Lamp",
    "+8s oxygen", "swim faster", "attracts pearls",
    "SELECT · buy    BACK · close",
    "Paused", "SELECT · resume",
    "You found the magic Pebble!",
    "The magic Pebble waits deep below...",
  },
  [L_NL] = {
    "PebbleDiver", "SELECT · duiken", "OMLAAG · winkel",
    "Record: %ld m  ·  Parels: %ld",
    "Zuurstof op!", "Diepte: %ld m", "Parels: +%ld", "Nieuw record!",
    "SELECT · opnieuw",
    "Winkel", "Zuurstoftank", "Vinnen", "Lamp",
    "+8s zuurstof", "sneller zwemmen", "trekt parels aan",
    "SELECT · kopen    TERUG · sluiten",
    "Gepauzeerd", "SELECT · verder",
    "Je vond de magische Pebble!",
    "De magische Pebble wacht in de diepte...",
  },
  [L_FR] = {
    "PebbleDiver", "SELECT · plonger", "BAS · boutique",
    "Record : %ld m  ·  Perles : %ld",
    "Plus d'air !", "Prof. : %ld m", "Perles : +%ld", "Nouveau record !",
    "SELECT · rejouer",
    "Boutique", "Bouteille d'O2", "Palmes", "Lampe",
    "+8 s d'oxygène", "nage plus vite", "attire les perles",
    "SELECT · acheter   RETOUR · fermer",
    "Pause", "SELECT · reprendre",
    "Pebble magique trouvé !",
    "Le Pebble magique attend au fond...",
  },
  [L_DE] = {
    "PebbleDiver", "SELECT · tauchen", "RUNTER · Laden",
    "Rekord: %ld m  ·  Perlen: %ld",
    "Keine Luft!", "Tiefe: %ld m", "Perlen: +%ld", "Neuer Rekord!",
    "SELECT · nochmal",
    "Laden", "Sauerstofftank", "Flossen", "Lampe",
    "+8s Sauerstoff", "schneller schwimmen", "zieht Perlen an",
    "SELECT · kaufen  ZURÜCK · schließen",
    "Pausiert", "SELECT · weiter",
    "Magischer Pebble gefunden!",
    "Der magische Pebble wartet tief unten...",
  },
  [L_ES] = {
    "PebbleDiver", "SELECT · bucear", "ABAJO · tienda",
    "Récord: %ld m  ·  Perlas: %ld",
    "¡Sin aire!", "Prof.: %ld m", "Perlas: +%ld", "¡Nuevo récord!",
    "SELECT · otra vez",
    "Tienda", "Tanque de O2", "Aletas", "Lámpara",
    "+8 s de oxígeno", "nada más rápido", "atrae las perlas",
    "SELECT · comprar   ATRÁS · cerrar",
    "Pausa", "SELECT · seguir",
    "¡Pebble mágico encontrado!",
    "El Pebble mágico espera en el fondo...",
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
  persist_write_bool(PK_FOUND, s_found);
}

static void load_progress(void) {
  s_pearls = persist_read_int(PK_PEARLS);
  s_best_m = persist_read_int(PK_BEST);
  s_tank = persist_read_int(PK_TANK);
  s_fins = persist_read_int(PK_FINS);
  s_lamp = persist_read_int(PK_LAMP);
  s_found = persist_read_bool(PK_FOUND);
}

static bool all_upgrades_max(void) {
  return s_tank >= MAX_LEVEL && s_fins >= MAX_LEVEL && s_lamp >= MAX_LEVEL;
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
    vibes_long_pulse();
    s_go_lock = true;
    app_timer_register(1000, go_unlock, NULL);
  }
  layer_mark_dirty(s_layer);
}

static void win_run(void) {
  s_found = true;
  finish_run();
  s_state = ST_VICTORY;
  vibes_double_pulse();
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
    if (roll < 35) e->type = ENT_ROCK;
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
  if (!s_o2_warned && s_oxygen_ms < 10000) {
    s_o2_warned = true;
    vibes_short_pulse();
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
        e->type = ENT_NONE;
      } else if (e->type == ENT_BUBBLE) {
        s_oxygen_ms += O2_BUBBLE_MS;
        if (s_oxygen_ms > s_max_oxygen_ms) s_oxygen_ms = s_max_oxygen_ms;
        e->type = ENT_NONE;
      } else if (s_invuln == 0) {
        s_oxygen_ms -= O2_HIT_MS;
        s_invuln = 45;
        vibes_short_pulse();
        if (s_oxygen_ms <= 0) {
          s_oxygen_ms = 0;
          end_run(true);
          return;
        }
      }
    }
  }
  if (s_invuln > 0) s_invuln--;

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
}

static void draw_center_text(GContext *ctx, const char *text, GFont font,
                             int16_t y, int16_t h, GRect b, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, GRect(4, y, b.size.w - 8, h),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void draw_title(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorCeleste);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, 4), 0, GCornerNone);

  draw_center_text(ctx, S(S_TITLE), s_f28b, 14, 70, b, GColorWhite);

  draw_diver(ctx, b.size.w / 2, 105);
  graphics_context_set_stroke_color(ctx, GColorCeleste);
  graphics_draw_circle(ctx, GPoint(b.size.w / 2 + 18, 92), 2);
  graphics_draw_circle(ctx, GPoint(b.size.w / 2 + 24, 84), 3);
  // Trophy: the diver carries the magic Pebble once it has been found
  if (s_found) draw_magic_pebble(ctx, GPoint(b.size.w / 2 - 28, 105), 5);

  draw_center_text(ctx, S(S_HINT_DIVE), s_f18b, 128, 24, b, GColorWhite);
  draw_center_text(ctx, S(S_HINT_SHOP), s_f18b, 150, 24, b, GColorWhite);

  char buf[48];
  snprintf(buf, sizeof(buf), S(S_STATS),
           (long)s_best_m, (long)s_pearls);
  draw_center_text(ctx, buf, s_f14, b.size.h - 24, 20, b, GColorOxfordBlue);
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

  const char *names[3] = {S(S_ITEM_TANK), S(S_ITEM_FINS), S(S_ITEM_LAMP)};
  const char *descs[3] = {S(S_DESC_TANK), S(S_DESC_FINS), S(S_DESC_LAMP)};
  int8_t levels[3] = {s_tank, s_fins, s_lamp};

  for (int i = 0; i < 3; i++) {
    int16_t y = 38 + i * 56;
    GRect card = GRect(6, y, b.size.w - 12, 52);
    graphics_context_set_fill_color(ctx, i == s_shop_sel ? GColorCobaltBlue : GColorDukeBlue);
    graphics_fill_rect(ctx, card, 4, GCornersAll);
    if (i == s_shop_sel) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_round_rect(ctx, card, 4);
    }

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, names[i], s_f18b, GRect(card.origin.x + 6, y, 130, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, GColorCeleste);
    graphics_draw_text(ctx, descs[i], s_f14, GRect(card.origin.x + 6, y + 19, 130, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // Level pips
    for (int p = 0; p < MAX_LEVEL; p++) {
      GRect pip = GRect(card.origin.x + 6 + p * 10, y + 40, 7, 7);
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
      graphics_fill_circle(ctx, GPoint(card.origin.x + card.size.w - 46, y + 25), 3);
      snprintf(buf, sizeof(buf), "%ld", (long)SHOP_COST[(int)levels[i]]);
    }
    graphics_draw_text(ctx, buf, s_f18b,
                       GRect(card.origin.x + card.size.w - 40, y + 14, 36, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  // Once everything is bought, the shop whispers about the legend instead
  draw_center_text(ctx, all_upgrades_max() ? S(S_LEGEND) : S(S_SHOP_HINT), s_f14,
                   b.size.h - 18, 16, b,
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

static void layer_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
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
  int8_t *lvl[3] = {&s_tank, &s_fins, &s_lamp};
  int8_t cur = *lvl[(int)s_shop_sel];
  if (cur >= MAX_LEVEL) return;
  int32_t cost = SHOP_COST[(int)cur];
  if (s_pearls < cost) {
    s_shop_flash = 1;
    app_timer_register(600, shop_flash_end, NULL);
    vibes_double_pulse();
    layer_mark_dirty(s_layer);
    return;
  }
  s_pearls -= cost;
  (*lvl[(int)s_shop_sel])++;
  save_progress();
  vibes_short_pulse();
  layer_mark_dirty(s_layer);
}

static void select_raw_down(ClickRecognizerRef rec, void *ctx) {
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
      break;
    case ST_TITLE:
      start_run();
      break;
    case ST_SHOP:
      try_buy();
      break;
  }
}

static void select_raw_up(ClickRecognizerRef rec, void *ctx) {
  s_thrust = false;
}

static void up_click(ClickRecognizerRef rec, void *ctx) {
  if (s_state == ST_SHOP && s_shop_sel > 0) {
    s_shop_sel--;
    layer_mark_dirty(s_layer);
  }
}

static void down_raw_down(ClickRecognizerRef rec, void *ctx) {
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
      if (s_shop_sel < 2) {
        s_shop_sel++;
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

  s_f14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
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
  window_stack_push(s_window, true);
}

static void deinit(void) {
  stop_timer();
  app_focus_service_unsubscribe();
  save_progress();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
