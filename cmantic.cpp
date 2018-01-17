/* CURRENT: utf-8 support
 *
 * TODO:
 *
 * Multiple cursors
 * Crash on empty file
 *
 * Update identifiers as you type
 *       When you make a change, go backwards to check if it was an
 *       identifier, and update the identifier list.
 *       To do this fast, have a hashmap of refcounts for each identifier
 *       if identifier disappears, remove from autocomplete list
 *
 * Move the terminal cursor to where our cursor is, to get tmux to behave better
 * Registers
 * Use 256 color
 * utf-8 support
 * Jumplist
 * Do DFS on autocompletion.
 * Colorize search results in view
 * Undo
 * Folding
 *
 * load files
 * Fuzzy file finding
 * movement (word, parentheses, block)
 * actions (Delete, Yank, ...)
 * add folders
 */

#ifdef _MSC_VER
  #define OS_WINDOWS 1
#else
  #define OS_LINUX 1
#endif

#ifdef OS_LINUX
  #define _POSIX_C_SOURCE 200112L
  #include <unistd.h>
  #include <termios.h>
  #include <sys/ioctl.h>
  #include <sys/select.h>
#else
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN 1
  #endif
  #include <windows.h>
#endif


#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include "array.hpp"

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
#define IS_UTF8_TRAIL(c) (((c)&0xC0) == 0x80)
#define IS_UTF8_HEAD(c) ((c)&0x80)
#define IS_IDENTIFIER_HEAD(c) (isalpha(c) || (c) == '_' || IS_UTF8_HEAD(c))
#define IS_IDENTIFIER_TAIL(c) (isalnum(c) || (c) == '_' || IS_UTF8_TRAIL(c))
#define IS_NUMBER_HEAD(c) (isdigit(c))
#define IS_NUMBER_TAIL(c) (isdigit(c) || ((c) >= 'A' && (c) <= 'F') || ((c) >= 'a' && (c) <= 'f') || (c) == 'x')

typedef unsigned int u32;
STATIC_ASSERT(sizeof(u32) == 4, u32_is_4_bytes);

static void array_push_str(Array<char> *a, const char *str) {
  for (; *str; ++str)
    array_push(*a, *str);
}

static void *memmem(void *needle, int needle_len, void *haystack, int haystack_len) {
  char *h, *hend;
  if (!needle_len || haystack_len < needle_len)
    return 0;

  h = (char*)haystack;
  hend = h + haystack_len - needle_len + 1;

  for (; h < hend; ++h)
    if (memcmp(needle, h, needle_len) == 0)
      return h;
  return 0;
}

static void term_reset_to_default_settings();
static void panic(const char *fmt = "", ...) {
  // term_reset_to_default_settings();
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  fflush(stdout);

  abort();
}

static int at_least(int a, int b) {
  return a < b ? b : a;
}

#define at_least(a,b) max(a,b)
#define at_most(a, b) min(a,b)

/* a,b inclusive */
static int clampi(int x, int a, int b) {
  return x < a ? a : (b < x ? b : x);
}

#define swap(a,b,tmp) ((tmp) = (a), (a) = (b), (b) = (tmp))

typedef enum Key {
  KEY_NONE = 0,
  KEY_UNKNOWN = 1,
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

enum {
  TOKEN_NULL = 0,
  TOKEN_EOF = -1,
  TOKEN_IDENTIFIER = -2,
  TOKEN_NUMBER = -3,
  TOKEN_STRING = -4,
  TOKEN_STRING_BEGIN = -5,
  TOKEN_BLOCK_COMMENT = -6,
  TOKEN_BLOCK_COMMENT_BEGIN = -7,
  TOKEN_BLOCK_COMMENT_END = -8
};

typedef struct TokenInfo {
  int token;
  int x;
} TokenInfo;
TokenInfo tokeninfo_create(int token, int x) {
  TokenInfo t;
  t.token = token;
  t.x = x;
  return t;
}

typedef struct {
  #define GHOST_EOL -1
  #define GHOST_BOL -2
  int x, y;
  int ghost_x; /* if going from a longer line to a shorter line, remember where we were before clamping to row length. a value of -1 means always go to end of line */
} Pos;

static Pos pos_create(int x, int y) {
  Pos p;
  p.x = x;
  p.y = y;
  p.ghost_x = x;
  return p;
}

struct Buffer {
  Array<Array<char>> lines;
  const char *filename;
  int tab_type; /* 0 for tabs, 1+ for spaces */

  /* parser stuff */
  Array<Array<TokenInfo>> tokens;
  Array<char*> identifiers;

  Pos pos;

  int modified;

  Array<char>& operator[](int i) {return lines[i];}
  const Array<char>& operator[](int i) const {return lines[i];}
  int num_lines() const {return lines.size;}
};

static void buffer_move_to_x(Buffer *b, int x);

static void buffer_goto_endline(Buffer *b) {
  buffer_move_to_x(b, b->lines[b->pos.y].size);
  b->pos.ghost_x = GHOST_EOL;
}

static int buffer_begin_of_line(Buffer *b, int y) {
  int x;

  x = 0;
  while (x < b->lines[y].size && (isspace(b->lines[y][x])))
    ++x;
  return x;
}

static void buffer_goto_beginline(Buffer *b) {
  int x;

  x = buffer_begin_of_line(b, b->pos.y);
  buffer_move_to_x(b, x);
  b->pos.ghost_x = GHOST_BOL;
}

static void buffer_empty(Buffer *b) {
  int i;

  b->modified = 1;

  for (i = 0; i < b->lines.size; ++i)
    array_free(b->lines[i]);

  b->lines.size = 0;
  array_pushz(b->lines);
  b->pos.x = b->pos.y = 0;
}

static void buffer_truncate_to_n_lines(Buffer *b, int n) {
  int i;

  b->modified = 1;
  n = at_least(n, 1);

  if (n >= b->lines.size)
    return;

  for (i = n; i < b->lines.size; ++i)
    array_free(b->lines[i]);
  b->lines.size = n;
  b->pos.y = at_most(b->pos.y, n-1);
}

static int buffer_advance_xy(Buffer *b, int *x, int *y) {
  ++(*x);
  if (*x >= b->lines[*y].size) {
    *x = 0;
    ++(*y);
    while (*y < b->lines.size && b->lines[*y].size == 0)
      ++(*y);
    if (*y == b->lines.size) {
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
      *x = b->lines[0].size;
      return 1;
    }
    *x = b->lines[*y].size;
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

static Array<char>* buffer_getline(Buffer *b, int y) {
  return &b->lines[y];
}

static char *buffer_getstr(Buffer *b, int x, int y) {
  return b->lines[y]+x;
}

static char *buffer_getstr_p(Buffer *b, Pos p) {
  return &b->lines[p.y][p.x];
}

static char buffer_getchar(Buffer *b, int x, int y) {
  return x >= b->lines[y].size ? '\n' : b->lines[y][x];
}

static void buffer_move_to(Buffer *b, int x, int y);
static void buffer_move(Buffer *b, int x, int y);
static void status_message_set(const char *fmt, ...);

static int buffer_find_r(Buffer *b, char *str, int n, int stay) {
  int x, y;

  if (!n)
    return 1;

  x = b->pos.x;
  if (!stay)
    --x;
  y = b->pos.y;

  for (;; --x) {
    char *p;
    if (y < 0)
      return 1;
    if (x < 0) {
      --y;
      if (y < 0)
        return 1;
      x = b->lines[y].size;
      continue;
    }

    p = (char*)memmem(str, n, b->lines[y], x);
    if (!p)
      continue;

    x = p - b->lines[y];
    buffer_move_to(b, x, y);
    return 0;
  }
}

static int buffer_find(Buffer *b, char *str, int n, int stay) {
  int x, y;

  if (!n)
    return 1;

  x = b->pos.x;
  if (!stay)
    ++x;
  y = b->pos.y;

  for (; y < b->lines.size; ++y, x = 0) {
    char *p;
    if (x >= b->lines[y].size)
      continue;

    p = (char*)memmem(str, n, b->lines[y]+x, b->lines[y].size-x);
    if (!p)
      continue;

    x = p - b->lines[y];
    buffer_move_to(b, x, y);
    return 0;
  }
  return 1;
}

static int buffer_autoindent(Buffer *b, int y);
static void buffer_move_y(Buffer *b, int dy);
static void buffer_move_x(Buffer *b, int dx);

static void buffer_insert_str(Buffer *b, int x, int y, const char *str, int n) {
  if (!n)
    return;
  b->modified = 1;
  array_inserta(b->lines[y], x, str, n);
  buffer_move_x(b, n);
}

static void buffer_replace(Buffer *b, int x0, int x1, int y, const char *str, int n) {
  b->modified = 1;
  array_remove_slown(b->lines[y], x0, x1-x0);
  buffer_move_to(b, x0, y);
  buffer_insert_str(b, x0, y, str, n);
}

static void buffer_remove_trailing_whitespace(Buffer *b, int y) {
  int x;
  
  x = b->lines[y].size - 1;
  while (x >= 0 && isspace(buffer_getchar(b, x, y)))
    --x;
  b->lines[y].size = x+1;
  if (b->pos.y == y && b->pos.x > x+1)
    buffer_move_to_x(b, x+1);
}

static void buffer_pretty_range(Buffer *b, int y0, int y1) {
  for (; y0 < y1; ++y0) {
#if 0
    int diff;
    diff = buffer_autoindent(b, y0);
    if (y0 == b->pos.y)
      buffer_move_x(b, diff);
#endif
    buffer_remove_trailing_whitespace(b, y0);
  }
}

static void buffer_pretty(Buffer *b, int y) {
  buffer_pretty_range(b, y, y+1);
}


static void buffer_insert_char(Buffer *b, unsigned char ch[4]) {
  int n = 1;
  if (IS_UTF8_HEAD(ch[0]))
    while (n < 4 && IS_UTF8_TRAIL(ch[n]))
      ++n;
  b->modified = 1;
  array_inserta(b->lines[b->pos.y], b->pos.x, (char*)ch, n);
  buffer_move_x(b, 1);
  if (ch[0] == '}')
    buffer_move_x(b, buffer_autoindent(b, b->pos.y));
}

static void buffer_delete_line_at(Buffer *b, int y) {
  b->modified = 1;
  array_free(b->lines[y]);
  if (b->lines.size == 1)
    return;
  array_remove_slow(b->lines, y);
}

static void buffer_delete_line(Buffer *b) {
  buffer_delete_line_at(b, b->pos.y);
}

/* b exclusive */
static void buffer_remove_range(Buffer *buf, Pos a, Pos b) {
  int y0 = a.y;
  int y1 = b.y;
  int y;

  if (a.y > b.y || (a.y == b.y && a.x > b.x)) {
    Pos tmp;
    swap(a, b, tmp);
  }

  if (y0 == y1) {
    array_remove_slown(buf->lines[y0], a.x, b.x-a.x);
    return;
  }

  buf->lines[y0].size = a.x;
  ++y0;
  for (y = y0; y < y1; ++y)
    buffer_delete_line_at(buf, y0);
  array_remove_slown(buf->lines[y0], 0, b.x);
}

static void buffer_delete_char(Buffer *b) {
  b->modified = 1;
  if (b->pos.x == 0) {
    if (b->pos.y == 0)
      return;

    /* move up and right */
    buffer_move_y(b, -1);
    buffer_goto_endline(b);
    array_push(b->lines[b->pos.y], b->lines[b->pos.y+1].data, b->lines[b->pos.y+1].size);
    buffer_delete_line_at(b, b->pos.y+1);
  }
  else {
    Pos p = b->pos;
    buffer_move_x(b, -1);
    buffer_remove_range(b, b->pos, p);
  }
}

static void buffer_insert_tab(Buffer *b) {
  int n;

  n = b->tab_type;

  b->modified = 1;
  if (n == 0) {
    array_insert(b->lines[b->pos.y], b->pos.x, '\t');
    buffer_move_x(b, 1);
  }
  else {
    /* TODO: optimize? */
    while (n--)
      array_insert(b->lines[b->pos.y], b->pos.x, ' ');
    buffer_move_x(b, b->tab_type);
  }
}

static int buffer_getindent(Buffer *b, int y) {
  int n = 0;
  int tab_size = b->tab_type ? b->tab_type : 1;
  char tab_char = b->tab_type ? ' ' : '\t';

  for (n = 0;;) {
    if (n >= b->lines[y].size)
      break;
    if (b->lines[y][n] != tab_char)
      break;
    ++n;
  }
  return n/tab_size;
}

static int buffer_autoindent(Buffer *b, int y) {
  int i,indent_above,indent_this,l, diff;
  int tab_size;
  char tab_char;

  tab_char = b->tab_type ? ' ' : '\t';
  tab_size = b->tab_type ? b->tab_type : 1;

  if (y == 0)
    return -buffer_getindent(b, y);

  /* count number of indents above */
  /* skip empty lines */
  for (i = y-1; i > 0; --i)
    if (b->lines[i].size > 0)
      break;
  if (i == 0)
    return -buffer_getindent(b, y);
  indent_above = buffer_getindent(b, i) * tab_size;

  /* TODO: open braces */
  l = b->lines[y-1].size - indent_above;
  diff = 0;
  i = b->lines[y-1].size-1;
  if (i >= 0 && b->lines[y-1][i] == '{')
    ++diff;
  else if (i >= 0 && b->lines[y-1][i] != '}' &&
          ((l >= 3 && strncmp("for",   b->lines[y-1]+indent_above, 3) == 0) ||
           (l >= 2 && strncmp("if",    b->lines[y-1]+indent_above, 2) == 0) ||
           (l >= 5 && strncmp("while", b->lines[y-1]+indent_above, 5) == 0) ||
           (l >= 4 && strncmp("else",  b->lines[y-1]+indent_above, 4) == 0)))
    ++diff;
  else if (y >= 2) {
    int indent_two_above;

    indent_two_above = buffer_getindent(b, y-2) * tab_size;
    l = b->lines[y-2].size - indent_two_above;
    i = b->lines[y-2].size-1;
    if (i >= 0 && b->lines[y-2][i] != '{' && b->lines[y-2][i] != '}' &&
       ((l >= 3 && strncmp("for",   b->lines[y-2]+indent_two_above, 3) == 0) ||
        (l >= 2 && strncmp("if",    b->lines[y-2]+indent_two_above, 2) == 0) ||
        (l >= 5 && strncmp("while", b->lines[y-2]+indent_two_above, 5) == 0) ||
        (l >= 4 && strncmp("else",  b->lines[y-2]+indent_two_above, 4) == 0)))
    --diff;
  }

  indent_this = buffer_getindent(b, y) * tab_size;
  i = b->lines[y].size-1;
  if (i >= 0 && b->lines[y][i] == '}' && !(
        (l >= 3 && strncmp("for",   b->lines[y]+indent_this, 3) == 0) ||
        (l >= 2 && strncmp("if",    b->lines[y]+indent_this, 2) == 0) ||
        (l >= 5 && strncmp("while", b->lines[y]+indent_this, 5) == 0) ||
        (l >= 4 && strncmp("else",  b->lines[y]+indent_this, 4) == 0)))
    --diff;

  /* count number of indents */
  diff = indent_above + diff*tab_size - indent_this;
  if (diff < 0) {
    array_remove_slown(b->lines[y], 0, -diff);
  }
  if (diff > 0) {
    array_insertn(b->lines[y], 0, diff);
    for (i = 0; i < diff; ++i)
      b->lines[y][i] = tab_char;
  }
  return diff;
}

static int buffer_isempty(Buffer *b) {
  return b->lines.size == 1 && b->lines[0].size == 0;
}

static void buffer_push_line(Buffer *b, const char *str) {
  int y, l;

  b->modified = 1;
  y = b->lines.size;
  if (!buffer_isempty(b))
    array_pushz(b->lines);
  else
    y = 0;
  l = strlen(str);
  array_pushn(b->lines[y], l);
  memcpy(b->lines[y], str, l);
}

static void buffer_move_to_y(Buffer *b, int y);

static void buffer_insert_newline(Buffer *b) {
  int left_of_line;

  b->modified = 1;

  left_of_line = b->lines[b->pos.y].size - b->pos.x;
  array_insertz(b->lines, b->pos.y+1);
  array_pusha(b->lines[b->pos.y+1], b->lines[b->pos.y] + b->pos.x, left_of_line);
  b->lines[b->pos.y].size = b->pos.x;


  buffer_move_y(b, 1);
  buffer_move_to_x(b, 0);
  buffer_move_x(b, buffer_autoindent(b, b->pos.y));
  buffer_pretty(b, b->pos.y-1);
}

static void buffer_insert_newline_below(Buffer *b) {
  b->modified = 1;
  array_insertz(b->lines, b->pos.y+1);
  buffer_move_y(b, 1);
  buffer_move_x(b, buffer_autoindent(b, b->pos.y));
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

typedef struct {
  unsigned int fcolor: 4;
  unsigned int bcolor: 4;
  unsigned int bold: 1;
  unsigned int italic: 1;
  unsigned int inverse: 1;
} Style;

typedef struct Pane {
  int gutter_width;
  Rect bounds;
  Buffer *buffer;
  Style style;
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
  char c[4]; /* should be enough to hold any utf8 char that we care about */
  Style style;
} Pixel;

static int style_cmp(Style a, Style b) {
  return a.fcolor != b.fcolor || a.bcolor != b.bcolor || a.bold != b.bold || a.italic != b.italic || a.inverse != b.inverse;
}

static struct State {
  /* @renderer some rendering state */
  Pixel *screen_buffer;
  Array<char> tmp_render_buffer;
  int term_width, term_height;
  Style default_style;
  Style comment_style,
        identifier_style,
        string_style,
        number_style,
        keyword_style;

  /* some editor state */
  Pane main_pane,
       bottom_pane,
       dropdown_pane;
  Buffer menu_buffer,
         search_buffer,
         message_buffer,
         dropdown_buffer;
  Mode mode;

  /* insert state */
  int insert_mode_begin_y;

  /* dropdown state */
  Pos dropdown_pos; /* where dropdown was initialized */
  int dropdown_backwards;
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
  for (i = 0; i < b->lines.size; ++i) {
    if (!b->lines[i])
      continue;

    /* skip comments */
    if (b->lines[i].size >= 2 && b->lines[i][0] == '/' && b->lines[i][1] == '*') {
      int j;
      j = 2;
      for (;;) {
        if (i >= b->lines.size) {
          b->tab_type = G.default_tab_type;
          return;
        }
        if (j >= b->lines[i].size-1) {
          j = 0;
          ++i;
          continue;
        }
        if (b->lines[i][j] == '*' && b->lines[i][j+1] == '/') {
          ++i;
          break;
        }
        ++j;
      }
    }

    if (!b->lines[i])
      continue;

    if (b->lines[i][0] == '\t') {
      b->tab_type = 0;
      break;
    }
    else if (b->lines[i][0] == ' ') {
      int num_spaces = 0;
      int j;

      for (j = 0; j < b->lines[i].size && b->lines[i][j] == ' '; ++j)
        ++num_spaces;

      if (j == b->lines[i].size)
        continue;

      b->tab_type = num_spaces;
      break;
    }
    else
      continue;
  }
  if (b->tab_type == -1)
    b->tab_type = G.default_tab_type;
}

static void char_to_wide(char c, char res[4]) {
  res[1] = res[2] = res[3] = 0;
  res[0] = c;
}

static int utf8_to_wide(const char *str, const char *end, char res[4]) {
  res[0] = res[1] = res[2] = res[3] = 0;

  if (str == end)
    return 0;

  *res++ = *str++;

  if (str == end || !IS_UTF8_TRAIL(*str))
    return 1;
  *res++ = *str++;
  if (str == end || !IS_UTF8_TRAIL(*str))
    return 2;
  *res++ = *str++;
  if (str == end || !IS_UTF8_TRAIL(*str))
    return 3;
  *res++ = *str++;
  return 4;
}

static void render_strn(int x0, int x1, int y, const char *str, int n) {
  Pixel *row = &G.screen_buffer[G.term_width*y];

  if (!str)
    return;

  for (const char *end = str+n; str != end && x0 < x1;) {
    if (*str == '\t') {
      int e = x0 + at_most(G.tab_width, x1 - x0);
      for (; x0 < e; ++x0)
        char_to_wide(' ', row[x0].c);
      ++str;
    }
    else {
      str += utf8_to_wide(str, end, row[x0].c);
      ++x0;
    }
  }
  /* pad with spaces to x1 */
  for (; x0 < x1; ++x0)
    char_to_wide(' ', row[x0].c);
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

static int to_visual_offset(Array<char> line, int x) {
  int result = 0;
  int i;

  if (!line) return result;

  for (i = 0; i < x; ++i) {
    if (IS_UTF8_TRAIL(line[i]))
      continue;

    ++result;
    if (line[i] == '\t')
      result += G.tab_width-1;
  }
  return result;
}

static Pos to_visual_pos(Buffer *b, Pos p) {
  p.x = to_visual_offset(b->lines[p.y], p.x);
  return p;
}

/* returns the logical index located visually at x */
static int from_visual_offset(Array<char> line, int x) {
  int visual = 0;
  int i;

  if (!line) return 0;

  for (i = 0; i < line.size; ++i) {
    if (IS_UTF8_TRAIL(line[i]))
      continue;
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

static int pane_calc_left_visible_column(Pane *pane) {
  return at_least(0, to_visual_offset(pane->buffer->lines[pane->buffer->pos.y], pane->buffer->pos.x) - (pane->bounds.w - pane->gutter_width - 3));
}

static Pos pane_to_screen_pos_xy(Pane *pane, int x, int y) {
  Pos result = {0};
  Array<char> line;

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
  y = clampi(y, 0, b->lines.size-1);
  b->pos.y = y;
}

static void buffer_move_to_x(Buffer *b, int x) {
  x = clampi(x, 0, b->lines[b->pos.y].size);
  b->pos.x = x;
  b->pos.ghost_x = x;
}

static void buffer_move_to(Buffer *b, int x, int y) {
  buffer_move_to_y(b, y);
  buffer_move_to_x(b, x);
}

static void buffer_move_y(Buffer *b, int dy) {
  b->pos.y = clampi(b->pos.y + dy, 0, b->lines.size - 1);

  if (b->pos.ghost_x == GHOST_EOL)
    b->pos.x = b->lines[b->pos.y].size;
  else if (b->pos.ghost_x == GHOST_BOL)
    b->pos.x = buffer_begin_of_line(b, b->pos.y);
  else
    b->pos.x = from_visual_offset(b->lines[b->pos.y], b->pos.ghost_x);
}

static void buffer_move_x(Buffer *b, int dx) {
  int w = b->lines[b->pos.y].size;

  if (dx > 0) {
    for (; dx > 0 && b->pos.x < w; --dx) {
      ++b->pos.x;
      while (b->pos.x < w && IS_UTF8_TRAIL(b->lines[b->pos.y][b->pos.x]))
        ++b->pos.x;
    }
  }
  if (dx < 0) {
    for (; dx < 0 && b->pos.x > 0; ++dx) {
      --b->pos.x;
      while (b->pos.x > 0 && IS_UTF8_TRAIL(b->lines[b->pos.y][b->pos.x]))
        --b->pos.x;
    }
  }
  b->pos.x = clampi(b->pos.x, 0, w);
  b->pos.ghost_x = to_visual_offset(b->lines[b->pos.y], b->pos.x);
}

static void buffer_move(Buffer *b, int dx, int dy) {
  if (dy)
    buffer_move_y(b, dy);
  if (dx)
    buffer_move_x(b, dx);
}

static void status_message_set(const char *fmt, ...) {
  int n;
  va_list args;

  buffer_truncate_to_n_lines(&G.message_buffer, 1);
  array_resize(G.message_buffer[0], G.term_width-1);

  va_start(args, fmt);
  n = vsnprintf(G.message_buffer[0], G.message_buffer[0].size, fmt, args);
  va_end(args);

  n = at_most(n, G.message_buffer[0].size);
  array_resize(G.message_buffer[0], n);
}

/****** @TOKENIZER ******/

static void tokenizer_push_token(Array<Array<TokenInfo>> *tokens, int x, int y, int token) {
  array_reserve(*tokens, y+1);
  array_push((*tokens)[y], tokeninfo_create(token, x));
}

static void tokenize(Buffer *b) {
  static Array<char> identifier_buffer;
  int x = 0, y = 0;

  /* reset old tokens */
  for (int i = 0; i < b->tokens.size; ++i)
    array_free(b->tokens[i]);
  array_resize(b->tokens, b->lines.size);
  for (int i = 0; i < b->tokens.size; ++i)
    b->tokens[i] = {};

  for (;;) {
    /* whitespace */
    while (isspace(buffer_getchar(b, x, y)))
      if (buffer_advance_xy(b, &x, &y))
        return;

    /* TODO: how do we handle comments ? */

    /* identifier */
    char *row = b->lines[y].data;
    int rowsize = b->lines[y].size;
    if (isalpha(row[x]) || row[x] == '_') {
      char *str;

      array_resize(identifier_buffer, 0);
      /* TODO: predefined keywords */
      tokenizer_push_token(&b->tokens, x, y, TOKEN_IDENTIFIER);

      for (; x < rowsize; ++x) {
        if (!isalnum(row[x]) && row[x] != '_')
          break;
        array_push(identifier_buffer, row[x]);
      }
      array_push(identifier_buffer, '\0');
      /* check if identifier already exists */
      for (int i = 0; i < b->identifiers.size; ++i)
        if (strcmp(identifier_buffer, b->identifiers[i]) == 0)
          goto identifier_done;

      str = (char*)malloc(identifier_buffer.size);
      array_copy(identifier_buffer, str);
      array_push(b->identifiers, str);

      identifier_done:;
    }

    /* number */
    else if (isdigit(row[x])) {
      tokenizer_push_token(&b->tokens, x, y, TOKEN_NUMBER);
      for (; x < rowsize && isdigit(row[x]); ++x);
      if (buffer_getchar(b, x, y) == '.')
        for (; x < rowsize && isdigit(row[x]); ++x);
    }

    /* single char token */
    else {
      tokenizer_push_token(&b->tokens, x, y, row[x]);
      if (buffer_advance_xy(b, &x, &y))
        return;
    }

    if (x == rowsize)
      if (buffer_advance_xy(b, &x, &y))
        return;
  }
}

static int token_read(Buffer *b, int *xp, int *yp, int y_end, Pos *start, Pos *end) {
  int token;
  int x,y;
  x = *xp, y = *yp;

  for (;;) {
    char c;

    if (y >= y_end || y >= b->lines.size) {
      token = TOKEN_NULL;
      break;
    }
    if (x >= b->lines[y].size) {
      x = 0;
      ++y;
      continue;
    }


    c = b->lines[y][x];

    if (isspace(c)) {
      ++x;
      continue;
    }

    /* identifier */
    if (IS_IDENTIFIER_HEAD(c)) {
      token = TOKEN_IDENTIFIER;
      if (start)
        *start = pos_create(x, y);
      while (x < b->lines[y].size) {
        if (end)
          *end = pos_create(x, y);
        c = b->lines[y][++x];
        if (!IS_IDENTIFIER_TAIL(c))
          break;
      }
      break;
    }

    /* block comment */
    else if (c == '/' && x+1 < b->lines[y].size && b->lines[y][x+1] == '*') {
      token = TOKEN_BLOCK_COMMENT;
      if (start)
        *start = pos_create(x, y);
      x += 2;
      /* goto matching end block */
      for (;;) {
        if (y >= y_end || y >= b->lines.size) {
          if (end)
            *end = pos_create(x, y);
          token = TOKEN_BLOCK_COMMENT_BEGIN;
          break;
        }
        if (x >= b->lines[y].size) {
          ++y;
          x = 0;
          continue;
        }

        c = b->lines[y][x];
        if (c == '*' && x+1 < b->lines[y].size && b->lines[y][x+1] == '/') {
          if (end)
            *end = pos_create(x+1, y);
          x += 2;
          break;
        }
        ++x;
      }
      break;
    }

    /* end of block comment */
    else if (c == '*' && x+1 < b->lines[y].size && b->lines[y][x+1] == '/') {
      token = TOKEN_BLOCK_COMMENT_END;
      if (start)
        *start = pos_create(x, y);
      if (end)
        *end = pos_create(x+1, y);
      x += 2;
      break;
    }

    /* number */
    else if (IS_NUMBER_HEAD(c)) {
      token = TOKEN_NUMBER;
      if (start)
        *start = pos_create(x, y);
      while (x < b->lines[y].size) {
        if (end)
          *end = pos_create(x, y);
        c = b->lines[y][++x];
        if (!IS_NUMBER_TAIL(c))
          break;
      }
      if (x == b->lines[y].size)
        break;
      if (c == '.' && x+1 < b->lines[y].size && isdigit(b->lines[y][x+1])) {
        c = b->lines[y][++x];
        while (isdigit(c) && x < b->lines[y].size) {
          if (end)
            *end = pos_create(x, y);
          c = b->lines[y][++x];
        }
        if (x == b->lines[y].size)
          break;
      }
      while ((c == 'u' || c == 'l' || c == 'L' || c == 'f') && x < b->lines[y].size) {
        if (end)
          *end = pos_create(x, y);
        c = b->lines[y][++x];
      }
      break;
    }

    else if (c == '"' || c == '\'') {
      char str_char = c;
      token = TOKEN_STRING;
      if (start)
        *start = pos_create(x, y);
      ++x;
      for (;;) {
        if (x >= b->lines[y].size) {
          token = TOKEN_STRING_BEGIN;
          if (end)
            *end = pos_create(x, y);
          ++y;
          x = 0;
          break;
        }

        c = b->lines[y][x];
        if (c == str_char) {
          if (end)
            *end = pos_create(x, y);
          ++x;
          break;
        }
        ++x;
      }
      break;
    }

    /* single char token */
    else {
      token = c;
      if (start)
        *start = pos_create(x, y);
      if (end)
        *end = pos_create(x, y);
      ++x;
      break;
    }
  }

  *xp = x;
  *yp = y;
  return token;
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

static void term_bold(Array<char> *buf, int bold) {
  if (bold)
    array_push_str(buf, "\x1b[1m");
  else
    array_push_str(buf, "\x1b[22m");
}

static void term_inverse_video(Array<char> *buf, int inverse) {
  if (inverse)
    array_push_str(buf, "\x1b[7m");
  else
    array_push_str(buf, "\x1b[27m");
}

static void term_apply_style_slow(Array<char> *buf, Style style) {
  array_push_str(buf, term_fcolors[style.fcolor].str);
  array_push_str(buf, term_bcolors[style.bcolor].str);
  term_bold(buf, style.bold);
  term_inverse_video(buf, style.inverse);
}

static void term_apply_style(Array<char> *buf, Style style, Style old_style) {
  if (style.fcolor != old_style.fcolor)
    array_push_str(buf, term_fcolors[style.fcolor].str);
  if (style.bcolor != old_style.bcolor)
    array_push_str(buf, term_bcolors[style.bcolor].str);
  if (style.inverse != old_style.inverse)
    term_inverse_video(buf, style.inverse);
  if (style.bold != old_style.bold)
    term_bold(buf, style.bold);
}

#define file_write(file, str, len) fwrite(str, len, 1, file)
#define file_read(file, buf, n) fread(buf, n, 1, file)

static void term_clear_screen() {
  if (file_write(stdout, "\x1b[2J", 4) != 4) panic();
}

static void term_hide_cursor() {
  if (file_write(stdout, "\x1b[?25l", 6) != 6) panic();
}

static void term_clear_line() {
  if (file_write(stdout, "\x1b[2K", 4) != 4) panic();
}

static void term_show_cursor() {
  if (file_write(stdout, "\x1b[?25h", 6) != 6) panic();
}

static void term_cursor_move(int x, int y) {
  char buf[32];
  unsigned int n = sprintf_s(buf, "\x1b[%i;%iH", y+1, x+1);
  if (file_write(stdout, buf, n+1) != n+1) panic();
}

static void term_reset_video() {
  if (file_write(stdout, "\x1b[0m", 4) != 4) panic();
}

static int term_get_dimensions(int *w, int *h) {
#ifdef OS_LINUX
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
  term_clear_screen();
  term_show_cursor();
  term_cursor_move(0,0);
  term_reset_video();
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &G.orig_termios);
}

/* returns:
 * -1 on error
 * 0 on timedout
 * number of bytes read on success
 */
static int read_timeout(unsigned char *buf, int n, int ms) {
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
  else return read(STDIN_FILENO, buf, n);
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
  Buffer buffer = {0};
  FILE* f;

  buffer.filename = filename;
  if (fopen_s(&f, buffer.filename, "r"))
    return -1;

  /* get line count */
  int num_lines = 0;
  {
    char c;
    while ((c = (char)fgetc(f)) != EOF)
      num_lines += c == '\n';
  }
  if (ferror(f)) {
    fclose(f);
    return -1;
  }
  IF_DEBUG(status_message_set("File has %i rows", num_lines));

  if (num_lines > 0) {
    array_resize(buffer.lines, num_lines);
    for(int i = 0; i < num_lines; ++i)
      buffer.lines[i] = {};

    char c = 0;
    fseek(f, 0, SEEK_SET);
    for (int i = 0; i < num_lines; ++i) {
      while (1) {
        c = (char)fgetc(f);
        if (c == EOF) {
          if (ferror(f))
            goto err;
          goto last_line;
        }
        if (c == '\r') c = (char)fgetc(f);
        if (c == '\n') break;
        array_push(buffer[i], c);
      }
    }
    last_line:;
    assert(fgetc(f) == EOF);
  } else {
    array_pushz(buffer.lines);
  }

  tokenize(&buffer);

  buffer_guess_tab_type(&buffer);

  *buffer_out = buffer;
  fclose(f);
  return 0;

  err:
  for (int i = 0; i < buffer.lines.size; ++i)
    array_free(buffer[i]);
  array_free(buffer.lines);
  fclose(f);

  return -1;
}

static void render_clear(int x0, int x1, int y) {
  for (; x0 < x1; ++x0) {
    char_to_wide(' ', G.screen_buffer[y*G.term_width + x0].c);
    G.screen_buffer[y*G.term_width + x0].style = G.default_style;
  }
}

static void render_set_style_block(Style style, int x0, int x1, int y0, int y1) {
  int x,y;
  for (y = y0; y < y1; ++y)
    for (x = x0; x < x1; ++x)
      G.screen_buffer[y*G.term_width + x].style = style;
}

/* a, b relative inside bounds */
static void render_set_style_text(Style style, Pos a, Pos b, Rect bounds) {
  int x,y,x0,x1,y0,y1;
  a.x = clampi(a.x, 0, bounds.w-1);
  a.y = clampi(a.y, 0, bounds.h-1);
  b.x = clampi(b.x, 0, bounds.w-1);
  b.y = clampi(b.y, 0, bounds.h-1);
  x0 = bounds.x;
  x1 = bounds.x + bounds.w;
  y0 = bounds.y + a.y;
  y1 = bounds.y + b.y;

  if (y0 == y1) {
    for (x = x0 + a.x, y = y0; x <= x0 + b.x; ++x)
      G.screen_buffer[y*G.term_width + x].style = style;
    return;
  }

  for (x = x0 + a.x, y = y0; x < x1 && y < y1; ++x)
    G.screen_buffer[y*G.term_width + x].style = style;
  for (++y; y < y1; ++y)
    for (x = x0; x < x1; ++x)
      G.screen_buffer[y*G.term_width + x].style = style;
  for (x = x0; x <= x0 + b.x; ++x)
    G.screen_buffer[y*G.term_width + x].style = style;
}

static void render_cursor(Pane *p) {
  Pos pos = pane_to_screen_pos(p);
  if (G.mode == MODE_INSERT) {
    G.screen_buffer[pos.y*G.term_width + pos.x].style.bcolor = COLOR_RED;
    G.screen_buffer[pos.y*G.term_width + pos.x].style.fcolor = COLOR_BLACK;
  }
  else
    G.screen_buffer[pos.y*G.term_width + pos.x].style.inverse = 1;
}

static const char *keywords[] = {
  "static",
  "const",
  "char",
  "short",
  "int",
  "long",
  "float",
  "double",
  "unsigned",
  "void",
  "define",
  "ifdef",
  "endif",
  "elif",
  "include",
  "switch",
  "case",
  "if",
  "else",
  "for",
  "while",
  "struct",
  "union",
  "enum",
  "typedef",
  "return",
  "continue",
  "break",
  "goto"
};

/* x,y: screen bounds for pane */
static void render_pane(Pane *p, int draw_gutter, int highlight) {
  Buffer *b;
  int buf_y, buf_y0, buf_y1;
  int x,y;
  int x0, x1, y0, y1;
  int buf_x0;
  Style style;

  /* calc bounds */
  b = p->buffer;
  x0 = clampi(p->bounds.x, 0, G.term_width-1);
  x1 = clampi(p->bounds.x + p->bounds.w, 0, G.term_width);
  y0 = clampi(p->bounds.y, 0, G.term_height);
  y1 = clampi(y0 + p->bounds.h, 0, G.term_height);
  buf_y0 = pane_calc_top_visible_row(p);
  buf_y1 = at_most(buf_y0 + y1-y0, b->lines.size);
  buf_x0 = pane_calc_left_visible_column(p);

  /* calc gutter width */
  if (draw_gutter)
    p->gutter_width = at_least(calc_num_chars(buf_y1) + 1, 4);
  else
    p->gutter_width = 0;

  /* set some styles */
  render_set_style_block(p->style, x0, x1, y0, y1);
  style = p->style;
  style.fcolor = COLOR_YELLOW;
  render_set_style_block(style, x0, x0+p->gutter_width-1, y0, y1);

  /* draw each line */
  for (y = y0, buf_y = buf_y0; y < y1; ++buf_y, ++y) {
    Array<char> line;

    line = b->lines[buf_y];

    /* beyond buffer ? */
    if (buf_y >= b->num_lines()) {
      render_clear(x0, x1, y);
      continue;
    }

    /* gutter */
    render_str(x0, x0+p->gutter_width, y, "%*i", p->gutter_width-1, buf_y+1);

    if (line.size <= buf_x0)
      continue;

    /* text */
    render_strn(x0 + p->gutter_width, x1, y, line + buf_x0, line.size - buf_x0);
  }

  /* color highlighting */
  if (highlight) {
    int token;

    y = buf_y0;
    x = 0;
    for (;;) {
      Pos prev, next;
      int do_render;

      token = token_read(b, &x, &y, buf_y1, &prev, &next);

      check_token:

      do_render = 0;

      if (token == TOKEN_NULL)
        break;
      switch (token) {

        case TOKEN_NUMBER:
          do_render = 1;
          style = G.number_style;
          break;

        case TOKEN_BLOCK_COMMENT_END:
          do_render = 1;
          style = G.comment_style;
          prev = pos_create(buf_x0, buf_y0);
          break;

        case TOKEN_BLOCK_COMMENT_BEGIN:
          do_render = 1;
          style = G.comment_style;
          next = pos_create(b->lines[buf_y1-1].size, buf_y1-1);
          break;

        case TOKEN_BLOCK_COMMENT:
          do_render = 1;
          style = G.comment_style;
          break;

        case TOKEN_STRING:
          do_render = 1;
          style = G.string_style;
          break;

        case TOKEN_STRING_BEGIN:
          do_render = 1;
          style = G.string_style;
          break;

        case TOKEN_IDENTIFIER: {
          Pos prev_tmp, next_tmp;
          int i, left_of_line;

          /* check for keywords */
          left_of_line = b->lines[prev.y].size - prev.x;
          for (i = 0; i < (int)ARRAY_LEN(keywords); ++i) {
            int keyword_len = strlen(keywords[i]);
            if (keyword_len <= left_of_line && strncmp(buffer_getstr_p(b, prev), keywords[i], keyword_len) == 0) {
              style = G.keyword_style;
              goto done;
            }
          }

          /* otherwise check for functions */
          /* should not be indented */
          if (isspace(buffer_getchar(b, 0, prev.y)))
            break;
          token = token_read(b, &x, &y, buf_y1, &prev_tmp, &next_tmp);
          if (token != '(') {
            prev = prev_tmp;
            next = next_tmp;
            goto check_token;
          }
          style = G.identifier_style;

          done:
          do_render = 1;
        } break;

        case '#':
          do_render = 1;
          style = G.keyword_style;
          break;


        default:
          break;
      }
      if (do_render) {
        Rect bounds;

        prev = to_visual_pos(b, prev);
        next = to_visual_pos(b, next);

        bounds = p->bounds;
        bounds.x += p->gutter_width;
        bounds.w -= p->gutter_width;

        prev.x -= buf_x0;
        prev.y -= buf_y0;
        next.x -= buf_x0;
        next.y -= buf_y0;

        render_set_style_text(style, prev, next, bounds);
      }

      if (y > buf_y1)
        break;
    }
  }
}

static const char* cman_strerror(int e) {
  static char buf[128];
  strerror_s(buf, sizeof(buf), e);
  return buf;
}

static void save_buffer(Buffer *b) {
  FILE* f;
  int i;

  assert(b->filename);

  if (fopen_s(&f, b->filename, "wb")) {
    status_message_set("Could not open file %s for writing: %s", b->filename, cman_strerror(errno));
    return;
  }

  /* TODO: actually write to a temporary file, and when we have succeeded, rename it over the old file */
  for (i = 0; i < b->num_lines(); ++i) {
    unsigned int num_to_write = b->lines[i].size;
    if (file_write(f, b->lines[i], num_to_write) != num_to_write) {
      status_message_set("Failed to write to %s: %s", b->filename, cman_strerror(errno));
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
    char_to_wide(' ', G.screen_buffer[i].c);
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
  G.screen_buffer = (Pixel*)malloc(G.term_width * G.term_height * sizeof(*G.screen_buffer));
  screen_buffer_reset();
  G.bottom_pane.bounds = rect_create(0, G.term_height-1, G.term_width-1, 1);
  G.main_pane.bounds = rect_create(0, 0, G.term_width-1, G.term_height-1);
}

static void render_flush() {
  Style style;

  array_resize(G.tmp_render_buffer, 0);

  style = G.default_style;
  term_apply_style_slow(&G.tmp_render_buffer, style);

  term_cursor_move(3, 3);

  #if 0
  for (int y = 0; y < G.term_height; ++y)
  for (int x = 0; x < G.term_width; ++x) {
    Pixel p = G.screen_buffer[y*G.term_width + x];

    if (style_cmp(p.style, style)) {
      /* flush buffer */
      file_write(stdout, G.tmp_render_buffer.data, G.tmp_render_buffer.size);
      array_resize(G.tmp_render_buffer, 0);

      /* apply style */
      term_apply_style(&G.tmp_render_buffer, p.style, style);
      style = p.style;
    }
    array_push(G.tmp_render_buffer, p.c[0]);
    if (!IS_UTF8_TRAIL(p.c[1]))
      continue;
    array_push(G.tmp_render_buffer, p.c[1]);
    if (!IS_UTF8_TRAIL(p.c[2]))
      continue;
    array_push(G.tmp_render_buffer, p.c[2]);
    if (!IS_UTF8_TRAIL(p.c[3]))
      continue;
    array_push(G.tmp_render_buffer, p.c[3]);
  }
  /* flush buffer */
  file_write(stdout, G.tmp_render_buffer.data, G.tmp_render_buffer.size);
  #endif
  G.tmp_render_buffer.size = 0;

  puts("WWAAAAYA");

  fflush(stdout);

  screen_buffer_reset();
}

static int menu_option_save() {
  save_buffer(G.main_pane.buffer);
          /* TODO: write out absolute path, instead of relative */
  status_message_set("Wrote %i lines to %s", G.main_pane.buffer->num_lines(), G.main_pane.buffer->filename);
  return 0;
}

static int menu_option_quit() {
  return 1;
}

static int menu_option_show_tab_type() {
  if (G.main_pane.buffer->tab_type == 0)
    status_message_set("Tabs is \\t");
  else
    status_message_set("Tabs is %i spaces", G.main_pane.buffer->tab_type);
  return 0;
}

static struct {char *name; int (*fun)();} menu_options[] = {
  {"quit", menu_option_quit},
  {"save", menu_option_save},
  {"show_tab_type", menu_option_show_tab_type}
};

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
  G.insert_mode_begin_y = G.main_pane.buffer->pos.y;
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
  const DropdownMatch *a = (DropdownMatch*)aa, *b = (DropdownMatch*)bb;

  if (a->points != b->points)
    return (int)(b->points - a->points + 0.5f);
  return strlen(a->str) - strlen(b->str);
}

static void dropdown_render(Pane *active_pane) {
  if (!G.dropdown_visible)
    return;

  int num_best_matches = 0;
  DropdownMatch *best_matches = (DropdownMatch*)malloc(G.dropdown_size * sizeof(*best_matches));

  Buffer *active_buffer = active_pane->buffer;
  Array<char*> identifiers = active_buffer->identifiers;

  /* this shouldn't happen.. but just to be safe */
  if (G.dropdown_pos.y != active_pane->buffer->pos.y) {
    G.dropdown_visible = 0;
    return;
  }

  /* find matching identifiers */
  char *input_str = buffer_getstr_p(active_pane->buffer, G.dropdown_pos);
  int input_len = active_pane->buffer->pos.x - G.dropdown_pos.x;
  num_best_matches = 0;

  for (int i = 0; i < identifiers.size; ++i) {
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
      /* Don't penalize special characters */
      else if (isalnum(*test))
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
      DropdownMatch *match;

      /* find worst match and replace */
      match = best_matches;
      for (int j = 1; j < G.dropdown_size; ++j)
        if (best_matches[j].points < match->points)
          match = best_matches+j;
      match->str = identifier;
      match->points = points;
    }

  }

  qsort(best_matches, num_best_matches, sizeof(*best_matches), dropdown_match_cmp);

  int max_width = 0;
  for (int i = 0; i < num_best_matches; ++i)
    max_width = max(max_width, (int)strlen(best_matches[i].str));

  /* position pane */
  Pos p = pane_to_screen_pos_xy(active_pane, G.dropdown_pos.x, G.dropdown_pos.y);
  G.dropdown_pane.bounds.x = p.x;
  G.dropdown_pane.bounds.y = p.y+1;
  G.dropdown_pane.bounds.h = at_most(num_best_matches, (G.term_height-1)/2);
  G.dropdown_pane.bounds.w = at_least(max_width, 30);
  int overflow = G.dropdown_pane.bounds.x + G.dropdown_pane.bounds.w - (G.term_width - 1);
  if (overflow > 0)
    G.dropdown_pane.bounds.x -= overflow;
  G.dropdown_backwards = G.dropdown_pane.bounds.y + G.dropdown_pane.bounds.h >= G.term_height;
  if (G.dropdown_backwards)
    G.dropdown_pane.bounds.y -= G.dropdown_pane.bounds.h+1;

  buffer_empty(&G.dropdown_buffer);
  for (int i = 0; i < num_best_matches; ++i) {
    int which = G.dropdown_backwards ? num_best_matches-1-i : i;
    buffer_push_line(&G.dropdown_buffer, best_matches[which].str);
  }
  free(best_matches);

  if (G.dropdown_visible && !buffer_isempty(&G.dropdown_buffer))
    render_pane(&G.dropdown_pane, 0, 0);
}

static int dropdown_get_first_line() {
  if (G.dropdown_backwards)
    return G.dropdown_buffer.num_lines()-1;
  else
    return 0;
}

static void dropdown_autocomplete(Buffer *b) {
  char *str;
  int l,y;

  if (!G.dropdown_visible || buffer_isempty(&G.dropdown_buffer))
    return;

  y = dropdown_get_first_line();
  str = G.dropdown_buffer[y];
  l = G.dropdown_buffer[y].size;
  buffer_replace(b, G.dropdown_pos.x, b->pos.x, b->pos.y, str, l);
  G.dropdown_visible = 0;
}

static void dropdown_update_on_insert(Pane *active_pane, unsigned char input[4]) {

  if (!G.dropdown_visible) {
    if (!IS_IDENTIFIER_HEAD(*input))
      goto dropdown_hide;

    G.dropdown_pos = active_pane->buffer->pos;
  }
  else
    if (!IS_IDENTIFIER_TAIL(*input))
      goto dropdown_hide;

  G.dropdown_visible = 1;
  return;

  dropdown_hide:
  G.dropdown_visible = 0;
}

static void insert_default(Pane *p, int special_key, unsigned char input[4]) {
  Buffer *b;

  b = p->buffer;

  /* TODO: should not set `modifier` if we just enter and exit insert mode */
  if (special_key == KEY_ESCAPE) {
    buffer_pretty_range(G.main_pane.buffer, G.insert_mode_begin_y, G.main_pane.buffer->pos.y+1);
    mode_normal(1);
    return;
  }

  if (!special_key) {
    /*if (p != &G.bottom_pane)*/
    dropdown_update_on_insert(p, input);
    buffer_insert_char(b, input);
    return;
  }

  switch (special_key) {
    case KEY_RETURN:
      buffer_insert_newline(b);
      break;

    case KEY_TAB: {
      if (G.dropdown_visible)
        dropdown_autocomplete(b);
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

static int read_char(unsigned char *c) {
  int err;

  for (;;) {
    err = read_timeout(c, 1, 0);
    if (err == 1)
      return 0;
    if (err == -1 && errno != EAGAIN)
      return 1;
  }
}

static int process_input() {
  Key special_key = KEY_NONE;
  /* utf8 input */
  unsigned char input[4] = {0};

  /* get input */

  {
    int nread;

    if (read_char(input))
      return 1;

    if (IS_UTF8_HEAD(*input)) {
      unsigned char *c = input+1;
      unsigned char x;

      for (x = *input << 1; x&0x80; x <<= 1, ++c)
        if (read_char(c))
          return 1;
    }

    /* some special chars */
    switch (*input) {
      case '\t':
        special_key = KEY_TAB;
        break;
      case '\r':
        special_key = KEY_RETURN;
        break;
      case KEY_BACKSPACE:
        special_key = KEY_BACKSPACE;
        break;
      default:
        break;
    }

    if (*input != '\x1b')
      goto input_done;

    /* escape sequence? */
    /* read '[' */
    nread = read_timeout(input, 1, G.read_timeout_ms);
    if (nread == -1 && errno != EAGAIN)
      return 1;
    if (nread == 0 || *input != '[') {
      special_key = KEY_ESCAPE;
      goto input_done;
    }

    /* read actual character */
    nread = read_timeout(input, 1, G.read_timeout_ms);
    if (nread == -1 && errno != EAGAIN)
      return 1;
    if (nread == 0) {
      special_key = KEY_UNKNOWN;
      goto input_done;
    }

    IF_DEBUG(fprintf(stderr, "escape %c\n", *input);)

    if (*input >= '0' && *input <= '9') {
      switch (*input) {
        case '1': special_key = KEY_HOME; break;
        case '4': special_key = KEY_END; break;
      }

      nread = read_timeout(input, 1, G.read_timeout_ms);
      if (nread == -1 && errno != EAGAIN)
        return 1;
      if (nread == 0 && *input != '~') {
        special_key = KEY_UNKNOWN;
        goto input_done;
      }
    } else {
      switch (*input) {
        case 'A': special_key = KEY_ARROW_UP; break;
        case 'B': special_key = KEY_ARROW_DOWN; break;
        case 'C': special_key = KEY_ARROW_RIGHT; break;
        case 'D': special_key = KEY_ARROW_LEFT; break;
        case 'F': special_key = KEY_END; break;
        case 'H': special_key = KEY_HOME; break;
        default: special_key = KEY_UNKNOWN; break;
      }
    }
  }

  input_done:;

  /* process input */
  switch (G.mode) {
    case MODE_MENU:
      G.bottom_pane.buffer = &G.menu_buffer;
      if (special_key == KEY_RETURN) {
        Array<char> line;
        int i;

        if (buffer_isempty(&G.menu_buffer)) {
          mode_normal(1);
          break;
        }

        if (G.dropdown_visible)
          dropdown_autocomplete(&G.menu_buffer);

        line = G.menu_buffer[0];
        for (i = 0; i < (int)ARRAY_LEN(menu_options); ++i) {
          const char *name = menu_options[i].name;

          if (line.size != (int)strlen(name) || strncmp(line, name, line.size))
            continue;
          if (menu_options[i].fun())
            return 1;
          goto done;
        }
        status_message_set("Unknown option '%.*s'", G.menu_buffer[0].size, G.menu_buffer[0].data);
        done:


        mode_normal(0);
      }
      else if (special_key == KEY_ESCAPE)
        mode_normal(1);
      else
        insert_default(&G.bottom_pane, special_key, input);
      /* FIXME: if backspace so that menu_buffer is empty, exit menu mode */
      break;

    case MODE_NORMAL:
      switch (special_key ? special_key : input[0]) {
        case KEY_UNKNOWN:
          break;

        case KEY_ESCAPE:
          if (G.main_pane.buffer->modified)
            status_message_set("You have unsaved changes. If you really want to exit, use :quit");
          else
            return 1;
          break;

        case 'q':
          /*jumplist_prev(G.main_pane.buffer);*/
          break;

        case 'w':
          /*jumplist_next(G.main_pane.buffer);*/
          break;

        case 'k':
        case KEY_ARROW_UP:
          buffer_move_y(G.main_pane.buffer, -1);
          break;

        case 'j':
        case KEY_ARROW_DOWN:
          buffer_move_y(G.main_pane.buffer, 1);
          break;

        case 'h':
        case KEY_ARROW_LEFT:
          buffer_move_x(G.main_pane.buffer, -1);
          break;

        case 'l':
        case KEY_ARROW_RIGHT:
          buffer_move_x(G.main_pane.buffer, 1);
          break;

        case 'L':
        case KEY_END:
          buffer_goto_endline(G.main_pane.buffer);
          break;

        case 'H':
        case KEY_HOME:
          buffer_goto_beginline(G.main_pane.buffer);
          break;

        case 'n': {
          int err;
          Pos prev;

          prev = G.main_pane.buffer->pos;

          err = buffer_find(G.main_pane.buffer, G.search_buffer[0], G.search_buffer[0].size, 0);
          if (err) {
            status_message_set("'%.*s' not found", G.search_buffer[0].size, G.search_buffer[0].data);
            break;
          }
          /*jumplist_push(prev);*/
        } break;

        case 'N': {
          int err;
          Pos prev;

          prev = G.main_pane.buffer->pos;

          err = buffer_find_r(G.main_pane.buffer, G.search_buffer[0], G.search_buffer[0].size, 0);
          if (err) {
            status_message_set("'%.*s' not found", G.search_buffer[0].size, G.search_buffer[0].data);
            break;
          }
          /*jumplist_push(prev);*/
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

        case 'J':
          buffer_move_y(G.main_pane.buffer, G.main_pane.bounds.h/2);
          break;

        case 'K':
          buffer_move_y(G.main_pane.buffer, -G.main_pane.bounds.h/2);
          break;

        case '}': {
          Buffer *b;
          int x,y,yend,t;
          Pos p;

          b = G.main_pane.buffer;
          x = b->pos.x;
          y = b->pos.y;
          yend = b->num_lines();

          for (;;) {
            t = token_read(b, &x, &y, yend, &p, 0);
            /* if we're already on one, ignore */
            if (t == '}' && p.x == b->pos.x && p.y == b->pos.y)
              continue;
            if (t == '{' || t == '}' || t == TOKEN_NULL)
              break;
          }

          if (t == '}')
            buffer_move_to(b, p.x, p.y);
          else if (t == '{') {
            int depth = 1;
            for (;;) {
              t = token_read(b, &x, &y, yend, &p, 0);
              if (t == TOKEN_NULL)
                break;
              if (t == '}')
                --depth;
              if (t == '{')
                ++depth;
              if (depth == 0) {
                buffer_move_to(b, p.x, p.y);
                break;
              }
            }
          }
        } break;

      }
      break;

    case MODE_GOTO:
      if (isdigit(input[0])) {
        G.goto_line_number *= 10;
        G.goto_line_number += input[0] - '0';
        buffer_move_to_y(G.main_pane.buffer, G.goto_line_number-1);
        status_message_set("goto %u", G.goto_line_number);
        break;
      }

      switch (special_key ? special_key : input[0]) {
        case 't':
          buffer_move_to(G.main_pane.buffer, 0, 0);
          break;
        case 'b':
          buffer_move_to(G.main_pane.buffer, 0, G.main_pane.buffer->num_lines()-1);
          break;
      }
      mode_normal(1);
      break;

    case MODE_SEARCH: {
      Array<char> search;

      G.bottom_pane.buffer = &G.search_buffer;
      if (special_key == KEY_ESCAPE)
        dropdown_autocomplete(&G.search_buffer);
      else
        insert_default(&G.bottom_pane, special_key, input);

      search = G.search_buffer.lines[0];
      G.search_failed = buffer_find(G.main_pane.buffer,
                               search,
                               search.size,
                               1);

      if (special_key == KEY_RETURN || special_key == KEY_ESCAPE) {
        if (G.dropdown_visible) {
          dropdown_get_first_line();
          G.search_failed = buffer_find(G.main_pane.buffer,
                                   search,
                                   search.size,
                                   1);
        }
        if (G.search_failed) {
          G.main_pane.buffer->pos = G.search_begin_pos;
          status_message_set("'%.*s' not found", G.search_buffer[0].size, G.search_buffer.lines[0].data);
          mode_normal(0);
        } else
          mode_normal(1);
      }

    } break;

    case MODE_DELETE:
      switch (special_key ? special_key : input[0]) {
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
      insert_default(&G.main_pane, special_key, input);
      break;

    case MODE_COUNT:
      break;
  }

  /* just in case */
  G.main_pane.buffer->pos.y = clampi(G.main_pane.buffer->pos.y, 0, G.main_pane.buffer->num_lines()-1);
  G.main_pane.buffer->pos.x = clampi(G.main_pane.buffer->pos.x, 0, G.main_pane.buffer->lines[G.main_pane.buffer->pos.y].size);
  return 0;
}

static void state_init() {
  int err, i;

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
  G.screen_buffer = (Pixel*)malloc(G.term_width * G.term_height * sizeof(*G.screen_buffer));
  screen_buffer_reset();

  /* init predefined buffers */
  buffer_empty(&G.menu_buffer);
  buffer_empty(&G.search_buffer);
  buffer_empty(&G.message_buffer);
  buffer_empty(&G.dropdown_buffer);

  G.main_pane.style.bcolor = COLOR_BLACK;
  G.main_pane.style.fcolor = COLOR_WHITE;

  G.menu_buffer.identifiers = {};
  array_pushn(G.menu_buffer.identifiers, (int)ARRAY_LEN(menu_options));
  for (i = 0; i < (int)ARRAY_LEN(menu_options); ++i)
    G.menu_buffer.identifiers[i] = menu_options[i].name;

  G.bottom_pane.style.bcolor = COLOR_MAGENTA;
  G.bottom_pane.style.fcolor = COLOR_BLACK;
  G.bottom_pane.bounds = rect_create(0, G.term_height-1, G.term_width-1, 1);
  G.bottom_pane.buffer = &G.message_buffer;

  G.dropdown_pane.buffer = &G.dropdown_buffer;
  G.dropdown_pane.style.bcolor = COLOR_MAGENTA;
  G.dropdown_pane.style.fcolor = COLOR_BLACK;

  G.main_pane.bounds = rect_create(0, 0, G.term_width-1, G.term_height-1);

  G.comment_style.fcolor = COLOR_BLUE;
  G.comment_style.bcolor = COLOR_BLACK;
  G.identifier_style.fcolor = COLOR_GREEN;
  G.identifier_style.bcolor = COLOR_BLACK;
  G.keyword_style.fcolor = COLOR_MAGENTA;
  G.keyword_style.bcolor = COLOR_BLACK;
  G.string_style.fcolor = COLOR_RED;
  G.string_style.bcolor = COLOR_BLACK;
  G.number_style.fcolor = COLOR_RED;
  G.number_style.bcolor = COLOR_BLACK;
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

  if (argc < 2) {
    fprintf(stderr, "Usage: cedit <file>\n");
    return 1;
  }

  IF_DEBUG(test());

  /* set up terminal */
  term_enable_raw_mode();
  atexit(term_reset_to_default_settings);

  /* initialize state */
  state_init();

  /* open a buffer */
  {
    const char *filename = argv[1];
    G.main_pane.buffer = (Buffer*)malloc(sizeof(*G.main_pane.buffer));
    err = file_open(filename, G.main_pane.buffer);
    if (err) {
      // status_message_set("Could not open file %s: %s\n", filename, cman_strerror(errno));
      fprintf(stderr, "Could not open file %s: %s\n", filename, cman_strerror(errno));
      exit(1);
    }
    if (!err)
      status_message_set("loaded %s, %i lines", filename, G.main_pane.buffer->num_lines());
  }

  while (1) {

    /* draw document */
    render_pane(&G.main_pane, 1, 1);

    /* draw menu/status bar */
    render_pane(&G.bottom_pane, 0, 0);

    /* draw dropdown ? */
    G.search_buffer.identifiers = G.main_pane.buffer->identifiers;
    if (G.mode == MODE_SEARCH || G.mode == MODE_MENU)
      dropdown_render(&G.bottom_pane);
    else
      dropdown_render(&G.main_pane);

    /* Draw cursors */
    render_cursor(&G.main_pane);
    if (G.mode == MODE_SEARCH || G.mode == MODE_MENU)
      render_cursor(&G.bottom_pane);

    /* render to screen */
    render_flush();

    /* get and process input */
    err = process_input();
    if (err)
      break;

    // check_terminal_resize();
  }

  return 0;
}
