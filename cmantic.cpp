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

// @includes
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

#include "graphics.h"

// @debug
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

// @utils
  #define STATIC_ASSERT(expr, name) typedef char static_assert_##name[expr?1:-1]
  #define ARRAY_LEN(a) (sizeof(a)/sizeof(*a))
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

  static void panic(const char *fmt = "", ...) {
    // term_reset_to_default_settings();
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fflush(stderr);

    abort();
  }

  #define at_least(a,b) max((a),(b))
  #define at_most(a, b) min((a),(b))

  /* a,b inclusive */
  static int clampi(int x, int a, int b) {
    return x < a ? a : (b < x ? b : x);
  }

  template<class T>
  T max(T a, T b) {return a < b ? b : a;}
  template<class T>
  T min(T a, T b) {return b < a ? b : a;}

  template<class T>
  void swap(T &a, T &b) {
    T tmp;
    tmp = a;
    a = b;
    b = tmp;
  }

// @utf8
  #define IS_UTF8_TRAIL(c) (((c)&0xC0) == 0x80)
  #define IS_UTF8_HEAD(c) ((c)&0x80)

enum Mode {
  MODE_NORMAL,
  MODE_INSERT,
  MODE_MENU,
  MODE_DELETE,
  MODE_GOTO,
  MODE_SEARCH,
  MODE_COUNT
};

struct Style{
  unsigned int fcolor: 4;
  unsigned int bcolor: 4;
  unsigned int bold: 1;
  unsigned int italic: 1;
  unsigned int inverse: 1;
};

struct Rect {
  float x,y,w,h;
};

struct Utf8char {
  char code[4];
};

enum Token{
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

struct TokenInfo {
  int token;
  int x;
};

struct Pos {
  #define GHOST_EOL -1
  #define GHOST_BOL -2
  int x, y;
  int ghost_x; /* if going from a longer line to a shorter line, remember where we were before clamping to row length. a value of -1 means always go to end of line */
};
static Pos pos_create(int x, int y) {
  return {x,y,x};
}

struct Buffer {
  Array<Array<char>> lines;
  const char *filename;
  int tab_type; /* 0 for tabs, 1+ for spaces */

  /* parser stuff */
  Array<Array<TokenInfo>> tokens;
  Array<const char*> identifiers;

  Pos pos;
  int modified;

  // methods
  Array<char>& operator[](int i) {return lines[i];}
  const Array<char>& operator[](int i) const {return lines[i];}
  int num_lines() const {return lines.size;}
};

struct Pane {
  int x,y,pw,ph; // in pixels
  int w,h; // in lines (including line margin)
  int gutter_width;
  Style style; // just for background and foreground
  Buffer *buffer;
};

static struct State {
  /* @renderer some rendering state */
  SDL_Window *window;
  int font_height;
  int font_width;
  int line_margin;

  int win_height, win_width;
  Style default_style;
  Style comment_style,
        identifier_style,
        string_style,
        number_style,
        keyword_style;

  /* some editor state */
  Pane main_pane,
       menu_pane,
       dropdown_pane,
       bottom_pane;
  Buffer menu_buffer,
         search_buffer,
         status_message_buffer,
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
  int tab_width; /* how wide are tabs when rendered */
  int default_tab_type; /* 0 for tabs, 1+ for spaces */
  Color gutter_color;
  #define DROPDOWN_SIZE 7
} G;

enum {
  COLOR_BLACK,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_YELLOW,
  COLOR_BLUE,
  COLOR_MAGENTA,
  COLOR_CYAN,
  COLOR_WHITE,
  COLOR_COUNT
};

enum SpecialKey {
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
};

/* @TOKENIZER */

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

  if (a.y > b.y || (a.y == b.y && a.x > b.x))
    swap(a, b);

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

  buffer_truncate_to_n_lines(&G.status_message_buffer, 1);
  array_resize(G.status_message_buffer[0], (int)G.bottom_pane.w);

  va_start(args, fmt);
  n = vsnprintf(G.status_message_buffer[0], G.status_message_buffer[0].size, fmt, args);
  va_end(args);

  n = at_most(n, G.status_message_buffer[0].size);
  array_resize(G.status_message_buffer[0], n);
}

/****** @TOKENIZER ******/

static void tokenizer_push_token(Array<Array<TokenInfo>> *tokens, int x, int y, int token) {
  array_reserve(*tokens, y+1);
  array_push((*tokens)[y], {token, x});
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
      array_push(b->identifiers, (const char*)str);

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

#define IS_NUMBER_HEAD(c) (isdigit(c))
#define IS_NUMBER_TAIL(c) (isdigit(c) || ((c) >= 'A' && (c) <= 'F') || ((c) >= 'a' && (c) <= 'f') || (c) == 'x')
#define IS_IDENTIFIER_HEAD(c) (isalpha(c) || (c) == '_' || IS_UTF8_HEAD(c))
#define IS_IDENTIFIER_TAIL(c) (isalnum(c) || (c) == '_' || IS_UTF8_TRAIL(c))
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

static int calc_num_chars(int i) {
  int result = 0;
  while (i > 0) {
    ++result;
    i /= 10;
  }
  return result;
}

static int file_open(FILE **f, const char *filename, const char *mode) {
  #ifdef OS_WINDOWS
  return fopen_s(f, filename, mode);
  #else
  *f = fopen(filename, mode);
  return *f == NULL;
  #endif
}

/* grabs ownership of filename */
static int buffer_from_file(const char *filename, Buffer *buffer_out) {
  Buffer buffer = {};
  FILE* f;

  buffer.filename = filename;
  if (file_open(&f, buffer.filename, "r"))
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

/* a, b relative inside bounds */
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

static const char* cman_strerror(int e) {
  #ifdef OS_WINDOWS
    static char buf[128];
    strerror_s(buf, sizeof(buf), e);
    return buf;
  #else
    return strerror(e);
  #endif
}

#define file_write(file, str, len) fwrite(str, len, 1, file)
#define file_read(file, buf, n) fread(buf, n, 1, file)


static void save_buffer(Buffer *b) {
  FILE* f;
  int i;

  assert(b->filename);

  if (file_open(&f, b->filename, "wb")) {
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

static struct {const char *name; int (*fun)();} menu_options[] = {
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
  G.bottom_pane.buffer = &G.status_message_buffer;
  status_message_set("goto");
}

static void mode_delete() {
  G.mode = MODE_DELETE;
  G.bottom_pane.buffer = &G.status_message_buffer;
  status_message_set("delete");
}

static void mode_normal(int set_message) {
  G.mode = MODE_NORMAL;
  G.bottom_pane.buffer = &G.status_message_buffer;

  G.dropdown_visible = 0;

  if (set_message)
    status_message_set("normal");
}

static void mode_insert() {
  G.mode = MODE_INSERT;
  G.bottom_pane.buffer = &G.status_message_buffer;
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
  const char *str;
  float points;
} DropdownMatch;

static int dropdown_match_cmp(const void *aa, const void *bb) {
  const DropdownMatch *a = (DropdownMatch*)aa, *b = (DropdownMatch*)bb;

  if (a->points != b->points)
    return (int)(b->points - a->points + 0.5f);
  return strlen(a->str) - strlen(b->str);
}

static void fill_dropdown_buffer(Pane *active_pane) {
  static DropdownMatch best_matches[DROPDOWN_SIZE];

  if (!G.dropdown_visible)
    return;

  int num_best_matches = 0;

  Buffer *active_buffer = active_pane->buffer;
  Array<const char*> identifiers = active_buffer->identifiers;

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
    const char *in, *in_end, *test, *test_end;
    int test_len;
    const char *identifier;

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

    if (num_best_matches < DROPDOWN_SIZE) {
      best_matches[num_best_matches].str = identifier;
      best_matches[num_best_matches].points = points;
      ++num_best_matches;
    } else {
      DropdownMatch *match;

      /* find worst match and replace */
      match = best_matches;
      for (int j = 1; j < DROPDOWN_SIZE; ++j)
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

  buffer_empty(&G.dropdown_buffer);
  for (int i = 0; i < num_best_matches; ++i) {
    int which = G.dropdown_backwards ? num_best_matches-1-i : i;
    buffer_push_line(&G.dropdown_buffer, best_matches[which].str);
  }

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

static void insert_default(Pane *p, SpecialKey special_key, unsigned char input[4]) {
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

static void state_init() {
  SDL_GetWindowSize(G.window, &G.win_width, &G.win_height);

  G.font_width = graphics_get_font_advance();
  G.tab_width = 4;
  G.default_tab_type = 4;
  G.line_margin = 5;

  G.default_style.fcolor = COLOR_WHITE;
  G.default_style.bcolor = COLOR_BLACK;
  G.default_style.bold = G.default_style.italic = G.default_style.inverse = 0;

  /* init predefined buffers */
  buffer_empty(&G.menu_buffer);
  buffer_empty(&G.search_buffer);
  buffer_empty(&G.status_message_buffer);
  buffer_empty(&G.dropdown_buffer);

  G.main_pane.style.bcolor = COLOR_BLACK;
  G.main_pane.style.fcolor = COLOR_WHITE;
  G.main_pane.x = 0;
  G.main_pane.y = 0;
  G.main_pane.pw = G.win_width;
  G.main_pane.ph = G.win_height;
  G.main_pane.w = G.main_pane.pw / G.font_width;
  G.main_pane.h = G.main_pane.ph / G.font_height / G.line_margin;
  G.main_pane.gutter_width = 1;
  G.main_pane.style = {};

  G.menu_buffer.identifiers = {};
  array_pushn(G.menu_buffer.identifiers, (int)ARRAY_LEN(menu_options));
  for (int i = 0; i < (int)ARRAY_LEN(menu_options); ++i)
    G.menu_buffer.identifiers[i] = menu_options[i].name;

  G.dropdown_pane.buffer = &G.dropdown_buffer;
  G.dropdown_pane.style.bcolor = COLOR_MAGENTA;
  G.dropdown_pane.style.fcolor = COLOR_BLACK;

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

  G.gutter_color = {1.0, 0.5, 0.7};
}

static int pane_calc_top_visible_row(Pane *pane) {
  return at_least(0, pane->buffer->pos.y - pane->h/2);
}

static int pane_calc_left_visible_column(Pane *pane) {
  return at_least(0, to_visual_offset(pane->buffer->lines[pane->buffer->pos.y], pane->buffer->pos.x) - (pane->w - pane->gutter_width - 3));
}

static void render_pane(Pane *p, bool draw_gutter) {
  Buffer *b;
  int buf_y, buf_y0, buf_y1;
  int buf_x0;
  Style style;

  /* calc bounds */
  b = p->buffer;
  buf_y0 = pane_calc_top_visible_row(p);
  buf_y1 = at_most(buf_y0 + p->h, b->lines.size);
  buf_x0 = pane_calc_left_visible_column(p);

  /* calc gutter width */
  if (draw_gutter)
    p->gutter_width = at_least(calc_num_chars(buf_y1) + 1, 2);
  else
    p->gutter_width = 0;

  /* set some styles */
  // render_set_style_block(p->style, x0, x1, y0, y1);
  // style = p->style;
  // style.fcolor = COLOR_YELLOW;
  // render_set_style_block(style, x0, x0+p->gutter_width-1, y0, y1);

  /* draw each line */
  for (int y = p->y + G.font_height, buf_y = buf_y0; buf_y < buf_y1; ++buf_y, y+=G.font_height + G.line_margin) {
    Array<char> line = b->lines[buf_y];

    /* gutter */
    push_textf(p->x, y, false, G.gutter_color, "%i", buf_y+1);

    if (line.size <= buf_x0)
      continue;

    /* text */
    push_textn(line + buf_x0, line.size, p->x + p->gutter_width*G.font_width, y, false, {0.1f, 0.1f, 0.1f});
  }
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

#ifdef OS_WINDOWS
int wmain(int argc, const char* argv)
#else
int main(int argc, const char **argv)
#endif
{

  if (argc < 2) {
    fprintf(stderr, "Usage: cedit <file>\n");
    return 1;
  }

  if (graphics_init(&G.window))
    return 1;

  G.font_height = 20;

  if (graphics_text_init("/usr/share/fonts/truetype/ubuntu-font-family/UbuntuMono-R.ttf", G.font_height))
    return 1;

  state_init();

  /* open a buffer */
  {
    const char *filename = argv[1];
    G.main_pane.buffer = (Buffer*)malloc(sizeof(*G.main_pane.buffer));
    int err = buffer_from_file(filename, G.main_pane.buffer);
    if (err) {
      // status_message_set("Could not open file %s: %s\n", filename, cman_strerror(errno));
      fprintf(stderr, "Could not open file %s: %s\n", filename, cman_strerror(errno));
      exit(1);
    }
    if (!err)
      status_message_set("loaded %s, %i lines", filename, G.main_pane.buffer->num_lines());
  }

  // char input[9] = {};
  // int in_pos = 0;
  for (;;) {

    for (SDL_Event event; SDL_PollEvent(&event);) {

      switch (event.type) {
      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_CLOSE)
          return 1;
        break;

      case SDL_KEYDOWN:
        if (event.key.keysym.sym == SDLK_ESCAPE)
          exit(0);
        break;

      case SDL_TEXTINPUT:
        // input[in_pos++] = *event.text.text;
        // in_pos = in_pos & 7;
        // strcpy(input, event.text.text);
        break;
      }
    }

    glClearColor(0.1f, 0.5f, 0.7f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    #if 0
    static float a;
    a += 0.05f;
    push_text(input, 0, 0, false);
    #endif

    render_pane(&G.main_pane, true);

    render_text();

    // draw the 
    // render_textatlas(0, 0, 200, 200);

    SDL_GL_SwapWindow(G.window);
  }

  return 0;
}
