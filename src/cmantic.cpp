/*
 * TODO:
 * HIGH:
 *   - refactor buffer_getchar to return Utf8char
 *   - refactor buffer_advance to be utf8 awar
 *   - Revert back to the old way of tokenizing comments
 *   - refactor token_read to return a TokenResult struct
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
#include "cmantic_string.h"

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
  #define foreach(a) for (auto it = a; it < (a)+ARRAY_LEN(a); ++it)
  #define findn_min_by(array, n, ptr, field) do { \
        ptr = array; \
        for (auto _it = array; _it < array + n; ++_it) { \
          if (ptr->field < _it->field) { \
            ptr = _it; \
          } \
        } \
    } while (0)
  #define find_min_by(array, ptr, field) findn_min_by(array, ARRAY_LEN(array), ptr, field)
  typedef unsigned int u32;
  STATIC_ASSERT(sizeof(u32) == 4, u32_is_4_bytes);
  static void array_push_str(Array<char> *a, const char *str) {
    for (; *str; ++str)
      array_push(*a, *str);
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

bool operator==(Utf8char uc, char c) {
  return !(uc.code & 0xFF00) && (uc.code & 0xFF) == (u32)c;
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
  void operator+=(Pos p) {
    x += p.x;
    y += p.y;
  }
};

static Pos operator+(Pos a, Pos b) {
  return {a.x+b.x, a.y+b.y};
}

#define IS_NUMBER_HEAD(c) (isdigit(c))
#define IS_NUMBER_TAIL(c) (isdigit(c) || ((c) >= 'A' && (c) <= 'F') || ((c) >= 'a' && (c) <= 'f') || (c) == 'x')

static bool is_identifier_head(char c) {
  return isalpha(c) || c == '_' || c == '#';
}

static bool is_identifier_head(Utf8char c) {
  return c.is_ansi() && is_identifier_head(c.ansi());
}

static bool is_identifier_tail(char c) {
  return isalnum(c) || c == '_';
}

static bool is_identifier_tail(Utf8char c) {
  return c.is_ansi() && is_identifier_tail(c.ansi());
}

struct Buffer {
  Array<String> lines;
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

  String& operator[](int i) {return lines[i];}

  const String& operator[](int i) const {return lines[i];}

  int num_lines() const {return lines.size;}

  String slice(Pos p, int len) {return lines[p.y](p.x,p.x+len); }
  Pos to_visual_pos(Pos p);
  void move_to_y(int y);
  void move_to_x(int x);
  void move_to(int x, int y);
  void move_to(Pos p);
  void move_y(int dy);
  void move_x(int dx);
  void move(int dx, int dy);
  void update();
  int find_r(char *str, int n, int stay);
  int find(String s, bool stay, Pos *pos);
  int find_and_move(String s, bool stay);
  void insert_str(int x, int y, String s);
  void replace(int x0, int x1, int y, String s);
  void remove_trailing_whitespace(int y);
  void pretty_range(int y0, int y1);
  void pretty(int y);
  void insert_char(Utf8char ch);
  void delete_line_at(int y);
  void delete_line();
  void remove_range(Pos a, Pos b);
  void delete_char();
  void insert_tab();
  int getindent(int y);
  int indentdepth(int y, bool *has_statement);
  int autoindent(const int y);
  int isempty();
  void push_line(const char *str);
  void insert_newline();
  void insert_newline_below();
  void guess_tab_type();
  void goto_endline();
  int begin_of_line(int y);
  void goto_beginline();
  void empty();
  void truncate_to_n_lines(int n);
  int advance(int *x, int *y);
  int advance(Pos &p);
  int advance();
  int advance_r(Pos &p);
  int advance_r();
  Utf8char getchar(Pos p);
  Utf8char getchar(int x, int y);
  Utf8char getchar();
  int token_read(Pos *p, int y_end, Pos *start, Pos *end);

};

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
  Color background;
  int margin;

  void render_strf(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, ...);
  void render(Pos offset);
  void render_str_v(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, va_list args);
  void render_str(Pos p, const Color *text_color, const Color *background_color, int xclip0, int xclip1, String s);
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
  const Color *background_color;
  const Color *highlight_background_color;
  const Color *text_color;
  bool syntax_highlight;
  Buffer *buffer;
  Pos buffer_offset;

  // visual settings
  int gutter_width;
  int margin;

  void render(bool draw_gutter);
  void render_as_menu(int selected);
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
  Color color;

  void reset() {amount = max + cooldown*speed*(max-min);}
  void tick() {
    assert(speed);
    amount -= speed*(max-min)*0.04f;
    color = Color::blend(base_color, popped_color, clamp(amount, min, max));
  }
};

struct RotatingColor {
  float speed;
  float saturation;
  float light;
  float hue; // 0
  Color color;

  void tick() {
    hue = fmodf(hue + speed*0.1f, 360.0f);
    color = Color::from_hsl(hue, saturation, light);
  }
  void jump() {
    hue = fmodf(hue + 180.0f, 360.0f);
  }
};

struct State {

  /* @renderer some rendering state */
  SDL_Window *window;
  int font_width;
  int font_height;
  int line_margin;
  int line_height;
  String tmp_render_buffer;
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

/* returns the logical index located visually at x */
static int from_visual_offset(Array<char> line, int x) {
  int visual = 0;
  int i;

  if (!line) return 0;

  for (i = 0; i < line.size; ++i) {
    if (is_utf8_trail(line[i]))
      continue;
    ++visual;
    if (line[i] == '\t')
      visual += G.tab_width-1;

    if (visual > x)
      return i;
  }

  return i;
}

enum SpecialKey {
  KEY_NONE = 0,
  KEY_UNKNOWN = 257,
  KEY_ESCAPE      = 258,
  KEY_RETURN      = 259,
  KEY_TAB         = 260,
  KEY_BACKSPACE   = 261,
  KEY_ARROW_UP    = 262,
  KEY_ARROW_DOWN  = 263,
  KEY_ARROW_LEFT  = 264,
  KEY_ARROW_RIGHT = 265,
  KEY_END         = 266,
  KEY_HOME        = 267,

  KEY_CONTROL = 1 << 10
};

/* @TOKENIZER */

static void status_message_set(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  G.status_message_buffer.truncate_to_n_lines(1);
  G.status_message_buffer[0].clear();
  G.status_message_buffer[0].formatv(fmt, args);
}

/****** @TOKENIZER ******/

static void tokenizer_push_token(Array<Array<TokenInfo>> *tokens, int x, int y, int token) {
  array_reserve(*tokens, y+1);
  array_push((*tokens)[y], {token, x});
}

static void tokenize(Buffer &b) {
  static Array<char> identifier_buffer;
  int x = 0, y = 0;

  /* reset old tokens */
  for (int i = 0; i < b.tokens.size; ++i)
    array_free(b.tokens[i]);
  array_resize(b.tokens, b.lines.size);
  for (int i = 0; i < b.tokens.size; ++i)
    b.tokens[i] = {};

  for (;;) {
    /* whitespace */
    // TODO: @utf8
    while (isspace(b.getchar(x, y).ansi()))
      if (b.advance(&x, &y))
        return;

    /* TODO: how do we handle comments ? */

    /* identifier */
    char *row = b.lines[y].chars;
    int rowsize = b.lines[y].length;
    if (isalpha(row[x]) || row[x] == '_') {
      char *str;

      array_resize(identifier_buffer, 0);
      /* TODO: predefined keywords */
      tokenizer_push_token(&b.tokens, x, y, TOKEN_IDENTIFIER);

      for (; x < rowsize; ++x) {
        if (!isalnum(row[x]) && row[x] != '_')
          break;
        array_push(identifier_buffer, row[x]);
      }
      array_push(identifier_buffer, '\0');
      /* check if identifier already exists */
      for (int i = 0; i < b.identifiers.size; ++i)
        if (strcmp(identifier_buffer, b.identifiers[i]) == 0)
          goto identifier_done;

      str = (char*)malloc(identifier_buffer.size);
      array_copy(identifier_buffer, str);
      array_push(b.identifiers, (const char*)str);

      identifier_done:;
    }

    /* number */
    else if (isdigit(row[x])) {
      tokenizer_push_token(&b.tokens, x, y, TOKEN_NUMBER);
      for (; x < rowsize && isdigit(row[x]); ++x);
      if (b.getchar(x, y) == '.')
        for (; x < rowsize && isdigit(row[x]); ++x);
    }

    /* single char token */
    else {
      tokenizer_push_token(&b.tokens, x, y, row[x]);
      if (b.advance(&x, &y))
        return;
    }

    if (x == rowsize)
      if (b.advance(&x, &y))
        return;
  }
}

// MUST BE REVERSE SIZE ORDER
static const String operators[] = {
  {(char*)"===", 3},
  {(char*)"!==", 3},
  {(char*)"<<=", 3},
  {(char*)">>=", 3},
  {(char*)"||", 2},
  {(char*)"&&", 2},
  {(char*)"==", 2},
  {(char*)"!=", 2},
  {(char*)"<<", 2},
  {(char*)">>", 2},
  {(char*)"++", 2},
  {(char*)"--", 2},
  {(char*)"+", 1},
  {(char*)"-", 1},
  {(char*)"*", 1},
  {(char*)"&", 1},
  {(char*)"%", 1},
  {(char*)"=", 1},
  {(char*)"<", 1},
  {(char*)">", 1},
};

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
  if (ferror(f))
    goto err;

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
        buffer[i] += c;
      }
    }
    last_line:;
    assert(fgetc(f) == EOF);
  } else {
    array_pushz(buffer.lines);
  }

  tokenize(buffer);

  buffer.guess_tab_type();

  *buffer_out = buffer;
  fclose(f);
  return 0;

  err:
  for (int i = 0; i < buffer.lines.size; ++i)
    buffer[i].free();
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

#define file_write(file, str, len) (fwrite(str, len, 1, file) != 1)
#define file_read(file, buf, n) (fread(buf, n, 1, file) != 1)

static void save_buffer(Buffer *b) {
  FILE* f;
  int i;

  assert(b->filename);

  if (file_open(&f, b->filename, "wb")) {
    status_message_set("Could not open file %s for writing: %s", b->filename, cman_strerror(errno));
    return;
  }

  printf("Opened file %s\n", b->filename);

  /* TODO: actually write to a temporary file, and when we have succeeded, rename it over the old file */
  for (i = 0; i < b->num_lines(); ++i) {
    unsigned int num_to_write = b->lines[i].length;

    if (num_to_write && file_write(f, b->lines[i].chars, num_to_write)) {
      status_message_set("Failed to write to %s: %s", b->filename, cman_strerror(errno));
      goto err;
    }

    /* TODO: windows endlines option */
    if (file_write(f, "\n", 1)) {
      status_message_set("Failed to write to %s: %s", b->filename, cman_strerror(errno));
      goto err;
    }
  }

  status_message_set("Wrote %i lines to %s", b->num_lines(), b->filename);

  b->modified = 0;

  err:
  fclose(f);
}

static void menu_option_save() {
  save_buffer(G.main_pane.buffer);
}

static void menu_option_quit() {
  exit(0);
}

static void menu_option_show_tab_type() {
  if (G.main_pane.buffer->tab_type == 0)
    status_message_set("Tabs is \\t");
  else
    status_message_set("Tabs is %i spaces", G.main_pane.buffer->tab_type);
}

static struct {const char *name; void(*fun)();} menu_options[] = {
  {"quit", menu_option_quit},
  {"save", menu_option_save},
  {"show_tab_type", menu_option_show_tab_type}
};

static void mode_search() {
  G.mode = MODE_SEARCH;
  G.search_begin_pos = G.main_pane.buffer->pos;
  G.search_failed = 0;
  G.bottom_pane.buffer = &G.search_buffer;
  G.search_buffer.empty();
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
  G.menu_buffer.empty();
}

struct FuzzyMatch {
  const char *str;
  float points;
};

static int fuzzy_cmp(const void *aa, const void *bb) {
  const FuzzyMatch *a = (FuzzyMatch*)aa, *b = (FuzzyMatch*)bb;

  if (a->points != b->points)
    return (int)(b->points - a->points + 0.5f);
  return strlen(a->str) - strlen(b->str);
}

// returns number of found matches
static int fuzzy_match(String string, Array<const char*> strings, FuzzyMatch result[], int max_results) {
  int num_results = 0;

  for (int i = 0; i < strings.size; ++i) {
    const char *identifier = strings[i];

    const int test_len = strlen(identifier);
    if (string.length > test_len)
      continue;

    const char *in = string.chars;
    const char *in_end = in + string.length;
    const char *test = identifier;
    const char *test_end = test + test_len;

    float points = 0;
    float gain = 10;

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

    if (num_results < max_results) {
      result[num_results].str = identifier;
      result[num_results].points = points;
      ++num_results;
    } else {
      /* find worst match and replace */
      FuzzyMatch *worst;
      findn_min_by(result, num_results, worst, points);
      worst->str = identifier;
      worst->points = points;
    }
  }

  qsort(result, num_results, sizeof(*result), fuzzy_cmp);
  return num_results;
}

static Pos find_start_of_identifier(Buffer &b) {
  Pos p = b.pos;
  b.advance_r(p);
  while (is_identifier_head(b.getchar(p)))
    if (b.advance_r(p))
      return p;
  // printf("%i %i - ", p.x, p.y);
  b.advance(p);
  if (p.y != b.pos.y)
    p.y = b.pos.y,
    p.x = 0;
  // printf("%i %i - ", p.x, p.y);
  // b.advance(p);
  // printf("%i %i\n", p.x, p.y);
  return p;
}

static void fill_dropdown_buffer(Pane *active_pane, bool grow_upwards) {
  static FuzzyMatch best_matches[DROPDOWN_SIZE];
  Buffer &b = *active_pane->buffer;

  if (!G.dropdown_visible)
    return;

  /* find matching identifiers */
  // go back to start of identifier
  Pos p = find_start_of_identifier(*active_pane->buffer);
  G.dropdown_pos = p;

  String input = b.slice({p.x, b.pos.y}, b.pos.x - p.x);
  printf("%i: %.*s\n", input.length, input.length, input.chars);
  int num_best_matches = fuzzy_match(input, b.identifiers, best_matches, ARRAY_LEN(best_matches));

  const int y = G.dropdown_buffer.pos.y;
  G.dropdown_buffer.empty();
  for (int i = 0; i < num_best_matches; ++i) {
    int which = grow_upwards ? num_best_matches-1-i : i;
    G.dropdown_buffer.push_line(best_matches[which].str);
  }
  G.dropdown_buffer.move_to_y(y);
}

static int dropdown_get_first_line() {
  if (G.dropdown_backwards)
    return G.dropdown_buffer.num_lines()-1;
  else
    return 0;
}

static void dropdown_autocomplete(Buffer &b) {
  if (!G.dropdown_visible || G.dropdown_buffer.isempty())
    return;

  String s = G.dropdown_buffer[G.dropdown_buffer.pos.y];
  b.replace(G.dropdown_pos.x, b.pos.x, b.pos.y, s);
  G.dropdown_visible = false;
}

static void dropdown_update_on_insert(Pane *active_pane, Utf8char input) {
  if (!G.dropdown_visible && is_identifier_head(input)) {
    G.dropdown_visible = true;
    G.dropdown_buffer.pos = {};
  }
  else if (G.dropdown_visible && !is_identifier_tail(input)) {
    G.dropdown_visible = false;
  }
}

#define CONTROL(c) ((c)|KEY_CONTROL)

// TODO: use a unified key instead of this specialkey hack
static void insert_default(Pane *p, SpecialKey special_key, Utf8char input, bool ctrl) {
  Buffer &b = *p->buffer;

  /* TODO: should not set `modified` if we just enter and exit insert mode */
  if (special_key == KEY_ESCAPE) {
    b.pretty_range(G.insert_mode_begin_y, b.pos.y+1);
    mode_normal(true);
    return;
  }

  if (!special_key && !ctrl) {
    dropdown_update_on_insert(p, input);
    b.insert_char(input);
    return;
  }

  int key = special_key ? special_key : input.ansi();
  if (ctrl)
    key |= KEY_CONTROL;
  switch (key) {
    case KEY_RETURN:
      b.insert_newline();
      break;

    case KEY_TAB: {
      if (G.dropdown_visible)
        dropdown_autocomplete(b);
      else
        b.insert_tab();
    } break;

    case KEY_BACKSPACE:
      b.delete_char();
      break;

    case CONTROL('j'):
      puts("moving down");
      ++G.dropdown_buffer.pos.y;
      break;

    case CONTROL('k'):
      puts("moving down");
      --G.dropdown_buffer.pos.y;
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
  {"class", KEYWORD_DECLARATION},
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
  G.default_text_color = COLOR_WHITE;
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
  G.menu_buffer.empty();
  G.search_buffer.empty();
  G.status_message_buffer.empty();
  G.dropdown_buffer.empty();

  // some pane settings
  G.main_pane.syntax_highlight = true;
  G.main_pane.background_color = &G.default_background_color;
  G.main_pane.text_color = &G.default_text_color;
  G.main_pane.highlight_background_color = &G.highlight_background_color.color;

  G.bottom_pane.syntax_highlight = true;
  G.bottom_pane.background_color = &COLOR_GREY;
  G.bottom_pane.buffer = &G.status_message_buffer;
  G.bottom_pane.text_color = &G.default_text_color;
  G.bottom_pane.highlight_background_color = &G.default_highlight_background_color;

  G.dropdown_pane.background_color = &COLOR_DEEP_ORANGE;
  G.dropdown_pane.text_color = &COLOR_BLACK;
  G.dropdown_pane.buffer = &G.dropdown_buffer;
  G.dropdown_pane.margin = 5;
  G.dropdown_pane.highlight_background_color = &COLOR_ORANGE;

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
  if (!G.dropdown_visible)
    return;

  G.dropdown_backwards = active_pane == &G.bottom_pane;
  fill_dropdown_buffer(active_pane, G.dropdown_backwards);

  // resize dropdown pane
  int max_width = 0;
  for (int i = 0; i < G.dropdown_buffer.lines.size; ++i)
    max_width = max(max_width, G.dropdown_buffer.lines[i].length);
  G.dropdown_pane.bounds.size = char2pixel(max_width, G.dropdown_buffer.lines.size-1);
  G.dropdown_pane.bounds.x = clamp(G.dropdown_pane.bounds.x, 0, G.win_width - G.dropdown_pane.bounds.w);
  // add margin
  G.dropdown_pane.bounds.size += {G.dropdown_pane.margin*2, G.dropdown_pane.margin*2};

  // position pane
  Pos p = active_pane->buffer->pos;
  if (G.dropdown_backwards) --p.y;
  else ++p.y;
  p = active_pane->buf2pixel(p);
  if (G.dropdown_backwards)
    p.y -= G.dropdown_pane.bounds.h;
  G.dropdown_pane.bounds.p = p;

  if (G.dropdown_visible && !G.dropdown_buffer.isempty())
    G.dropdown_pane.render_as_menu(0);
}

static void handle_input(Utf8char input, SpecialKey special_key, bool ctrl) {
  // TODO: insert utf8 characters
  if (!input.is_ansi() && !special_key)
    return;

  Buffer &buffer = *G.main_pane.buffer;
  int key = special_key ? special_key : input.ansi();
  if (ctrl)
    key |= KEY_CONTROL;

  switch (G.mode) {
  case MODE_GOTO:
    if (isdigit(key)) {
      G.goto_line_number *= 10;
      G.goto_line_number += input.ansi() - '0';
      buffer.move_to_y(G.goto_line_number-1);
      status_message_set("goto %u", G.goto_line_number);
      break;
    }

    switch (key) {
      case 't':
        buffer.move_to(0, 0);
        break;
      case 'b':
        buffer.move_to(0, buffer.num_lines()-1);
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
      String line;

      if (G.menu_buffer.isempty()) {
        mode_normal(true);
        break;
      }

      if (G.dropdown_visible)
        dropdown_autocomplete(G.menu_buffer);

      line = G.menu_buffer[0];
      foreach(menu_options) {
        if (G.menu_buffer[0] == it->name) {
          it->fun();
          goto done;
        }
      }
      status_message_set("Unknown option '{}'", G.menu_buffer[0]);
      done:

      mode_normal(false);
    }
    else if (special_key == KEY_ESCAPE)
      mode_normal(true);
    else
      insert_default(&G.bottom_pane, special_key, input, ctrl);
    /* FIXME: if backspace so that menu_buffer is empty, exit menu mode */
    break;

  case MODE_SEARCH: {
    String search;

    G.bottom_pane.buffer = &G.search_buffer;
    if (special_key == KEY_ESCAPE)
      dropdown_autocomplete(G.search_buffer);
    else
      insert_default(&G.bottom_pane, special_key, input, ctrl);

    search = G.search_buffer.lines[0];
    G.search_failed = buffer.find_and_move(search, true);

    if (special_key == KEY_RETURN || special_key == KEY_ESCAPE) {
      if (G.dropdown_visible) {
        dropdown_get_first_line();
        G.search_failed = buffer.find_and_move(search, true);
      }
      if (G.search_failed) {
        buffer.pos = G.search_begin_pos;
        status_message_set("'{}' not found", G.search_buffer[0]);
        mode_normal(false);
      } else
        mode_normal(true);
    } else
      G.search_term_background_color.reset();
  } break;

  case MODE_DELETE:
    switch (key) {
      case 'd':
        buffer.delete_line();
        buffer.update();
        mode_normal(true);
        break;

      default:
        mode_normal(true);
        break;
    }
    break;

  case MODE_INSERT:
    insert_default(&G.main_pane, special_key, input, ctrl);
    break;

  case MODE_NORMAL:
    switch (key) {
    case '=':
      if (buffer.lines[buffer.pos.y].length == 0)
        break;
      buffer.modified = true;
      buffer.move_x(buffer.autoindent(buffer.pos.y));
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
      if (buffer.advance_r())
        break;
      // if not in word, back to word
      Utf8char c = buffer.getchar();
      if (!(is_identifier_tail(c) || is_identifier_head(c)))
        while (c = buffer.getchar(), !(is_identifier_tail(c) || is_identifier_head(c)))
          if (buffer.advance_r())
            break;
      // go to beginning of word
      while (c = buffer.getchar(), is_identifier_tail(c) || is_identifier_head(c))
        if (buffer.advance_r())
          break;
      buffer.advance();
      break;}
    case 'w': {
      Utf8char c = buffer.getchar();
      if (is_identifier_tail(c) || is_identifier_head(c))
        while (c = buffer.getchar(), is_identifier_tail(c) || is_identifier_head(c))
          if (buffer.advance())
            break;
      while (c = buffer.getchar(), !is_identifier_head(c))
        if (buffer.advance())
          break;
      break;}
    case 'q':
      if (buffer.modified)
        status_message_set("You have unsaved changes. If you really want to exit, use :quit");
      else
        exit(0);
      break;
    case 'i':
      mode_insert();
      break;
    case 'j':
      buffer.move_y(1);
      break;
    case 'k':
      buffer.move_y(-1);
      break;
    case 'h':
      buffer.advance_r();
      break;
    case 'l':
      buffer.advance();
      break;
    case 'L':
      buffer.goto_endline();
      break;
    case 'H':
      buffer.goto_beginline();
      break;
    case 'n': {
      G.search_term_background_color.reset();
      Pos p = buffer.pos;
      if (buffer.find(G.search_buffer[0], false, &p)) {
        status_message_set("'{}' not found", G.search_buffer[0]);
        break;
      }
      buffer.move_to(p);
      /*jumplist_push(prev);*/
      break;}
    case 'N': {
      G.search_term_background_color.reset();

      int err = buffer.find_r(G.search_buffer[0].chars, G.search_buffer[0].length, 0);
      if (err) {
        status_message_set("'{}' not found", G.search_buffer[0]);
        break;
      }
      /*jumplist_push(prev);*/
      break;}
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
      buffer.insert_newline_below();
      mode_insert();
      break;
    case ':':
      mode_menu();
      break;
    case 'd':
      mode_delete();
      break;
    case 'J':
      buffer.move_y(G.main_pane.numchars_y()/2);
      break;
    case 'K':
      buffer.move_y(-G.main_pane.numchars_y()/2);
      break;
    }
  }
}

Pos Buffer::to_visual_pos(Pos p) {
  p.x = lines[p.y].visual_offset(p.x, G.tab_width);
  return p;
}

void Buffer::move_to_y(int y) {
  y = clamp(y, 0, lines.size-1);
  pos.y = y;
}

void Buffer::move_to_x(int x) {
  x = clamp(x, 0, lines[pos.y].length);
  pos.x = x;
  ghost_x = x;
}

void Buffer::move_to(int x, int y) {
  move_to_y(y);
  move_to_x(x);
}

void Buffer::move_to(Pos p) {
  move_to_y(p.y);
  move_to_x(p.x);
}

void Buffer::move_y(int dy) {
  pos.y = clamp(pos.y + dy, 0, lines.size - 1);

  if (ghost_x == GHOST_EOL)
    pos.x = lines[pos.y].length;
  else if (ghost_x == GHOST_BOL)
    pos.x = begin_of_line(pos.y);
  else
    pos.x = lines[pos.y].visual_offset(ghost_x, G.tab_width);
}

void Buffer::move_x(int dx) {
  int w = lines[pos.y].length;

  if (dx > 0) {
    for (; dx > 0 && pos.x < w; --dx) {
      ++pos.x;
      while (pos.x < w && is_utf8_trail(lines[pos.y][pos.x]))
        ++pos.x;
    }
  }
  if (dx < 0) {
    for (; dx < 0 && pos.x > 0; ++dx) {
      --pos.x;
      while (pos.x > 0 && is_utf8_trail(lines[pos.y][pos.x]))
        --pos.x;
    }
  }
  pos.x = clamp(pos.x, 0, w);
  ghost_x = lines[pos.y].visual_offset(pos.x, G.tab_width);
}

void Buffer::move(int dx, int dy) {
  if (dy)
    move_y(dy);
  if (dx)
    move_x(dx);
}

void Buffer::update() {
  move(0, 0);
}

int Buffer::find_r(char *str, int n, int stay) {
  int x, y;

  if (!n)
    return 1;

  x = pos.x;
  if (!stay)
    --x;
  y = pos.y;

  for (;; --x) {
    char *p;
    if (y < 0)
      return 1;
    if (x < 0) {
      --y;
      if (y < 0)
        return 1;
      x = lines[y].length;
      continue;
    }

    p = (char*)memmem(str, n, lines[y].chars, x);
    if (!p)
      continue;

    x = p - lines[y].chars;
    move_to(x, y);
    return 0;
  }
}

int Buffer::find(String s, bool stay, Pos *pos) {
  int x, y;
  if (!s)
    return 1;

  x = pos->x;
  if (!stay)
    ++x;
  y = pos->y;

  // first line
  if (x < lines[y].length && lines[y].begins_with(x, s)) {
    pos->x = x;
    pos->y = y;
    return 0;
  }

  // following lines
  for (; y < lines.size; ++y) {
    x = lines[y].find(x, s);
    if (x != -1) {
      pos->x = x;
      pos->y = y;
      return 0;
    }
  }
  return 1;
}

int Buffer::find_and_move(String s, bool stay) {
  Pos p = pos;
  if (find(s, stay, &p))
    return 1;
  move_to(p);
  return 0;
}

void Buffer::insert_str(int x, int y, String s) {
  modified = 1;
  lines[y].insert(x, s);
  move_x(s.length);
}

void Buffer::replace(int x0, int x1, int y, String s) {
  modified = 1;
  lines[y].remove(x0, x1-x0);
  move_to(x0, y);
  insert_str(x0, y, s);
}

void Buffer::remove_trailing_whitespace(int y) {
  int x;
  
  x = lines[y].length - 1;
  // TODO: @utf8
  while (x >= 0 && isspace(getchar(x, y).ansi()))
    --x;
  lines[y].length = x+1;
  if (pos.y == y && pos.x > x+1)
    move_to_x(x+1);
}

void Buffer::pretty_range(int y0, int y1) {
  for (; y0 < y1; ++y0) {
    #if 0
      int diff;
      diff = autoindent(y0);
      if (y0 == pos.y)
        move_x(diff);
    #endif
    remove_trailing_whitespace(y0);
  }
}

void Buffer::pretty(int y) {
  pretty_range(y, y+1);
}

void Buffer::insert_char(Utf8char ch) {
  modified = true;
  lines[pos.y].insert(pos.x, ch);
  move_x(1);
  if (ch == '}' || ch == ')' || ch == ']' || ch == '>')
    move_x(autoindent(pos.y));
}

void Buffer::delete_line_at(int y) {
  modified = true;
  lines[y].free();
  if (lines.size == 1)
    return;
  array_remove_slow(lines, y);
}

void Buffer::delete_line() {
  delete_line_at(pos.y);
}

void Buffer::remove_range(Pos a, Pos b) {
  modified = true;
  int y0 = a.y;
  int y1 = b.y;
  int y;

  if (a.y > b.y || (a.y == b.y && a.x > b.x))
    swap(a, b);

  if (y0 == y1) {
    lines[y0].remove(a.x, b.x-a.x);
    return;
  }

  lines[y0].length = a.x;
  ++y0;
  for (y = y0; y < y1; ++y)
    delete_line_at(y0);
  lines[y0].remove(0, b.x);
}

void Buffer::delete_char() {
  modified = 1;
  if (pos.x == 0) {
    if (pos.y == 0)
      return;

    /* move up and right */
    move_y(-1);
    goto_endline();
    lines[pos.y] += lines[pos.y+1];
    delete_line_at(pos.y+1);
  }
  else {
    Pos p = pos;
    move_x(-1);
    remove_range(pos, p);
  }
}

void Buffer::insert_tab() {
  int n;

  n = tab_type;

  modified = 1;
  if (n == 0) {
    lines[pos.y].insert(pos.x, '\t');
    move_x(1);
  }
  else {
    /* TODO: optimize? */
    while (n--)
      lines[pos.y].insert(pos.x, ' ');
    move_x(tab_type);
  }
}

int Buffer::getindent(int y) {
  if (y < 0 || y > lines.size)
    return 0;

  int n = 0;
  int tab_size = tab_type ? tab_type : 1;
  char tab_char = tab_type ? ' ' : '\t';

  for (n = 0;;) {
    if (n >= lines[y].length)
      break;
    if (lines[y][n] != tab_char)
      break;
    ++n;
  }
  return n/tab_size;
}

int Buffer::indentdepth(int y, bool *has_statement) {
  if (has_statement)
    *has_statement = false;
  if (y < 0)
    return 0;

  int depth = 0;
  Pos p = {0,y};

  bool first = true;
  while (1) {
    Pos a,b;
    int t = token_read(&p, y+1, &a, &b);

    if (t == TOKEN_NULL)
      break;

    switch (t) {
      case '{': ++depth; break;
      case '}': --depth; break;
      case '[': ++depth; break;
      case ']': --depth; break;
      case '(': ++depth; break;
      case ')': --depth; break;
      case TOKEN_IDENTIFIER: {
        String s = lines[y](a.x, b.x+1);
        assert(b.y == a.y);
        if (first && (
            s == "for" ||
            s == "if" ||
            s == "while" ||
            s == "else"
            )) {
          if (has_statement)
            *has_statement = true;
        }
      } break;
      default: break;
    }
    first = false;
  }
  return depth;
}

int Buffer::autoindent(const int y) {
  const char tab_char = tab_type ? ' ' : '\t';
  const int tab_size = tab_type ? tab_type : 1;

  int diff = 0;

  const int current_indent = getindent(y);

  int y_above = y-1;
  while (y_above >= 0 && lines[y_above].length == 0)
    --y_above;
  if (y_above == 0) {
    diff = -getindent(y);
    goto done;
  } else {
    /* skip empty lines */
    bool above_is_statement;
    const int above_depth = indentdepth(y_above, &above_is_statement);
    const bool above_is_indenting = (above_depth > 0 || above_is_statement);
    const int above_indent = getindent(y_above);
    int target_indent = above_indent;
    if (above_is_indenting)
      ++target_indent;

    bool this_is_statement;
    int this_depth = indentdepth(y, &this_is_statement);
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
        const int indent = indentdepth(yy, &is_statement);
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
    lines[y].remove(0, at_most(current_indent*tab_size, -diff));
  if (diff > 0) {
    lines[y].insert(0, tab_char, diff);
    for (int i = 0; i < diff; ++i)
      lines[y][i] = tab_char;
  }
  return diff;
}

int Buffer::isempty() {
  return lines.size == 1 && lines[0].length == 0;
}

void Buffer::push_line(const char *str) {
  modified = 1;
  int y = lines.size;
  if (!isempty())
    array_pushz(lines);
  else
    y = 0;
  lines[y] += str;
}

void Buffer::insert_newline() {
  modified = 1;

  array_insertz(lines, pos.y+1);
  lines[pos.y+1] += lines[pos.y] + pos.x;
  lines[pos.y].length = pos.x;

  move_y(1);
  move_to_x(0);
  move_x(autoindent(pos.y));
  pretty(pos.y-1);
}

void Buffer::insert_newline_below() {
  modified = 1;
  array_insertz(lines, pos.y+1);
  move_y(1);
  move_x(autoindent(pos.y));
}

void Buffer::guess_tab_type() {
  int i;
  /* try to figure out tab type */
  /* TODO: use tokens here instead, so we skip comments */
  tab_type = -1;
  for (i = 0; i < lines.size; ++i) {
    if (!lines[i])
      continue;

    /* skip comments */
    if (lines[i].length >= 2 && lines[i][0] == '/' && lines[i][1] == '*') {
      int j;
      j = 2;
      for (;;) {
        if (i >= lines.size) {
          tab_type = G.default_tab_type;
          return;
        }
        if (j >= lines[i].length-1) {
          j = 0;
          ++i;
          continue;
        }
        if (lines[i][j] == '*' && lines[i][j+1] == '/') {
          ++i;
          break;
        }
        ++j;
      }
    }

    if (!lines[i])
      continue;

    if (lines[i][0] == '\t') {
      tab_type = 0;
      break;
    }
    else if (lines[i][0] == ' ') {
      int num_spaces = 0;
      int j;

      for (j = 0; j < lines[i].length && lines[i][j] == ' '; ++j)
        ++num_spaces;

      if (j == lines[i].length)
        continue;

      tab_type = num_spaces;
      break;
    }
    else
      continue;
  }
  if (tab_type == -1)
    tab_type = G.default_tab_type;
}

void Buffer::goto_endline() {
  move_to_x(lines[pos.y].length);
  ghost_x = GHOST_EOL;
}

int Buffer::begin_of_line(int y) {
  int x;

  x = 0;
  while (x < lines[y].length && (isspace(lines[y][x])))
    ++x;
  return x;
}

void Buffer::goto_beginline() {
  int x;

  x = begin_of_line(pos.y);
  move_to_x(x);
  ghost_x = GHOST_BOL;
}

void Buffer::empty() {
  int i;

  modified = 1;

  for (i = 0; i < lines.size; ++i)
    lines[i].free();

  lines.size = 0;
  array_pushz(lines);
  pos.x = pos.y = 0;
}

void Buffer::truncate_to_n_lines(int n) {
  int i;

  modified = 1;
  n = at_least(n, 1);

  if (n >= lines.size)
    return;

  for (i = n; i < lines.size; ++i)
    lines[i].free();
  lines.size = n;
  pos.y = at_most(pos.y, n-1);
}

int Buffer::advance(int *x, int *y) {
  *x += 1;
  if (*x > lines[*y].length) {
    *x = 0;
    *y += 1;
    if (*y >= lines.size) {
      *y = lines.size - 1;
      *x = lines[*y].length;
      return 1;
    }
  }
  return 0;
}

int Buffer::advance(Pos &p) {
  return advance(&p.x, &p.y);
}

int Buffer::advance() {
  int err = advance(&pos.x, &pos.y);
  if (err)
    return err;
  ghost_x = pos.x;
  return 0;
}

int Buffer::advance_r(Pos &p) {
  p.x -= 1;
  if (p.x < 0) {
    p.y -= 1;
    if (p.y < 0) {
      p.y = 0;
      p.x = 0;
      return 1;
    }
    p.x = lines[p.y].length;
  }
  return 0;
}

int Buffer::advance_r() {
  int err = advance_r(pos);
  if (err)
    return err;
  ghost_x = pos.x;
  return 0;
}

// TODO, FIXME: properly implement
Utf8char Buffer::getchar(int x, int y) {
  return Utf8char::create(x >= lines[y].length ? '\n' : lines[y][x]);
}

Utf8char Buffer::getchar(Pos p) {
  return getchar(p.x, p.y);
}

Utf8char Buffer::getchar() {
  return Utf8char::create(pos.x >= lines[pos.y].length ? '\n' : lines[pos.y][pos.x]);
}

int Buffer::token_read(Pos *p, int y_end, Pos *start, Pos *end) {
  int token;
  int x,y;
  x = p->x, y = p->y;

  for (;;) {
    if (y >= y_end || y >= lines.size) {
      token = TOKEN_NULL;
      goto done;
    }

    // endline
    if (x >= lines[y].length) {
      token = TOKEN_EOL;
      if (start)
        *start = {x,y};
      if (end)
        *end = {x,y};
      x = 0;
      ++y;
      goto done;
    }

    char c = lines[y][x];

    if (isspace(c)) {
      ++x;
      continue;
    }

    /* identifier */
    if (is_identifier_head(c)) {
      token = TOKEN_IDENTIFIER;
      if (start)
        *start = {x, y};
      while (x < lines[y].length) {
        if (end)
          *end = {x, y};
        c = lines[y][++x];
        if (!is_identifier_tail(c))
          break;
      }
      goto done;
    }

    /* start of block comment */
    if (c == '/' && x+1 < lines[y].length && lines[y][x+1] == '*') {
      token = TOKEN_BLOCK_COMMENT_BEGIN;
      if (start)
        *start = {x, y};
      if (end)
        *end = {x+1, y};
      x += 2;
      goto done;
    }

    /* end of block comment */
    if (c == '*' && x+1 < lines[y].length && lines[y][x+1] == '/') {
      token = TOKEN_BLOCK_COMMENT_END;
      if (start)
        *start = {x, y};
      if (end)
        *end = {x+1, y};
      x += 2;
      goto done;
    }

    /* line comment */
    if (c == '/' && x+1 < lines[y].length && lines[y][x+1] == '/') {
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
      while (x < lines[y].length) {
        if (end)
          *end = {x, y};
        c = lines[y][++x];
        if (!IS_NUMBER_TAIL(c))
          break;
      }
      if (x == lines[y].length)
        goto done;
      if (c == '.' && x+1 < lines[y].length && isdigit(lines[y][x+1])) {
        c = lines[y][++x];
        while (isdigit(c) && x < lines[y].length) {
          if (end)
            *end = {x, y};
          c = lines[y][++x];
        }
        if (x == lines[y].length)
          goto done;
      }
      while ((c == 'u' || c == 'l' || c == 'L' || c == 'f') && x < lines[y].length) {
        if (end)
          *end = {x, y};
        c = lines[y][++x];
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
        if (x >= lines[y].length) {
          token = TOKEN_STRING_BEGIN;
          if (end)
            *end = {x, y};
          ++y;
          x = 0;
          break;
        }

        c = lines[y][x];
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
      if (lines[y].begins_with(x, operators[i])) {
        token = TOKEN_OPERATOR;
        if (start)
          *start = {x,y};
        if (end)
          *end = {x+operators[i].length-1, y};
        x += operators[i].length;
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

void Pane::render_as_menu(int selected) {
  Buffer &b = *buffer;

  Canvas canvas;
  canvas.init(this->numchars_x(), this->numchars_y());
  canvas.background = *this->background_color;
  canvas.fill(Utf8char{' '});
  canvas.fill(Style{*this->text_color, *this->background_color});
  canvas.margin = this->margin;

  // draw each line 
  for (int y = 0; y < b.lines.size; ++y) {
    canvas.render_str({0, y}, this->text_color, 0, 0, -1, b.lines[y]);
  }

  // highlight the line you're on
  canvas.fill_background({0, b.pos.y, {-1, 1}}, *highlight_background_color);

  canvas.render(this->bounds.p);

  canvas.free();
  render_quads();
  render_text();

}

void Pane::render(bool draw_gutter) {
  Buffer &b = *buffer;
  // calc bounds 
  int buf_y0 = this->calc_top_visible_row();
  int buf_y1 = at_most(buf_y0 + this->numchars_y(), b.lines.size);

  if (draw_gutter)
    this->gutter_width = at_least(calc_num_chars(buf_y1) + 3, 6);
  else
    this->gutter_width = 0;

  int buf_x0 = this->calc_left_visible_column();

  Canvas canvas;
  canvas.init(this->numchars_x(), this->numchars_y());
  canvas.fill(Utf8char{' '});
  canvas.fill(Style{*this->text_color, *this->background_color});

  // draw each line 
  for (int y = 0, buf_y = buf_y0; buf_y < buf_y1; ++buf_y, ++y) {
    // gutter 
    canvas.render_strf({0, y}, &G.default_gutter_style.text_color, &G.default_gutter_style.background_color, 0, this->gutter_width, " %i", buf_y+1);
    // text 
    canvas.render_str(buf2char({buf_x0, buf_y}), this->text_color, 0, 0, -1, b.lines[buf_y]);
  }

  // syntax @highlighting
  if (this->syntax_highlight) {
    Pos pos = {0, buf_y0};
    for (;;) {
      Pos prev, next;

      int token = b.token_read(&pos, buf_y1, &prev, &next);

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
          next = {b.lines[prev.y].length-1, prev.y};
          highlighted_text_color = G.comment_color;
          break;
        }

        case TOKEN_BLOCK_COMMENT_BEGIN: {
          do_render = true;
          Pos start = prev;
          while (token != TOKEN_BLOCK_COMMENT_END && token != TOKEN_NULL)
            token = b.token_read(&pos, buf_y1, &prev, &next);
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
            if (b[prev.y].begins_with(prev.x, keywords[i].name)) {
              highlighted_text_color = keyword_colors[keywords[i].type];
              goto done;
            }
          }

          // otherwise check for functions
          // we assume something not indented and followed by a '(' is a function
          // TODO: @utf8
          if (isspace(b.getchar(0, prev.y).ansi()))
            break;
          Pos prev_tmp, next_tmp;
          token = b.token_read(&pos, buf_y1, &prev_tmp, &next_tmp);
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
        prev = b.to_visual_pos(prev);
        next = b.to_visual_pos(next);

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

  // highlight the line you're on
  if (G.selected_pane == this)
    canvas.fill_background({0, buf2char(b.pos).y, {-1, 1}}, *this->highlight_background_color);

  // if there is a search term, highlight that as well
  if (G.selected_pane == this && G.search_buffer.lines[0].length > 0) {
    Pos pos = {0, buf_y0};
    while (!b.find(G.search_buffer.lines[0], false, &pos) && pos.y < buf_y1) {
      canvas.fill_background({this->buf2char(pos), G.search_buffer.lines[0].length, 1}, G.search_term_background_color.color);
      // canvas.fill_textcolor(this->gutter_width + x0, pos.y - buf_y0, x1-x0, 1, G.search_term_text_color);
    }
  }

  // draw marker
  if (G.selected_pane == this) {
    canvas.fill_background({buf2char(b.pos), {1, 1}}, G.marker_background_color.color);
    // canvas.invert_color(this->gutter_width + pos.x - buf_x0, b.pos.y - buf_y0);
  }

  canvas.render(this->bounds.p);

  canvas.free();
  render_quads();
  render_text();
}

Pos Pane::buf2pixel(Pos p) const {
  p = buffer->to_visual_pos(p);
  p.x -= this->calc_left_visible_column();
  p.y -= this->calc_top_visible_row();
  p.x += this->gutter_width;
  p = char2pixel(p) + this->bounds.p;
  return p;
}

Pos Pane::buf2char(Pos p) const {
  p = buffer->to_visual_pos(p);
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
  x = this->buffer->lines[this->buffer->pos.y].visual_offset(x, G.tab_width);
  x -= (this->numchars_x() - this->gutter_width)*6/7;
  return at_least(x, 0);
}

// fills a to b but only inside the bounds 
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

void Canvas::render_str(Pos p, const Color *text_color, const Color *background_color, int xclip0, int xclip1, String s) {
  if (!s)
    return;

  if (xclip1 == -1)
    xclip1 = this->w;

  Utf8char *row = &this->chars[p.y*w];
  Style *style_row = &this->styles[p.y*w];

  for (Utf8char c : s) {
    if (c == '\t') {
      for (int i = 0; i < G.tab_width; ++i, ++p.x)
        if (p.x >= xclip0 && p.x < xclip1) {
          row[p.x] = ' ';
          if (text_color)
            style_row[p.x].text_color = *text_color;
          if (background_color)
            style_row[p.x].background_color = *background_color;
        }
    }
    else {
      if (p.x >= xclip0 && p.x < xclip1) {
        row[p.x] = c;
        if (text_color)
          style_row[p.x].text_color = *text_color;
        if (background_color)
          style_row[p.x].background_color = *background_color;
      }
      ++p.x;
    }
  }
}

void Canvas::render_str_v(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, va_list args) {
  if (p.x >= w)
    return;
  G.tmp_render_buffer.clear();
  G.tmp_render_buffer.formatv(fmt, args);
  render_str(p, text_color, background_color, x0, x1, G.tmp_render_buffer);
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

  Pos size = char2pixel(w,h) + Pos{2*margin, 2*margin};
  // render base background
  push_square_quad((float)offset.x, (float)offset.x+size.x, (float)offset.y, (float)offset.y+size.y, background);
  offset.x += margin;
  offset.y += margin;

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
  for (int row = 0; row < h; ++row) {
    G.tmp_render_buffer.clear();
    G.tmp_render_buffer.append(&chars[row*w], w);
    int y = char2pixely(row+1) + text_offset_y + offset.y;
    for (int x0 = 0, x1 = 1; x1 <= w; ++x1) {
      if (styles[row*w + x1].text_color == styles[row*w + x0].text_color && x1 < w)
        continue;
      int x = char2pixelx(x0) + offset.x;
      push_textn(G.tmp_render_buffer.chars + x0, x1 - x0, x, y, false, styles[row*w + x0].text_color);
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
  return (this->bounds.w - 2*this->margin) / G.font_width + 1;
}

int Pane::numchars_y() const {
  return (this->bounds.h - 2*this->margin) / G.line_height + 1;
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
int wmain(int argc, const wchar_t *argv[], wchar_t *[])
#else
int main(int argc, const char *argv[])
#endif
{
  state_init();

  /* open a buffer */
  {
    const char *filename = argc >= 2 ? argv[1] : "src/cmantic.cpp";
    G.main_pane.buffer = (Buffer*)malloc(sizeof(*G.main_pane.buffer));
    int err = buffer_from_file(filename, G.main_pane.buffer);
    if (err) {
      fprintf(stderr, "Could not open file %s: %s\n", filename, cman_strerror(errno));
      exit(1);
    }
    if (!err) {
      status_message_set("loaded '%s', %i lines", (char*)filename, (int)G.main_pane.buffer->lines.size);
    }
  }

  for (;;) {

    Utf8char input = {};
    SpecialKey special_key = KEY_NONE;
    bool ctrl = false;
    for (SDL_Event event; SDL_PollEvent(&event);) {

      switch (event.type) {
      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_CLOSE)
          return 1;
        break;

      case SDL_KEYDOWN:
        if (event.key.keysym.mod & KMOD_CTRL)
          ctrl = true;

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
        case SDLK_a:
        case SDLK_b:
        case SDLK_c:
        case SDLK_d:
        case SDLK_e:
        case SDLK_f:
        case SDLK_g:
        case SDLK_h:
        case SDLK_i:
        case SDLK_j:
        case SDLK_k:
        case SDLK_l:
        case SDLK_m:
        case SDLK_n:
        case SDLK_o:
        case SDLK_p:
        case SDLK_q:
        case SDLK_r:
        case SDLK_s:
        case SDLK_t:
        case SDLK_u:
        case SDLK_v:
        case SDLK_w:
        case SDLK_x:
        case SDLK_y:
        case SDLK_z:
          input = (event.key.keysym.mod & KMOD_SHIFT) ? toupper(event.key.keysym.sym) : event.key.keysym.sym;
          break;
        }
        break;

      case SDL_TEXTINPUT:
        // ignore weird characters
        if (strlen(event.text.text) > sizeof(input))
          break;
        input = event.text.text;
        break;
      }
    }

    // handle input
    if (input.code || special_key)
      handle_input(input, special_key, ctrl);

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
    G.marker_background_color.popped_color = G.default_marker_background_color.color;

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

