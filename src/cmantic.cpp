/*
 * CURRENT: refactory token_read to return a TokenResult struct
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
  #define IS_UTF8(utf8char) IS_UTF8_HEAD((utf8char).code[0])

enum Mode {
  MODE_NORMAL,
  MODE_INSERT,
  MODE_MENU,
  MODE_DELETE,
  MODE_GOTO,
  MODE_SEARCH,
  MODE_COUNT
};

struct Style {
  Color text_color;
  Color background_color;
};

static bool operator==(Style a, Style b) {
  return a.text_color == b.text_color && a.background_color == b.background_color;
}

struct Rect {
  float x,y,w,h;
};

struct Utf8char {
  char code[4];

  void operator=(char c) {
    code[1] = code[2] = code[3] = 0;
    code[0] = c;
  }

  void write_to_string(char *&str) {
    for (int i = 0; i < 4 && code[i]; ++i)
      *str++ = code[i];
  }

  static Utf8char from_string(const char *&str, const char *end) {
    Utf8char r = {};
    char *res = r.code;

    if (str == end)
      return r;

    *res++ = *str++;

    if (str == end || !IS_UTF8_TRAIL(*str))
      return r;
    *res++ = *str++;
    if (str == end || !IS_UTF8_TRAIL(*str))
      return r;
    *res++ = *str++;
    if (str == end || !IS_UTF8_TRAIL(*str))
      return r;
    *res++ = *str++;
    return r;
  }

  static void to_string(Utf8char *in, int n, Array<char> &out) {
    array_resize(out, n*4);
    char * const begin = out.data;
    char *end = begin;
    for (int i = 0; i < n; ++i)
      in[i].write_to_string(end);
    array_resize(out, end - begin);
  }
};

bool operator==(Utf8char uc, char c) {
  return !uc.code[1] && uc.code[0] == c;
}

bool operator==(char c, Utf8char uc) {
  return uc == c;
}

enum Token{
  TOKEN_NULL = 0,
  TOKEN_IDENTIFIER = -2,
  TOKEN_NUMBER = -3,
  TOKEN_STRING = -4,
  TOKEN_EOL = -4,
  TOKEN_STRING_BEGIN = -5,
  TOKEN_BLOCK_COMMENT_BEGIN = -6,
  TOKEN_BLOCK_COMMENT_END = -7,
  TOKEN_LINE_COMMENT_BEGIN = -8,
};

struct TokenInfo {
  int token;
  int x;
};

struct Pos {
  int x,y;
};

struct Buffer {
  Array<Array<char>> lines;
  const char *filename;
  int tab_type; /* 0 for tabs, 1+ for spaces */

  /* parser stuff */
  Array<Array<TokenInfo>> tokens;
  Array<const char*> identifiers;

#define GHOST_EOL -1
#define GHOST_BOL -2
  Pos pos;
  int ghost_x; /* if going from a longer line to a shorter line, remember where we were before clamping to row length. a value of -1 means always go to end of line */
  int modified;

  // methods
  Array<char>& operator[](int i) {return lines[i];}
  const Array<char>& operator[](int i) const {return lines[i];}
  int num_lines() const {return lines.size;}
};

struct Pane {
  int x,y,pw,ph; // in pixels
  Buffer *buffer;
  int numchars_x() const;
  int numchars_y() const;
  int slot2pixelx(int x) const;
  int slot2pixely(int y) const;
};

struct State {
  /* @renderer some rendering state */
  SDL_Window *window;
  int font_width;
  int line_height;
  Array<char> tmp_render_buffer;
  int win_height, win_width;

  /* some settings */
  Style default_style;
  Style default_gutter_style;
  Color marker_background_color;
  Color hairline_color;
  Color highlight_background_color;
  Color number_color;
  Color comment_color;
  Color string_color;
  Color keyword_color;
  Color identifier_color;

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

  Pane *selected_pane;

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
  #define DROPDOWN_SIZE 7
};

State G;

struct IRect {
  int x,y,w,h;
};

struct Canvas {
  Utf8char *chars;
  Style *styles;
  int w, h;

  void init(int w, int h) {
    this->w = w;
    this->h = h;
    this->chars = new Utf8char[w*h]();
    this->styles = new Style[w*h]();
  }

  void resize(int w, int h) {
    if (this->chars)
      delete [] this->chars;
    if (this->styles)
      delete [] this->styles;
    this->init(w, h);
  }

  void free() {
    delete [] this->chars;
    delete [] this->styles;
  }

  void fill(Utf8char c) {
    for (int i = 0; i < w*h; ++i)
      this->chars[i] = c;
  }

  void fill(Style s) {
    for (int i = 0; i < w*h; ++i)
      this->styles[i] = s;
  }

  // fills a to b with the bounds 
  void fill_textcolor(Pos a, Pos b, IRect bounds, Color c) {
    if (bounds.w == -1)
      bounds.w = this->w - bounds.x;
    if (bounds.h == -1)
      bounds.h = this->h - bounds.y;
    // single line outside of bounds, early exit
    if (a.y == b.y && (b.x < 0 || a.x > this->w))
      return;

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
        this->styles[y*this->w + x].text_color = c;
      return;
    }

    for (x = x0 + a.x, y = y0; x < x1 && y < y1; ++x)
      this->styles[y*this->w + x].text_color = c;
    for (++y; y < y1; ++y)
    for (x = x0; x < x1; ++x)
      this->styles[y*this->w + x].text_color = c;
    for (x = x0; x <= x0 + b.x; ++x)
      this->styles[y*this->w + x].text_color = c;
  }

  // w,h: use -1 to say it goes to the end
  void fill_textcolor(int x, int y, int w, int h, Color c) {
    if (w == -1)
      w = this->w - x;
    if (h == -1)
      h = this->h - y;
    w = at_most(w, this->w - x);
    h = at_most(h, this->h - y);
    if (w < 0 || h < 0)
      return;

    for (int yy = y; yy < y+h; ++yy)
    for (int xx = x; xx < x+w; ++xx)
      styles[yy*this->w + xx].text_color = c;
  }

  // w,h: use -1 to say it goes to the end
  void fill_background(int x, int y, int w, int h, Color c) {
    if (w == -1)
      w = this->w - x;
    if (h == -1)
      h = this->h - y;
    w = at_most(w, this->w - x);
    h = at_most(h, this->h - y);
    if (w < 0 || h < 0)
      return;

    for (int yy = y; yy < y+h; ++yy)
    for (int xx = x; xx < x+w; ++xx)
      styles[yy*this->w + xx].background_color = c;
  }

  void render_strn(int x, int y, Style style, int xclip0, int xclip1, const char *str, int n) {
    if (!str)
      return;

    if (xclip1 == -1)
      xclip1 = this->w;

    Utf8char *row = &this->chars[y*w];
    Style *style_row = &this->styles[y*w];
    for (const char *end = str+n; str != end && x < xclip1;) {
      if (*str == '\t') {
        for (int i = 0; i < G.tab_width; ++i, ++x)
          if (x >= xclip0 && x < xclip1) {
            row[x] = ' ';
            style_row[x] = style;
          }
        ++str;
      }
      else {
        Utf8char c = Utf8char::from_string(str, end);
        if (x >= xclip0 && x < xclip1) {
          row[x] = c;
          style_row[x] = style;
        }
        ++x;
      }
    }
  }

  void render_str_v(int x, int y, Style style, int x0, int x1, const char *fmt, va_list args) {
    int max_chars = w-x+1;
    if (max_chars <= 0)
      return;
    array_resize(G.tmp_render_buffer, max_chars);
    int n = vsnprintf(G.tmp_render_buffer, max_chars, fmt, args);
    render_strn(x, y, style, x0, x1, G.tmp_render_buffer, min(max_chars, n));
  }

  void render_strf(int x, int y, Style style, int x0, int x1, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    this->render_str_v(x, y, style, x0, x1, fmt, args);
    va_end(args);
  }

  void render(const Pane *p) {
    #if 0
    printf("PRINTING SCREEN\n\n");
    for (int i = 0; i < h; ++i) {
      for (int j = 0; j < w; ++j)
        putchar('a' + styles[i*w + j].background_color.r * 10);
      putchar('\n');
    }
    #endif

    // render background
    for (int y = 0; y < h; ++y) {
      for (int x0 = 0, x1 = 1; x1 <= w; ++x1) {
        if (styles[y*w + x1] == styles[y*w + x0] && x1 < w)
          continue;
        float xf0 = (float)p->slot2pixelx(x0);
        float xf1 = (float)p->slot2pixelx(x1);
        float yf0 = (float)p->slot2pixely(y);
        float yf1 = (float)p->slot2pixely(y+1);
        const Color c = styles[y*w + x0].background_color;
        push_square_quad(xf0, xf1, yf0, yf1, c);
        x0 = x1;
      }
    }

    // render text
    const float text_offset_y = -3; // TODO: get this from truetype?
    array_resize(G.tmp_render_buffer, w*sizeof(Utf8char));
    for (int row = 0; row < h; ++row) {
      Utf8char::to_string(&this->chars[row*w], w, G.tmp_render_buffer);
      int y = p->slot2pixely(row+1) + text_offset_y;
      for (int x0 = 0, x1 = 1; x1 <= w; ++x1) {
        if (styles[row*w + x1].text_color == styles[row*w + x0].text_color && x1 < w)
          continue;
        int x = p->x + x0*G.font_width;
        push_textn(G.tmp_render_buffer.data + x0, x1 - x0, x, y, false, styles[row*w + x0].text_color);
        x0 = x1;
      }
    }
  }
};

int Pane::slot2pixelx(int x) const {
  return this->x + x*G.font_width;
}

int Pane::slot2pixely(int y) const {
  return this->y + y*G.line_height;
}

int Pane::numchars_x() const {
  return this->pw / G.font_width + 1;
}
int Pane::numchars_y() const {
  return this->ph / G.line_height + 1;
}

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

static int token_read(const Buffer *b, Pos *p, int y_end, Pos *start, Pos *end);

static void buffer_move_to_x(Buffer *b, int x);

static void buffer_goto_endline(Buffer *b) {
  buffer_move_to_x(b, b->lines[b->pos.y].size);
  b->ghost_x = GHOST_EOL;
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
  b->ghost_x = GHOST_BOL;
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

bool buffer_streq(const Buffer *buf, Pos a, Pos b, const char *str) {
  assert(b.y - a.y <= 1);
  int identifier_len = b.y > a.y ? buf->lines[a.y].size - a.x : b.x - a.x + 1;
  return identifier_len == (int)strlen(str)
         && !memcmp(buf->lines[a.y]+a.x, str, identifier_len);
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

static int buffer_advance(Buffer *b, int *x, int *y) {
  *x += 1;
  if (*x > b->lines[*y].size) {
    *x = 0;
    *y += 1;
    if (*y >= b->lines.size) {
      *y = b->lines.size - 1;
      *x = b->lines[*y].size;
      return 1;
    }
  }
  return 0;
}

static int buffer_advance(Buffer *b) {
  int err = buffer_advance(b, &b->pos.x, &b->pos.y);
  if (err)
    return err;
  b->ghost_x = b->pos.x;
  return 0;
}

static int buffer_advance_r(Buffer *b, int *x, int *y) {
  *x -= 1;
  if (*x < 0) {
    *y -= 1;
    if (*y < 0) {
      *y = 0;
      *x = 0;
      return 1;
    }
    *x = b->lines[*y].size;
  }
  return 0;
}

static int buffer_advance_r(Buffer *b) {
  int err = buffer_advance_r(b, &b->pos.x, &b->pos.y);
  if (err)
    return err;
  b->ghost_x = b->pos.x;
  return 0;
}



static Array<char>* buffer_getline(Buffer *b, int y) {
  return &b->lines[y];
}

static char *buffer_getstr(Buffer *b, int x, int y) {
  return b->lines[y]+x;
}

static char *buffer_getstr(Buffer *b, Pos p) {
  return &b->lines[p.y][p.x];
}

static char buffer_getchar(Buffer *b, int x, int y) {
  return x >= b->lines[y].size ? '\n' : b->lines[y][x];
}

static char buffer_getchar(Buffer *b) {
  return b->pos.x >= b->lines[b->pos.y].size ? '\n' : b->lines[b->pos.y][b->pos.x];
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


static void buffer_insert_char(Buffer *b, Utf8char ch) {
  int n = 1;
  if (IS_UTF8_HEAD(ch.code[0]))
    while (n < 4 && IS_UTF8_TRAIL(ch.code[n]))
      ++n;
  b->modified = 1;
  array_inserta(b->lines[b->pos.y], b->pos.x, (char*)ch.code, n);
  buffer_move_x(b, 1);
  if (ch == '}' || ch == ')' || ch == ']' || ch == '>')
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

static int buffer_indentdepth(const Buffer *buffer, int y, bool *has_statement) {
  if (has_statement)
    *has_statement = false;
  if (y < 0)
    return 0;


  int depth = 0;
  Pos p = {0,y};
  Pos a,b;

  bool first = true;
  while (1) {
    int t = token_read(buffer, &p, y+1, &a, &b);
    if (t == TOKEN_NULL)
      break;
    switch (t) {
      case '{': ++depth; break;
      case '}': --depth; break;
      case '[': ++depth; break;
      case ']': --depth; break;
      case '(': ++depth; break;
      case ')': --depth; break;
      case TOKEN_IDENTIFIER: 
        if (first && (
            buffer_streq(buffer, a, b, "for") ||
            buffer_streq(buffer, a, b, "if") ||
            buffer_streq(buffer, a, b, "while") ||
            buffer_streq(buffer, a, b, "else"))) {
          if (has_statement)
            *has_statement = true;
        }
        break;
      default: break;
    }
    first = false;
  }
  return depth;
}

static int buffer_autoindent(Buffer *buffer, const int y) {
  const char tab_char = buffer->tab_type ? ' ' : '\t';
  const int tab_size = buffer->tab_type ? buffer->tab_type : 1;

  int diff = 0;

  const int current_indent = buffer_getindent(buffer, y);

  int y_above = y-1;
  while (y_above >= 0 && buffer->lines[y_above].size == 0)
    --y_above;
  if (y_above == 0) {
    diff = -buffer_getindent(buffer, y);
    goto done;
  } else {
    /* skip empty lines */
    bool above_is_statement;
    const int above_depth = buffer_indentdepth(buffer, y_above, &above_is_statement);
    const bool above_is_indenting = (above_depth > 0 || above_is_statement);
    const int above_indent = buffer_getindent(buffer, y_above);
    int target_indent = above_indent;
    if (above_is_indenting)
      ++target_indent;

    bool this_is_statement;
    int this_depth = buffer_indentdepth(buffer, y, &this_is_statement);
    bool this_is_deintenting = this_depth < 0 && !this_is_statement;
    if (this_is_deintenting)
      --target_indent;

    // fix special case of
    // if (...)
    //   if (...)
    //     some_thing_not_if
    // this_line
    if (!above_is_indenting && above_depth == 0) {
      for (int yy = y_above-1; yy >= 0; --yy) {
        bool is_statement;
        const int indent = buffer_indentdepth(buffer, yy, &is_statement);
        if (is_statement && indent == 0)
          --target_indent;
        else
          break;
      }
    }

    diff = tab_size * (target_indent - current_indent);
    printf("%i\n", tab_size);
  }


  done:
  if (diff < -current_indent*tab_size)
    diff = -current_indent*tab_size;
  if (diff < 0)
    array_remove_slown(buffer->lines[y], 0, at_most(current_indent*tab_size, -diff));
  if (diff > 0) {
    array_insertn(buffer->lines[y], 0, diff);
    for (int i = 0; i < diff; ++i)
      buffer->lines[y][i] = tab_char;
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
  b->modified = 1;

  int left_of_line = b->lines[b->pos.y].size - b->pos.x;
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
  b->ghost_x = x;
}

static void buffer_move_to(Buffer *b, int x, int y) {
  buffer_move_to_y(b, y);
  buffer_move_to_x(b, x);
}

static void buffer_move_to(Buffer *b, Pos p) {
  buffer_move_to_y(b, p.y);
  buffer_move_to_x(b, p.x);
}

static void buffer_move_y(Buffer *b, int dy) {
  b->pos.y = clampi(b->pos.y + dy, 0, b->lines.size - 1);

  if (b->ghost_x == GHOST_EOL)
    b->pos.x = b->lines[b->pos.y].size;
  else if (b->ghost_x == GHOST_BOL)
    b->pos.x = buffer_begin_of_line(b, b->pos.y);
  else
    b->pos.x = from_visual_offset(b->lines[b->pos.y], b->ghost_x);
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
  b->ghost_x = to_visual_offset(b->lines[b->pos.y], b->pos.x);
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
  array_resize(G.status_message_buffer[0], (int)G.bottom_pane.numchars_x());

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
      if (buffer_advance(b, &x, &y))
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
      if (buffer_advance(b, &x, &y))
        return;
    }

    if (x == rowsize)
      if (buffer_advance(b, &x, &y))
        return;
  }
}

#define IS_NUMBER_HEAD(c) (isdigit(c))
#define IS_NUMBER_TAIL(c) (isdigit(c) || ((c) >= 'A' && (c) <= 'F') || ((c) >= 'a' && (c) <= 'f') || (c) == 'x')
#define IS_IDENTIFIER_HEAD(c) (isalpha(c) || (c) == '_' || IS_UTF8_HEAD(c))
#define IS_IDENTIFIER_TAIL(c) (isalnum(c) || (c) == '_' || IS_UTF8_TRAIL(c))
static int token_read(const Buffer *b, Pos *p, int y_end, Pos *start, Pos *end) {
  int token;
  int x,y;
  x = p->x, y = p->y;

  for (;;) {
    if (y >= y_end || y >= b->lines.size) {
      token = TOKEN_NULL;
      break;
    }

    // endline
    if (x >= b->lines[y].size) {
      token = TOKEN_EOL;
      if (start)
        *start = {x,y};
      if (end)
        *end = {x,y};
      x = 0;
      ++y;
      break;
    }

    char c = b->lines[y][x];

    if (isspace(c)) {
      ++x;
      continue;
    }

    /* identifier */
    if (IS_IDENTIFIER_HEAD(c)) {
      token = TOKEN_IDENTIFIER;
      if (start)
        *start = {x, y};
      while (x < b->lines[y].size) {
        if (end)
          *end = {x, y};
        c = b->lines[y][++x];
        if (!IS_IDENTIFIER_TAIL(c))
          break;
      }
      break;
    }

    /* start of block comment */
    else if (c == '/' && x+1 < b->lines[y].size && b->lines[y][x+1] == '*') {
      token = TOKEN_BLOCK_COMMENT_BEGIN;
      if (start)
        *start = {x, y};
      if (end)
        *end = {x+1, y};
      x += 2;
      break;
    }

    /* end of block comment */
    else if (c == '*' && x+1 < b->lines[y].size && b->lines[y][x+1] == '/') {
      token = TOKEN_BLOCK_COMMENT_END;
      if (start)
        *start = {x, y};
      if (end)
        *end = {x+1, y};
      x += 2;
      break;
    }

    /* line comment */
    else if (c == '/' && x+1 < b->lines[y].size && b->lines[y][x+1] == '/') {
      token = TOKEN_LINE_COMMENT_BEGIN;
      if (start)
        *start = {x, y};
      if (end)
        *end = {x+1, y};
      x += 2;
      break;
    }

    /* number */
    else if (IS_NUMBER_HEAD(c)) {
      token = TOKEN_NUMBER;
      if (start)
        *start = {x, y};
      while (x < b->lines[y].size) {
        if (end)
          *end = {x, y};
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
            *end = {x, y};
          c = b->lines[y][++x];
        }
        if (x == b->lines[y].size)
          break;
      }
      while ((c == 'u' || c == 'l' || c == 'L' || c == 'f') && x < b->lines[y].size) {
        if (end)
          *end = {x, y};
        c = b->lines[y][++x];
      }
      break;
    }

    else if (c == '"' || c == '\'') {
      char str_char = c;
      token = TOKEN_STRING;
      if (start)
        *start = {x, y};
      ++x;
      for (;;) {
        if (x >= b->lines[y].size) {
          token = TOKEN_STRING_BEGIN;
          if (end)
            *end = {x, y};
          ++y;
          x = 0;
          break;
        }

        c = b->lines[y][x];
        if (c == str_char) {
          if (end)
            *end = {x, y};
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
        *start = {x, y};
      if (end)
        *end = {x, y};
      ++x;
      break;
    }
  }

  p->x = x;
  p->y = y;
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

struct DropdownMatch {
  const char *str;
  float points;
};

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
  char *input_str = buffer_getstr(active_pane->buffer, G.dropdown_pos);
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

static void dropdown_update_on_insert(Pane *active_pane, Utf8char input) {

  if (!G.dropdown_visible) {
    if (!IS_IDENTIFIER_HEAD(*input.code))
      goto dropdown_hide;

    G.dropdown_pos = active_pane->buffer->pos;
  }
  else
    if (!IS_IDENTIFIER_TAIL(*input.code))
      goto dropdown_hide;

  G.dropdown_visible = 1;
  return;

  dropdown_hide:
  G.dropdown_visible = 0;
}

static void insert_default(Pane *p, SpecialKey special_key, Utf8char input) {
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

  if (graphics_init(&G.window))
    exit(1);

  const int font_height = 14;

  if (graphics_text_init("/usr/share/fonts/truetype/ubuntu-font-family/UbuntuMono-R.ttf", font_height))
    exit(1);

  if (graphics_quad_init())
    exit(1);

  SDL_GetWindowSize(G.window, &G.win_width, &G.win_height);

  G.font_width = graphics_get_font_advance();
  G.tab_width = 4;
  G.default_tab_type = 4;
  const int line_margin = 0;
  G.line_height = font_height + line_margin;

  G.default_style.text_color = {0.8f, 0.8f, 0.8f};
  G.default_style.background_color = {0.1, 0.1, 0.1};
  G.default_gutter_style.text_color = {0.5f, 0.5f, 0.5f};
  G.default_gutter_style.background_color = G.default_style.background_color;
  G.marker_background_color = {0.92549, 0.25098, 0.4784};
  G.number_color = {1.0f, 0.921568627451f, 0.23137254902f};
  G.comment_color = {0.329411764706f, 0.43137254902f, 0.478431372549f};
  G.string_color = {0.611764705882f, 0.152941176471f, 0.690196078431f};
  G.identifier_color = {0.262745098039f, 0.627450980392f, 0.278431372549f};
  G.keyword_color = {0.92549, 0.25098, 0.4784};

  G.hairline_color = {0.2f, 0.2f, 0.2f};
  G.highlight_background_color = {0.2f, 0.2f, 0.2f};

  /* init predefined buffers */
  buffer_empty(&G.menu_buffer);
  buffer_empty(&G.search_buffer);
  buffer_empty(&G.status_message_buffer);
  buffer_empty(&G.dropdown_buffer);

  G.main_pane.x = 0;
  G.main_pane.y = 0;
  G.main_pane.pw = G.win_width;
  G.main_pane.ph = G.win_height;

  G.selected_pane = &G.main_pane;

  G.menu_buffer.identifiers = {};
  array_pushn(G.menu_buffer.identifiers, (int)ARRAY_LEN(menu_options));
  for (int i = 0; i < (int)ARRAY_LEN(menu_options); ++i)
    G.menu_buffer.identifiers[i] = menu_options[i].name;

  G.dropdown_pane.buffer = &G.dropdown_buffer;
}

static int pane_calc_top_visible_row(Pane *pane) {
  return at_least(0, pane->buffer->pos.y - pane->numchars_y()/2);
}

static int pane_calc_left_visible_column(Pane *pane, int gutter_width) {
  int x = pane->buffer->pos.x;
  x = to_visual_offset(pane->buffer->lines[pane->buffer->pos.y], x);
  x -= (pane->numchars_x() - gutter_width)*6/7;
  return at_least(x, 0);
}

static void render_pane(Pane *p, bool draw_gutter) {
  Buffer *b;
  int buf_y, buf_y0, buf_y1;
  int buf_x0;
  Style style;

  // calc bounds 
  b = p->buffer;
  buf_y0 = pane_calc_top_visible_row(p);
  buf_y1 = at_most(buf_y0 + p->numchars_y(), b->lines.size);

  int gutter_width;
  if (draw_gutter)
    gutter_width = at_least(calc_num_chars(buf_y1) + 3, 6);
  else
    gutter_width = 0;

  buf_x0 = pane_calc_left_visible_column(p, gutter_width);

  Canvas canvas;
  canvas.init(p->numchars_x(), p->numchars_y());
  canvas.fill(Utf8char{' '});
  canvas.fill(G.default_style);

  // draw each line 
  for (int y = 0, buf_y = buf_y0; buf_y < buf_y1; ++buf_y, ++y) {
    Array<char> line = b->lines[buf_y];
    // gutter 
    canvas.render_strf(0, y, G.default_gutter_style, 0, gutter_width, " %i", buf_y+1);
    // text 
    canvas.render_strn(gutter_width - buf_x0, y, G.default_style, gutter_width, -1, line, line.size);
  }

  // highlight the line you're on
  Pos pos = to_visual_pos(b, b->pos);
  canvas.fill_background(gutter_width, b->pos.y - buf_y0, -1, 1, G.highlight_background_color);
  // draw marker
  canvas.fill_background(gutter_width + pos.x - buf_x0, b->pos.y - buf_y0, 1, 1, G.marker_background_color);

  if (1) {
    Pos p = {0, buf_y0};
    for (;;) {
      Pos prev, next;

      int token = token_read(b, &p, buf_y1, &prev, &next);

      check_token:

      bool do_render = false;

      Color text_color;

      if (token == TOKEN_NULL)
        break;
      switch (token) {

        case TOKEN_NUMBER:
          do_render = true;
          text_color = G.number_color;
          break;

        case TOKEN_BLOCK_COMMENT_END:
          do_render = true;
          text_color = G.comment_color;
          prev = {buf_x0, buf_y0};
          break;

        case TOKEN_LINE_COMMENT_BEGIN: {
          do_render = true;
          // just fast forward to the end of the line, no need to parse it
          p = {0, prev.y+1};
          next = {b->lines[prev.y].size-1, prev.y};
          text_color = G.comment_color;
          break;
        }

        case TOKEN_BLOCK_COMMENT_BEGIN: {
          do_render = true;
          Pos start = prev;
          while (token != TOKEN_BLOCK_COMMENT_END && token != TOKEN_NULL)
            token = token_read(b, &p, buf_y1, &prev, &next);
          prev = start;
          text_color = G.comment_color;
          break;
        }

        case TOKEN_STRING:
          do_render = true;
          text_color = G.string_color;
          break;

        case TOKEN_STRING_BEGIN:
          do_render = true;
          text_color = G.string_color;
          break;

        case TOKEN_IDENTIFIER: {
          /* check for keywords */
          for (int i = 0; i < (int)ARRAY_LEN(keywords); ++i) {
            if (buffer_streq(b, prev, next, keywords[i])) {
              text_color = G.keyword_color;
              goto done;
            }
          }

          /* otherwise check for functions */
          /* should not be indented */
          if (isspace(buffer_getchar(b, 0, prev.y)))
            break;
          Pos prev_tmp, next_tmp;
          token = token_read(b, &p, buf_y1, &prev_tmp, &next_tmp);
          if (token != '(') {
            prev = prev_tmp;
            next = next_tmp;
            goto check_token;
          }
          text_color = G.identifier_color;

          done:
          do_render = true;
        } break;

        case '#':
          do_render = true;
          text_color = G.keyword_color;
          break;

        default:
          break;
      }
      if (do_render) {
        prev = to_visual_pos(b, prev);
        next = to_visual_pos(b, next);

        prev.x -= buf_x0;
        prev.y -= buf_y0;
        next.x -= buf_x0;
        next.y -= buf_y0;

        canvas.fill_textcolor(prev, next, IRect{gutter_width, 0, -1, -1}, text_color);
      }

      if (p.y > buf_y1)
        break;
    }
  }

  canvas.render(p);

  canvas.free();
}

static void handle_input(Utf8char input, SpecialKey special_key) {
  // TODO: if it is utf8
  if (!special_key && IS_UTF8(input))
    return;

  Buffer *buffer = G.main_pane.buffer;

  switch (G.mode) {
  case MODE_GOTO:
    if (isdigit(input.code[0])) {
      G.goto_line_number *= 10;
      G.goto_line_number += input.code[0] - '0';
      buffer_move_to_y(buffer, G.goto_line_number-1);
      status_message_set("goto %u", G.goto_line_number);
      break;
    }

    switch (special_key ? special_key : input.code[0]) {
      case 't':
        buffer_move_to(buffer, 0, 0);
        break;
      case 'b':
        buffer_move_to(buffer, 0, buffer->num_lines()-1);
        break;
    }
    mode_normal(1);
    break;

  case MODE_COUNT:
    break;

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
          exit(1);
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

  case MODE_SEARCH: {
    Array<char> search;

    G.bottom_pane.buffer = &G.search_buffer;
    if (special_key == KEY_ESCAPE)
      dropdown_autocomplete(&G.search_buffer);
    else
      insert_default(&G.bottom_pane, special_key, input);

    search = G.search_buffer.lines[0];
    G.search_failed = buffer_find(buffer,
                             search,
                             search.size,
                             1);

    if (special_key == KEY_RETURN || special_key == KEY_ESCAPE) {
      if (G.dropdown_visible) {
        dropdown_get_first_line();
        G.search_failed = buffer_find(buffer,
                                 search,
                                 search.size,
                                 1);
      }
      if (G.search_failed) {
        buffer->pos = G.search_begin_pos;
        status_message_set("'%.*s' not found", G.search_buffer[0].size, G.search_buffer.lines[0].data);
        mode_normal(0);
      } else
        mode_normal(1);
    }
  } break;

  case MODE_DELETE:
    switch (special_key ? special_key : input.code[0]) {
      case 'd':
        buffer_delete_line(buffer);
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

  case MODE_NORMAL:
    switch (input.code[0]) {
    case '=':
      buffer->modified = true;
      buffer_move_x(buffer, buffer_autoindent(buffer, buffer->pos.y));
      break;

    case 'w': {
      char c = buffer_getchar(buffer);
      if (!isspace(c))
        while (c = buffer_getchar(buffer), !isspace(c))
          if (buffer_advance(buffer))
            break;
      while (c = buffer_getchar(buffer), isspace(c))
        if (buffer_advance(buffer))
          break;
    } break;

    case 'q':
      if (buffer->modified)
        status_message_set("You have unsaved changes. If you really want to exit, use :quit");
      else
        exit(0);
      break;
    case 'i':
      mode_insert();
      break;
    case 'j':
      buffer_move_y(G.selected_pane->buffer, 1);
      break;
    case 'k':
      buffer_move_y(G.selected_pane->buffer, -1);
      break;
    case 'h':
      buffer_advance_r(buffer);
      break;
    case 'l':
      buffer_advance(buffer);
      break;
    case 'L':
      buffer_goto_endline(buffer);
      break;
    case 'H':
      buffer_goto_beginline(buffer);
      break;
    case 'n': {
      int err;
      Pos prev;

      prev = buffer->pos;

      err = buffer_find(buffer, G.search_buffer[0], G.search_buffer[0].size, 0);
      if (err) {
        status_message_set("'%.*s' not found", G.search_buffer[0].size, G.search_buffer[0].data);
        break;
      }
      /*jumplist_push(prev);*/
    } break;

    case 'N': {
      Pos prev = buffer->pos;

      int err = buffer_find_r(buffer, G.search_buffer[0], G.search_buffer[0].size, 0);
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
      buffer_insert_newline_below(buffer);
      mode_insert();
      break;

    case ':':
      mode_menu();
      break;

    case 'd':
      mode_delete();
      break;

    case 'J':
      buffer_move_y(buffer, G.main_pane.numchars_y()/2);
      break;

    case 'K':
      buffer_move_y(buffer, -G.main_pane.numchars_y()/2);
      break;
    }
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

  for (;;) {

    Utf8char input = {};
    SpecialKey special_key = KEY_NONE;
    for (SDL_Event event; SDL_PollEvent(&event);) {

      switch (event.type) {
      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_CLOSE)
          return 1;
        break;

      case SDL_KEYDOWN:
        switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
          special_key = KEY_ESCAPE;
          break;
        case SDLK_DOWN:
          special_key = KEY_ARROW_DOWN;
          break;
        case SDLK_UP:
          special_key = KEY_ARROW_UP;
          break;
        case SDLK_LEFT:
          special_key = KEY_ARROW_LEFT;
          break;
        case SDLK_RIGHT:
          special_key = KEY_ARROW_RIGHT;
          break;
        case SDLK_RETURN:
          special_key = KEY_RETURN;
          break;
        case SDLK_TAB:
          special_key = KEY_TAB;
          break;
        case SDLK_BACKSPACE:
          special_key = KEY_BACKSPACE;
          break;
        case SDLK_HOME:
          special_key = KEY_HOME;
          break;
        case SDLK_END:
          special_key = KEY_END;
          break;
        }
        break;

      case SDL_TEXTINPUT:
        // ignore weird characters
        if (strlen(event.text.text) > sizeof(input))
          break;
        memcpy(input.code, event.text.text, strlen(event.text.text));
        break;
      }
    }

    // handle input
    if (input.code[0] || special_key)
      handle_input(input, special_key);

    glClearColor(0.9f, 0.9f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    render_pane(&G.main_pane, true);

    render_quads();
    render_text();

    // draw the 
    // render_textatlas(0, 0, 200, 200);

    SDL_GL_SwapWindow(G.window);
  }

  return 0;
}
