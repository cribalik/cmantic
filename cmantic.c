/* CURRENT: Search
 *
 * TODO:
 *
 * Token autocomplete
 * Search backwards
 * Colorize search results in view
 * Color highlighting
 * Action search
 * Draw custom marker instead of using terminals marker
 *
 * load files
 * movement (word, parenthises, block)
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
  #define DEBUG(stmt) stmt
  static void* debug_malloc(unsigned long size, int line, const char* file) {
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
  #define DEBUG(stmt)
#endif

#define STATIC_ASSERT(expr, name) typedef char static_assert_##name[expr?1:-1]
#define ARRAY_LEN(a) (sizeof(a)/sizeof(*a))

static void term_reset_to_default_settings();
static void panic() {
  term_reset_to_default_settings();
  abort();
}

static int at_least(int a, int b) {
  return a < b ? b : a;
}
static int at_most(int a, int b) {
  return b < a ? b : a;
}
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
  const char* filename;
  int tab_type; /* 0 for tabs, 1+ for spaces */

  /* parser stuff */
  Array(Array(TokenInfo)) tokens;

  Pos pos;

  int modified;
} Buffer;

static void buffer_move_to(Buffer *b, int x, int y);
static void buffer_goto_endline(Buffer *b) {
  b->pos.ghost_x = -1;
}

static void buffer_empty(Buffer *b) {
  int i;

  for (i = 0; i < array_len(b->lines); ++i)
    array_free(b->lines[i]);

  array_resize(b->lines, 1);
  b->lines[0] = 0;
  b->pos.x = b->pos.y = 0;
}

static void buffer_truncate_to_n_lines(Buffer *b, int n) {
  int i;

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

static char buffer_getchar(Buffer *b, int x, int y) {
  return b->lines[y][x];
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

static void status_message_set(const char *fmt, ...);
static int buffer_find(Buffer *b, char *str, int n) {
  int x, y;

  if (!n)
    return 1;

  x = b->pos.x;
  y = b->pos.y;

  for (; y < array_len(b->lines); ++y, x = 0) {
    char *p;
    if (x >= array_len(b->lines[y]))
      continue;

    p = memmem(str, n, b->lines[y]+x, array_len(b->lines[y])-x);
    if (!p)
      continue;

    x = p - b->lines[y];
    buffer_move_to(b, x, y);
    status_message_set("jumping to (%i,%i), but arrived at (%i, %i)", x, y, b->pos.x, b->pos.y);
    return 0;
  }
  return 1;
}

static void buffer_move(Buffer *b, int x, int y);
static void buffer_insert_char(Buffer *b, char ch) {
  b->modified = 1;
  array_insert(b->lines[b->pos.y], b->pos.x, ch);
  buffer_move(b, 1, 0);
}

static void buffer_delete_line_at(Buffer *b, int y) {
  array_free(b->lines[y]);
  if (array_len(b->lines) == 1)
    return;
  array_remove_slow(b->lines, y);
}

static void buffer_delete_line(Buffer *b) {
  buffer_delete_line_at(b, b->pos.y);
}

static void buffer_delete_char(Buffer *b) {
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

static void buffer_insert_newline(Buffer *b) {
  int left_of_line;

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
  Color color;
  int x, y;
} RenderColorEntry;
RenderColorEntry render_color_entry_create(Color color, int x, int y) {
  RenderColorEntry result;
  result.color = color;
  result.x = x;
  result.y = y;
  return result;
}

static int render_color_entry_cmp(const void *aa, const void *bb) {
  const RenderColorEntry *a = aa;
  const RenderColorEntry *b = bb;
  if (a->y < b->y)
    return -1;
  if (b->y < a->y)
    return 1;
  return a->x - b->x;
}

static struct State {
  /* some rendering memory */
  struct Renderer {
    char *screen_buffer;
    Array(char) tmp_render_buffer;
    Array(RenderColorEntry) font_colors;
    Array(RenderColorEntry) background_colors;
    int term_width, term_height;
  } renderer;

  /* some editor state */
  Pane main_pane,
       bottom_pane;
  Buffer menu_buffer,
         search_buffer,
         message_buffer;
  Mode mode;

  /* search state */
  int search_failed;
  Pos search_begin_pos;

  /* some settings */
  struct termios orig_termios;
  int read_timeout_ms;
  int tab_width; /* how wide are tabs when rendered */
  int default_tab_type; /* 0 for tabs, 1+ for spaces */
} G;

static void render_strn(int x0, int x1, int y, const char* str, int n) {
  char *out = &G.renderer.screen_buffer[G.renderer.term_width*y];

  if (!str)
    return;

  while (n-- && x0 < x1) {
    if (*str == '\t') {
      int num_to_write = at_most(G.tab_width, x1 - x0);
      memset(out + x0, ' ', num_to_write);
      x0 += num_to_write;
    }
    else
      out[x0++] = *str;
    ++str;
  }
}

static void render_str_v(int x0, int x1, int y, const char* fmt, va_list args) {
  int n;

  array_resize(G.renderer.tmp_render_buffer, x1-x0+1);
  n = vsnprintf(G.renderer.tmp_render_buffer, x1-x0+1, fmt, args);
  render_strn(x0, x1, y, G.renderer.tmp_render_buffer, n);
}

static void render_str(int x0, int x1, int y, const char* fmt, ...) {
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
  return at_most(pane_calc_top_visible_row(pane) + pane->bounds.h, array_len(pane->buffer->lines)) - 1;
}

static int pane_calc_left_visible_column(Pane *pane) {
  return at_least(0, to_visual_offset(pane->buffer->lines[pane->buffer->pos.y], pane->buffer->pos.x) - (pane->bounds.w - pane->gutter_width - 3));
}

static Pos pane_to_screen_pos(Pane *pane) {
  Pos result = {0};
  Array(char) line;

  line = pane->buffer->lines[pane->buffer->pos.y];
  result.x = pane->bounds.x + pane->gutter_width + to_visual_offset(line, pane->buffer->pos.x) - pane_calc_left_visible_column(pane);
  /* try to center */
  result.y = pane->bounds.y + pane->buffer->pos.y - pane_calc_top_visible_row(pane);
  return result;
}

static void buffer_move_to(Buffer *b, int x, int y) {
  y = clampi(y, 0, array_len(b->lines)-1);
  b->pos.y = y;
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
  array_resize(G.message_buffer.lines[0], G.renderer.term_width-1);

  va_start(args, fmt);
  n = vsnprintf(G.message_buffer.lines[0], array_len(G.message_buffer.lines[0]), fmt, args);
  va_end(args);

  n = at_most(n, array_len(G.message_buffer.lines[0]));
  array_resize(G.message_buffer.lines[0], n);
}

/****** @TOKENIZER ******/

static void tokenizer_push_token(Array(Array(TokenInfo)) *tokens, int x, int y, Token token) {
  if (array_len(*tokens) < y)
    array_resize(*tokens, y+1);
  array_push((*tokens)[y], tokeninfo_create(token, x));
}

static void tokenize(Buffer *b) {
  int x = 0, y = 0, i;

  /* reset old tokens */
  for (i = 0; i < array_len(b->tokens); ++i)
    array_free(b->tokens[i]);
  array_resize(b->tokens, array_len(b->lines));
  for (i = 0; i < array_len(b->tokens); ++i)
    b->tokens[i] = 0;

  for (;;) {
    /* whitespace */
    while (isspace(buffer_getchar(b, x, y)))
      if (buffer_advance_xy(b, &x, &y))
        return;

    /* TODO: how do we handle comments ? */

    /* identifier */
    if (isalpha(buffer_getchar(b, x, y))) {
      /* TODO: predefined keywords */
      tokenizer_push_token(&b->tokens, x, y, TOKEN_IDENTIFIER);
      while (isalnum(buffer_getchar(b, x, y)))
        if (buffer_advance_xy(b, &x, &y))
          return;
    }

    /* number */
    else if (isdigit(buffer_getchar(b, x, y))) {
      tokenizer_push_token(&b->tokens, x, y, TOKEN_NUMBER);
      while (isdigit(buffer_getchar(b, x, y)))
        if (buffer_advance_xy(b, &x, &y))
          return;
      if (buffer_getchar(b, x, y) == '.')
        while (isdigit(buffer_getchar(b, x, y)))
          if (buffer_advance_xy(b, &x, &y))
            return;
    }

    /* single char token */
    else {
      tokenizer_push_token(&b->tokens, x, y, buffer_getchar(b, x, y));
      if (buffer_advance_xy(b, &x, &y))
        return;
    }
  }
}


/****** TERMINAL STUFF !!DO NOT USE OUTSIDE OF RENDERER!! ******/

typedef struct {const char *str; int len;} TermColor;
static TermColor term_fcolors[] = {
  {"\x1b[30m", 5}, /* COLOR_BLACK  */
  {"\x1b[31m", 5}, /* COLOR_RED    */
  {"\x1b[32m", 5}, /* COLOR_GREEN  */
  {"\x1b[33m", 5}, /* COLOR_YELLOW */
  {"\x1b[34m", 5}, /* COLOR_BLUE   */
  {"\x1b[35m", 5}, /* COLOR_MAGENTA*/
  {"\x1b[36m", 5}, /* COLOR_CYAN   */
  {"\x1b[37m", 5}, /* COLOR_WHITE  */
};
STATIC_ASSERT(ARRAY_LEN(term_fcolors) == COLOR_COUNT, all_term_font_colors_defined);
static TermColor term_fcolor(Color color) {
  return term_fcolors[color];
}

static TermColor term_bcolors[] = {
  {"\x1b[40m", 5}, /* COLOR_BLACK  */
  {"\x1b[41m", 5}, /* COLOR_RED    */
  {"\x1b[42m", 5}, /* COLOR_GREEN  */
  {"\x1b[43m", 5}, /* COLOR_YELLOW */
  {"\x1b[44m", 5}, /* COLOR_BLUE   */
  {"\x1b[45m", 5}, /* COLOR_MAGENTA*/
  {"\x1b[46m", 5}, /* COLOR_CYAN   */
  {"\x1b[47m", 5}, /* COLOR_WHITE  */
};
STATIC_ASSERT(ARRAY_LEN(term_bcolors) == COLOR_COUNT, all_term_background_colors_defined);
static TermColor term_bcolor(Color color) {
  return term_bcolors[color];
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

static void term_cursor_move_p(Pos p) {
  term_cursor_move(p.x, p.y);
}

static void term_inverse_video(int state) {
  static int inv = 0;
  int val;
  if (state == -1) inv = !inv;
  val = state == -1 ? inv : state;
  if (val) {
    if (write(STDOUT_FILENO, "\x1b[7m", 4) != 4) panic();
  } else {
    if (write(STDOUT_FILENO, "\x1b[m", 3) != 3) panic();
  }
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
}

static void term_reset_to_default_settings() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &G.orig_termios);
  term_clear_screen();
  term_show_cursor();
  term_cursor_move(0,0);
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
static int file_open(const char* filename, Buffer *buffer_out) {
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
  DEBUG(display_message("File has %i rows", num_lines));

  if (num_lines > 0) {
    array_resize(buffer.lines, num_lines);
    memset(buffer.lines, 0, num_lines * sizeof(*buffer.lines));

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

  /* try to figure out tab type */
  /* TODO: use tokens here instead, so we skip comments */
  buffer.tab_type = -1;
  for (i = 0; i < array_len(buffer.lines); ++i) {
    if (!buffer.lines[i])
      continue;

    if (buffer.lines[i][0] == '\t') {
      buffer.tab_type = 0;
    }
    else if (buffer.lines[i][0] == ' ') {
      int num_spaces = 0;
      int j;

      for (j = 0; j < array_len(buffer.lines[i]) && buffer.lines[i][j] == ' '; ++j)
        ++num_spaces;

      if (j == array_len(buffer.lines[i]))
        continue;

      buffer.tab_type = num_spaces;
    }
    else
      continue;
  }
  if (buffer.tab_type == -1)
    buffer.tab_type = G.default_tab_type;

  *buffer_out = buffer;
  return 0;

  err:
  for (i = 0; i < array_len(buffer.lines); ++i)
    array_free(buffer.lines[i]);
  array_free(buffer.lines);

  return -1;
}

static void render_clear(int x0, int x1, int y) {
  memset(&G.renderer.screen_buffer[y*G.renderer.term_width + x0], ' ', x1 - x0);
}

static void render_set_font_color(Color color, int x, int y) {
  array_push(G.renderer.font_colors, render_color_entry_create(color, x, y));
}

static void render_set_background_color(Color color, int x, int y) {
  array_push(G.renderer.background_colors, render_color_entry_create(color, x, y));
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

  /* recalc gutter width */
  if (draw_gutter)
    p->gutter_width = calc_num_chars(array_len(b->lines)) + 1;
  else
    p->gutter_width = 0;

  line_offset_x = pane_calc_left_visible_column(p);

  render_set_background_color(p->background_color, x0, y);
  render_set_font_color(p->font_color, x0, y);

  /* calc where we start */
  i = pane_calc_top_visible_row(p);
  i_end = pane_calc_bottom_visible_row(p);
  for (; i <= i_end; ++i, ++y) {
    Array(char) line;

    if (i >= array_len(b->lines))
      render_clear(x0, x1, y);
    else {
      line = b->lines[i];

      /* gutter */
      render_str(x0, x0+p->gutter_width, y, "%i", i+1);

      if (!line || array_len(line) <= line_offset_x)
        continue;

      /* text */
      render_strn(x0 + p->gutter_width, x1, y, line + line_offset_x, array_len(line) - line_offset_x);
    }
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
  for (i = 0; i < array_len(b->lines); ++i) {
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

static void render_flush() {
  int i;
  Array(char) *tmp_render_buffer = &G.renderer.tmp_render_buffer;
  RenderColorEntry *bcolor, *bcolor_end,
                   *fcolor, *fcolor_end;

  array_resize(*tmp_render_buffer, 0);

  /* basically write the G.renderer.screen_buffer to the terminal */
  term_hide_cursor();
  term_cursor_move(0, 0);

  /* sort the list of color commands */
  qsort(G.renderer.background_colors,
        array_len(G.renderer.background_colors),
        sizeof(*G.renderer.background_colors),
        render_color_entry_cmp);
  qsort(G.renderer.font_colors,
        array_len(G.renderer.font_colors),
        sizeof(*G.renderer.font_colors),
        render_color_entry_cmp);
  bcolor = G.renderer.background_colors;
  bcolor_end = bcolor + array_len(G.renderer.background_colors);
  fcolor = G.renderer.font_colors;
  fcolor_end = fcolor + array_len(G.renderer.font_colors);

  for (i = 0; i < G.renderer.term_height; ++i) {
    char *row;
    int x = 0;

    /* TODO: dirty checking with hashes of row buffer */
    /* TODO: optimize for special case of scrolling down/up */
    row = G.renderer.screen_buffer + i*G.renderer.term_width;

    while ((fcolor < fcolor_end && fcolor->y == i) || (bcolor < bcolor_end && bcolor->y == i)) {
      if (fcolor < fcolor_end && fcolor->y == i && fcolor->x <= x) {
        TermColor tc = term_fcolor(fcolor->color);
        array_push_a(*tmp_render_buffer, tc.str, tc.len);
        ++fcolor;
      }
      else if (bcolor < bcolor_end && bcolor->y == i && bcolor->x <= x) {
        TermColor tc = term_bcolor(bcolor->color);
        array_push_a(*tmp_render_buffer, tc.str, tc.len);
        ++bcolor;
      } else {
        array_push(*tmp_render_buffer, row[x]);
        ++x;
      }
    }
    array_push_a(*tmp_render_buffer, row, G.renderer.term_width-x);
  }
  term_cursor_move(0, 0);
  if (write(STDOUT_FILENO, *tmp_render_buffer, array_len(*tmp_render_buffer)) != array_len(*tmp_render_buffer))
    panic();
  term_cursor_move_p(pane_to_screen_pos(&G.main_pane));
  term_show_cursor();
  fflush(stdout);
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
  if (set_message)
    status_message_set("normal");
}

static void mode_insert() {
  G.mode = MODE_INSERT;
  G.bottom_pane.buffer = &G.message_buffer;
  status_message_set("insert");
}

static void mode_menu() {
  G.mode = MODE_MENU;
  G.bottom_pane.buffer = &G.menu_buffer;
  buffer_empty(&G.menu_buffer);
}

static void insert_default(Pane *p, int input) {
  Buffer *b = p->buffer;

  /* TODO: should not set `modifier` if we just enter and exit insert mode */
  if (input == KEY_ESCAPE)
    mode_normal(1);
  else if (!key_is_special(input)) {
    buffer_insert_char(b, input);
  } else {
    switch (input) {
      case KEY_RETURN:
        buffer_insert_newline(b);
        break;

      case KEY_TAB:
        buffer_insert_tab(b);
        break;

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
    while ((nread = read_timeout(&c, 1, 0)) != 1 && nread != 0)
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

      DEBUG(fprintf(stderr, "escape %c\n", c);)

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
      DEBUG(fprintf(stderr, "%c\n", c);)
    }
  }

  input_done:;

  /* process input */
  switch (G.mode) {
    case MODE_MENU:
      G.bottom_pane.buffer = &G.menu_buffer;
      if (input == KEY_RETURN && array_len(G.menu_buffer.lines[0]) > 0) {
        Array(char) line;

        line = G.menu_buffer.lines[0];
        #define IS_OPTION(str) (array_len(line) == strlen(str) && strncmp(line, str, array_len(line)) == 0)

        /* FIXME: this might buffer overrun */
        if (IS_OPTION("inv")) {
          term_inverse_video(-1);
          mode_normal(1);
        }
        else if (IS_OPTION("save")) {
          save_buffer(G.main_pane.buffer);
          /* TODO: write out absolute path, instead of relative */
          status_message_set("Wrote %i lines to %s", array_len(G.main_pane.buffer->lines), G.main_pane.buffer->filename);
          mode_normal(0);
        }
        else if (IS_OPTION("quit") || IS_OPTION("exit"))
          return 1;
        else {
          status_message_set("Unknown option '%.*s'", array_len(G.menu_buffer.lines[0]), G.menu_buffer.lines[0]);
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
          err = buffer_find(G.main_pane.buffer, G.search_buffer.lines[0], array_len(G.search_buffer.lines[0]));
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
      switch (input) {
        case 't':
          buffer_move_to(G.main_pane.buffer, 0, 0);
          break;
        case 'b':
          buffer_move_to(G.main_pane.buffer, 0, array_len(G.main_pane.buffer->lines)-1);
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

void state_init() {
  int err;

  /* read terminal dimensions */
  err = term_get_dimensions(&G.renderer.term_width, &G.renderer.term_height);
  if (err) panic();
  DEBUG(fprintf(stderr, "terminal dimensions: %i %i\n", G.renderer.term_width, G.renderer.term_height););

  /* set default settings */
  G.read_timeout_ms = 1;
  G.tab_width = 4;
  G.default_tab_type = 4;

  /* allocate memory */
  G.renderer.screen_buffer = malloc(sizeof(*G.renderer.screen_buffer) * G.renderer.term_width * G.renderer.term_height);

  buffer_empty(&G.menu_buffer);
  buffer_empty(&G.search_buffer);
  buffer_empty(&G.message_buffer);
  G.main_pane.background_color = COLOR_BLACK;
  G.main_pane.font_color = COLOR_WHITE;
  G.bottom_pane.background_color = COLOR_RED;
  G.bottom_pane.font_color = COLOR_BLACK;
  G.bottom_pane.bounds = rect_create(0, G.renderer.term_height-1, G.renderer.term_width-1, 1);
  G.bottom_pane.buffer = &G.message_buffer;
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

int main(int argc, const char** argv) {
  int err;
  (void) argc, (void)argv;

  if (argc < 2) {
    fprintf(stderr, "Usage: cedit <file>\n");
    return 1;
  }

  test();

  /* set up terminal */
  term_enable_raw_mode();

  state_init();

  /* open a buffer */
  {
    const char* filename = argv[1];
    G.main_pane.buffer = malloc(sizeof(*G.main_pane.buffer));
    err = file_open(filename, G.main_pane.buffer);
    if (err) {
      status_message_set("Could not open file %s: %s\n", filename, strerror(errno));
      goto done;
    }
    if (!err)
      status_message_set("loaded %s, %i lines", filename, array_len(G.main_pane.buffer->lines));
    G.main_pane.bounds.x = 0;
    G.main_pane.bounds.y = 0;
    G.main_pane.bounds.w = G.renderer.term_width - 1;
    G.main_pane.bounds.h = G.renderer.term_height - 2; /* leave room for menu */
  }

  while (1) {

    /* clear rendering buffer */
    memset(G.renderer.screen_buffer, ' ', G.renderer.term_width * G.renderer.term_height * sizeof(*G.renderer.screen_buffer));

    /* draw document */
    render_pane(&G.main_pane, 1);

    /* Draw menu/status bar */
    render_pane(&G.bottom_pane, 0);

    /* render to screen */
    render_flush();

    /* get and process input */
    err = process_input();
    if (err)
      break;

  }

  done:
  term_reset_to_default_settings();
  return 0;
}
