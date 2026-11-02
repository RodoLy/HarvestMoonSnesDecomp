/*
 * touch.c — on-screen controls for touch screens, in the look of the game's
 * menus: a wooden cross for the arrows, A B X Y as parchment coins with the
 * SNES pad's letters, L and R as wooden tabs, Select and Start as little
 * parchment signs, and four small signs: the pause menu, the crop bag, the
 * launcher and fast forward.
 *
 * They are drawn at the game's own pixel size (the screen's height is 224
 * of them). Beside a 4:3 picture they sit in the bars, solid, on the pause
 * menu's checkered floor; when the picture fills the screen (widescreen)
 * they go over its edges, see-through (solid while pressed). Auto (the default): they show when the screen is
 * touched and hide when keys or a gamepad are used (the AYN Thor's own
 * buttons); on a phone with no gamepad they are there from the start.
 *
 * Several fingers at once. A finger that lands on the cross stays on it
 * (it may slide anywhere: the direction is from the cross's middle, eight
 * ways); one on A B X Y may slide from button to button. A touch on the
 * field, with mouse controls on, is the mouse: a tap a left click, a drag
 * the left button held (walking after the finger), a long press (half a
 * second, still) a right click.
 */
#include <math.h>
#include <string.h>

#include "touch.h"
#include "config.h"
#include "ui.h"

enum { LH = 224, LW_MAX = 720, FINGERS = 10 };
enum { E_DPAD, E_A, E_B, E_X, E_Y, E_L, E_R, E_SELECT, E_START, E_MENU, E_BAG, E_LAUNCHER, E_FAST, NE };
enum { R_NONE, R_DPAD, R_FACE, R_BTN, R_MENU, R_LAUNCHER, R_BAG, R_FAST, R_FIELD };
typedef struct { int x, y, w, h; } Box;            /* the round ones: (x, y) the middle, w the radius */
typedef struct {
  bool on; SDL_FingerID id; int role; uint16_t mask;
  float lx, ly, lx0, ly0;                          /* overlay pixels */
  float wx, wy;                                    /* window pixels */
  uint64_t t0; bool moved, held, right;
} Finger;

static struct {
  bool visible;
  TouchFieldFn field;
  int lw, gx0, gx1;                                /* the overlay's width; the picture's columns */
  float scale;                                     /* output pixels per overlay pixel */
  Box e[NE];
  Finger f[FINGERS];
  int request;
  uint64_t clear_at;
  uint32_t px[LW_MAX * LH];
  SDL_Texture *tex; int tex_w;
} ts;

static const uint16_t kFace[4] = { B_A, B_B, B_X, B_Y };

static bool show_menu(void) { return g_cfg.pause_menu; }
static bool show_bag(void) { return g_cfg.pause_menu && g_cfg.crop_bag; }

static void layout(int out_w, int out_h, SDL_FRect game)
{
  ts.scale = out_h > 0 ? out_h / (float)LH : 1.0f;
  int lw = (int)(out_w / ts.scale + 0.5f);
  ts.lw = lw < 256 ? 256 : lw > LW_MAX ? LW_MAX : lw;
  ts.gx0 = (int)(game.x / ts.scale + 0.5f);
  ts.gx1 = (int)((game.x + game.w) / ts.scale + 0.5f);
  if (game.w <= 0) { ts.gx0 = 0; ts.gx1 = ts.lw; }
  int ml = ts.gx0, mr = ts.lw - ts.gx1;
  /* in the bars when they are wide enough (a 16:9 screen's are 71), else at the edges, over the picture */
  int cl = ml >= 70 ? ml / 2 : 44, cr = mr >= 70 ? ts.lw - mr / 2 : ts.lw - 44;
  Box *e = ts.e;
  e[E_DPAD] = (Box){ cl, 132, 33, 0 };
  e[E_A] = (Box){ cr + 23, 132, 12, 0 };
  e[E_B] = (Box){ cr, 155, 12, 0 };
  e[E_X] = (Box){ cr, 109, 12, 0 };
  e[E_Y] = (Box){ cr - 23, 132, 12, 0 };
  e[E_L] = (Box){ cl - 30, 10, 60, 18 };
  e[E_R] = (Box){ cr - 30, 10, 60, 18 };
  e[E_MENU] = (Box){ cl - 30, 34, 28, 18 };
  e[E_BAG] = (Box){ cl + 2, 34, 28, 18 };
  e[E_LAUNCHER] = (Box){ cr - 30, 34, 28, 18 };
  e[E_FAST] = (Box){ cr + 2, 34, 28, 18 };
  e[E_SELECT] = (Box){ cl - 24, 190, 48, 16 };
  e[E_START] = (Box){ cr - 24, 190, 48, 16 };
}

static bool round_hit(const Box *b, float x, float y)
{
  float dx = x - b->x, dy = y - b->y, r = b->w + 6.0f;
  return dx * dx + dy * dy <= r * r;
}
static bool box_hit(const Box *b, float x, float y)
{
  return x >= b->x - 3 && x < b->x + b->w + 3 && y >= b->y - 3 && y < b->y + b->h + 3;
}
static int face_at(float x, float y)
{
  for (int i = 0; i < 4; i++) if (round_hit(&ts.e[E_A + i], x, y)) return i;
  return -1;
}
static uint16_t dpad_dir(float x, float y)
{
  float dx = x - ts.e[E_DPAD].x, dy = y - ts.e[E_DPAD].y;
  if (dx * dx + dy * dy < 36.0f) return 0;                    /* the middle: nothing */
  float ax = fabsf(dx), ay = fabsf(dy);
  uint16_t m = 0;
  if (ax > ay * 0.42f) m |= dx < 0 ? B_LEFT : B_RIGHT;        /* eight ways */
  if (ay > ax * 0.42f) m |= dy < 0 ? B_UP : B_DOWN;
  return m;
}

/* what a finger landing at (x, y) does */
static void land(Finger *f)
{
  float x = f->lx, y = f->ly;
  const Box *e = ts.e;
  f->role = R_NONE; f->mask = 0;
  if (ts.visible) {
    if (fabsf(x - e[E_DPAD].x) <= 42 && fabsf(y - e[E_DPAD].y) <= 42) { f->role = R_DPAD; f->mask = dpad_dir(x, y); return; }
    int k = face_at(x, y);
    if (k >= 0) { f->role = R_FACE; f->mask = kFace[k]; return; }
    if (box_hit(&e[E_L], x, y)) { f->role = R_BTN; f->mask = B_L; return; }
    if (box_hit(&e[E_R], x, y)) { f->role = R_BTN; f->mask = B_R; return; }
    if (box_hit(&e[E_SELECT], x, y)) { f->role = R_BTN; f->mask = B_SELECT; return; }
    if (box_hit(&e[E_START], x, y)) { f->role = R_BTN; f->mask = B_START; return; }
    if (show_menu() && box_hit(&e[E_MENU], x, y)) { f->role = R_MENU; ts.request = 1; return; }
    if (show_bag() && box_hit(&e[E_BAG], x, y)) { f->role = R_BAG; return; }
    if (box_hit(&e[E_LAUNCHER], x, y)) { f->role = R_LAUNCHER; ts.request = 2; return; }
    if (box_hit(&e[E_FAST], x, y)) { f->role = R_FAST; return; }
  }
  if (g_cfg.mouse && ts.field && x >= ts.gx0 && x < ts.gx1) {
    f->role = R_FIELD;
    ts.field(f->wx, f->wy, 0, false);
  }
}

static Finger *finger(SDL_FingerID id, bool make)
{
  for (int i = 0; i < FINGERS; i++) if (ts.f[i].on && ts.f[i].id == id) return &ts.f[i];
  if (!make) return NULL;
  for (int i = 0; i < FINGERS; i++)
    if (!ts.f[i].on) { memset(&ts.f[i], 0, sizeof ts.f[i]); ts.f[i].on = true; ts.f[i].id = id; return &ts.f[i]; }
  return NULL;
}

void touch_init(bool show_at_start, TouchFieldFn field)
{
  ts.field = field;
  ts.visible = g_cfg.touch == TOUCH_ALWAYS || (g_cfg.touch == TOUCH_AUTO && show_at_start);
}

void touch_event(const SDL_Event *ev, int win_w, int win_h, int out_w, int out_h, SDL_FRect game)
{
  if (ev->type != SDL_EVENT_FINGER_DOWN && ev->type != SDL_EVENT_FINGER_MOTION &&
      ev->type != SDL_EVENT_FINGER_UP && ev->type != SDL_EVENT_FINGER_CANCELED) return;
  if (ev->tfinger.touchID == SDL_MOUSE_TOUCHID) return;            /* the mouse, as a finger: not here */
  layout(out_w, out_h, game);
  bool down = ev->type == SDL_EVENT_FINGER_DOWN;
  Finger *f = finger(ev->tfinger.fingerID, down);
  if (!f) return;
  f->lx = ev->tfinger.x * out_w / ts.scale; f->ly = ev->tfinger.y * out_h / ts.scale;
  f->wx = ev->tfinger.x * win_w; f->wy = ev->tfinger.y * win_h;
  uint64_t now = SDL_GetTicks();
  if (down) {
    if (g_cfg.touch != TOUCH_OFF && !ts.visible) ts.visible = true;
    f->lx0 = f->lx; f->ly0 = f->ly; f->t0 = now;
    land(f);
    return;
  }
  if (ev->type == SDL_EVENT_FINGER_MOTION) {
    float dx = f->lx - f->lx0, dy = f->ly - f->ly0;
    if (dx * dx + dy * dy > 36.0f) f->moved = true;
    switch (f->role) {
    case R_DPAD: f->mask = dpad_dir(f->lx, f->ly); break;
    case R_FACE: { int k = face_at(f->lx, f->ly); if (k >= 0) f->mask = kFace[k]; break; }
    case R_FIELD:
      if (f->moved && !f->held && !f->right) { f->held = true; ts.field(f->wx, f->wy, 1, true); }
      else ts.field(f->wx, f->wy, 0, false);
      break;
    default: break;
    }
    return;
  }
  /* up, or taken away by the system */
  if (f->role == R_FIELD) {
    if (f->held) ts.field(f->wx, f->wy, 1, false);
    else if (!f->right && ev->type == SDL_EVENT_FINGER_UP) { ts.field(f->wx, f->wy, 1, true); ts.field(f->wx, f->wy, 1, false); }
    ts.clear_at = now + 700;
  }
  f->on = false;
}

void touch_frame(void)
{
  uint64_t now = SDL_GetTicks();
  bool field_down = false;
  for (int i = 0; i < FINGERS; i++) {
    Finger *f = &ts.f[i];
    if (!f->on || f->role != R_FIELD) continue;
    field_down = true;
    if (!f->moved && !f->held && !f->right && now - f->t0 >= 500) {   /* a long press: the right button */
      f->right = true;
      ts.field(f->wx, f->wy, 2, true);
      ts.field(f->wx, f->wy, 2, false);
    }
  }
  if (ts.clear_at && now >= ts.clear_at && !field_down) {
    ts.clear_at = 0;
    if (ts.field) ts.field(-1, -1, 0, false);
  }
}

uint16_t touch_pad(void)
{
  uint16_t m = 0;
  for (int i = 0; i < FINGERS; i++)
    if (ts.f[i].on && (ts.f[i].role == R_DPAD || ts.f[i].role == R_FACE || ts.f[i].role == R_BTN)) m |= ts.f[i].mask;
  return m;
}
static bool role_held(int role)
{
  for (int i = 0; i < FINGERS; i++) if (ts.f[i].on && ts.f[i].role == role) return true;
  return false;
}
bool touch_bag(void) { return role_held(R_BAG); }
bool touch_fast(void) { return role_held(R_FAST); }
int touch_request(void) { int r = ts.request; ts.request = 0; return r; }
void touch_input_seen(uint16_t pad)
{
  if (!pad || g_cfg.touch != TOUCH_AUTO || !ts.visible) return;
  for (int i = 0; i < FINGERS; i++) if (ts.f[i].on) return;
  ts.visible = false;
}
void touch_release_all(void)
{
  for (int i = 0; i < FINGERS; i++) ts.f[i].on = false;
  ts.request = 0;
}
bool touch_visible(void) { return ts.visible && g_cfg.touch != TOUCH_OFF; }

/* ── Drawing ───────────────────────────────────────────────────────────── */
static void disc(int cx, int cy, int r, uint32_t c)
{
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++)
      if (x * x + y * y <= r * r + r) ui_px(cx + x, cy + y, c);
}
/* A B X Y: a parchment coin on a wooden rim, the letter in the SNES pad's
 * colour; held: the menus' highlight */
static void coin(const Box *b, const char *letter, uint32_t ink, bool held)
{
  int cx = b->x, cy = b->y, r = b->w;
  disc(cx, cy + 1, r, C_WOOD_EDGE);
  disc(cx, cy, r, C_WOOD_EDGE);
  disc(cx, cy, r - 1, C_WOOD);
  disc(cx, cy, r - 3, held ? C_SELECT : C_PAPER);
  if (!held)
    for (int y = -r + 3; y <= 0; y++)                         /* a shine on the upper left */
      for (int x = -r + 3; x <= 0; x++) {
        int d = x * x + y * y, rr = (r - 3) * (r - 3);
        if (d <= rr + r && d > (r - 4) * (r - 4)) ui_px(cx + x, cy + y, C_CREAM);
      }
  ui_text_c(UI_BODY, cx, cy + 5, letter, ink);
}
/* the cross: wood, cream arrows; the arm held darker */
static void cross(const Box *b, uint16_t held)
{
  int cx = b->x, cy = b->y, R = 31, a = 10;
  ui_rect(cx - R - 1, cy - a - 1, 2 * R + 2, 2 * a + 3, C_WOOD_EDGE);
  ui_rect(cx - a - 1, cy - R - 1, 2 * a + 2, 2 * R + 3, C_WOOD_EDGE);
  ui_rect(cx - R, cy - a, 2 * R, 2 * a, C_WOOD);
  ui_rect(cx - a, cy - R, 2 * a, 2 * R, C_WOOD);
  if (held & B_LEFT) ui_rect(cx - R, cy - a, R - a, 2 * a, C_WOOD_DARK);
  if (held & B_RIGHT) ui_rect(cx + a, cy - a, R - a, 2 * a, C_WOOD_DARK);
  if (held & B_UP) ui_rect(cx - a, cy - R, 2 * a, R - a, C_WOOD_DARK);
  if (held & B_DOWN) ui_rect(cx - a, cy + a, 2 * a, R - a, C_WOOD_DARK);
  ui_hline(cx - R, cy - a, R - a, C_WOOD_LIGHT); ui_hline(cx + a, cy - a, R - a, C_WOOD_LIGHT);
  ui_hline(cx - a, cy - R, 2 * a, C_WOOD_LIGHT);
  ui_vline(cx - a, cy - R, R - a, C_WOOD_LIGHT); ui_vline(cx - R, cy - a, 2 * a, C_WOOD_LIGHT);
  disc(cx, cy, 6, C_WOOD_DARK);
  disc(cx, cy, 4, C_WOOD);
  for (int k = 0; k < 5; k++) {
    uint32_t c;
    c = (held & B_UP) ? C_SELECT : C_CREAM;    ui_hline(cx - k, cy - R + 5 + k, 2 * k + 1, c);
    c = (held & B_DOWN) ? C_SELECT : C_CREAM;  ui_hline(cx - k, cy + R - 5 - k, 2 * k + 1, c);
    c = (held & B_LEFT) ? C_SELECT : C_CREAM;  ui_vline(cx - R + 5 + k, cy - k, 2 * k + 1, c);
    c = (held & B_RIGHT) ? C_SELECT : C_CREAM; ui_vline(cx + R - 5 - k, cy - k, 2 * k + 1, c);
  }
}
/* L and R: wooden tabs, as the pause menu's */
static void tab(const Box *b, const char *t, bool held)
{
  ui_rect(b->x + 1, b->y, b->w - 2, b->h, C_WOOD_EDGE);
  ui_rect(b->x, b->y + 1, b->w, b->h - 1, C_WOOD_EDGE);
  ui_rect(b->x + 1, b->y + 1, b->w - 2, b->h - 2, held ? C_WOOD_DARK : C_WOOD);
  if (!held) ui_hline(b->x + 2, b->y + 1, b->w - 4, C_WOOD_LIGHT);
  ui_text_sh(UI_BODY, b->x + (b->w - ui_text_w(UI_BODY, t)) / 2, b->y + 14, t, C_CREAM);
}
/* the little parchment signs */
static void sign(const Box *b, bool held)
{
  ui_rect(b->x + 1, b->y, b->w - 2, b->h, C_WOOD_EDGE);
  ui_rect(b->x, b->y + 1, b->w, b->h - 2, C_WOOD_EDGE);
  ui_rect(b->x + 1, b->y + 1, b->w - 2, b->h - 2, held ? C_SELECT : C_PAPER);
  ui_hline(b->x + 2, b->y + b->h - 2, b->w - 4, held ? C_GOLD : C_PAPER_DARK);
}
static void sign_text(const Box *b, const char *t, bool held)
{
  sign(b, held);
  ui_text_c(UI_SMALL, b->x + b->w / 2, b->y + b->h - 5, t, C_INK);
}
static void art(int x, int y, const char *const *rows, int n, uint32_t ink, uint32_t fill)
{
  for (int j = 0; j < n; j++)
    for (int i = 0; rows[j][i]; i++)
      if (rows[j][i] == '#') ui_px(x + i, y + j, ink);
      else if (rows[j][i] == 'c') ui_px(x + i, y + j, fill);
}
static void icon_menu(const Box *b, bool held)     /* the pause menu: its grid of slots */
{
  sign(b, held);
  int x = b->x + (b->w - 13) / 2, y = b->y + 4;
  for (int j = 0; j < 2; j++)
    for (int i = 0; i < 3; i++) {
      ui_rect(x + i * 5, y + j * 5, 4, 4, C_INK);
      ui_rect(x + i * 5 + 1, y + j * 5 + 1, 2, 2, j ? C_BLUE : C_GREEN);
    }
}
static void icon_bag(const Box *b, bool held)      /* the crop bag: a sack */
{
  static const char *sack[10] = { "...##...", "..#cc#..", "...##...", "..#cc#..", ".#cccc#.",
                                  "#cccccc#", "#cccccc#", "#cccccc#", ".#cccc#.", "..####.." };
  sign(b, held);
  art(b->x + (b->w - 8) / 2, b->y + 3, sack, 10, C_INK, C_WOOD_LIGHT);
}
static void icon_launcher(const Box *b, bool held) /* the launcher: three lines */
{
  sign(b, held);
  int x = b->x + (b->w - 12) / 2;
  for (int j = 0; j < 3; j++) ui_rect(x, b->y + 4 + j * 3, 12, 2, C_INK);
}
static void icon_fast(const Box *b, bool held)     /* fast forward: two arrows */
{
  sign(b, held);
  int x = b->x + (b->w - 12) / 2, y = b->y + 8;
  for (int k = 0; k < 5; k++) { ui_vline(x + k, y - 4 + k, 9 - 2 * k, C_INK); ui_vline(x + 6 + k, y - 4 + k, 9 - 2 * k, C_INK); }
}

static void draw_controls(bool only_held)
{
  uint16_t held = touch_pad();
  const Box *e = ts.e;
  bool menu = role_held(R_MENU), bag = role_held(R_BAG), launcher = role_held(R_LAUNCHER), fast = role_held(R_FAST);
  if (!only_held || (held & (B_UP | B_DOWN | B_LEFT | B_RIGHT))) cross(&e[E_DPAD], held);
  if (!only_held || (held & B_A)) coin(&e[E_A], "A", C_RED, held & B_A);
  if (!only_held || (held & B_B)) coin(&e[E_B], "B", C_GOLD_DARK, held & B_B);
  if (!only_held || (held & B_X)) coin(&e[E_X], "X", C_BLUE, held & B_X);
  if (!only_held || (held & B_Y)) coin(&e[E_Y], "Y", C_GREEN_DARK, held & B_Y);
  if (!only_held || (held & B_L)) tab(&e[E_L], "L", held & B_L);
  if (!only_held || (held & B_R)) tab(&e[E_R], "R", held & B_R);
  if (!only_held || (held & B_SELECT)) sign_text(&e[E_SELECT], "SELECT", held & B_SELECT);
  if (!only_held || (held & B_START)) sign_text(&e[E_START], "START", held & B_START);
  if (show_menu() && (!only_held || menu)) icon_menu(&e[E_MENU], menu);
  if (show_bag() && (!only_held || bag)) icon_bag(&e[E_BAG], bag);
  if (!only_held || launcher) icon_launcher(&e[E_LAUNCHER], launcher);
  if (!only_held || fast) icon_fast(&e[E_FAST], fast);
}

void touch_draw(SDL_Renderer *r, int out_w, int out_h, SDL_FRect game)
{
  if (!touch_visible() || out_h <= 0) return;
  layout(out_w, out_h, game);
  int lw = ts.lw;
  memset(ts.px, 0, sizeof(uint32_t) * (size_t)lw * LH);
  ui_target(ts.px, lw, LH);
  if (ts.gx0 >= 70 && lw - ts.gx1 >= 70) {
    /* bars wide enough beside the picture: there, solid, on the pause
     * menu's checkered floor (darker) */
    for (int y = 0; y < LH; y++)
      for (int x = 0; x < lw; x++)
        if (x < ts.gx0 || x >= ts.gx1) ts.px[y * lw + x] = (((x >> 4) ^ (y >> 4)) & 1) ? C_CHECK_B : C_CHECK_A;
    ui_dim(0, 0, ts.gx0, LH, 1);
    ui_dim(ts.gx1, 0, lw - ts.gx1, LH, 1);
    draw_controls(false);
  } else {
    /* the picture fills the screen (widescreen): see-through over it, so
     * nothing of the game is hidden; the ones being pressed solid */
    draw_controls(false);
    for (int i = 0; i < lw * LH; i++)
      if (ts.px[i] >> 24) ts.px[i] = (ts.px[i] & 0x00FFFFFFu) | 0x80000000u;
    draw_controls(true);
  }

  if (!ts.tex || ts.tex_w != lw) {
    if (ts.tex) SDL_DestroyTexture(ts.tex);
    ts.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, lw, LH);
    if (!ts.tex) return;
    SDL_SetTextureBlendMode(ts.tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(ts.tex, SDL_SCALEMODE_NEAREST);
    ts.tex_w = lw;
  }
  SDL_UpdateTexture(ts.tex, NULL, ts.px, lw * 4);
  SDL_FRect dst = { 0, 0, lw * ts.scale, (float)out_h };
  SDL_RenderTexture(r, ts.tex, NULL, &dst);
}
