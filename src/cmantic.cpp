/*
 * TODO:
 * HIGH:
 *   - refactor buffer_getchar to return Utf8char
 *   - refactor buffer_advance to be utf8 awar
 *   - Revert back to the old way of tokenizing comments
 *   - refactor token_read to return a TokenResult struct
 *   - refactor canvas to be more of a "View", which is gutter and buffer-offset-aware
 *   - add outline to canvas?
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
    #define NOMINMAX
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
  template<class T>
  static T clamp(T x, T a, T b) {
    return x < a ? a : (b < x ? b : x);
  }

  template<class T>
  T max(T a, T b) {return a < b ? b : a;}
  template<class T>
  T min(T a, T b) {return b < a ? b : a;}

  float angle_to_range(float v, float a, float b) {
    return (sinf(v)*0.5f + 0.5f)*(b-a) + a;
  }

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
  MODE_HIGHLIGHT,
  MODE_COUNT
};

struct Style {
  Color text_color;
  Color background_color;
};

static bool operator==(Style a, Style b) {
  return a.text_color == b.text_color && a.background_color == b.background_color;
}

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

enum Token {
  TOKEN_NULL = 0,
  TOKEN_IDENTIFIER = -2,
  TOKEN_NUMBER = -3,
  TOKEN_STRING = -4,
  TOKEN_EOL = -5,
  TOKEN_STRING_BEGIN = -6,
  TOKEN_BLOCK_COMMENT_BEGIN = -7,
  TOKEN_BLOCK_COMMENT_END = -8,
  TOKEN_LINE_COMMENT_BEGIN = -9,
  TOKEN_OPERATOR = -10,
};

struct TokenInfo {
  int token;
  int x;
};

static int calc_num_chars(int i) {
  int result = 0;
  while (i > 0) {
    ++result;
    i /= 10;
  }
  return result;
}

struct Pos {
  int x,y;
  bool operator!=(Pos p) {
    return x != p.x || y != p.y;
  }
};
Pos operator+(Pos a, Pos b) {
  return {a.x+b.x, a.y+b.y};
}

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

static int token_read(const Buffer *b, Pos *p, int y_end, Pos *start, Pos *end);

struct v2 {
  int x,y;
};

struct Range {
  Pos a,b;
};
union Rect {
  struct {
    Pos p;
    Pos size;
  };
  struct {
    int x,y,w,h;
  };
};

struct Canvas {
  Utf8char *chars;
  Style *styles;
  int w, h;

  void render_strf(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, ...);
  void render(Pos offset);
  void render_str_v(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, va_list args);
  void render_strn(Pos p, const Color *text_color, const Color *background_color, int xclip0, int xclip1, const char *str, int n);
  void fill_background(Rect r, Color c);
  void fill_textcolor(Rect r, Color c);
  void fill_textcolor(Range range, Rect bounds, Color c);
  void invert_color(Pos p);
  void fill(Style s);
  void fill(Utf8char c);
  void free();
  void resize(int w, int h);
  void init(int w, int h);
};

struct Pane {
  Rect bounds;
  Color background_color;
  Color text_color;
  bool syntax_highlight;
  Buffer *buffer;

  // visual settings
  int gutter_width;

  void render(bool draw_gutter);
  int calc_top_visible_row() const;
  int calc_left_visible_column() const;

  int numchars_x() const;
  int numchars_y() const;
  Pos slot2pixel(Pos p) const;
  int slot2pixelx(int x) const;
  int slot2pixely(int y) const;
  Pos slot2global(Pos p) const;
  Pos buf2char(Pos p) const;
  Pos buf2pixel(Pos p) const;
};

struct PoppedColor {
  Color base_color;
  Color popped_color;
  float speed;
  float cooldown;
  float min;
  float max;
  float amount; // 0
  void reset() {amount = max + cooldown*speed*(max-min);}
  void tick() {assert(speed); amount -= speed*(max-min)*0.04f;}
  Color get() {return Color::blend(base_color, popped_color, clamp(amount, min, max));}
};

struct RotatingColor {
  float speed;
  float saturation;
  float light;
  float hue; // 0
  void tick() {hue = fmodf(hue + speed*0.1f, 360.0f);}
  void jump() {hue = fmodf(hue + 180.0f, 360.0f);}
  Color get() {return Color::from_hsl(hue, saturation, light);}
};

struct State {

  /* @renderer some rendering state */
  SDL_Window *window;
  int font_width;
  int font_height;
  int line_margin;
  int line_height;
  Array<char> tmp_render_buffer;
  int win_height, win_width;

  /* some settings */
  Color default_background_color;
  Color default_text_color;
  Style default_gutter_style;
  Color default_highlight_background_color;
  Color number_color;
  Color default_number_color;
  Color comment_color;
  Color string_color;
  Color operator_color;
  Color default_keyword_color;
  Color identifier_color;
  Color default_search_term_text_color;
  Color search_term_text_color;
  PoppedColor marker_background_color;
  PoppedColor search_term_background_color;
  PoppedColor highlight_background_color;
  RotatingColor default_marker_background_color;

  /* highlighting flags */
  bool highlight_number;

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
  Pos dropdown_pos;
  bool dropdown_backwards;
  bool dropdown_visible;

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

static Pos to_visual_pos(Buffer *b, Pos p) {
  p.x = to_visual_offset(b->lines[p.y], p.x);
  return p;
}

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
static void buffer_move_to(Buffer *b, Pos p);
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

static int buffer_find(Buffer *b, char *str, int n, bool stay, Pos *pos) {
  int x, y;
  if (!n)
    return 1;

  x = pos->x;
  if (!stay)
    ++x;
  y = pos->y;

  for (; y < b->lines.size; ++y, x = 0) {
    char *p;
    if (x >= b->lines[y].size)
      continue;

    p = (char*)memmem(str, n, b->lines[y]+x, b->lines[y].size-x);
    if (!p)
      continue;

    x = p - b->lines[y];
    pos->x = x;
    pos->y = y;
    return 0;
  }
  return 1;
}

static int buffer_find_and_move(Buffer *b, char *str, int n, bool stay) {
  Pos p = b->pos;
  if (buffer_find(b, str, n, stay, &p))
    return 1;
  buffer_move_to(b, p);
  return 0;
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
  b->modified = true;
  int n = 1;
  if (IS_UTF8_HEAD(ch.code[0]))
    while (n < 4 && IS_UTF8_TRAIL(ch.code[n]))
      ++n;
  array_inserta(b->lines[b->pos.y], b->pos.x, (char*)ch.code, n);
  buffer_move_x(b, 1);
  if (ch == '}' || ch == ')' || ch == ']' || ch == '>')
    buffer_move_x(b, buffer_autoindent(b, b->pos.y));
}

static void buffer_delete_line_at(Buffer *b, int y) {
  b->modified = true;
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
  buf->modified = true;
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
  b->modified = 1;
  int y = b->lines.size;
  if (!buffer_isempty(b))
    array_pushz(b->lines);
  else
    y = 0;
  int l = strlen(str);
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

static void buffer_move_to_y(Buffer *b, int y) {
  y = clamp(y, 0, b->lines.size-1);
  b->pos.y = y;
}

static void buffer_move_to_x(Buffer *b, int x) {
  x = clamp(x, 0, b->lines[b->pos.y].size);
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
  b->pos.y = clamp(b->pos.y + dy, 0, b->lines.size - 1);

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
  b->pos.x = clamp(b->pos.x, 0, w);
  b->ghost_x = to_visual_offset(b->lines[b->pos.y], b->pos.x);
}

static void buffer_move(Buffer *b, int dx, int dy) {
  if (dy)
    buffer_move_y(b, dy);
  if (dx)
    buffer_move_x(b, dx);
}

// makes sure positions are in bounds and so on
static void buffer_update(Buffer *b) {
  buffer_move(b, 0, 0);
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

// MUST BE REVERSE SIZE ORDER
static const struct {const char *str; int len;} operators[] = {
  {"===", 3},
  {"!==", 3},
  {"<<=", 3},
  {">>=", 3},
  {"||", 2},
  {"&&", 2},
  {"==", 2},
  {"!=", 2},
  {"<<", 2},
  {">>", 2},
  {"++", 2},
  {"--", 2},
  {"+", 1},
  {"-", 1},
  {"*", 1},
  {"&", 1},
  {"%", 1},
  {"=", 1},
  {"<", 1},
  {">", 1},
};

#define IS_NUMBER_HEAD(c) (isdigit(c))
#define IS_NUMBER_TAIL(c) (isdigit(c) || ((c) >= 'A' && (c) <= 'F') || ((c) >= 'a' && (c) <= 'f') || (c) == 'x')
#define IS_IDENTIFIER_HEAD(c) (isalpha(c) || (c) == '_' || (c) == '#' || IS_UTF8_HEAD(c))
#define IS_IDENTIFIER_TAIL(c) (isalnum(c) || (c) == '_' || IS_UTF8_TRAIL(c))

static int token_read(const Buffer *b, Pos *p, int y_end, Pos *start, Pos *end) {
  int token;
  int x,y;
  x = p->x, y = p->y;

  for (;;) {
    if (y >= y_end || y >= b->lines.size) {
      token = TOKEN_NULL;
      goto done;
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
      goto done;
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
      goto done;
    }

    /* start of block comment */
    if (c == '/' && x+1 < b->lines[y].size && b->lines[y][x+1] == '*') {
      token = TOKEN_BLOCK_COMMENT_BEGIN;
      if (start)
        *start = {x, y};
      if (end)
        *end = {x+1, y};
      x += 2;
      goto done;
    }

    /* end of block comment */
    if (c == '*' && x+1 < b->lines[y].size && b->lines[y][x+1] == '/') {
      token = TOKEN_BLOCK_COMMENT_END;
      if (start)
        *start = {x, y};
      if (end)
        *end = {x+1, y};
      x += 2;
      goto done;
    }

    /* line comment */
    if (c == '/' && x+1 < b->lines[y].size && b->lines[y][x+1] == '/') {
      token = TOKEN_LINE_COMMENT_BEGIN;
      if (start)
        *start = {x, y};
      if (end)
        *end = {x+1, y};
      x += 2;
      goto done;
    }

    /* number */
    if (IS_NUMBER_HEAD(c)) {
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
        goto done;
      if (c == '.' && x+1 < b->lines[y].size && isdigit(b->lines[y][x+1])) {
        c = b->lines[y][++x];
        while (isdigit(c) && x < b->lines[y].size) {
          if (end)
            *end = {x, y};
          c = b->lines[y][++x];
        }
        if (x == b->lines[y].size)
          goto done;
      }
      while ((c == 'u' || c == 'l' || c == 'L' || c == 'f') && x < b->lines[y].size) {
        if (end)
          *end = {x, y};
        c = b->lines[y][++x];
      }
      goto done;
    }

    /* string */
    if (c == '"' || c == '\'') {
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
      goto done;
    }

    /* operators */
    for (int i = 0; i < (int)ARRAY_LEN(operators); ++i) {
      if (x + operators[i].len <= b->lines[y].size && memcmp(operators[i].str, b->lines[y]+x, operators[i].len) == 0) {
        token = TOKEN_OPERATOR;
        if (start)
          *start = {x,y};
        if (end)
          *end = {x+operators[i].len-1, y};
        x += operators[i].len;
        goto done;
      }
    }

    /* single char token */
    token = c;
    if (start)
      *start = {x, y};
    if (end)
      *end = {x, y};
    ++x;
    goto done;
  }

  done:
  p->x = x;
  p->y = y;
  return token;
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

static void mode_highlight() {
  G.mode = MODE_HIGHLIGHT;
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

static void mode_normal(bool set_message) {
  if (G.mode == MODE_INSERT)
    G.default_marker_background_color.jump();

  G.mode = MODE_NORMAL;
  G.bottom_pane.buffer = &G.status_message_buffer;

  G.dropdown_visible = false;

  if (set_message)
    status_message_set("normal");
}

static void mode_insert() {
  G.default_marker_background_color.jump();

  G.mode = MODE_INSERT;
  G.bottom_pane.buffer = &G.status_message_buffer;
  G.dropdown_visible = false;
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

static void fill_dropdown_buffer(Pane *active_pane, bool grow_upwards) {
  static DropdownMatch best_matches[DROPDOWN_SIZE];

  if (!G.dropdown_visible)
    return;

  int num_best_matches = 0;

  Buffer *active_buffer = active_pane->buffer;
  Array<const char*> identifiers = active_buffer->identifiers;

  /* this shouldn't happen.. but just to be safe */
  if (G.dropdown_pos.y != active_pane->buffer->pos.y) {
    G.dropdown_visible = false;
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

  buffer_empty(&G.dropdown_buffer);
  for (int i = 0; i < num_best_matches; ++i) {
    int which = grow_upwards ? num_best_matches-1-i : i;
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
  if (!G.dropdown_visible || buffer_isempty(&G.dropdown_buffer))
    return;

  int y;
  if (G.dropdown_backwards)
    y = G.dropdown_buffer.num_lines()-1;
  else
    y = 0;
  char *str = G.dropdown_buffer[y];
  int l = G.dropdown_buffer[y].size;
  buffer_replace(b, G.dropdown_pos.x, b->pos.x, b->pos.y, str, l);
  G.dropdown_visible = false;
}

static void dropdown_update_on_insert(Pane *active_pane, Utf8char input) {

  if (!G.dropdown_visible) {
    if (!IS_IDENTIFIER_HEAD(*input.code)) {
      G.dropdown_visible = false;
      return;
    }

    G.dropdown_pos = active_pane->buffer->pos;
    // printf("%i %i\n", G.dropdown_pos.x, G.dropdown_pos.y);
  }
  else if (!IS_IDENTIFIER_TAIL(*input.code)) {
    G.dropdown_visible = false;
    return;
  }

  G.dropdown_visible = true;
}

static void insert_default(Pane *p, SpecialKey special_key, Utf8char input) {
  Buffer *b;

  b = p->buffer;

  /* TODO: should not set `modifier` if we just enter and exit insert mode */
  if (special_key == KEY_ESCAPE) {
    buffer_pretty_range(G.main_pane.buffer, G.insert_mode_begin_y, G.main_pane.buffer->pos.y+1);
    mode_normal(true);
    return;
  }

  if (!special_key) {
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

static const char *ttf_file = "font.ttf";

#define rgb(r,g,b) {(r)/255.0f, (g)/255.0f, (b)/255.0f}
static const Color COLOR_PINK = {0.92549f, 0.25098f, 0.4784f};
static const Color COLOR_YELLOW = {1.0f, 0.921568627451f, 0.23137254902f};
static const Color COLOR_AMBER = rgb(255,193,7);
static const Color COLOR_DEEP_ORANGE = rgb(255,138,101);
static const Color COLOR_ORANGE = rgb(255,183,77);
static const Color COLOR_GREEN = rgb(129,199,132);
static const Color COLOR_LIGHT_GREEN = rgb(174,213,129);
static const Color COLOR_INDIGO = rgb(121,134,203);
static const Color COLOR_DEEP_PURPLE = rgb(149,117,205);
static const Color COLOR_RED = rgb(229,115,115);
static const Color COLOR_CYAN = rgb(77,208,225);
static const Color COLOR_LIGHT_BLUE = rgb(79,195,247);
static const Color COLOR_PURPLE = rgb(186,104,200);
static const Color COLOR_BLUEGREY = {0.329411764706f, 0.43137254902f, 0.478431372549f};
static const Color COLOR_GREY = {0.2f, 0.2f, 0.2f};
static const Color COLOR_BLACK = {0.1f, 0.1f, 0.1f};
static const Color COLOR_WHITE = {0.9f, 0.9f, 0.9f};
static const Color COLOR_BLUE = rgb(79,195,247);
static const Color COLOR_DARK_BLUE = rgb(124, 173, 213);

enum KeywordType {
  KEYWORD_NONE,
  KEYWORD_CONTROL, // control flow
  KEYWORD_TYPE,
  KEYWORD_SPECIFIER,
  KEYWORD_DECLARATION,
  KEYWORD_FUNCTION,
  KEYWORD_MACRO,
  KEYWORD_COUNT
};

Color keyword_colors[KEYWORD_COUNT];
STATIC_ASSERT(ARRAY_LEN(keyword_colors) == KEYWORD_COUNT, all_keyword_colors_assigned);

struct Keyword {
  const char *name;
  KeywordType type;
};
static Keyword keywords[] = {
  // types

  {"char", KEYWORD_TYPE},
  {"short", KEYWORD_TYPE},
  {"int", KEYWORD_TYPE},
  {"long", KEYWORD_TYPE},
  {"float", KEYWORD_TYPE},
  {"double", KEYWORD_TYPE},
  {"unsigned", KEYWORD_TYPE},
  {"void", KEYWORD_TYPE},
  {"bool", KEYWORD_TYPE},
  {"uint64_t", KEYWORD_TYPE},
  {"uint32_t", KEYWORD_TYPE},
  {"uint16_t", KEYWORD_TYPE},
  {"uint8_t", KEYWORD_TYPE},
  {"int64_t", KEYWORD_TYPE},
  {"int32_t", KEYWORD_TYPE},
  {"int16_t", KEYWORD_TYPE},
  {"int8_t", KEYWORD_TYPE},
  {"u64", KEYWORD_TYPE},
  {"u32", KEYWORD_TYPE},
  {"u16", KEYWORD_TYPE},
  {"u8", KEYWORD_TYPE},
  {"i64", KEYWORD_TYPE},
  {"i32", KEYWORD_TYPE},
  {"i16", KEYWORD_TYPE},
  {"i8", KEYWORD_TYPE},
  {"va_list", KEYWORD_TYPE},

  // function

  #if 0
  {"typeof", KEYWORD_FUNCTION},
  {"sizeof", KEYWORD_FUNCTION},
  {"printf", KEYWORD_FUNCTION},
  {"puts", KEYWORD_FUNCTION},
  {"strcmp", KEYWORD_FUNCTION},
  {"strlen", KEYWORD_FUNCTION},
  {"fprintf", KEYWORD_FUNCTION},
  {"malloc", KEYWORD_FUNCTION},
  {"free", KEYWORD_FUNCTION},
  {"new", KEYWORD_FUNCTION},
  {"delete", KEYWORD_FUNCTION},
  {"fflush", KEYWORD_FUNCTION},
  {"va_start", KEYWORD_FUNCTION},
  {"vfprintf", KEYWORD_FUNCTION},
  {"va_end", KEYWORD_FUNCTION},
  {"abort", KEYWORD_FUNCTION},
  {"exit", KEYWORD_FUNCTION},
  {"min", KEYWORD_FUNCTION},
  {"max", KEYWORD_FUNCTION},
  {"memcmp", KEYWORD_FUNCTION},
  {"putchar", KEYWORD_FUNCTION},
  {"putc", KEYWORD_FUNCTION},
  {"fputc", KEYWORD_FUNCTION},
  {"getchar", KEYWORD_FUNCTION},
  {"swap", KEYWORD_FUNCTION},
  #endif

  // specifiers

  {"static", KEYWORD_SPECIFIER},
  {"const", KEYWORD_SPECIFIER},
  {"extern", KEYWORD_SPECIFIER},
  {"nothrow", KEYWORD_SPECIFIER},
  {"noexcept", KEYWORD_SPECIFIER},

  // declarations

  {"struct", KEYWORD_DECLARATION},
  {"union", KEYWORD_DECLARATION},
  {"enum", KEYWORD_DECLARATION},
  {"typedef", KEYWORD_DECLARATION},
  {"template", KEYWORD_DECLARATION},

  // macro

  {"#include", KEYWORD_MACRO},
  {"#define", KEYWORD_MACRO},
  {"#ifdef", KEYWORD_MACRO},
  {"#ifndef", KEYWORD_MACRO},
  {"#endif", KEYWORD_MACRO},
  {"#elif", KEYWORD_MACRO},
  {"#else", KEYWORD_MACRO},
  {"#if", KEYWORD_MACRO},

  // flow control

  {"switch", KEYWORD_CONTROL},
  {"case", KEYWORD_CONTROL},
  {"if", KEYWORD_CONTROL},
  {"else", KEYWORD_CONTROL},
  {"for", KEYWORD_CONTROL},
  {"while", KEYWORD_CONTROL},
  {"return", KEYWORD_CONTROL},
  {"continue", KEYWORD_CONTROL},
  {"break", KEYWORD_CONTROL},
  {"goto", KEYWORD_CONTROL},
};

static void state_init() {

  // initialize graphics library
  if (graphics_init(&G.window))
    exit(1);
  G.font_height = 14;
  if (graphics_text_init(ttf_file, G.font_height))
    exit(1);
  if (graphics_quad_init())
    exit(1);

  SDL_GetWindowSize(G.window, &G.win_width, &G.win_height);

  // font stuff
  G.font_width = graphics_get_font_advance();
  G.tab_width = 4;
  G.default_tab_type = 4;
  G.line_margin = 0;
  G.line_height = G.font_height + G.line_margin;

  // @colors!
  G.default_text_color = {0.8f, 0.8f, 0.8f};
  G.default_background_color = {0.13f, 0.13f, 0.13f};
  G.default_gutter_style.text_color = {0.5f, 0.5f, 0.5f};
  G.default_gutter_style.background_color = G.default_background_color;
  G.default_number_color = G.number_color = COLOR_RED;
  G.string_color =                          COLOR_RED;
  G.operator_color =                        COLOR_RED;
  G.comment_color = COLOR_BLUEGREY;
  G.identifier_color = COLOR_GREEN;
  G.default_search_term_text_color = G.search_term_text_color = COLOR_WHITE;

  keyword_colors[KEYWORD_NONE]        = {};
  keyword_colors[KEYWORD_CONTROL]     = COLOR_DARK_BLUE;
  keyword_colors[KEYWORD_TYPE]        = COLOR_DARK_BLUE;
  keyword_colors[KEYWORD_SPECIFIER]   = COLOR_DARK_BLUE;
  keyword_colors[KEYWORD_DECLARATION] = COLOR_PINK;
  keyword_colors[KEYWORD_FUNCTION]    = COLOR_BLUE;
  keyword_colors[KEYWORD_MACRO]       = COLOR_DEEP_ORANGE;

  G.highlight_background_color.base_color = G.default_background_color;
  G.highlight_background_color.popped_color = COLOR_GREY;
  G.highlight_background_color.speed = 1.0f;
  G.highlight_background_color.cooldown = 1.0f;
  G.highlight_background_color.min = 0.5f;
  G.highlight_background_color.max = 1.0f;

  G.default_marker_background_color.speed = 0.2f;
  G.default_marker_background_color.saturation = 0.8f;
  G.default_marker_background_color.light = 0.7f;
  G.default_marker_background_color.hue = 340.0f;

  G.marker_background_color.base_color = COLOR_GREY;
  G.marker_background_color.popped_color = COLOR_PINK;
  G.marker_background_color.speed = 1.0f;
  G.marker_background_color.cooldown = 1.0f;
  G.marker_background_color.min = 0.5f;
  G.marker_background_color.max = 1.0f;

  G.search_term_background_color.base_color = G.default_background_color;
  G.search_term_background_color.popped_color = COLOR_LIGHT_BLUE;
  G.search_term_background_color.speed = 1.0f;
  G.search_term_background_color.cooldown = 4.0f;
  G.search_term_background_color.min = 0.4f;
  G.search_term_background_color.max = 1.0f;

  /* init predefined buffers */
  buffer_empty(&G.menu_buffer);
  buffer_empty(&G.search_buffer);
  buffer_empty(&G.status_message_buffer);
  buffer_empty(&G.dropdown_buffer);

  // some pane settings
  G.main_pane.syntax_highlight = true;
  G.main_pane.background_color = G.default_background_color;
  G.main_pane.text_color = G.default_text_color;

  G.bottom_pane.syntax_highlight = true;
  G.bottom_pane.background_color = {0.2f, 0.2f, 0.2f};
  G.bottom_pane.buffer = &G.status_message_buffer;
  G.bottom_pane.text_color = G.default_text_color;

  G.dropdown_pane.background_color = COLOR_DEEP_ORANGE;
  G.dropdown_pane.text_color = COLOR_BLACK;
  G.dropdown_pane.buffer = &G.dropdown_buffer;

  G.selected_pane = &G.main_pane;

  G.menu_buffer.identifiers = {};
  array_pushn(G.menu_buffer.identifiers, (int)ARRAY_LEN(menu_options));
  for (int i = 0; i < (int)ARRAY_LEN(menu_options); ++i)
    G.menu_buffer.identifiers[i] = menu_options[i].name;

}

static int char2pixelx(int x) {
  return x*G.font_width;
}

static int char2pixely(int y) {
  return y*G.line_height;
}

static Pos char2pixel(int x, int y) {
  return {char2pixelx(x), char2pixely(y)};
}

static Pos char2pixel(Pos p) {
  return char2pixel(p.x, p.y);
}

static void render_dropdown(Pane *active_pane) {
  G.dropdown_backwards = active_pane == &G.bottom_pane;
  fill_dropdown_buffer(active_pane, G.dropdown_backwards);

  // resize dropdown pane
  int max_width = 0;
  for (int i = 0; i < G.dropdown_buffer.lines.size; ++i)
    max_width = max(max_width, G.dropdown_buffer.lines[i].size);
  G.dropdown_pane.bounds.size = char2pixel(max_width, G.dropdown_buffer.lines.size-1);
  G.dropdown_pane.bounds.x = clamp(G.dropdown_pane.bounds.x, 0, G.win_width - G.dropdown_pane.bounds.w);

  /* position pane */
  Pos p = G.dropdown_pos;
  if (G.dropdown_backwards) --p.y;
  else ++p.y;
  p = active_pane->buf2pixel(p);
  if (G.dropdown_backwards)
    p.y -= G.dropdown_pane.bounds.h;
  printf("%i %i %i\n", (int)p.x, (int)p.y, G.dropdown_backwards);
  G.dropdown_pane.bounds.p = p;
  // printf("%i %i %i %i\n", G.dropdown_pane.bounds.x, G.dropdown_pane.bounds.y, G.dropdown_pane.bounds.w, G.dropdown_pane.bounds.h);

  if (G.dropdown_visible && !buffer_isempty(&G.dropdown_buffer))
    G.dropdown_pane.render(false);
}

static void handle_input(Utf8char input, SpecialKey special_key) {
  // TODO: if it is utf8
  if (!special_key && IS_UTF8(input))
    return;

  Buffer *buffer = G.main_pane.buffer;
  int key = special_key ? special_key : input.code[0];

  switch (G.mode) {
  case MODE_GOTO:
    if (isdigit(input.code[0])) {
      G.goto_line_number *= 10;
      G.goto_line_number += input.code[0] - '0';
      buffer_move_to_y(buffer, G.goto_line_number-1);
      status_message_set("goto %u", G.goto_line_number);
      break;
    }

    switch (key) {
      case 't':
        buffer_move_to(buffer, 0, 0);
        break;
      case 'b':
        buffer_move_to(buffer, 0, buffer->num_lines()-1);
        break;
    }
    mode_normal(true);
    break;

  case MODE_COUNT:
    break;

  case MODE_HIGHLIGHT:
    switch (key) {
      case 'n':
        G.highlight_number = !G.highlight_number;
        mode_normal(false);
        break;
    }
    mode_normal(false);
    break;

  case MODE_MENU:
    G.bottom_pane.buffer = &G.menu_buffer;
    if (special_key == KEY_RETURN) {
      Array<char> line;
      int i;

      if (buffer_isempty(&G.menu_buffer)) {
        mode_normal(true);
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


      mode_normal(false);
    }
    else if (special_key == KEY_ESCAPE)
      mode_normal(true);
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
    G.search_failed = buffer_find_and_move(buffer,
                             search,
                             search.size,
                             true);

    if (special_key == KEY_RETURN || special_key == KEY_ESCAPE) {
      if (G.dropdown_visible) {
        dropdown_get_first_line();
        G.search_failed = buffer_find_and_move(buffer,
                                 search,
                                 search.size,
                                 true);
      }
      if (G.search_failed) {
        buffer->pos = G.search_begin_pos;
        status_message_set("'%.*s' not found", G.search_buffer[0].size, G.search_buffer.lines[0].data);
        mode_normal(false);
      } else
        mode_normal(true);
    } else
      G.search_term_background_color.reset();
  } break;

  case MODE_DELETE:
    switch (key) {
      case 'd':
        buffer_delete_line(buffer);
        buffer_update(buffer);
        mode_normal(true);
        break;

      default:
        mode_normal(true);
        break;
    }
    break;

  case MODE_INSERT:
    insert_default(&G.main_pane, special_key, input);
    break;

  case MODE_NORMAL:
    switch (input.code[0]) {
    case '=':
      if (buffer->lines[buffer->pos.y].size == 0)
        break;
      buffer->modified = true;
      buffer_move_x(buffer, buffer_autoindent(buffer, buffer->pos.y));
      break;

    case '+':
      G.font_height = at_most(G.font_height+1, 50);
      if (graphics_set_font_options(ttf_file, G.font_height))
        exit(1);
      break;

    case '-':
      G.font_height = at_least(G.font_height-1, 7);
      if (graphics_set_font_options(ttf_file, G.font_height))
        exit(1);
      break;

    case 'b': {
      if (buffer_advance_r(buffer))
        break;
      // if not in word, back to word
      char c = buffer_getchar(buffer);
      if (!(IS_IDENTIFIER_TAIL(c) || IS_IDENTIFIER_HEAD(c)))
        while (c = buffer_getchar(buffer), !(IS_IDENTIFIER_TAIL(c) || IS_IDENTIFIER_HEAD(c)))
          if (buffer_advance_r(buffer))
            break;
      // go to beginning of word
      while (c = buffer_getchar(buffer), IS_IDENTIFIER_TAIL(c) || IS_IDENTIFIER_HEAD(c))
        if (buffer_advance_r(buffer))
          break;
      buffer_advance(buffer);
    } break;

    case 'w': {
      char c = buffer_getchar(buffer);
      if (IS_IDENTIFIER_TAIL(c) || IS_IDENTIFIER_HEAD(c))
        while (c = buffer_getchar(buffer), IS_IDENTIFIER_TAIL(c) || IS_IDENTIFIER_HEAD(c))
          if (buffer_advance(buffer))
            break;
      while (c = buffer_getchar(buffer), !IS_IDENTIFIER_HEAD(c))
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
      G.search_term_background_color.reset();
      Pos p = buffer->pos;
      if (buffer_find(buffer, G.search_buffer[0], G.search_buffer[0].size, false, &p)) {
        status_message_set("'%.*s' not found", G.search_buffer[0].size, G.search_buffer[0].data);
        break;
      }
      buffer_move_to(buffer, p);
      /*jumplist_push(prev);*/
    } break;

    case 'N': {
      G.search_term_background_color.reset();
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
    case 'v':
      mode_highlight();
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

void Canvas::init(int width, int height) {
  this->w = width;
  this->h = height;
  this->chars = new Utf8char[w*h]();
  this->styles = new Style[w*h]();
}

void Canvas::resize(int width, int height) {
  if (this->chars)
    delete [] this->chars;
  if (this->styles)
    delete [] this->styles;
  this->init(width, height);
}

void Canvas::free() {
  delete [] this->chars;
  delete [] this->styles;
}

void Canvas::fill(Utf8char c) {
  for (int i = 0; i < w*h; ++i)
    this->chars[i] = c;
}

void Canvas::fill(Style s) {
  for (int i = 0; i < w*h; ++i)
    this->styles[i] = s;
}

void Canvas::invert_color(Pos p) {
  swap(styles[p.y*w + p.x].text_color, styles[p.y*w + p.x].background_color);
}

void Pane::render(bool draw_gutter) {
  // calc bounds 
  int buf_y0 = this->calc_top_visible_row();
  int buf_y1 = at_most(buf_y0 + this->numchars_y(), buffer->lines.size);

  if (draw_gutter)
    this->gutter_width = at_least(calc_num_chars(buf_y1) + 3, 6);
  else
    this->gutter_width = 0;

  int buf_x0 = this->calc_left_visible_column();

  Canvas canvas;
  canvas.init(this->numchars_x(), this->numchars_y());
  canvas.fill(Utf8char{' '});
  canvas.fill(Style{this->text_color, this->background_color});

  // draw each line 
  for (int y = 0, buf_y = buf_y0; buf_y < buf_y1; ++buf_y, ++y) {
    Array<char> line = buffer->lines[buf_y];
    // gutter 
    canvas.render_strf({0, y}, &G.default_gutter_style.text_color, &G.default_gutter_style.background_color, 0, this->gutter_width, " %i", buf_y+1);
    // text 
    canvas.render_strn(buf2char({buf_x0, buf_y}), &this->text_color, 0, 0, -1, line, line.size);
  }

  // highlight the line you're on
  if (G.selected_pane == this)
    canvas.fill_background({buf2char(buffer->pos), {-1, 1}}, G.highlight_background_color.get());

  // syntax @highlighting
  if (this->syntax_highlight) {
    Pos pos = {0, buf_y0};
    for (;;) {
      Pos prev, next;

      int token = token_read(buffer, &pos, buf_y1, &prev, &next);

      check_token:

      bool do_render = false;

      Color highlighted_text_color = {};

      if (token == TOKEN_NULL)
        break;
      switch (token) {

        case TOKEN_NUMBER:
          do_render = true;
          highlighted_text_color = G.number_color;
          break;

        case TOKEN_BLOCK_COMMENT_END:
          do_render = true;
          highlighted_text_color = G.comment_color;
          prev = {buf_x0, buf_y0};
          break;

        case TOKEN_LINE_COMMENT_BEGIN: {
          do_render = true;
          // just fast forward to the end of the line, no need to parse it
          pos = {0, prev.y+1};
          next = {buffer->lines[prev.y].size-1, prev.y};
          highlighted_text_color = G.comment_color;
          break;
        }

        case TOKEN_BLOCK_COMMENT_BEGIN: {
          do_render = true;
          Pos start = prev;
          while (token != TOKEN_BLOCK_COMMENT_END && token != TOKEN_NULL)
            token = token_read(buffer, &pos, buf_y1, &prev, &next);
          prev = start;
          highlighted_text_color = G.comment_color;
          break;
        }

        case TOKEN_STRING:
          do_render = true;
          highlighted_text_color = G.string_color;
          break;

        case TOKEN_STRING_BEGIN:
          do_render = true;
          highlighted_text_color = G.string_color;
          break;

        case TOKEN_OPERATOR:
          do_render = true;
          highlighted_text_color = G.operator_color;
          break;

        case TOKEN_IDENTIFIER: {
          // check for keywords
          for (int i = 0; i < (int)ARRAY_LEN(keywords); ++i) {
            if (buffer_streq(buffer, prev, next, keywords[i].name)) {
              highlighted_text_color = keyword_colors[keywords[i].type];
              goto done;
            }
          }

          // otherwise check for functions
          // we assume something not indented and followed by a '(' is a function
          if (isspace(buffer_getchar(buffer, 0, prev.y)))
            break;
          Pos prev_tmp, next_tmp;
          token = token_read(buffer, &pos, buf_y1, &prev_tmp, &next_tmp);
          if (token != '(') {
            prev = prev_tmp;
            next = next_tmp;
            goto check_token;
          }
          highlighted_text_color = G.identifier_color;

          done:
          do_render = true;
        } break;

        case '#':
          do_render = true;
          highlighted_text_color = keyword_colors[KEYWORD_MACRO];
          break;

        default:
          break;
      }
      if (do_render) {
        prev = to_visual_pos(buffer, prev);
        next = to_visual_pos(buffer, next);

        prev.x -= buf_x0;
        prev.y -= buf_y0;
        next.x -= buf_x0;
        next.y -= buf_y0;

        canvas.fill_textcolor({prev, next}, Rect{this->gutter_width, 0, -1, -1}, highlighted_text_color);
      }

      if (pos.y > buf_y1)
        break;
    }
  }

  // if there is a search term, highlight that as well
  if (G.selected_pane == this && G.search_buffer.lines[0].size > 0) {
    Pos pos = {0, buf_y0};
    while (!buffer_find(buffer, G.search_buffer.lines[0], G.search_buffer.lines[0].size, false, &pos) && pos.y < buf_y1) {
      canvas.fill_background({this->buf2char(pos), G.search_buffer.lines[0].size, 1}, G.search_term_background_color.get());
      // canvas.fill_textcolor(this->gutter_width + x0, pos.y - buf_y0, x1-x0, 1, G.search_term_text_color);
    }
  }

  // draw marker
  if (G.selected_pane == this) {
    canvas.fill_background({this->buf2char(buffer->pos), {1, 1}}, G.marker_background_color.get());
    // canvas.invert_color(this->gutter_width + pos.x - buf_x0, b->pos.y - buf_y0);
  }

  canvas.render(this->bounds.p);

  canvas.free();
  render_quads();
  render_text();
}

Pos Pane::buf2pixel(Pos p) const {
  p = to_visual_pos(this->buffer, p);
  p.x -= this->calc_left_visible_column();
  p.y -= this->calc_top_visible_row();
  p.x += this->gutter_width;
  p = char2pixel(p) + this->bounds.p;
  return p;
}

Pos Pane::buf2char(Pos p) const {
  p = to_visual_pos(this->buffer, p);
  p.x -= this->calc_left_visible_column();
  p.y -= this->calc_top_visible_row();
  p.x += this->gutter_width;
  return p;
}

int Pane::calc_top_visible_row() const {
  return at_least(0, this->buffer->pos.y - this->numchars_y()/2);
}

int Pane::calc_left_visible_column() const {
  int x = this->buffer->pos.x;
  x = to_visual_offset(this->buffer->lines[this->buffer->pos.y], x);
  x -= (this->numchars_x() - this->gutter_width)*6/7;
  return at_least(x, 0);
}


// fills a to b with the bounds 
void Canvas::fill_textcolor(Range range, Rect bounds, Color c) {
  Pos a = range.a;
  Pos b = range.b;
  if (bounds.w == -1)
    bounds.w = this->w - bounds.x;
  if (bounds.h == -1)
    bounds.h = this->h - bounds.y;
  // single line outside of bounds, early exit
  if (a.y == b.y && (b.x < 0 || a.x > this->w))
    return;

  int x,y,x0,x1,y0,y1;
  a.x = clamp(a.x, 0, bounds.w-1);
  a.y = clamp(a.y, 0, bounds.h-1);
  b.x = clamp(b.x, 0, bounds.w-1);
  b.y = clamp(b.y, 0, bounds.h-1);
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
void Canvas::fill_textcolor(Rect r, Color c) {
  if (r.w == -1)
    r.w = this->w - r.x;
  if (r.h == -1)
    r.h = this->h - r.y;
  r.w = at_most(r.w, this->w - r.x);
  r.h = at_most(r.h, this->h - r.y);
  if (r.w < 0 || r.h < 0)
    return;

  for (int y = r.y; y < r.y+r.h; ++y)
  for (int x = r.x; x < r.x+r.w; ++x)
    styles[y*this->w + x].text_color = c;
}

// w,h: use -1 to say it goes to the end
void Canvas::fill_background(Rect r, Color c) {
  if (r.w == -1)
    r.w = this->w - r.x;
  if (r.h == -1)
    r.h = this->h - r.y;
  r.w = at_most(r.w, this->w - r.x);
  r.h = at_most(r.h, this->h - r.y);
  if (r.w < 0 || r.h < 0)
    return;

  for (int y = r.y; y < r.y+r.h; ++y)
  for (int x = r.x; x < r.x+r.w; ++x)
    styles[y*this->w + x].background_color = c;
}

void Canvas::render_strn(Pos p, const Color *text_color, const Color *background_color, int xclip0, int xclip1, const char *str, int n) {
  int x = p.x;
  int y = p.y;
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
          if (text_color)
            style_row[x].text_color = *text_color;
          if (background_color)
            style_row[x].background_color = *background_color;
        }
      ++str;
    }
    else {
      Utf8char c = Utf8char::from_string(str, end);
      if (x >= xclip0 && x < xclip1) {
        row[x] = c;
        if (text_color)
          style_row[x].text_color = *text_color;
        if (background_color)
          style_row[x].background_color = *background_color;
      }
      ++x;
    }
  }
}

void Canvas::render_str_v(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, va_list args) {
  int max_chars = w-p.x+1;
  if (max_chars <= 0)
    return;
  array_resize(G.tmp_render_buffer, max_chars);
  int n = vsnprintf(G.tmp_render_buffer, max_chars, fmt, args);
  render_strn(p, text_color, background_color, x0, x1, G.tmp_render_buffer, min(max_chars, n));
}

void Canvas::render_strf(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  this->render_str_v(p, text_color, background_color, x0, x1, fmt, args);
  va_end(args);
}

void Canvas::render(Pos offset) {
  #if 0
  printf("PRINTING SCREEN\n\n");
  for (int i = 0; i < h; ++i) {
    for (int j = 0; j < w; ++j)
      putchar('a' + (int)(styles[i*w + j].background_color.r * 25.0f));
    putchar('\n');
  }
  #endif

  // render background
  for (int y = 0; y < h; ++y) {
    for (int x0 = 0, x1 = 1; x1 <= w; ++x1) {
      if (styles[y*w + x1] == styles[y*w + x0] && x1 < w)
        continue;
      Pos p0 = char2pixel(x0,y) + offset;
      Pos p1 = char2pixel(x1,y+1) + offset;
      const Color c = styles[y*w + x0].background_color;
      push_square_quad((float)p0.x, (float)p1.x, (float)p0.y, (float)p1.y, c);
      x0 = x1;
    }
  }

  // render text
  const int text_offset_y = (int)(-G.font_height*4.0f/15.0f); // TODO: get this from truetype?
  array_resize(G.tmp_render_buffer, w*sizeof(Utf8char));
  for (int row = 0; row < h; ++row) {
    Utf8char::to_string(&this->chars[row*w], w, G.tmp_render_buffer);
    int y = char2pixely(row+1) + text_offset_y + offset.y;
    for (int x0 = 0, x1 = 1; x1 <= w; ++x1) {
      if (styles[row*w + x1].text_color == styles[row*w + x0].text_color && x1 < w)
        continue;
      int x = char2pixelx(x0) + offset.x;
      push_textn(G.tmp_render_buffer.data + x0, x1 - x0, x, y, false, styles[row*w + x0].text_color);
      x0 = x1;
    }
  }
}

Pos Pane::slot2pixel(Pos p) const {
  return {this->bounds.x + p.x*G.font_width, this->bounds.y + p.y*G.line_height};
}

int Pane::slot2pixelx(int x) const {
  return this->bounds.x + x*G.font_width;
}

int Pane::slot2pixely(int y) const {
  return this->bounds.y + y*G.line_height;
}

int Pane::numchars_x() const {
  return this->bounds.w / G.font_width + 1;
}

int Pane::numchars_y() const {
  return this->bounds.h / G.line_height + 1;
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
int wmain(int, const wchar_t *[], wchar_t *[])
#else
int main(int, const char *[])
#endif
{

  state_init();

  /* open a buffer */
  {
    const char *filename = "src/cmantic.cpp";
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

    // boost marker when you move or change modes
    static Pos prev_pos;
    static Mode prev_mode;
    if (prev_pos != G.main_pane.buffer->pos || prev_mode != G.mode) {
      G.marker_background_color.reset();
      G.highlight_background_color.reset();
    }
    prev_pos = G.main_pane.buffer->pos;
    prev_mode = G.mode;

    G.marker_background_color.tick();
    G.highlight_background_color.tick();
    G.search_term_background_color.tick();

    // highlight some colors
    static float hue;
    const float sat = 0.3f;
    hue = fmodf(hue + 7.0f, 360.0f);
    if (G.highlight_number)
      G.number_color = Color::from_hsl(hue, sat, 0.5f);
    else
      G.number_color = G.default_number_color;
    G.default_marker_background_color.tick();
    G.marker_background_color.popped_color = G.default_marker_background_color.get();

    // render
    G.font_width = graphics_get_font_advance();
    G.line_height = G.font_height + G.line_margin;
    SDL_GetWindowSize(G.window, &G.win_width, &G.win_height);
    glClearColor(G.default_background_color.r, G.default_background_color.g, G.default_background_color.g, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // reflow panes
    G.main_pane.bounds = {0, 0, G.win_width, G.win_height - G.line_height};
    G.bottom_pane.bounds = {0, G.main_pane.bounds.y + G.main_pane.bounds.h, G.win_width, G.line_height};

    G.main_pane.render(true);
    G.bottom_pane.render(false);

    /* draw dropdown ? */
    G.search_buffer.identifiers = G.main_pane.buffer->identifiers;
    if (G.mode == MODE_SEARCH || G.mode == MODE_MENU)
      render_dropdown(&G.bottom_pane);
    else
      render_dropdown(&G.main_pane);

    // render_textatlas(0, 0, 200, 200);
    render_quads();
    render_text();

    SDL_GL_SwapWindow(G.window);
  }
}
