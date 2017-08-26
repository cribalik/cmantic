/* CURRENT:
 *
 * TODO:
 *
 * Autocomplete other buffers
 * Search backwards
 * Colorize search results in view
 * Color highlighting
 * Action search
 * Undo
 * Multiple cursors
 *
 * load files
 * movement (word, parentheses, block)
 * actions (Delete, Yank, ...)
 * add folders
 */

#define _POSIX_C_SOURCE 200112L
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include "array.h"

/* @debug */
#if 0
  #define DEBUG
  #define IF_DEBUG(stmt) stmt
  static void* debug_malloc(unsigned long size, int line, const char *file) {
    struct Header {
      int line;
      const char *file;
    };
    struct Header *p = malloc(sizeof(struct Header) + size);
    p->line = line;
    p->file = file;
    return (char*)p + sizeof(struct Header);
  }
  #define malloc(size) debug_malloc(size, __LINE__, __FILE__)
#else
  #define IF_DEBUG(stmt)
#endif

#define STATIC_ASSERT(expr, name) typedef char static_assert_##name[expr?1:-1]
#define ARRAY_LEN(a) (sizeof(a)/sizeof(*a))

typedef unsigned int u32;
STATIC_ASSERT(sizeof(u32) == 4, u32_is_4_bytes);

static void array_push_str(Array(char) *a, const char *str) {
  for (; *str; ++str)
    array_push(*a, *str);
}

static void *memmem(void *needle, int needle_len, void *haystack, int haystack_len) {
  char *h, *hend;

  if (!needle_len || haystack_len < needle_len)
    return 0;

  h = haystack;
  hend = h + haystack_len - needle_len + 1;

  for (; h < hend; ++h)
    if (memcmp(needle, h, needle_len) == 0)
      return h;
  return 0;
}

static void term_reset_to_default_settings();
static void panic() {
  term_reset_to_default_settings();
  abort();
}

static int at_least(int a, int b) {
  return a < b ? b : a;
}
#define max(a, b) at_least(a, b)
static int at_most(int a, int b) {
  return b < a ? b : a;
}
#define min(a, b) at_most(a, b)
/* a,b inclusive */
static int clampi(int x, int a, int b) {
  return x < a ? a : (b < x ? b : x);
}
#define arrcount(arr) (sizeof(arr)/sizeof(*arr))

typedef enum Key {
  KEY_UNKNOWN = 0,
  KEY_ESCAPE = '\x1b',
  KEY_RETURN = '\r',
  KEY_TAB = '\t',
  KEY_BACKSPACE = 127,

  KEY_SPECIAL = 1000, /* so we can do c >= KEY_SPECIAL to check for special keys */
  KEY_ARROW_UP,
  KEY_ARROW_DOWN,
  KEY_ARROW_LEFT,
  KEY_ARROW_RIGHT,
  KEY_END,
  KEY_HOME
} Key;

static int key_is_special(Key key) {
  return key == KEY_ESCAPE || key == KEY_RETURN || key == KEY_TAB || key == KEY_BACKSPACE || key >= KEY_SPECIAL;
}

typedef struct Rect {
  int x,y,w,h;
} Rect;

Rect rect_create(int x, int y, int w, int h) {
  Rect result;
  result.x = x;
  result.y = y;
  result.w = w;
  result.h = h;
  return result;
}

typedef enum {
  TOKEN_EOF = -1,
  TOKEN_IDENTIFIER = -2,
  TOKEN_NUMBER = -3,
  TOKEN_STRING = -4
} Token;

typedef struct TokenInfo {
  Token token;
  int x;
} TokenInfo;
TokenInfo tokeninfo_create(Token token, int x) {
  TokenInfo t;
  t.token = token;
  t.x = x;
  return t;
}

typedef struct {
  #define GHOST_EOL -1
  int x, y;
  int ghost_x; /* if going from a longer line to a shorter line, remember where we were before clamping to row length. a value of -1 means always go to end of line */
} Pos;

typedef struct Buffer {
  Array(Array(char)) lines;
  const char *filename;
  int tab_type; /* 0 for tabs, 1+ for spaces */

  /* parser stuff */
  Array(Array(TokenInfo)) tokens;
  Array(char*) identifiers;

  Pos pos;

  int modified;
} Buffer;

static int buffer_numlines(Buffer *b) {
  return array_len(b->lines);
}

static int buffer_linesize(Buffer *b, int y) {
  return array_len(b->lines[y]);
}

static void buffer_goto_endline(Buffer *b) {
  b->pos.ghost_x = -1;
}

static void buffer_empty(Buffer *b) {
  int i;

  b->modified = 1;

  for (i = 0; i < array_len(b->lines); ++i)
    array_free(b->lines[i]);

  array_resize(b->lines, 1);
  b->lines[0] = 0;
  b->pos.x = b->pos.y = 0;
}

static void buffer_truncate_to_n_lines(Buffer *b, int n) {
  int i;

  b->modified = 1;
  n = at_least(n, 1);

  if (n >= array_len(b->lines))
    return;

  for (i = n; i < array_len(b->lines); ++i)
    array_free(b->lines[i]);
  array_resize(b->lines, n);
  b->pos.y = at_most(b->pos.y, n-1);
}

static int buffer_advance_xy(Buffer *b, int *x, int *y) {
  ++(*x);
  if (*x >= array_len(b->lines[*y])) {
    *x = 0;
    ++(*y);
    while (*y < array_len(b->lines) && array_len(b->lines[*y]) == 0)
      ++(*y);
    if (*y == array_len(b->lines)) {
      --(*y);
      return 1;
    }
  }
  return 0;
}

static int buffer_advance(Buffer *b) {
  int r;

  r = buffer_advance_xy(b, &b->pos.x, &b->pos.y);
  if (r)
    return r;
  b->pos.ghost_x = b->pos.x;
  return 0;
}

static int buffer_advance_r_xy(Buffer *b, int *x, int *y) {
  --(*x);
  if (*x < 0) {
    --(*y);
    while (!b->lines[*y] && *y > 0)
      --(*y);
    if (*y <= 0) {
      *x = array_len(b->lines[0]);
      return 1;
    }
    *x = array_len(b->lines[*y]);
  }
  return 0;
}

static int buffer_advance_r(Buffer *b) {
  int r;

  r = buffer_advance_r_xy(b, &b->pos.x, &b->pos.y);
  if (r)
    return r;
  b->pos.ghost_x = b->pos.x;
  return 0;
}

static Array(char) buffer_getline(Buffer *b, int y) {
  return b->lines[y];
}

static char *buffer_getstr(Buffer *b, int x, int y) {
  return b->lines[y]+x;
}

static char *buffer_getstr_p(Buffer *b, Pos p) {
  return &b->lines[p.y][p.x];
}

static char buffer_getchar(Buffer *b, int x, int y) {
  return b->lines[y][x];
}

static void buffer_move_to(Buffer *b, int x, int y);
static void buffer_move(Buffer *b, int x, int y);
static void status_message_set(const char *fmt, ...);

static int buffer_find(Buffer *b, char *str, int n) {
  int x, y;

  if (!n)
    return 1;

  x = b->pos.x;
  y = b->pos.y;

  for (; y < buffer_numlines(b); ++y, x = 0) {
    char *p;
    if (x >= buffer_linesize(b, y))
      continue;

    p = memmem(str, n, b->lines[y]+x, buffer_linesize(b, y)-x);
    if (!p)
      continue;

    x = p - b->lines[y];
    buffer_move_to(b, x, y);
    status_message_set("jumping to (%i,%i), but arrived at (%i, %i)", x, y, b->pos.x, b->pos.y);
    return 0;
  }
  return 1;
}


static void buffer_insert_str(Buffer *b, int x, int y, const char *str, int n) {
  b->modified = 1;
  array_insert_a(b->lines[y], x, str, n);
  buffer_move(b, n, 0);
}

static void buffer_replace(Buffer *b, int x0, int x1, int y, const char *str, int n) {
  b->modified = 1;
  array_remove_slow_n(b->lines[y], x0, x1-x0);
  buffer_move_to(b, x0, y);
  buffer_insert_str(b, x0, y, str, n);
}

static void buffer_move(Buffer *b, int x, int y);
static void buffer_insert_char(Buffer *b, char ch) {
  b->modified = 1;
  array_insert(b->lines[b->pos.y], b->pos.x, ch);
  buffer_move(b, 1, 0);
}

static void buffer_delete_line_at(Buffer *b, int y) {
  b->modified = 1;
  array_free(b->lines[y]);
  if (array_len(b->lines) == 1)
    return;
  array_remove_slow(b->lines, y);
}

static void buffer_delete_line(Buffer *b) {
  buffer_delete_line_at(b, b->pos.y);
}

static void buffer_delete_char(Buffer *b) {
  b->modified = 1;
  if (b->pos.x == 0) {
    int i;

    if (b->pos.y == 0)
      return;

    /* move up and right */
    buffer_move(b, 0, -1);
    buffer_move_to(b, array_len(b->lines[b->pos.y]), b->pos.y);
    for (i = 0; i < array_len(b->lines[b->pos.y+1]); ++i)
      array_push(b->lines[b->pos.y], b->lines[b->pos.y+1][i]);
    buffer_delete_line_at(b, b->pos.y+1);
  }
  else {
    buffer_move(b, -1, 0);
    array_remove_slow(b->lines[b->pos.y], b->pos.x);
  }
}

static void buffer_insert_tab(Buffer *b) {
  int n = b->tab_type;

  b->modified = 1;
  if (n == 0) {
    array_insert(b->lines[b->pos.y], b->pos.x, '\t');
    buffer_move(b, 1, 0);
  }
  else {
    /* TODO: optimize? */
    while (n--)
      array_insert(b->lines[b->pos.y], b->pos.x, ' ');
    buffer_move(b, b->tab_type, 0);
  }
}

static int buffer_isempty(Buffer *b) {
  return array_len(b->lines) == 1 && array_len(b->lines[0]) == 0;
}

static void buffer_push_line(Buffer *b, const char *str) {
  int y, l;

  b->modified = 1;
  y = buffer_numlines(b);
  if (!buffer_isempty(b))
    array_push(b->lines, 0);
  else
    y = 0;
  l = strlen(str);
  array_push_n(b->lines[y], l);
  memcpy(b->lines[y], str, l);
}

static void buffer_insert_newline(Buffer *b) {
  int left_of_line;

  b->modified = 1;

  left_of_line = array_len(b->lines[b->pos.y]) - b->pos.x;
  array_insert(b->lines, b->pos.y + 1, 0);
  array_push_a(b->lines[b->pos.y+1], b->lines[b->pos.y] + b->pos.x, left_of_line);
  array_len_get(b->lines[b->pos.y]) = b->pos.x;

  b->pos.x = 0;
  b->pos.ghost_x = 0;
  ++b->pos.y;
}

static void buffer_insert_newline_below(Buffer *b) {
  b->modified = 1;
  array_insert(b->lines, b->pos.y+1, 0);
  buffer_move(b, 0, 1);
}

typedef enum {
  COLOR_BLACK,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_YELLOW,
  COLOR_BLUE,
  COLOR_MAGENTA,
  COLOR_CYAN,
  COLOR_WHITE,
  COLOR_COUNT
} Color;

typedef struct Pane {
  int gutter_width;
  Rect bounds;
  Buffer *buffer;
  Color background_color, font_color;
} Pane;

typedef enum Mode {
  MODE_NORMAL,
  MODE_INSERT,
  MODE_MENU,
  MODE_DELETE,
  MODE_GOTO,
  MODE_SEARCH,
  MODE_COUNT
} Mode;

typedef struct {
  int fcolor: 5;
  int bcolor: 5;
  int bold: 2;
  int italic: 2;
  int inverse: 2;
} Style;

typedef struct {
  char c;
  /* TODO: why isn't 1 working for flags (we get overflow warnings)? */
  Style style;
} Pixel;

static int style_cmp(Style a, Style b) {
  return a.fcolor != b.fcolor || a.bcolor != b.bcolor || a.bold != b.bold || a.italic != b.italic || a.inverse != b.inverse;
}

static struct State {
  /* @renderer some rendering state */
  Pixel *screen_buffer;
  Array(char) tmp_render_buffer;
  int term_width, term_height;
  Style default_style;

  /* some editor state */
  Pane main_pane,
       bottom_pane,
       dropdown_pane;
  Buffer menu_buffer,
         search_buffer,
         message_buffer,
         dropdown_buffer;
  Mode mode;

  /* dropdown state */
  Pos dropdown_pos; /* where dropdown was initialized */
  int dropdown_visible;

  /* goto state */
  unsigned int goto_line_number; /* unsigned in order to prevent undefined behavior on wrap around */

  /* search state */
  int search_failed;
  Pos search_begin_pos;

  /* some settings */
  struct termios orig_termios;
  int read_timeout_ms;
  int tab_width; /* how wide are tabs when rendered */
  int default_tab_type; /* 0 for tabs, 1+ for spaces */
  int dropdown_size;
} G;

static void buffer_guess_tab_type(Buffer *b) {
  int i;
  /* try to figure out tab type */
  /* TODO: use tokens here instead, so we skip comments */
  b->tab_type = -1;
  for (i = 0; i < array_len(b->lines); ++i) {
    if (!b->lines[i])
      continue;

    if (b->lines[i][0] == '\t') {
      b->tab_type = 0;
    }
    else if (b->lines[i][0] == ' ') {
      int num_spaces = 0;
      int j;

      for (j = 0; j < array_len(b->lines[i]) && b->lines[i][j] == ' '; ++j)
        ++num_spaces;

      if (j == array_len(b->lines[i]))
        continue;

      b->tab_type = num_spaces;
    }
    else
      continue;
  }
  if (b->tab_type == -1)
    b->tab_type = G.default_tab_type;
}

static void render_strn(int x0, int x1, int y, const char *str, int n) {
  Pixel *row = &G.screen_buffer[G.term_width*y];

  if (!str)
    return;

  while (n-- && x0 < x1) {
    if (*str == '\t') {
      int end = x0 + at_most(G.tab_width, x1 - x0);
      for (; x0 < end; ++x0)
        row[x0].c = ' ';
    }
    else
      row[x0++].c = *str;
    ++str;
  }
  /* pad with spaces to x1 */
  for (; x0 < x1; ++x0)
    row[x0].c = ' ';
}

static void render_str_v(int x0, int x1, int y, const char *fmt, va_list args) {
  int n;

  array_resize(G.tmp_render_buffer, x1-x0+1);
  n = vsnprintf(G.tmp_render_buffer, x1-x0+1, fmt, args);
  render_strn(x0, x1, y, G.tmp_render_buffer, n);
}

static void render_str(int x0, int x1, int y, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  render_str_v(x0, x1, y, fmt, args);
  va_end(args);
}

static int to_visual_offset(Array(char) line, int x) {
  int result = 0;
  int i;

  if (!line) return result;

  for (i = 0; i < x; ++i) {
    ++result;
    if (line[i] == '\t')
      result += G.tab_width-1;

  }
  return result;
}

/* returns the logical index located visually at x */
static int from_visual_offset(Array(char) line, int x) {
  int visual = 0;
  int i;

  if (!line) return 0;

  for (i = 0; i < array_len(line); ++i) {
    ++visual;
    if (line[i] == '\t')
      visual += G.tab_width-1;

    if (visual > x)
      return i;
  }

  return i;
}

static int pane_calc_top_visible_row(Pane *pane) {
  return at_least(0, pane->buffer->pos.y - pane->bounds.h/2);
}

static int pane_calc_bottom_visible_row(Pane *pane) {
  return at_most(pane_calc_top_visible_row(pane) + pane->bounds.h, buffer_numlines(pane->buffer)) - 1;
}

static int pane_calc_left_visible_column(Pane *pane) {
  return at_least(0, to_visual_offset(pane->buffer->lines[pane->buffer->pos.y], pane->buffer->pos.x) - (pane->bounds.w - pane->gutter_width - 3));
}

static Pos pane_to_screen_pos_xy(Pane *pane, int x, int y) {
  Pos result = {0};
  Array(char) line;

  line = pane->buffer->lines[y];
  result.x = pane->bounds.x + pane->gutter_width + to_visual_offset(line, x) - pane_calc_left_visible_column(pane);
  /* try to center */
  result.y = pane->bounds.y + y - pane_calc_top_visible_row(pane);
  return result;
}

static Pos pane_to_screen_pos(Pane *pane) {
  return pane_to_screen_pos_xy(pane, pane->buffer->pos.x, pane->buffer->pos.y);
}

static void buffer_move_to_y(Buffer *b, int y) {
  y = clampi(y, 0, buffer_numlines(b)-1);
  b->pos.y = y;
}

static void buffer_move_to(Buffer *b, int x, int y) {
  buffer_move_to_y(b, y);
  x = clampi(x, 0, array_len(b->lines[y]));
  b->pos.x = x;
  b->pos.ghost_x = x;
}

static void buffer_move(Buffer *b, int dx, int dy) {
  Array(char) old_line;
  Array(char) new_line;
  int old_x;

  old_line = b->lines[b->pos.y];
  old_x = b->pos.x;

  /* y is easy */
  b->pos.y = clampi(b->pos.y + dy, 0, array_len(b->lines) - 1);

  new_line = b->lines[b->pos.y];

  /* use ghost pos? */
  if (dx == 0) {
    if (b->pos.ghost_x == GHOST_EOL)
      b->pos.x = array_len(new_line);
    else
      b->pos.x = from_visual_offset(new_line, b->pos.ghost_x);
  }
  else {
    int old_vis_x;

    /* buffer_move visually down/up */
    /* TODO: only need to do this if dy != 0 */
    old_vis_x = to_visual_offset(old_line, old_x);
    b->pos.x = from_visual_offset(new_line, old_vis_x);

    b->pos.x = clampi(b->pos.x, 0, array_len(new_line));

    b->pos.x += dx;

    b->pos.ghost_x = to_visual_offset(new_line, b->pos.x);
  }
}

static void status_message_set(const char *fmt, ...) {
  int n;
  va_list args;

  buffer_truncate_to_n_lines(&G.message_buffer, 1);
  array_resize(G.message_buffer.lines[0], G.term_width-1);

  va_start(args, fmt);
  n = vsnprintf(G.message_buffer.lines[0], buffer_linesize(&G.message_buffer, 0), fmt, args);
  va_end(args);

  n = at_most(n, buffer_linesize(&G.message_buffer, 0));
  array_resize(G.message_buffer.lines[0], n);
}

/****** @TOKENIZER ******/

static void tokenizer_push_token(Array(Array(TokenInfo)) *tokens, int x, int y, Token token) {
  array_reserve(*tokens, y+1);
  array_push((*tokens)[y], tokeninfo_create(token, x));
}

static void tokenize(Buffer *b) {
  static Array(char) identifier_buffer;
  int x = 0, y = 0, i;

  /* reset old tokens */
  for (i = 0; i < array_len(b->tokens); ++i)
    array_free(b->tokens[i]);
  array_resize(b->tokens, buffer_numlines(b));
  for (i = 0; i < array_len(b->tokens); ++i)
    b->tokens[i] = 0;

  for (;;) {
    Array(char) row;
    /* whitespace */
    while (isspace(buffer_getchar(b, x, y)))
      if (buffer_advance_xy(b, &x, &y))
        return;

    /* TODO: how do we handle comments ? */

    /* identifier */
    row = buffer_getline(b, y);
    if (isalpha(row[x]) || row[x] == '_') {
      char *str;

      array_resize(identifier_buffer, 0);
      /* TODO: predefined keywords */
      tokenizer_push_token(&b->tokens, x, y, TOKEN_IDENTIFIER);

      for (; x < array_len(row); ++x) {
        if (!isalnum(row[x]) && row[x] != '_')
          break;
        array_push(identifier_buffer, row[x]);
      }
      array_push(identifier_buffer, 0);
      /* check if identifier already exists */
      for (i = 0; i < array_len(b->identifiers); ++i)
        if (strcmp(identifier_buffer, b->identifiers[i]) == 0)
          goto identifier_done;

      str = malloc(array_len(identifier_buffer));
      strcpy(str, identifier_buffer);
      array_push(b->identifiers, str);

      identifier_done:;
    }

    /* number */
    else if (isdigit(row[x])) {
      tokenizer_push_token(&b->tokens, x, y, TOKEN_NUMBER);
      for (; x < array_len(row) && isdigit(row[x]); ++x);
      if (buffer_getchar(b, x, y) == '.')
        for (; x < array_len(row) && isdigit(row[x]); ++x);
    }

    /* single char token */
    else {
      tokenizer_push_token(&b->tokens, x, y, row[x]);
      if (buffer_advance_xy(b, &x, &y))
        return;
    }

    if (x == array_len(row))
      buffer_advance_xy(b, &x, &y);
  }
}


/****** TERMINAL STUFF !!DO NOT USE OUTSIDE OF RENDERER!! ******/

typedef struct {const char *str; int len;} TermCommand;
static TermCommand term_fcolors[] = {
  {"\x1b[30m", 5}, /* COLOR_BLACK  */
  {"\x1b[31m", 5}, /* COLOR_RED    */
  {"\x1b[32m", 5}, /* COLOR_GREEN  */
  {"\x1b[33m", 5}, /* COLOR_YELLOW */
  {"\x1b[34m", 5}, /* COLOR_BLUE   */
  {"\x1b[35m", 5}, /* COLOR_MAGENTA*/
  {"\x1b[36m", 5}, /* COLOR_CYAN   */
  {"\x1b[37m", 5}, /* COLOR_WHITE  */
};
static TermCommand term_bcolors[] = {
  {"\x1b[40m", 5}, /* COLOR_BLACK  */
  {"\x1b[41m", 5}, /* COLOR_RED    */
  {"\x1b[42m", 5}, /* COLOR_GREEN  */
  {"\x1b[43m", 5}, /* COLOR_YELLOW */
  {"\x1b[44m", 5}, /* COLOR_BLUE   */
  {"\x1b[45m", 5}, /* COLOR_MAGENTA*/
  {"\x1b[46m", 5}, /* COLOR_CYAN   */
  {"\x1b[47m", 5}, /* COLOR_WHITE  */
};
STATIC_ASSERT(ARRAY_LEN(term_fcolors) == COLOR_COUNT, all_term_font_colors_defined);
STATIC_ASSERT(ARRAY_LEN(term_bcolors) == COLOR_COUNT, all_term_background_colors_defined);

static void term_inverse_video(Array(char) *buf, int inverse) {
  if (inverse)
    array_push_str(buf, "\x1b[7m");
  else
    array_push_str(buf, "\x1b[27m");
}

static void term_apply_style_slow(Array(char) *buf, Style style) {
  array_push_str(buf, term_fcolors[style.fcolor].str);
  array_push_str(buf, term_bcolors[style.bcolor].str);
  term_inverse_video(buf, style.inverse);
}

static void term_apply_style(Array(char) *buf, Style style, Style old_style) {
  if (style.fcolor != old_style.fcolor)
    array_push_str(buf, term_fcolors[style.fcolor].str);
  if (style.bcolor != old_style.bcolor)
    array_push_str(buf, term_bcolors[style.bcolor].str);
  if (style.inverse != old_style.inverse)
    term_inverse_video(buf, style.inverse);
}

static void term_clear_screen() {
  if (write(STDOUT_FILENO, "\x1b[2J", 4) != 4) panic();
}

static void term_hide_cursor() {
  if (write(STDOUT_FILENO, "\x1b[?25l", 6) != 6) panic();
}

static void term_clear_line() {
  if (write(STDOUT_FILENO, "\x1b[2K", 4) != 4) panic();
}

static void term_show_cursor() {
  if (write(STDOUT_FILENO, "\x1b[?25h", 6) != 6) panic();
}

static void term_cursor_move(int x, int y) {
  char buf[32];
  int n = sprintf(buf, "\x1b[%i;%iH", y+1, x+1);
  if (write(STDOUT_FILENO, buf, n+1) != n+1) panic();
}

static void term_reset_video() {
  if (write(STDOUT_FILENO, "\x1b[0m", 4) != 4) panic();
}

static int term_get_dimensions(int *w, int *h) {
  struct winsize ws;
  int res = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
  if (res == -1 || !ws.ws_col) {
    /* fall back to moving the cursor to the bottom right and querying its position */
    printf("%s", "\x1b[999C\x1b[999B\x1b[6n");
    fflush(stdout);
    res = scanf("\x1b[%d;%dR", h, w);
    if (res != 2 || !*w || !*h) return -1;
  } else {
    *w = ws.ws_col;
    *h = ws.ws_row;
  }

  return 0;
}

static void term_enable_raw_mode() {
  struct termios new_termios;
  tcgetattr(STDIN_FILENO, &G.orig_termios);
  new_termios = G.orig_termios;

  new_termios.c_iflag &= ~(IXON /* Disable Control-s and Control-q */
                           | ICRNL /* Let Control-m and Enter be read as carriage returns ('\r') */ );
  new_termios.c_oflag &= ~(OPOST /* Disable that '\n' output is turned into "\r\n" */ );
  new_termios.c_lflag &= ~(ECHO /* Don't echo the input */
                           | ICANON /* Get input byte by byte, instead of waiting for endline */
                           /*| ISIG */ /* Don't catch control-c */
                           | IEXTEN /* Disable Control-v */);

  /* These should already be set, but we set them anyway just in case */
  new_termios.c_cflag |= (CS8); /* set character to be 8 bits, should already be set */
  new_termios.c_iflag &= ~(INPCK /* No parity checking */
                           | ISTRIP /* don't strip the 8th bit of each input byte */);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios);
  term_hide_cursor();
}

static void term_reset_to_default_settings() {
  static int panicking = 0;
  /* to prevent recursion in term_* functions */
  if (panicking)
    return;
  panicking = 1;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &G.orig_termios);
  term_clear_screen();
  term_show_cursor();
  term_cursor_move(0,0);
  term_reset_video();
}

/* returns:
 * -1 on error
 * 0 on timedout
 * number of bytes read on success
 */
static int read_timeout(unsigned char *buf, int n, int ms) {
  #if 1
  fd_set files;
  struct timeval timeout = {0};
  int res;
  FD_ZERO(&files);
  FD_SET(STDIN_FILENO, &files);

  timeout.tv_sec = 0;
  timeout.tv_usec = ms*1000;

  res = select(STDIN_FILENO+1, &files, 0, 0, ms ? &timeout : 0);
  if (res == -1) return res; /* error */
  else if (res == 0) return res; /* timeout */
  else
  #endif
    return read(STDIN_FILENO, buf, n);
}

static int calc_num_chars(int i) {
  int result = 0;
  while (i > 0) {
    ++result;
    i /= 10;
  }
  return result;
}

/* grabs ownership of filename */
static int file_open(const char *filename, Buffer *buffer_out) {
  int row, num_lines, i;
  Buffer buffer = {0};
  FILE* f;

  buffer.filename = filename;
  f = fopen(buffer.filename, "r");
  if (!f)
    return -1;

  /* get line count */
  num_lines = 0;
  while (!feof(f) && !ferror(f)) num_lines += fgetc(f) == '\n';
  if (ferror(f)) {
    fclose(f);
    return -1;
  }
  IF_DEBUG(status_message_set("File has %i rows", num_lines));

  if (num_lines > 0) {
    array_resize(buffer.lines, num_lines);
    for (i = 0; i < num_lines; ++i)
      buffer.lines[i] = 0;

    fseek(f, 0, SEEK_SET);
    for (row = 0; row < array_len(buffer.lines); ++row) {
      while (1) {
        char c = fgetc(f);
        if (c == EOF) {
          if (ferror(f))
            goto err;
          goto last_line;
        }
        if (c == '\r') c = fgetc(f);
        if (c == '\n') break;
        array_push(buffer.lines[row], c);
      }
    }
    last_line:;
    assert(fgetc(f) == EOF);
  } else {
    array_resize(buffer.lines, 1);
    buffer.lines[0] = 0;
  }

  tokenize(&buffer);

  buffer_guess_tab_type(&buffer);

  *buffer_out = buffer;
  fclose(f);
  return 0;

  err:
  for (i = 0; i < array_len(buffer.lines); ++i)
    array_free(buffer.lines[i]);
  array_free(buffer.lines);
  fclose(f);

  return -1;
}

static void render_clear(int x0, int x1, int y) {
  for (; x0 < x1; ++x0)
    G.screen_buffer[y*G.term_width + x0].c = ' ';
}

static void render_set_font_color(Color color, int x0, int x1, int y0, int y1) {
  int x,y;
  for (y = y0; y < y1; ++y)
    for (x = x0; x < x1; ++x)
      G.screen_buffer[y*G.term_width + x].style.fcolor = color;
}

static void render_set_background_color(Color color, int x0, int x1, int y0, int y1) {
  int x,y;
  for (x = x0; x < x1; ++x)
    for (y = y0; y < y1; ++y)
      G.screen_buffer[y*G.term_width + x].style.bcolor = color;
}

static void render_marker(Pane *p) {
  Pos pos = pane_to_screen_pos(p);
  G.screen_buffer[pos.y*G.term_width + pos.x].style.inverse = 1;
}

/* x,y: screen bounds for pane */
static void render_pane(Pane *p, int draw_gutter) {
  Buffer *b;
  int i, i_end;
  int x0,x1,y;
  int line_offset_x;

  b = p->buffer;
  x0 = p->bounds.x;
  x1 = p->bounds.x + p->bounds.w;
  y = p->bounds.y;

  /* calc gutter width */
  i = pane_calc_top_visible_row(p);
  i_end = pane_calc_bottom_visible_row(p)+1;

  if (draw_gutter)
    p->gutter_width = calc_num_chars(i_end) + 1;
  else
    p->gutter_width = 0;

  line_offset_x = pane_calc_left_visible_column(p);

  render_set_background_color(p->background_color, x0, x1, y, y+p->bounds.h);
  render_set_font_color(p->font_color, x0, x1, y, y+p->bounds.h);
  render_set_font_color(COLOR_YELLOW, x0, x0+p->gutter_width - 1, p->bounds.y, p->bounds.y+p->bounds.h);

  /* calc where we start */
  for (; i < i_end; ++i, ++y) {
    Array(char) line;

    if (i >= buffer_numlines(b)) {
      render_clear(x0, x1, y);
      continue;
    }

    line = b->lines[i];

    /* gutter */
    render_str(x0, x0+p->gutter_width, y, "%*i", p->gutter_width-1, i+1);

    if (!line || array_len(line) <= line_offset_x)
      continue;

    /* text */
    render_strn(x0 + p->gutter_width, x1, y, line + line_offset_x, array_len(line) - line_offset_x);
  }
}

static void save_buffer(Buffer *b) {
  FILE* f;
  int i;

  assert(b->filename);

  f = fopen(b->filename, "wb");
  if (!f) {
    status_message_set("Could not open file %s for writing: %s", b->filename, strerror(errno));
    return;
  }

  /* TODO: actually write to a temporary file, and when we have succeeded, rename it over the old file */
  for (i = 0; i < buffer_numlines(b); ++i) {
    unsigned int num_to_write = array_len(b->lines[i]);
    if (fwrite(b->lines[i], 1, num_to_write, f) != num_to_write) {
      status_message_set("Failed to write to %s: %s", b->filename, strerror(errno));
      return;
    }
    /* TODO: windows endlines option */
    fputc('\n', f);
  }

  b->modified = 0;

  fclose(f);
}

static void screen_buffer_reset() {
  int i;
  for (i = 0; i < G.term_width * G.term_height; ++i) {
    G.screen_buffer[i].c = ' ';
    G.screen_buffer[i].style = G.default_style;
  }
}

static void check_terminal_resize() {
  int w,h;
  term_get_dimensions(&w, &h);
  if (w == G.term_width && h == G.term_height)
    return;

  G.term_width = w;
  G.term_height = h;
  free(G.screen_buffer);
  G.screen_buffer = malloc(G.term_width * G.term_height * sizeof(*G.screen_buffer));
  screen_buffer_reset();
  G.bottom_pane.bounds = rect_create(0, G.term_height-1, G.term_width-1, 1);
  G.main_pane.bounds = rect_create(0, 0, G.term_width-1, G.term_height-1);
}

static void render_flush() {
  int x,y;
  Style style;

  array_resize(G.tmp_render_buffer, 0);

  style = G.default_style;
  term_apply_style_slow(&G.tmp_render_buffer, style);

  term_cursor_move(0, 0);

  for (y = 0; y < G.term_height; ++y)
  for (x = 0; x < G.term_width; ++x) {
    Pixel p = G.screen_buffer[y*G.term_width + x];

    if (style_cmp(p.style, style)) {
      /* flush buffer */
      write(STDOUT_FILENO, G.tmp_render_buffer, array_len(G.tmp_render_buffer));
      array_resize(G.tmp_render_buffer, 0);

      /* apply style */
      term_apply_style(&G.tmp_render_buffer, p.style, style);
      style = p.style;
    }
    array_push(G.tmp_render_buffer, p.c);
  }
  /* flush buffer */
  write(STDOUT_FILENO, G.tmp_render_buffer, array_len(G.tmp_render_buffer));
  array_resize(G.tmp_render_buffer, 0);

  fflush(stdout);

  screen_buffer_reset();

}

static void mode_search() {
  G.mode = MODE_SEARCH;
  G.search_begin_pos = G.main_pane.buffer->pos;
  G.search_failed = 0;
  G.bottom_pane.buffer = &G.search_buffer;
  buffer_empty(&G.search_buffer);
}

static void mode_goto() {
  G.mode = MODE_GOTO;
  G.goto_line_number = 0;
  G.bottom_pane.buffer = &G.message_buffer;
  status_message_set("goto");
}

static void mode_delete() {
  G.mode = MODE_DELETE;
  G.bottom_pane.buffer = &G.message_buffer;
  status_message_set("delete");
}

static void mode_normal(int set_message) {
  G.mode = MODE_NORMAL;
  G.bottom_pane.buffer = &G.message_buffer;

  G.dropdown_visible = 0;

  if (set_message)
    status_message_set("normal");
}

static void mode_insert() {
  G.mode = MODE_INSERT;
  G.bottom_pane.buffer = &G.message_buffer;
  G.dropdown_visible = 0;
  status_message_set("insert");
}

static void mode_menu() {
  G.mode = MODE_MENU;
  G.bottom_pane.buffer = &G.menu_buffer;
  buffer_empty(&G.menu_buffer);
}

typedef struct {
  char *str;
  float points;
} DropdownMatch;

static int dropdown_match_cmp(const void *aa, const void *bb) {
  const DropdownMatch *a = aa, *b = bb;

  if (a->points != b->points)
    return b->points - a->points;
  return strlen(a->str) - strlen(b->str);
}

static void dropdown_render(Pane *active_pane) {
  int i, max_width, input_len;
  char *input_str;
  Array(char*) identifiers;
  DropdownMatch *best_matches;
  int num_best_matches;
  Buffer *active_buffer;
  Pos p;

  num_best_matches = 0;
  best_matches = malloc(G.dropdown_size * sizeof(*best_matches));

  active_buffer = active_pane->buffer;
  identifiers = active_buffer->identifiers;

  /* this shouldn't happen.. but just to be safe */
  if (G.dropdown_pos.y != active_pane->buffer->pos.y) {
    G.dropdown_visible = 0;
    return;
  }

  /* find matching identifiers */
  input_str = buffer_getstr_p(active_pane->buffer, G.dropdown_pos);
  input_len = active_pane->buffer->pos.x - G.dropdown_pos.x;
  num_best_matches = 0;

  for (i = 0; i < array_len(identifiers); ++i) {
    float points, gain;
    char *in, *in_end, *test, *test_end;
    int test_len;
    char *identifier;

    identifier = identifiers[i];

    points = 0;
    gain = 10;

    test_len = strlen(identifier);
    if (test_len < input_len)
      continue;

    in = input_str;
    in_end = in + input_len;
    test = identifier;
    test_end = test + test_len;

    for (; in < in_end && test < test_end; ++test) {
      if (*in == *test) {
        points += gain;
        gain = 10;
        ++in;
      }
      else if (tolower(*in) == tolower(*test)) {
        points += gain*0.8f;
        gain = 10;
        ++in;
      }
      else
        gain *= 0.7;
    }

    if (in != in_end || points <= 0)
      continue;

    /* push match */

    if (num_best_matches < G.dropdown_size) {
      best_matches[num_best_matches].str = identifier;
      best_matches[num_best_matches].points = points;
      ++num_best_matches;
    } else {
      DropdownMatch *p;
      int i;

      /* find worst match and replace */
      p = best_matches;
      for (i = 1; i < G.dropdown_size; ++i)
        if (best_matches[i].points < p->points)
          p = best_matches+i;
      p->str = identifier;
      p->points = points;
    }

  }

  qsort(best_matches, num_best_matches, sizeof(*best_matches), dropdown_match_cmp);
  max_width = 0;
  buffer_empty(&G.dropdown_buffer);
  for (i = 0; i < num_best_matches; ++i) {
    buffer_push_line(&G.dropdown_buffer, best_matches[i].str);
    max_width = max(max_width, strlen(best_matches[i].str));
  }
  free(best_matches);

  /* position pane */
  p = pane_to_screen_pos_xy(active_pane, G.dropdown_pos.x, G.dropdown_pos.y);
  G.dropdown_pane.bounds.x = p.x;
  G.dropdown_pane.bounds.y = p.y+1;
  G.dropdown_pane.bounds.h = buffer_numlines(&G.dropdown_buffer);
  G.dropdown_pane.bounds.w = max_width + 5;


  if (G.dropdown_visible && !buffer_isempty(&G.dropdown_buffer))
    render_pane(&G.dropdown_pane, 0);
}

static void dropdown_update_on_insert(Pane *active_pane, int input) {

  if (!G.dropdown_visible) {
    if (!isalpha(input) && input != '_')
      goto dropdown_hide;

    G.dropdown_pos = active_pane->buffer->pos;
  }
  else
    if (!isalnum(input) && input != '_')
      goto dropdown_hide;

  G.dropdown_visible = 1;
  return;

  dropdown_hide:
  G.dropdown_visible = 0;
}

static void insert_default(Pane *p, int input) {
  Buffer *b = p->buffer;

  /* TODO: should not set `modifier` if we just enter and exit insert mode */
  if (input == KEY_ESCAPE)
    mode_normal(1);
  else if (!key_is_special(input)) {
    if (p != &G.bottom_pane)
      dropdown_update_on_insert(&G.main_pane, input);
    buffer_insert_char(b, input);
  } else {
    switch (input) {
      case KEY_RETURN:
        buffer_insert_newline(b);
        break;

      case KEY_TAB: {
        if (p != &G.bottom_pane && G.dropdown_visible) {
          char *str;
          int l;

          if (buffer_isempty(&G.dropdown_buffer))
            break;

          str = G.dropdown_buffer.lines[0];
          l = buffer_linesize(&G.dropdown_buffer, 0);
          buffer_replace(b, G.dropdown_pos.x, b->pos.x, b->pos.y, str, l);
          G.dropdown_visible = 0;
        }
        else
          buffer_insert_tab(b);
      } break;

      case KEY_BACKSPACE:
        buffer_delete_char(b);
        break;

      default:
        break;
    }
  }
}

static int process_input() {
  int input;

  /* get input */
  {
    unsigned char c;
    int nread;
    while ((nread = read_timeout(&c, 1, 0)) != 1)
      if (nread == -1 && errno != EAGAIN)
        return 1;

    /* escape sequence? */
    if (c == '\x1b') {
      /* read '[' */
      nread = read_timeout(&c, 1, G.read_timeout_ms);
      if (nread == -1 && errno != EAGAIN)
        return 1;
      if (nread == 0 || c != '[') {
        input = KEY_ESCAPE;
        goto input_done;
      }

      /* read actual character */
      nread = read_timeout(&c, 1, G.read_timeout_ms);
      if (nread == -1 && errno != EAGAIN)
        return 1;
      if (nread == 0) {
        input = KEY_UNKNOWN;
        goto input_done;
      }

      IF_DEBUG(fprintf(stderr, "escape %c\n", c);)

      if (c >= '0' && c <= '9') {
        switch (c) {
          case '1': input = KEY_HOME; break;
          case '4': input = KEY_END; break;
        }

        nread = read_timeout(&c, 1, G.read_timeout_ms);
        if (nread == -1 && errno != EAGAIN)
          return 1;
        if (nread == 0 && c != '~') {
          input = KEY_UNKNOWN;
          goto input_done;
        }
      } else {
        switch (c) {
          case 'A': input = KEY_ARROW_UP; break;
          case 'B': input = KEY_ARROW_DOWN; break;
          case 'C': input = KEY_ARROW_RIGHT; break;
          case 'D': input = KEY_ARROW_LEFT; break;
          case 'F': input = KEY_END; break;
          case 'H': input = KEY_HOME; break;
          default: input = 0; break;
        }
      }
    }
    else {
      input = c;
      IF_DEBUG(fprintf(stderr, "%c\n", c);)
    }
  }

  input_done:;

  /* process input */
  switch (G.mode) {
    case MODE_MENU:
      G.bottom_pane.buffer = &G.menu_buffer;
      if (input == KEY_RETURN) {
        Array(char) line;

        if (buffer_isempty(&G.menu_buffer)) {
          mode_normal(1);
          break;
        }

        line = G.menu_buffer.lines[0];
        #define IS_OPTION(str) (array_len(line) == strlen(str) && strncmp(line, str, array_len(line)) == 0)

        /* FIXME: this might buffer overrun */
        if (IS_OPTION("save")) {
          save_buffer(G.main_pane.buffer);
          /* TODO: write out absolute path, instead of relative */
          status_message_set("Wrote %i lines to %s", buffer_numlines(G.main_pane.buffer), G.main_pane.buffer->filename);
          mode_normal(0);
        }
        else if (IS_OPTION("quit") || IS_OPTION("exit"))
          return 1;
        else {
          status_message_set("Unknown option '%.*s'", buffer_linesize(&G.menu_buffer, 0), G.menu_buffer.lines[0]);
          mode_normal(0);
        }

        #undef IS_OPTION
      }
      else if (input == KEY_ESCAPE)
        mode_normal(1);
      else
        insert_default(&G.bottom_pane, input);
      /* FIXME: if backspace so that menu_buffer is empty, exit menu mode */
      break;

    case MODE_NORMAL:
      switch (input) {
        case KEY_UNKNOWN:
          break;

        case 'q':
          if (G.main_pane.buffer->modified)
            status_message_set("You have unsaved changes. If you really want to exit, use :quit");
          else
            return 1;
          break;

        case 'k':
        case KEY_ARROW_UP:
          buffer_move(G.main_pane.buffer, 0, -1);
          break;

        case 'j':
        case KEY_ARROW_DOWN:
          buffer_move(G.main_pane.buffer, 0, 1);
          break;

        case 'h':
        case KEY_ARROW_LEFT:
          buffer_move(G.main_pane.buffer, -1, 0);
          break;

        case 'l':
        case KEY_ARROW_RIGHT:
          buffer_move(G.main_pane.buffer, 1, 0);
          break;

        case 'L':
        case KEY_END:
          buffer_goto_endline(G.main_pane.buffer);
          break;

        case 'H':
        case KEY_HOME:
          buffer_move_to(G.main_pane.buffer, 0, G.main_pane.buffer->pos.y);
          break;

        case 'n': {
          int err;
          Pos old_pos;

          old_pos = G.main_pane.buffer->pos;

          buffer_advance(G.main_pane.buffer);
          err = buffer_find(G.main_pane.buffer, G.search_buffer.lines[0], buffer_linesize(&G.search_buffer, 0));
          if (err)
            G.main_pane.buffer->pos = old_pos;
        } break;

        case ' ':
          mode_search();
          break;

        case 'g':
          mode_goto();
          break;

        case 'o':
          buffer_insert_newline_below(G.main_pane.buffer);
          mode_insert();
          break;

        case 'i':
          mode_insert();
          break;

        case ':':
          mode_menu();
          break;

        case 'd':
          mode_delete();
          break;
      }
      break;

    case MODE_GOTO:
      if (isdigit(input)) {
        G.goto_line_number *= 10;
        G.goto_line_number += input - '0';
        buffer_move_to_y(G.main_pane.buffer, G.goto_line_number-1);
        break;
      }
      switch (input) {
        case 't':
          buffer_move_to(G.main_pane.buffer, 0, 0);
          break;
        case 'b':
          buffer_move_to(G.main_pane.buffer, 0, buffer_numlines(G.main_pane.buffer)-1);
          break;
      }
      mode_normal(1);
      break;

    case MODE_SEARCH:
      G.bottom_pane.buffer = &G.search_buffer;

      if (input == KEY_RETURN || input == KEY_ESCAPE) {
        if (G.search_failed) {
          G.main_pane.buffer->pos = G.search_begin_pos;
          mode_normal(0);
        } else
          mode_normal(1);
      }
      else {
        Array(char) search;
        insert_default(&G.bottom_pane, input);
        search = G.search_buffer.lines[0];
        G.search_failed = buffer_find(G.main_pane.buffer,
                                 search,
                                 array_len(search));
      }
      break;

    case MODE_DELETE:
      switch (input) {
        case 'd':
          buffer_delete_line(G.main_pane.buffer);
          mode_normal(1);
          break;

        default:
          mode_normal(1);
          break;
      }
      break;

    case MODE_INSERT:
      insert_default(&G.main_pane, input);
      break;

    case MODE_COUNT:
      break;
  }

  buffer_move(G.main_pane.buffer, 0, 0); /* update cursor in case anything happened (like changing modes) */
  return 0;
}

static void state_init() {
  int err;

  /* read terminal dimensions */
  err = term_get_dimensions(&G.term_width, &G.term_height);
  if (err) panic();
  IF_DEBUG(fprintf(stderr, "terminal dimensions: %i %i\n", G.term_width, G.term_height););

  /* set default settings */
  G.read_timeout_ms = 1;
  G.tab_width = 4;
  G.default_tab_type = 4;
  G.dropdown_size = 7;

  G.default_style.fcolor = COLOR_WHITE;
  G.default_style.bcolor = COLOR_BLACK;
  G.default_style.bold = G.default_style.italic = G.default_style.inverse = 0;

  /* init screen buffer */
  G.screen_buffer = malloc(G.term_width * G.term_height * sizeof(*G.screen_buffer));
  screen_buffer_reset();

  /* init predefined buffers */
  buffer_empty(&G.menu_buffer);
  buffer_empty(&G.search_buffer);
  buffer_empty(&G.message_buffer);
  buffer_empty(&G.dropdown_buffer);

  G.main_pane.background_color = COLOR_BLACK;
  G.main_pane.font_color = COLOR_WHITE;

  G.bottom_pane.background_color = COLOR_CYAN;
  G.bottom_pane.font_color = COLOR_BLACK;
  G.bottom_pane.bounds = rect_create(0, G.term_height-1, G.term_width-1, 1);
  G.bottom_pane.buffer = &G.message_buffer;

  G.dropdown_pane.buffer = &G.dropdown_buffer;
  G.dropdown_pane.background_color = COLOR_MAGENTA;
  G.dropdown_pane.font_color = COLOR_BLACK;

  G.main_pane.bounds = rect_create(0, 0, G.term_width-1, G.term_height-1);

}

#ifdef DEBUG
static void test() {
  assert(memmem("a", 1, "a", 1));
  assert(!memmem("", 0, "", 0));
  assert(!memmem("", 0, "ab", 2));
  assert(!memmem("a", 1, "z", 1));
  assert(memmem("aa", 2, "baa", 3));
  assert(memmem("aa", 2, "aab", 3));
  assert(memmem("aa", 2, "baab", 4));
  assert(!memmem("aa", 2, "a", 1));
  assert(memmem("abcd", 4, "abcd", 4));
  assert(memmem("abcd", 4, "xabcdy", 6));
}
#endif

int main(int argc, const char **argv) {
  int err;
  (void) argc, (void)argv;

  if (argc < 2) {
    fprintf(stderr, "Usage: cedit <file>\n");
    return 1;
  }

  IF_DEBUG(test());

  /* set up terminal */
  term_enable_raw_mode();

  state_init();

  /* open a buffer */
  {
    const char *filename = argv[1];
    G.main_pane.buffer = malloc(sizeof(*G.main_pane.buffer));
    err = file_open(filename, G.main_pane.buffer);
    if (err) {
      status_message_set("Could not open file %s: %s\n", filename, strerror(errno));
      goto done;
    }
    if (!err)
      status_message_set("loaded %s, %i lines", filename, buffer_numlines(G.main_pane.buffer));
  }

  while (1) {

    /* draw document */
    render_pane(&G.main_pane, 1);

    /* draw menu/status bar */
    render_pane(&G.bottom_pane, 0);

    /* draw dropdown ? */
    dropdown_render(&G.main_pane);

    /* Draw markers */
    render_marker(&G.main_pane);
    if (G.mode == MODE_SEARCH || G.mode == MODE_MENU)
      render_marker(&G.bottom_pane);

    /* render to screen */
    render_flush();

    /* get and process input */
    err = process_input();
    if (err)
      break;

    check_terminal_resize();
  }

  done:
  term_reset_to_default_settings();
  return 0;
}
