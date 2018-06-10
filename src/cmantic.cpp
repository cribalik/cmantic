/*
 * TODO:
 *   implement Pane::render_as_menu to list fuzzy matches
 *   File tree
 *
 * Update identifiers as you type
 *       When you make a change, go backwards to check if it was an
 *       identifier, and update the identifier list.
 *       To do this fast, have a hashmap of refcounts for each identifier
 *       if identifier disappears, remove from autocomplete list
 *
 * Undo
 * Folding
 * Multiuser editing
 * Pane stack
 * TODO stack
 * copy-paste
 * Jumplist
 * Do DFS on autocompletion.
 *
 * load files
 * Fuzzy file finding
 * movement (parentheses, block, etc.)
 * actions (Delete, Yank, ...)
 * SDL doesn't handle caps-escape swapping correctly on Windows.. We probably need to use VK directly for windows
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

#include "graphics.h"
#include "util.h"

// @debug
  #if 0
    #define DEBUG
    #define IF_DEBUG(stmt) stmt
    // replace malloc
    #if 0
    static void* debug_malloc(unsigned long size, int line, const char *file) {
      struct Header {
        int line;
        const char *file;
      };

      struct Header *p = (Header*)malloc(sizeof(struct Header) + size);
      p->line = line;
      p->file = file;
      printf("%s:%i malloc %lu\n", file, line, size);
      return (char*)p + sizeof(struct Header);
    }
    #define malloc(size) debug_malloc(size, __LINE__, __FILE__)
    #endif
  #else
    #define IF_DEBUG(stmt)
  #endif

enum Mode {
  MODE_NORMAL,
  MODE_INSERT,
  MODE_MENU,
  MODE_DELETE,
  MODE_GOTO,
  MODE_SEARCH,
  MODE_FILESEARCH,
  MODE_COUNT
};

bool operator==(Utf8char uc, char c) {
  return !(uc.code & 0xFF00) && (uc.code & 0xFF) == (u32)c;
}

bool operator==(char c, Utf8char uc) {
  return uc == c;
}

enum Token {
  TOKEN_NULL                =  0,
  TOKEN_IDENTIFIER          = -2,
  TOKEN_NUMBER              = -3,
  TOKEN_STRING              = -4,
  TOKEN_EOL                 = -5,
  TOKEN_STRING_BEGIN        = -6,
  TOKEN_BLOCK_COMMENT       = -7,
  TOKEN_BLOCK_COMMENT_BEGIN = -8,
  TOKEN_BLOCK_COMMENT_END   = -9,
  TOKEN_LINE_COMMENT_BEGIN  = -10,
  TOKEN_OPERATOR            = -11,
};

struct TokenInfo {
  int token;
  int x;
};

void util_free(TokenInfo) {}

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

union Marker {
  struct {
    int x,y;
  };
  struct {
    Pos pos;
    /* if going from a longer line to a shorter line, remember where we were before clamping to row length. a value of -1 means always go to end of line */
    int ghost_x;
  };
};

void util_free(Marker) {}

struct Buffer {
  String filename;

  Array<StringBuffer> lines;
  int tab_type; /* 0 for tabs, 1+ for spaces */

  /* parser stuff */
  Array<Array<TokenInfo>> tokens;
  Array<String> identifiers;

  #define GHOST_EOL -1
  #define GHOST_BOL -2
  Array<Marker> cursors;
  int modified;

  // methods

  StringBuffer& operator[](int i) {return lines[i];}

  const StringBuffer& operator[](int i) const {return lines[i];}

  int num_lines() const {return lines.size;}

  Slice slice(Pos p, int len) {return lines[p.y](p.x,p.x+len); }
  Pos to_visual_pos(Pos p);
  void move_to_y(int marker_idx, int y);
  void move_to_x(int marker_idx, int x);
  void move_to(int marker_idx, int x, int y);
  void move_to(int marker_idx, Pos p);
  void move_y(int marker_idx, int dy);
  void move_x(int marker_idx, int dx);
  void move(int marker_idx, int dx, int dy);
  void update();
  bool find_r(StringBuffer s, int stay, Pos *pos);
  bool find_r(char c, int stay, Pos *pos);
  bool find(Slice s, bool stay, Pos *pos);
  bool find(char c, bool stay, Pos *pos);
  bool find_and_move(Slice s, bool stay);
  bool find_and_move(char c, bool stay);
  bool find_and_move_r(StringBuffer s, bool stay);
  bool find_and_move_r(char c, bool stay);
  bool find_start_of_identifier(Pos p, Pos *pout);
  void insert(Slice s);
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
  void push_line(Slice s);
  void insert_newline();
  void insert_newline_below();
  void guess_tab_type();
  void goto_endline();
  void goto_endline(int marker_idx);
  int begin_of_line(int y);
  void goto_beginline();
  void empty();
  void truncate_to_n_lines(int n);
  int advance(int *x, int *y) const;
  int advance(Pos &p) const;
  int advance(int marker_idx);
  int advance_r(Pos &p) const;
  int advance_r(int marker_idx);
  Utf8char getchar(Pos p);
  Utf8char getchar(int x, int y);
  Utf8char getchar(int marker_idx);

  struct TokenResult {
    int tok;
    Pos a;
    Pos b;
  };
  TokenResult token_read(Pos *p, int y_end);

  static Buffer* from_file(Slice filename);
};

void util_free(Buffer &b) {
  util_free(b.lines);
  util_free(b.filename);
  util_free(b.tokens);
  util_free(b.identifiers);
  util_free(b.cursors);
}

struct v2 {
  int x;
  int y;
};

struct Range {
  Pos a;
  Pos b;
};
union Rect {
  struct {
    Pos p;
    Pos size;
  };
  struct {
    int x;
    int y;
    int w;
    int h;
  };
};

struct Canvas {
  Utf8char *chars;
  Color *background_colors;
  Color *text_colors;
  int w, h;
  Color background;
  int margin;
  bool draw_shadow;

  void render_strf(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, ...);
  void render(Pos offset);
  void render_str_v(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, va_list args);
  void render_str(Pos p, const Color *text_color, const Color *background_color, int xclip0, int xclip1, Slice s);
  void fill(Color text, Color background);
  void fill_background(Rect r, Color c);
  void fill_textcolor(Rect r, Color c);
  void fill_textcolor(Range range, Rect bounds, Color c);
  void invert_color(Pos p);
  void fill(Utf8char c);
  void resize(int w, int h);
  void init(int w, int h);
};

void util_free(Canvas &c) {
  delete [] c.chars;
  delete [] c.background_colors;
  delete [] c.text_colors;
}

enum PaneType {
  PANETYPE_NULL,
  PANETYPE_EDIT,
  PANETYPE_DROPDOWN,
  PANETYPE_MENU,
};

struct MenuOption {
  Slice name;
  Slice description;
};

struct Pane {
  PaneType type;

  Rect bounds;
  const Color *background_color;
  const Color *highlight_background_color;
  const Color *text_color;
  bool syntax_highlight;
  Buffer *buffer; // TODO: move to PANETYPE_EDIT specific data
  int _gutter_width;

  // visual settings
  int margin;

  // type-specific data
  union {
    // PANETYPE_EDIT
    struct {
    };

    // PANETYPE_MENU
    struct {
      String prefix;
      Array<MenuOption> options;
    } menu;

    // PANETYPE_DROPDOWN
    struct {
    };
  };

  // methods
  void render();

  // internal methods
  void render_as_edit();
  void render_as_dropdown();
  void render_as_menu();
  void render_syntax_highlight(Canvas &canvas, int x0, int y0, int y1);
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

void util_free(Pane &p) {
  switch (p.type) {
  case PANETYPE_MENU:
    util_free(p.menu.prefix);
    break;
  default:
    break;
  }
}

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
  void tick(float dt) {
    assert(speed);
    amount -= dt*speed*(max-min)*0.04f;
    color = Color::blend(base_color, popped_color, clamp(amount, min, max));
  }
};

struct RotatingColor {
  float speed;
  float saturation;
  float light;
  float hue; // 0
  Color color;

  void tick(float dt) {
    hue = fmodf(hue + dt*speed*0.1f, 360.0f);
    color = Color::from_hsl(hue, saturation, light);
  }
  void jump() {
    hue = fmodf(hue + 180.0f, 360.0f);
  }
};

struct FileNode {
  FileType type;
  Path path;
  Array<FileNode> children;
};

struct State {
  /* @renderer rendering state */
  SDL_Window *window;
  int font_width;
  int font_height;
  int line_margin;
  int line_height;
  StringBuffer tmp_render_buffer;
  int win_height, win_width;

  /* settings */
  Color default_background_color;
  Color default_text_color;
  Color default_gutter_text_color;
  Color default_gutter_background_color;
  Color default_highlight_background_color;
  Color number_color;
  Color comment_color;
  Color string_color;
  Color operator_color;
  Color default_keyword_color;
  Color identifier_color;
  Color default_search_term_text_color;
  Color search_term_text_color;
  Color marker_inactive_color;
  PoppedColor marker_background_color;
  PoppedColor search_term_background_color;
  PoppedColor highlight_background_color;
  RotatingColor default_marker_background_color;
  PoppedColor bottom_pane_highlight;

  /* editor state */
  Pane *center_pane;
  Pane *bottom_pane;
  Pane *selected_pane;

  Pane main_pane;
  Pane search_pane;
  Buffer search_buffer;
  Pane menu_pane;
  Buffer menu_buffer;
  Pane status_message_pane;
  Buffer status_message_buffer;
  Pane dropdown_pane;
  Buffer dropdown_buffer;
  Pane filetree_pane;
  Buffer filetree_buffer;
  Buffer null_buffer;

  Array<Buffer*> buffers;

  Mode mode;

  struct {
    bool cursor_dirty;
  } flags;

  /* goto state */
  unsigned int goto_line_number; /* unsigned in order to prevent undefined behavior on wrap around */

  /* search state */
  int search_failed;
  Pos search_begin_pos;

  /* file tree state */
  Array<Path> files;

  /* some settings */
  int tab_width; /* how wide are tabs when rendered */
  int default_tab_type; /* 0 for tabs, 1+ for spaces */
  #define DROPDOWN_SIZE 7

};

State G;

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
  G.status_message_buffer[0].appendv(fmt, args);
}

/****** @TOKENIZER ******/

static void tokenize(Buffer &b) {
  static StringBuffer identifier_buffer;
  int x = 0, y = 0;

  /* reset old tokens */
  for (int i = 0; i < b.tokens.size; ++i)
    util_free(b.tokens[i]);
  b.tokens.resize(b.lines.size);
  for (int i = 0; i < b.tokens.size; ++i)
    b.tokens[i] = {};

  for (;;) {
    /* whitespace */
    // TODO: @utf8
    while (b.getchar(x, y).isspace())
      if (b.advance(&x, &y))
        return;

    /* TODO: how do we handle comments ? */

    /* identifier */
    char *row = b.lines[y].chars;
    int rowsize = b.lines[y].length;
    if (is_identifier_head(row[x])) {
      identifier_buffer.length = 0;
      /* TODO: predefined keywords */
      b.tokens[y].push({TOKEN_IDENTIFIER, x});

      identifier_buffer += row[x];
      for (++x; x < rowsize; ++x) {
        if (!is_identifier_tail(row[x]))
          break;
        identifier_buffer += row[x];
      }
      /* check if identifier already exists */
      for (int i = 0; i < b.identifiers.size; ++i)
        if (identifier_buffer.str() == b.identifiers[i].str())
          goto identifier_done;

      b.identifiers.push(identifier_buffer.copy());

      identifier_done:;
    }

    /* number */
    else if (isdigit(row[x])) {
      b.tokens[y].push({x, TOKEN_NUMBER});
      for (; x < rowsize && isdigit(row[x]); ++x);
      if (b.getchar(x, y) == '.')
        for (; x < rowsize && isdigit(row[x]); ++x);
    }

    /* single char token */
    else {
      b.tokens[y].push({x, row[x]});
      if (b.advance(&x, &y))
        return;
    }

    if (x == rowsize)
      if (b.advance(&x, &y))
        return;
  }
}

// MUST BE REVERSE SIZE ORDER
static const Slice operators[] = {
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

  assert(b->filename.length);

  if (file_open(&f, b->filename.chars, "wb")) {
    status_message_set("Could not open file %s for writing: %s", b->filename, cman_strerror(errno));
    return;
  }

  printf("Opened file %s\n", b->filename.chars);

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
  save_buffer(G.center_pane->buffer);
}

static void menu_option_quit() {
  exit(0);
}

static void menu_option_show_tab_type() {
  if (G.center_pane->buffer->tab_type == 0)
    status_message_set("Tabs is \\t");
  else
    status_message_set("Tabs is %i spaces", G.center_pane->buffer->tab_type);
}

static struct {MenuOption opt; void(*fun)();} menu_options[] = {
  {
    Slice::create("quit"),
    Slice::create("Quit the program"),
    menu_option_quit
  },
  {
    Slice::create("save"),
    Slice::create("Save file"),
    menu_option_save
  },
  {
    Slice::create("show_tab_type"),
    Slice::create("Show if tab type is spaces or tab"),
    menu_option_show_tab_type
  }
};

static void mode_cleanup() {
  G.flags.cursor_dirty = true;

  if (G.mode == MODE_FILESEARCH)
    G.filetree_buffer.empty();
}

static void mode_search() {
  mode_cleanup();

  G.center_pane->buffer->cursors.size = 1;

  G.mode = MODE_SEARCH;
  G.bottom_pane_highlight.reset();
  G.selected_pane = &G.search_pane;
  G.search_begin_pos = G.center_pane->buffer->cursors[0].pos;
  G.search_failed = 0;
  G.bottom_pane = &G.search_pane;
  puts("Using searc pane");
  G.search_buffer.empty();
}

static void mode_filesearch() {
  mode_cleanup();
  G.mode = MODE_FILESEARCH;
  G.bottom_pane = &G.filetree_pane;
}

static void mode_goto() {
  mode_cleanup();
  G.mode = MODE_GOTO;
  G.goto_line_number = 0;
  G.bottom_pane = &G.status_message_pane;
  status_message_set("goto");
}

static void mode_delete() {
  mode_cleanup();
  G.mode = MODE_DELETE;
  G.bottom_pane = &G.status_message_pane;
  status_message_set("delete");
}

static void mode_normal(bool set_message) {
  mode_cleanup();
  if (G.mode == MODE_INSERT)
    G.default_marker_background_color.jump();

  G.mode = MODE_NORMAL;
  G.bottom_pane = &G.status_message_pane;
  G.selected_pane = &G.main_pane;

  if (set_message)
    status_message_set("normal");
}

static void mode_insert() {
  mode_cleanup();
  G.default_marker_background_color.jump();

  G.mode = MODE_INSERT;
  G.bottom_pane = &G.status_message_pane;
  status_message_set("insert");
}

static void mode_menu() {
  mode_cleanup();
  G.mode = MODE_MENU;
  G.selected_pane = &G.menu_pane;
  G.bottom_pane_highlight.reset();
  G.bottom_pane = &G.menu_pane;
  G.menu_buffer.empty();
}

struct FuzzyMatch {
  Slice str;
  float points;
};

static int fuzzy_cmp(const void *aa, const void *bb) {
  const FuzzyMatch *a = (FuzzyMatch*)aa, *b = (FuzzyMatch*)bb;

  if (a->points != b->points)
    return (int)(b->points - a->points + 0.5f);
  return a->str.length - b->str.length;
}

// returns number of found matches
static int fuzzy_match(Slice string, View<Slice> strings, View<FuzzyMatch> result) {
  int num_results = 0;

  for (int i = 0; i < strings.size; ++i) {
    Slice identifier = strings[i];

    const int test_len = identifier.length;
    if (string.length > test_len)
      continue;

    const char *in = string.chars;
    const char *in_end = in + string.length;
    const char *test = identifier.chars;
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

    if (num_results < result.size) {
      result[num_results].str = identifier;
      result[num_results].points = points;
      ++num_results;
    } else {
      /* find worst match and replace */
      FuzzyMatch &worst = result[ARRAY_MIN_BY(result, points)];
      if (points > worst.points) {
        worst.str = identifier;
        worst.points = points;
      }
    }
  }

  qsort(result.items, num_results, sizeof(result[0]), fuzzy_cmp);
  return num_results;
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

bool Buffer::find_start_of_identifier(Pos pos, Pos *pout) {
  *pout = pos;
  advance_r(*pout);
  Utf8char c = getchar(*pout);

  if (!is_identifier_tail(c) && !is_identifier_head(c))
    return false;

  while (is_identifier_tail(getchar(*pout)))
    if (advance_r(*pout))
      return true;
  if (!is_identifier_head(getchar(*pout)))
    advance(*pout);
  if (pout->y != pos.y)
    pout->y = pos.y,
    pout->x = 0;
  return true;
}

static bool dropdown_autocomplete(Buffer &b) {
  if (G.dropdown_buffer.isempty() || G.dropdown_buffer.cursors.size > 1)
    return false;

  Pos start;
  if (!b.find_start_of_identifier(b.cursors[0].pos, &start))
    return false;
  for (int i = b.cursors[0].x - start.x; i; --i)
    b.delete_char();
  b.insert(G.dropdown_buffer[G.dropdown_buffer.cursors[0].y].slice);
  return true;
}

static void render_dropdown(Pane *pane) {
  Buffer &b = *pane->buffer;

  // don't show dropdown unless in edit mode
  if (pane->type == PANETYPE_EDIT && G.mode != MODE_INSERT)
    return;

  // we have to be on an identifier
  Pos identifier_start;
  if (!b.find_start_of_identifier(b.cursors[0].pos, &identifier_start))
    return;

  // since fuzzy matching is expensive, we only update we moved since last time
  if (G.flags.cursor_dirty) {
    Slice identifier = b.slice(identifier_start, b.cursors[0].x - identifier_start.x);
    StackArray<FuzzyMatch, DROPDOWN_SIZE> best_matches;
    View<Slice> input = VIEW(b.identifiers, slice);
    best_matches.size = fuzzy_match(identifier, input, view(best_matches));

    G.dropdown_buffer.empty();
    for (int i = 0; i < best_matches.size; ++i)
      G.dropdown_buffer.push_line(best_matches[i].str);
  }

  if (G.dropdown_buffer.isempty())
    return;

  // resize dropdown pane
  int max_width = ARRAY_MAXVAL_BY(G.dropdown_buffer.lines, length);

  G.dropdown_pane.bounds.size =
    char2pixel(max_width, G.dropdown_buffer.lines.size-1) + Pos{G.dropdown_pane.margin*2, G.dropdown_pane.margin*2};
  G.dropdown_pane.bounds.h = at_most(G.dropdown_pane.bounds.h, G.win_height - 10*G.line_height);

  // position pane
  Pos p = pane->buf2pixel(identifier_start);
  p.y += G.line_height;
  G.dropdown_pane.bounds.p = p;

  // move it up if it would go outside the screen
  if (G.dropdown_pane.bounds.y + G.dropdown_pane.bounds.h > G.win_height)
    G.dropdown_pane.bounds.y -= G.dropdown_pane.bounds.h + 2*G.line_height;

  G.dropdown_pane.bounds.x = clamp(G.dropdown_pane.bounds.x, 0, G.win_width - G.dropdown_pane.bounds.w);

  G.dropdown_pane.render();
}

#define CONTROL(c) ((c)|KEY_CONTROL)

// TODO: use a unified key instead of this specialkey hack
static void insert_default(Pane *p, SpecialKey special_key, Utf8char input, bool ctrl) {
  Buffer &b = *p->buffer;

  /* TODO: should not set `modified` if we just enter and exit insert mode */
  if (special_key == KEY_ESCAPE) {
    mode_normal(true);
    return;
  }

  if (!special_key && !ctrl) {
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

    case KEY_TAB:
      if (dropdown_autocomplete(b))
        break;
      b.insert_tab();
      break;

    case KEY_BACKSPACE:
      b.delete_char();
      break;

    case CONTROL('j'): {
      // this move should not dirty cursor
      int f = G.flags.cursor_dirty;
      G.dropdown_buffer.move_y(0, 1);
      G.flags.cursor_dirty = f;
      break;}

    case CONTROL('k'): {
      // this move should not dirty cursor
      int f = G.flags.cursor_dirty;
      G.dropdown_buffer.move_y(0, -1);
      G.flags.cursor_dirty = f;
      break;}

    default:
      break;
  }
}

static const char *ttf_file = "font.ttf";

static const Color COLOR_PINK = {236, 64, 122, 255};
static const Color COLOR_YELLOW = {255, 235, 59, 255};
static const Color COLOR_AMBER = {255,193,7, 255};
static const Color COLOR_DEEP_ORANGE = {255,138,101, 255};
static const Color COLOR_ORANGE = {255,183,77, 255};
static const Color COLOR_GREEN = {129,199,132, 255};
static const Color COLOR_LIGHT_GREEN = {174,213,129, 255};
static const Color COLOR_INDIGO = {121,134,203, 255};
static const Color COLOR_DEEP_PURPLE = {149,117,205, 255};
static const Color COLOR_RED = {229,115,115, 255};
static const Color COLOR_CYAN = {77,208,225, 255};
static const Color COLOR_LIGHT_BLUE = {79,195,247, 255};
static const Color COLOR_PURPLE = {186,104,200, 255};
static const Color COLOR_BLUEGREY = {84, 110, 122, 255};
static const Color COLOR_GREY = {51, 51, 51, 255};
static const Color COLOR_LIGHT_GREY = {76, 76, 76, 255};
static const Color COLOR_BLACK = {25, 25, 25, 255};
static const Color COLOR_WHITE = {230, 230, 230, 255};
static const Color COLOR_BLUE = {79,195,247, 255};
static const Color COLOR_DARK_BLUE = {124, 173, 213, 255};

enum KeywordType {
  KEYWORD_NONE,
  KEYWORD_CONTROL, // control flow
  KEYWORD_TYPE,
  KEYWORD_SPECIFIER,
  KEYWORD_DECLARATION,
  KEYWORD_FUNCTION,
  KEYWORD_MACRO,
  KEYWORD_CONSTANT,
  KEYWORD_COUNT
};

Color keyword_colors[KEYWORD_COUNT];

struct Keyword {
  const char *name;
  KeywordType type;
};
static Keyword keywords[] = {

  // constants

  {"true", KEYWORD_CONSTANT},
  {"false", KEYWORD_CONSTANT},
  {"NULL", KEYWORD_CONSTANT},
  {"delete", KEYWORD_CONSTANT},
  {"new", KEYWORD_CONSTANT},

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
  {"operator", KEYWORD_DECLARATION},

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

static void _filetree_fill(Path &path) {
  Array<StringBuffer> files = {};
  if (!File::list_files(path, &files))
    goto err;

  #if 1
  for (StringBuffer f : files) {
    path.push(f);
    if (File::filetype(path) == FILETYPE_DIR)
      _filetree_fill(path);
    else {
      String name = path.name().copy();
      G.filetree_buffer.identifiers += name;
    }
    path.pop();
  }
  #endif

  err:
  util_free(files);
}

static void filetree_init() {
  G.filetree_buffer.empty();

  Path cwd = {};
  if (File::cwd(&cwd))
    _filetree_fill(cwd);
  util_free(cwd);
}

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
  G.default_background_color = {41, 41, 41, 255};
  G.default_gutter_text_color = {127, 127, 127, 255};
  G.default_gutter_background_color = G.default_background_color;
  G.number_color = COLOR_RED;
  G.string_color =                          COLOR_RED;
  G.operator_color =                        COLOR_RED;
  G.comment_color = COLOR_BLUEGREY;
  G.identifier_color = COLOR_GREEN;
  G.default_search_term_text_color = G.search_term_text_color = COLOR_WHITE;
  G.marker_inactive_color = COLOR_LIGHT_GREY;

  keyword_colors[KEYWORD_NONE]        = {};
  keyword_colors[KEYWORD_CONTROL]     = COLOR_DARK_BLUE;
  keyword_colors[KEYWORD_TYPE]        = COLOR_DARK_BLUE;
  keyword_colors[KEYWORD_SPECIFIER]   = COLOR_DARK_BLUE;
  keyword_colors[KEYWORD_DECLARATION] = COLOR_PINK;
  keyword_colors[KEYWORD_FUNCTION]    = COLOR_BLUE;
  keyword_colors[KEYWORD_MACRO]       = COLOR_DEEP_ORANGE;
  keyword_colors[KEYWORD_CONSTANT]    = G.number_color;

  G.highlight_background_color.base_color = G.default_background_color;
  G.highlight_background_color.popped_color = COLOR_WHITE;
  G.highlight_background_color.speed = 1.0f;
  G.highlight_background_color.cooldown = 1.0f;
  G.highlight_background_color.min = 0.1f;
  G.highlight_background_color.max = 0.18f;

  G.bottom_pane_highlight.base_color = G.default_background_color;
  G.bottom_pane_highlight.popped_color = COLOR_WHITE;
  G.bottom_pane_highlight.speed = 1.3f;
  G.bottom_pane_highlight.cooldown = 0.4f;
  G.bottom_pane_highlight.min = 0.0f;
  G.bottom_pane_highlight.max = 0.1f;

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

  // @panes

  G.null_buffer.empty();
  G.main_pane.type = PANETYPE_EDIT;
  G.main_pane.buffer = &G.null_buffer;
  G.main_pane.syntax_highlight = true;
  G.main_pane.background_color = &G.default_background_color;
  G.main_pane.text_color = &G.default_text_color;
  G.main_pane.highlight_background_color = &G.highlight_background_color.color;

  // search pane
  G.search_buffer.empty();
  G.search_pane.type = PANETYPE_MENU;
  G.search_pane.buffer = &G.search_buffer;
  G.search_pane.syntax_highlight = true;
  G.search_pane.background_color = &G.bottom_pane_highlight.color;
  G.search_pane.text_color = &G.default_text_color;
  // G.search_pane.highlight_background_color = &G.highlight_background_color.color;
  G.search_pane.margin = 5;
  G.search_pane.menu.prefix = String::create("search: ");

  // menu pane
  G.menu_buffer.empty();
  G.menu_pane.type = PANETYPE_MENU;
  G.menu_pane.buffer = &G.menu_buffer;
  G.menu_pane.syntax_highlight = true;
  G.menu_pane.background_color = &G.bottom_pane_highlight.color;
  G.menu_pane.text_color = &G.default_text_color;
  // G.menu_pane.highlight_background_color = &G.highlight_background_color.color;
  G.menu_pane.margin = 5;
  G.menu_pane.menu.prefix = String::create("menu: ");

  // file tree pane
  G.filetree_buffer.empty();
  G.filetree_pane.type = PANETYPE_MENU;
  G.filetree_pane.buffer = &G.filetree_buffer;
  G.filetree_pane.syntax_highlight = false;
  G.filetree_pane.background_color = &COLOR_GREY;
  G.filetree_pane.text_color = &G.default_text_color;
  // G.filetree_pane.highlight_background_color = &COLOR_DEEP_PURPLE;
  G.filetree_pane.margin = 5;
  G.filetree_pane.menu.prefix = String::create("open: ");

  // dropdown pane
  G.dropdown_buffer.empty();
  G.dropdown_pane.type = PANETYPE_DROPDOWN;
  G.dropdown_pane.buffer = &G.dropdown_buffer;
  G.dropdown_pane.background_color = &COLOR_GREY;
  G.dropdown_pane.text_color = &COLOR_WHITE;
  G.dropdown_pane.margin = 10;
  G.dropdown_pane.highlight_background_color = &COLOR_DEEP_PURPLE;

  // status message pane
  G.status_message_buffer.empty();
  G.status_message_pane.type = PANETYPE_MENU;
  G.status_message_pane.buffer = &G.status_message_buffer;
  G.status_message_pane.syntax_highlight = true;
  G.status_message_pane.background_color = &G.bottom_pane_highlight.color;
  G.status_message_pane.text_color = &G.default_text_color;
  // G.status_message_pane.highlight_background_color = &G.highlight_background_color.color;
  G.status_message_pane.margin = 5;

  G.center_pane = &G.main_pane;
  G.bottom_pane = &G.status_message_pane;
  G.selected_pane = &G.main_pane;

  G.menu_pane.menu.options.pushn((int)ARRAY_LEN(menu_options));
  for (int i = 0; i < (int)ARRAY_LEN(menu_options); ++i)
    G.menu_pane.menu.options[i] = menu_options[i].opt;

  filetree_init();
  status_message_set("Welcome!");
}

static Pos char2pixel(Pos p) {
  return char2pixel(p.x, p.y);
}

static void handle_input(Utf8char input, SpecialKey special_key, bool ctrl) {

  // TODO: insert utf8 characters
  if (!input.is_ansi() && !special_key)
    return;

  Buffer &buffer = *G.center_pane->buffer;
  int key = special_key ? special_key : input.ansi();
  if (ctrl)
    key |= KEY_CONTROL;

  switch (G.mode) {
  case MODE_GOTO:
    buffer.cursors.size = 1;
    if (isdigit(key)) {
      G.goto_line_number *= 10;
      G.goto_line_number += input.ansi() - '0';
      buffer.move_to_y(0, G.goto_line_number-1);
      status_message_set("goto %u", G.goto_line_number);
      break;
    }

    switch (key) {
      case 't':
        buffer.move_to(0, 0, 0);
        break;
      case 'b':
        buffer.move_to(0, 0, buffer.num_lines()-1);
        break;
    }
    mode_normal(true);
    break;

  case MODE_COUNT:
    break;

  case MODE_MENU:
    if (special_key == KEY_RETURN) {
      StringBuffer line;

      if (G.menu_buffer.isempty()) {
        mode_normal(true);
        break;
      }

      // dropdown_autocomplete(G.menu_buffer);

      line = G.menu_buffer[0];
      foreach(menu_options) {
        if (G.menu_buffer[0].str() == it->opt.name) {
          it->fun();
          goto done;
        }
      }
      status_message_set("Unknown option '{}'", &G.menu_buffer[0]);
      done:

      mode_normal(false);
    }
    else if (special_key == KEY_ESCAPE)
      mode_normal(true);
    else
      insert_default(&G.menu_pane, special_key, input, ctrl);
    /* FIXME: if backspace so that menu_buffer is empty, exit menu mode */
    break;

  case MODE_FILESEARCH:
    if (special_key == KEY_RETURN) {
      Slice filename = G.filetree_buffer[0].slice;

      Buffer *b = 0;
      for (Buffer *bb : G.buffers) {
        if (filename == bb->filename.slice) {
          b = bb;
          break;
        }
      }
      if (b) {
        status_message_set("Switched to {}", &filename);
        G.main_pane.buffer = b;
      }
      else {
        b = Buffer::from_file(filename);
        if (b) {
          G.buffers.push(b);
          G.main_pane.buffer = b;
          status_message_set("Loaded file {}", &filename);
        } else {
          status_message_set("Failed to load file {}", &filename);
        }
      }
      G.filetree_buffer.empty();
      mode_normal(false);
    }
    else
      insert_default(G.bottom_pane, special_key, input, ctrl);
    break;

  case MODE_SEARCH: {
    StringBuffer search;

    if (special_key == KEY_ESCAPE)
      dropdown_autocomplete(G.search_buffer);
    else
      insert_default(&G.search_pane, special_key, input, ctrl);

    search = G.search_buffer.lines[0];
    G.search_failed = !buffer.find_and_move(search.str(), true);

    if (special_key == KEY_RETURN || special_key == KEY_ESCAPE) {
      G.search_failed = !buffer.find_and_move(search.str(), true);
      if (G.search_failed) {
        buffer.move_to(0, G.search_begin_pos);
        status_message_set("'{}' not found", &G.search_buffer[0]);
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
    case KEY_ESCAPE:
      buffer.cursors.size = 1;
      break;
    case '=':
      for (int i = 0; i < buffer.cursors.size; ++i) {
        Pos pos = buffer.cursors[i].pos;
        if (buffer.lines[pos.y].length == 0)
          break;
        buffer.modified = true;
        buffer.move_x(i, buffer.autoindent(pos.y));
      }
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
      for (int i = 0; i < buffer.cursors.size; ++i) {
        if (buffer.advance_r(i))
          break;
        // if not in word, back to word
        Utf8char c = buffer.getchar(i);
        if (!(is_identifier_tail(c) || is_identifier_head(c)))
          while (c = buffer.getchar(i), !(is_identifier_tail(c) || is_identifier_head(c)))
            if (buffer.advance_r(i))
              break;
        // go to beginning of word
        while (c = buffer.getchar(i), is_identifier_tail(c) || is_identifier_head(c))
          if (buffer.advance_r(i))
            break;
        buffer.advance(i);
      }
      break;}
    case 'w': {
      for (int i = 0; i < buffer.cursors.size; ++i) {
        Utf8char c = buffer.getchar(i);
        if (is_identifier_tail(c) || is_identifier_head(c))
          while (c = buffer.getchar(i), is_identifier_tail(c) || is_identifier_head(c))
            if (buffer.advance(i))
              break;
        while (c = buffer.getchar(i), !is_identifier_head(c))
          if (buffer.advance(i))
            break;
      }
      break;}
    case 'q':
      // TODO: uncomment
      // if (buffer.modified)
        // status_message_set("You have unsaved changes. If you really want to exit, use :quit");
      // else
        exit(0);
      break;
    case 'i':
      mode_insert();
      break;
    case 'j':
      for (int i = 0; i < buffer.cursors.size; ++i)
        buffer.move_y(i, 1);
      break;
    case 'k':
      for (int i = 0; i < buffer.cursors.size; ++i)
        buffer.move_y(i, -1);
      break;
    case 'h':
      for (int i = 0; i < buffer.cursors.size; ++i)
        buffer.advance_r(i);
      break;
    case 'l':
      for (int i = 0; i < buffer.cursors.size; ++i)
        buffer.advance(i);
      break;
    case 'L':
      buffer.goto_endline();
      break;
    case 'H':
      buffer.goto_beginline();
      break;
    case 'n':
      G.search_term_background_color.reset();
      if (!buffer.find_and_move(G.search_buffer[0].str(), false))
        status_message_set("'{}' not found", &G.search_buffer[0]);
      /*jumplist_push(prev);*/
      break;
    case 'm': {
      int i = buffer.cursors.size;
      buffer.cursors.push(buffer.cursors[i-1]);
      buffer.move_y(i, 1);
      break;}
    case 'N':
      G.search_term_background_color.reset();
      if (!buffer.find_and_move_r(G.search_buffer[0], false))
        status_message_set("'{}' not found", &G.search_buffer[0]);
      /*jumplist_push(prev);*/
      break;
    case ' ':
      mode_search();
      break;
    case CONTROL('p'):
      mode_filesearch();
      break;
    case 'g':
      mode_goto();
      break;
    case 'o':
      buffer.insert_newline_below();
      mode_insert();
      break;
    case '{':
      buffer.find_and_move_r('{', false);
      break;
    case '}':
      buffer.find_and_move('}', false);
      break;
    case ':':
      mode_menu();
      break;
    case 'd':
      mode_delete();
      break;
    case 'J':
      for (int i = 0; i < buffer.cursors.size; ++i)
        buffer.move_y(i, G.center_pane->numchars_y()/2);
      break;
    case 'K':
      for (int i = 0; i < buffer.cursors.size; ++i)
        buffer.move_y(i, -G.center_pane->numchars_y()/2);
      break;
    }
  }
}

static void handle_rendering(float dt) {
  // boost marker when you move or change modes
  static Pos prev_pos;
  static Mode prev_mode;
  if (prev_pos != G.center_pane->buffer->cursors[0].pos || (prev_mode != G.mode && G.mode != MODE_SEARCH && G.mode != MODE_MENU)) {
    G.marker_background_color.reset();
    G.highlight_background_color.reset();
  }
  prev_pos = G.center_pane->buffer->cursors[0].pos;
  prev_mode = G.mode;

  // update colors
  G.marker_background_color.tick(dt);
  G.highlight_background_color.tick(dt);
  G.bottom_pane_highlight.tick(dt);
  G.search_term_background_color.tick(dt);

  // highlight some colors
  G.default_marker_background_color.tick(dt);
  G.marker_background_color.popped_color = G.default_marker_background_color.color;

  // render
  G.font_width = graphics_get_font_advance();
  G.line_height = G.font_height + G.line_margin;
  SDL_GetWindowSize(G.window, &G.win_width, &G.win_height);
  glClearColor(G.default_background_color.r, G.default_background_color.g, G.default_background_color.g, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // reflow panes
  G.bottom_pane->margin = 3;
  G.bottom_pane->bounds.h = G.line_height + 2*G.bottom_pane->margin;
  G.center_pane->bounds = {0, 0, G.win_width, G.win_height - G.bottom_pane->bounds.h};
  G.bottom_pane->bounds = {0, G.center_pane->bounds.y + G.center_pane->bounds.h, G.win_width, G.bottom_pane->bounds.h};

  G.center_pane->render();
  G.bottom_pane->render();

  // update search buffer identifier list
  if (G.mode == MODE_SEARCH)
    G.search_buffer.identifiers = G.center_pane->buffer->identifiers;

  // draw dropdown
  if (G.mode == MODE_SEARCH)
    render_dropdown(&G.search_pane);
  else
    render_dropdown(&G.main_pane);
}

Pos Buffer::to_visual_pos(Pos p) {
  p.x = lines[p.y].visual_offset(p.x, G.tab_width);
  return p;
}

void Buffer::move_to_y(int marker_idx, int y) {
  G.flags.cursor_dirty = true;
  y = clamp(y, 0, lines.size-1);
  cursors[marker_idx].y = y;
}

void Buffer::move_to_x(int marker_idx, int x) {
  G.flags.cursor_dirty = true;
  x = clamp(x, 0, lines[cursors[marker_idx].y].length);
  cursors[marker_idx].x = x;
  cursors[marker_idx].ghost_x = lines[cursors[marker_idx].y].visual_offset(x, G.tab_width);
}

void Buffer::move_to(int marker_idx, int x, int y) {
  G.flags.cursor_dirty = true;
  move_to_y(marker_idx, y);
  move_to_x(marker_idx, x);
}

void Buffer::move_to(int marker_idx, Pos p) {
  move_to_y(marker_idx, p.y);
  move_to_x(marker_idx, p.x);
}

void Buffer::move_y(int marker_idx, int dy) {
  if (!dy)
    return;
  G.flags.cursor_dirty = true;

  Pos &pos = cursors[marker_idx].pos;
  int ghost_x = cursors[marker_idx].ghost_x;

  pos.y = clamp(pos.y + dy, 0, lines.size - 1);

  if (ghost_x == GHOST_EOL)
    pos.x = lines[pos.y].length;
  else if (ghost_x == GHOST_BOL)
    pos.x = begin_of_line(pos.y);
  else
    pos.x = lines[pos.y].from_visual_offset(ghost_x, G.tab_width);
}

void Buffer::move_x(int marker_idx, int dx) {
  if (!dx)
    return;
  G.flags.cursor_dirty = true;

  Pos &pos = cursors[marker_idx].pos;
  if (dx > 0)
    for (; dx > 0; --dx)
      pos.x = lines[pos.y].next(pos.x);
  if (dx < 0)
    for (; dx < 0; ++dx)
      pos.x = lines[pos.y].prev(pos.x);
  pos.x = clamp(pos.x, 0, lines[pos.y].length);
  cursors[marker_idx].ghost_x = lines[pos.y].visual_offset(pos.x, G.tab_width);
}

Buffer* Buffer::from_file(Slice filename) {
  FILE* f = 0;
  Buffer *buffer = (Buffer*)malloc(sizeof(Buffer));
  Buffer &b = *buffer;
  if (!buffer)
    goto err;
  b = {};
  b.cursors.push({});

  b.filename = filename.copy();
  if (file_open(&f, b.filename.chars, "r"))
    goto err;

  /* get line count */
  {
    int num_lines = 0;
    for (char c; (c = (char)fgetc(f)), !feof(f);)
      num_lines += c == '\n';
    if (ferror(f))
      goto err;

    printf("Number of lines: %i\n", num_lines)
    IF_DEBUG(status_message_set("File has %i rows", num_lines));

    if (num_lines > 0) {
      b.lines.resize(num_lines+1);
      b.lines.zero();

      char c = 0;
      fseek(f, 0, SEEK_SET);
      for (int i = 0; i <= num_lines; ++i) {
        while (1) {
          c = (char)fgetc(f);
          if (feof(f)) {
            if (ferror(f))
              goto err;
            goto last_line;
          }
          if (c == '\r') c = (char)fgetc(f);
          if (c == '\n') break;
          b[i] += c;
        }
      }
      last_line:;
      assert(feof(f));
    } else {
      b.lines.push();
    }
  }

  tokenize(b);

  b.guess_tab_type();

  fclose(f);
  return buffer;

  err:
  if (buffer) {
    util_free(*buffer);
    free(buffer);
  }
  if (f)
    fclose(f);
  return 0;
}

void Buffer::move(int marker_idx, int dx, int dy) {
  if (dy)
    move_y(marker_idx, dy);
  if (dx)
    move_x(marker_idx, dx);
}

void Buffer::update() {
  for (int i = 0; i < cursors.size; ++i)
    move(i, 0, 0);
}

bool Buffer::find_r(StringBuffer s, int stay, Pos *p) {
  int x, y;

  if (!s.length)
    return false;

  x = p->x;
  y = p->y;
  if (!stay)
    --x;
  y = p->y;

  // first line
  if (lines[y](0,x+1).find_r(s, &x)) {
    p->x = x;
    p->y = y;
    return true;
  }

  // following lines
  for (--y; y >= 0; --y) {
    if (lines[y].find_r(s, &x)) {
      p->x = x;
      p->y = y;
      return true;
    }
  }
  return false;
}

bool Buffer::find_r(char s, int stay, Pos *p) {
  int x, y;

  x = p->x;
  y = p->y;
  if (!stay)
    --x;
  y = p->y;

  // first line
  if (lines[y](0,x+1).find_r(s, &x)) {
    p->x = x;
    p->y = y;
    return true;
  }

  // following lines
  for (--y; y >= 0; --y) {
    if (lines[y].find_r(s, &x)) {
      p->x = x;
      p->y = y;
      return true;
    }
  }
  return false;
}

bool Buffer::find(char s, bool stay, Pos *p) {
  int x, y;

  x = p->x;
  if (!stay)
    ++x;
  y = p->y;

  // first line
  if (x < lines[y].length) {
    if (lines[y].find(x, s, &x)) {
      p->x = x;
      p->y = y;
      return true;
    }
  }

  // following lines
  for (++y; y < lines.size; ++y) {
    if (lines[y].find(0, s, &x)) {
      p->x = x;
      p->y = y;
      return true;
    }
  }
  return false;
}

bool Buffer::find(Slice s, bool stay, Pos *p) {
  int x, y;
  if (!s.length)
    return false;

  x = p->x;
  if (!stay)
    ++x;
  y = p->y;

  // first line
  if (x < lines[y].length) {
    if (lines[y].find(x, s, &x)) {
      p->x = x;
      p->y = y;
      return true;
    }
  }

  // following lines
  for (++y; y < lines.size; ++y) {
    if (lines[y].find(0, s, &x)) {
      p->x = x;
      p->y = y;
      return true;
    }
  }
  return false;
}

bool Buffer::find_and_move_r(char c, bool stay) {
  cursors.size = 1;
  Pos p = cursors[0].pos;
  if (!find_r(c, stay, &p))
    return false;
  move_to(0, p);
  return true;
}

bool Buffer::find_and_move_r(StringBuffer s, bool stay) {
  cursors.size = 1;
  Pos p = cursors[0].pos;
  if (!find_r(s, stay, &p))
    return false;
  move_to(0, p);
  return true;
}

bool Buffer::find_and_move(char c, bool stay) {
  cursors.size = 1;
  Pos p = cursors[0].pos;
  if (!find(c, stay, &p))
    return false;
  move_to(0, p);
  return true;
}

bool Buffer::find_and_move(Slice s, bool stay) {
  cursors.size = 1;
  Pos p = cursors[0].pos;
  if (!find(s, stay, &p))
    return false;
  move_to(0, p);
  return true;
}

void Buffer::insert(Slice s) {
  modified = 1;
  for (int i = 0; i < cursors.size; ++i) {
    lines[cursors[i].y].insert(cursors[i].x, s);
    move_x(i, s.length);
  }
}

void Buffer::remove_trailing_whitespace(int y) {
  int x;
  
  x = lines[y].length - 1;
  // TODO: @utf8
  while (x >= 0 && isspace(getchar(x, y).ansi()))
    --x;
  lines[y].length = x+1;
  for (int i = 0; i < cursors.size; ++i) {
    if (cursors[i].y == y && cursors[i].x > x+1)
      move_to_x(i, x+1);
  }
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
  for (int i = 0; i < cursors.size; ++i) {
    Pos &pos = cursors[i].pos;
    modified = true;
    lines[pos.y].insert(pos.x, ch);
    move_x(i, 1);
    if (ch == '}' || ch == ')' || ch == ']' || ch == '>')
      move_x(i, autoindent(pos.y));
  }
}

void Buffer::delete_line_at(int y) {
  modified = true;
  util_free(lines[y]);
  if (lines.size == 1)
    return;
  lines.remove_slow(y);
  for (int i = 0; i < cursors.size; ++i)
    if (cursors[i].y >= y)
      move_y(i, -1);
}

void Buffer::delete_line() {
  for (int i = 0; i < cursors.size; ++i)
    delete_line_at(cursors[i].y);
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
  for (int i = 0; i < cursors.size; ++i) {
    Pos &pos = cursors[i].pos;
    if (pos.x == 0) {
      if (pos.y == 0)
        return;

      /* move up and right */
      move_y(i, -1);
      lines[pos.y] += lines[pos.y+1];
      delete_line_at(pos.y+1);
      goto_endline(i);
    }
    else {
      Pos p = pos;
      move_x(i, -1);
      remove_range(pos, p);
    }
  }
}

void Buffer::insert_tab() {
  int n;

  n = tab_type;

  modified = 1;
  for (int i =0 ; i < cursors.size; ++i) {
    Pos pos = cursors[i].pos;
    if (n == 0) {
      lines[pos.y].insert(pos.x, '\t');
      move_x(i, 1);
    }
    else {
      /* TODO: optimize? */
      while (n--)
        lines[pos.y].insert(pos.x, ' ');
      move_x(i, tab_type);
    }
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
    auto t = token_read(&p, y+1);

    if (t.tok == TOKEN_NULL)
      break;

    switch (t.tok) {
      case '{': ++depth; break;
      case '}': --depth; break;
      case '[': ++depth; break;
      case ']': --depth; break;
      case '(': ++depth; break;
      case ')': --depth; break;
      case TOKEN_IDENTIFIER: {
        Slice s = lines[y](t.a.x, t.b.x+1);
        assert(t.b.y == t.a.y);
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
  // skip empty lines
  while (y_above >= 0 && lines[y_above].length == 0)
    --y_above;
  if (y_above == 0) {
    diff = -getindent(y);
    goto done;
  } else {
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
  if (diff > 0)
    lines[y].insert(0, tab_char, diff);
  return diff;
}

int Buffer::isempty() {
  return lines.size == 1 && lines[0].length == 0;
}

void Buffer::push_line(const char *str) {
  modified = 1;
  int y = lines.size;
  if (!isempty())
    lines.push();
  else
    y = 0;
  lines[y] += str;
}

void Buffer::push_line(Slice s) {
  modified = 1;
  int y = lines.size;
  if (!isempty())
    lines.push();
  else
    y = 0;
  lines[y] += s;
}

void Buffer::insert_newline() {
  modified = 1;

  for (int i = 0; i < cursors.size; ++i) {
    Pos &pos = cursors[i].pos;
    lines.insertz(pos.y+1);

    // after we add a new line we have to push y of all cursors below us
    for (int j = 0; j < cursors.size; ++j) {
      if (i == j) continue;
      if (cursors[j].pos.y == pos.y) {
        cursors[j].pos = {cursors[j].pos.x - pos.x, cursors[j].pos.y+1};
        cursors[j].ghost_x = cursors[j].pos.x;
      }
      else if (cursors[j].pos.y > pos.y) {
        ++cursors[j].pos.y;
      }
    }

    lines[pos.y+1] += lines[pos.y](pos.x, -1);
    lines[pos.y].length = pos.x;

    ++cursors[i].y;
    cursors[i].x = cursors[i].ghost_x = 0;
    move_x(i, autoindent(pos.y));
    // pretty(pos.y-1);
  }
}

void Buffer::insert_newline_below() {
  modified = 1;
  for (int i = 0; i < cursors.size; ++i) {
    Pos &pos = cursors[i].pos;

    lines.insertz(pos.y+1);

    // after we add a new line we have to push y of all cursors below us
    for (int j = 0; j < cursors.size; ++j)
      if (cursors[j].pos.y > pos.y)
        ++cursors[j].y;

    move_y(i, 1);
    move_x(i, autoindent(pos.y));
  }
}

void Buffer::guess_tab_type() {
  int i;
  /* try to figure out tab type */
  /* TODO: use tokens here instead, so we skip comments */
  tab_type = -1;
  for (i = 0; i < lines.size; ++i) {
    if (!lines[i].length)
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

    if (!lines[i].length)
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

void Buffer::goto_endline(int marker_idx) {
  Pos pos = cursors[marker_idx].pos;
  move_to_x(marker_idx, lines[pos.y].length);
  cursors[marker_idx].ghost_x = GHOST_EOL;
}

void Buffer::goto_endline() {
  for (int i = 0; i < cursors.size; ++i)
    goto_endline(i);
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

  for (int i = 0; i < cursors.size; ++i) {
    x = begin_of_line(cursors[i].y);
    move_to_x(i, x);
    cursors[i].ghost_x = GHOST_BOL;
  }
}

void Buffer::empty() {
  modified = 1;

  util_free(lines);
  lines.push();
  if (!cursors.size)
    cursors.push({});
  cursors.size = 1;
  move_to(0, 0, 0);
}

void Buffer::truncate_to_n_lines(int n) {
  int i;

  modified = 1;
  cursors.size = 1;
  n = at_least(n, 1);

  if (n >= lines.size)
    return;

  for (i = n; i < lines.size; ++i)
    util_free(lines[i]);
  lines.size = n;
  cursors[0].y = at_most(cursors[0].y, n-1);
}

int Buffer::advance(int *x, int *y) const {
  if (*x < lines[*y].length)
    *x = lines[*y].next(*x);
  else {
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

int Buffer::advance(Pos &p) const {
  return advance(&p.x, &p.y);
}

int Buffer::advance(int marker_idx) {
  Pos &pos = cursors[marker_idx].pos;
  int err = advance(&pos.x, &pos.y);
  if (err)
    return err;
  cursors[marker_idx].ghost_x = lines[pos.y].visual_offset(pos.x, G.tab_width);
  return 0;
}

int Buffer::advance_r(Pos &p) const {
  if (p.x > 0)
    p.x = lines[p.y].prev(p.x);
  else {
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

int Buffer::advance_r(int marker_idx) {
  Pos &pos = cursors[marker_idx].pos;
  int err = advance_r(pos);
  if (err)
    return err;
  cursors[marker_idx].ghost_x = lines[cursors[marker_idx].y].visual_offset(pos.x, G.tab_width);
  return 0;
}

// TODO, FIXME: properly implement
Utf8char Buffer::getchar(int x, int y) {
  return Utf8char::create(x >= lines[y].length ? '\n' : lines[y][x]);
}

Utf8char Buffer::getchar(Pos p) {
  return getchar(p.x, p.y);
}

Utf8char Buffer::getchar(int marker_idx) {
  Pos pos = cursors[marker_idx].pos;
  return Utf8char::create(pos.x >= lines[pos.y].length ? '\n' : lines[pos.y][pos.x]);
}

Buffer::TokenResult Buffer::token_read(Pos *p, int y_end) {
  TokenResult result = {};
  int x,y;
  x = p->x, y = p->y;

  for (;;) {
    if (y >= y_end || y >= lines.size) {
      result.tok = TOKEN_NULL;
      goto done;
    }

    // endline
    if (x >= lines[y].length) {
      result.tok = TOKEN_EOL;
      result.a = {x,y};
      result.b = {x,y};
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
      result.tok = TOKEN_IDENTIFIER;
      result.a = {x, y};
      while (x < lines[y].length) {
        result.b = {x, y};
        c = lines[y][++x];
        if (!is_identifier_tail(c))
          break;
      }
      goto done;
    }

    /* block comment */
    if (c == '/' && x+1 < lines[y].length && lines[y][x+1] == '*') {
      result.tok = TOKEN_BLOCK_COMMENT;
      result.a = {x, y};
      x += 2;
      /* goto matching end block */
      for (;;) {
        if (y >= y_end || y >= lines.size) {
          result.b = {x, y};
          result.tok = TOKEN_BLOCK_COMMENT_BEGIN;
          break;
        }
        if (x >= lines[y].length) {
          ++y;
          x = 0;
          continue;
        }
        c = lines[y][x];
        if (c == '*' && x+1 < lines[y].length && lines[y][x+1] == '/') {
          result.b = {x+1, y};
          x += 2;
          break;
        }
        ++x;
      }
      break;
    }

    /* end of block comment */
    if (c == '*' && x+1 < lines[y].length && lines[y][x+1] == '/') {
      result.tok = TOKEN_BLOCK_COMMENT_END;
      result.a = {x, y};
      result.b = {x+1, y};
      x += 2;
      goto done;
    }

    /* line comment */
    if (c == '/' && x+1 < lines[y].length && lines[y][x+1] == '/') {
      result.tok = TOKEN_LINE_COMMENT_BEGIN;
      result.a = {x, y};
      result.b = {x+1, y};
      x += 2;
      goto done;
    }

    /* number */
    if (IS_NUMBER_HEAD(c)) {
      result.tok = TOKEN_NUMBER;
      result.a = {x, y};
      while (x < lines[y].length) {
        result.b = {x, y};
        c = lines[y][++x];
        if (!IS_NUMBER_TAIL(c))
          break;
      }
      if (x == lines[y].length)
        goto done;
      if (c == '.' && x+1 < lines[y].length && isdigit(lines[y][x+1])) {
        c = lines[y][++x];
        while (isdigit(c) && x < lines[y].length) {
          result.b = {x, y};
          c = lines[y][++x];
        }
        if (x == lines[y].length)
          goto done;
      }
      while ((c == 'u' || c == 'l' || c == 'L' || c == 'f') && x < lines[y].length) {
        result.b = {x, y};
        c = lines[y][++x];
      }
      goto done;
    }

    /* string */
    if (c == '"' || c == '\'') {
      char str_char = c;
      result.tok = TOKEN_STRING;
      result.a = {x, y};
      ++x;
      for (;;) {
        if (x >= lines[y].length) {
          result.tok = TOKEN_STRING_BEGIN;
          result.b = {x, y};
          ++y;
          x = 0;
          break;
        }

        c = lines[y][x];
        if (c == str_char) {
          result.b = {x, y};
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
        result.tok = TOKEN_OPERATOR;
        result.a = {x,y};
        result.b = {x+operators[i].length-1, y};
        x += operators[i].length;
        goto done;
      }
    }

    /* single char token */
    result.tok = c;
    result.a = {x, y};
    result.b = {x, y};
    ++x;
    goto done;
  }

  done:
  p->x = x;
  p->y = y;
  return result;
}

void Canvas::init(int width, int height) {
  (*this) = {};
  this->w = width;
  this->h = height;
  this->chars = new Utf8char[w*h]();
  this->background_colors = new Color[w*h]();
  this->text_colors = new Color[w*h]();
}

void Canvas::resize(int width, int height) {
  if (this->chars)
    delete [] this->chars;
  if (this->background_colors)
    delete [] this->background_colors;
  if (this->text_colors)
    delete [] this->text_colors;
  this->init(width, height);
}

void Canvas::fill(Utf8char c) {
  for (int i = 0; i < w*h; ++i)
    this->chars[i] = c;
}

void Canvas::fill(Color text, Color backgrnd) {
  for (int i = 0; i < w*h; ++i)
    background_colors[i] = backgrnd;
  for (int i = 0; i < w*h; ++i)
    text_colors[i] = text;
}

void Canvas::invert_color(Pos p) {
  swap(text_colors[p.y*w + p.x], background_colors[p.y*w + p.x]);
}

void Pane::render_as_dropdown() {
  Buffer &b = *buffer;

  Canvas canvas;
  int y_max = this->numchars_y();
  canvas.init(this->numchars_x(), this->numchars_y());
  canvas.background = *this->background_color;
  canvas.fill(Utf8char{' '});
  canvas.fill(*this->text_color, *this->background_color);
  canvas.draw_shadow = true;
  canvas.margin = this->margin;

  // draw each line 
  for (int y = 0; y < at_most(b.lines.size, y_max); ++y)
    canvas.render_str({0, y}, this->text_color, NULL, 0, -1, b.lines[y].str());

  // highlight the line you're on
  if (highlight_background_color) {
    for (int i = 0; i < b.cursors.size; ++i)
      canvas.fill_background({0, b.cursors[i].y, {-1, 1}}, *highlight_background_color);
  }

  canvas.render(this->bounds.p);

  util_free(canvas);
  render_quads();
  render_text();
}

void Pane::render_syntax_highlight(Canvas &canvas, int x0, int y0, int y1) {
  Buffer &b = *this->buffer;
  // syntax @highlighting
  Pos pos = {0, y0};
  for (;;) {
    Pos prev,next;
    auto t = b.token_read(&pos, y1);
    prev = t.a;
    next = t.b;

    check_token:

    bool do_render = false;

    Color highlighted_text_color = {};

    if (t.tok == TOKEN_NULL)
      break;
    switch (t.tok) {

      case TOKEN_NUMBER:
        do_render = true;
        highlighted_text_color = G.number_color;
        break;

      case TOKEN_BLOCK_COMMENT_BEGIN:
        do_render = 1;
        highlighted_text_color = G.comment_color;
        next = {b.lines[y1-1].length, y1-1};
        break;

      case TOKEN_BLOCK_COMMENT:
        do_render = 1;
        highlighted_text_color = G.comment_color;
        break;

      case TOKEN_BLOCK_COMMENT_END:
        do_render = 1;
        highlighted_text_color = G.comment_color;
        prev = {x0, y0};
        break;

      case TOKEN_LINE_COMMENT_BEGIN:
        do_render = true;
        // just fast forward to the end of the line, no need to parse it
        pos = {0, prev.y+1};
        next = {b.lines[prev.y].length-1, prev.y};
        highlighted_text_color = G.comment_color;
        break;

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
          if (b[prev.y](prev.x, next.x+1) == keywords[i].name) {
            highlighted_text_color = keyword_colors[keywords[i].type];
            goto done;
          }
        }

        // otherwise check for functions
        // we assume something not indented and followed by a '(' is a function
        // TODO: @utf8
        if (b.getchar(0, prev.y).isspace())
          break;
        t = b.token_read(&pos, y1);
        if (t.tok != '(') {
          prev = t.a;
          next = t.b;
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

      prev.x -= x0;
      prev.y -= y0;
      next.x -= x0;
      next.y -= y0;

      canvas.fill_textcolor({prev, next}, Rect{0, 0, -1, -1}, highlighted_text_color);
    }

    if (pos.y > y1)
      break;
  }
}

void Pane::render() {
  switch (type) {
    case PANETYPE_NULL:
      break;
    case PANETYPE_EDIT:
      render_as_edit();
      break;
    case PANETYPE_MENU:
      render_as_menu();
      break;
    case PANETYPE_DROPDOWN:
      render_as_dropdown();
      break;
  }
}

void Pane::render_as_menu() {
  Buffer &b = *buffer;
  // TODO: scrolling in x
  Pos buf_offset = {this->menu.prefix.length, 0};

  Canvas canvas;
  canvas.init(this->numchars_x(), 1);
  canvas.fill(*this->text_color, *this->background_color);
  canvas.background = *this->background_color;
  canvas.margin = this->margin;

  // draw prefix
  canvas.render_str({0, 0}, &G.default_gutter_text_color, NULL, 0, -1, this->menu.prefix.str());

  // draw buffer
  canvas.render_str(buf_offset, this->text_color, NULL, 0, -1, b.lines[0].str());

  // highlight
  if (this->highlight_background_color)
    canvas.fill_background({0, 0, {-1, 1}}, *this->highlight_background_color);

  // draw marker
  for (int i = 0; i < b.cursors.size; ++i) {
    if (G.selected_pane == this)
      canvas.fill_background({b.cursors[i].pos + buf_offset, {1, 1}}, G.marker_background_color.color);
    else if (G.bottom_pane != this)
      canvas.fill_background({b.cursors[i].pos + buf_offset, {1, 1}}, G.marker_inactive_color);
  }

  canvas.render(this->bounds.p);

  util_free(canvas);
  render_quads();
  render_text();
}

void Pane::render_as_edit() {
  Buffer &b = *buffer;
  // calc bounds 
  Pos buf_offset = {this->calc_left_visible_column(), this->calc_top_visible_row()};
  int buf_y1 = at_most(buf_offset.y + this->numchars_y(), b.lines.size);

  // draw gutter
  this->_gutter_width = at_least(calc_num_chars(buf_y1) + 3, 6);
  {
    Canvas gutter;
    gutter.init(_gutter_width, this->numchars_y());
    gutter.background = *this->background_color;
    for (int y = 0, line = buf_offset.y; y < this->numchars_y(); ++y, ++line)
      gutter.render_strf({0, y}, &G.default_gutter_text_color, &G.default_gutter_background_color, 0, _gutter_width, " %i", line + 1);
    gutter.render(this->bounds.p);
    util_free(gutter);
  }

  Canvas canvas;
  canvas.init(this->numchars_x()-_gutter_width, this->numchars_y());
  canvas.fill(*this->text_color, *this->background_color);
  canvas.background = *this->background_color;
  canvas.margin = this->margin;

  // draw each line 
  for (int y = 0, buf_y = buf_offset.y; buf_y < buf_y1; ++buf_y, ++y)
    canvas.render_str({0, y}, this->text_color, NULL, 0, -1, b.lines[buf_y].str());

  if (this->syntax_highlight)
    this->render_syntax_highlight(canvas, buf_offset.x, buf_offset.y, buf_y1);

  // highlight the line you're on
  if (this->highlight_background_color) {
    for (int i = 0; i < b.cursors.size; ++i)
      canvas.fill_background({0, buf2char(b.cursors[i].pos).y, {-1, 1}}, *this->highlight_background_color);
  }

  // if there is a search term, highlight that as well
  if (G.selected_pane == this && G.search_buffer.lines[0].length > 0) {
    Pos pos = {0, buf_offset.y};
    while (b.find(G.search_buffer.lines[0].str(), false, &pos) && pos.y < buf_y1)
      canvas.fill_background({buf2char(pos), G.search_buffer.lines[0].length, 1}, G.search_term_background_color.color);
  }

  // draw marker
  for (int i = 0; i < b.cursors.size; ++i) {
    Pos pos = b.cursors[i].pos;
    if (G.selected_pane == this)
      // canvas.fill_background({buf2char(pos), {1, 1}}, Color::from_hsl(fmodf(i*360.0f/b.markers.size, 360.0f), 0.7f, 0.7f));
      canvas.fill_background({buf2char(pos), {1, 1}}, G.default_marker_background_color.color);
    else if (G.bottom_pane != this)
      canvas.fill_background({buf2char(pos), {1, 1}}, G.marker_inactive_color);
  }

  canvas.render(this->bounds.p + Pos{_gutter_width*G.font_width, 0});

  util_free(canvas);
  render_quads();
  render_text();
}

Pos Pane::buf2pixel(Pos p) const {
  p = buffer->to_visual_pos(p);
  p.x -= this->calc_left_visible_column();
  p.y -= this->calc_top_visible_row();
  p = char2pixel(p) + this->bounds.p;
  p.x += this->_gutter_width * G.font_width;
  return p;
}

Pos Pane::buf2char(Pos p) const {
  p = buffer->to_visual_pos(p);
  p.x -= this->calc_left_visible_column();
  p.y -= this->calc_top_visible_row();
  return p;
}

int Pane::calc_top_visible_row() const {
  return at_least(0, this->buffer->cursors[0].y - this->numchars_y()/2);
}

int Pane::calc_left_visible_column() const {
  int x = this->buffer->cursors[0].x;
  x = this->buffer->lines[this->buffer->cursors[0].y].visual_offset(x, G.tab_width);
  x -= this->numchars_x()*6/7;
  return at_least(x, 0);
}

// fills a to b but only inside the bounds 
void Canvas::fill_textcolor(Range range, Rect bounds, Color c) {
  Pos a = range.a;
  Pos b = range.b;
  if (bounds.w == -1)
    bounds.w = w - bounds.x;
  if (bounds.h == -1)
    bounds.h = h - bounds.y;

  bounds.x = clamp(bounds.x, 0, w-1);
  bounds.w = clamp(bounds.w, 0, w-bounds.x);
  bounds.y = clamp(bounds.y, 0, h-1);
  bounds.h = clamp(bounds.h, 0, h-bounds.y);

  a.x = clamp(a.x, bounds.x, bounds.w-1);
  a.y = clamp(a.y, bounds.y, bounds.h-1);
  b.x = clamp(b.x, bounds.x, bounds.w-1);
  b.y = clamp(b.y, bounds.y, bounds.h-1);

  if (a.y == b.y) {
    for (int x = a.x; x <= b.x; ++x)
      this->text_colors[a.y*this->w + x] = c;
    return;
  }

  int y = a.y;
  if (y < b.y)
    for (int x = a.x; x < bounds.x+bounds.w; ++x)
      this->text_colors[y*this->w + x] = c;
  for (++y; y < b.y; ++y)
    for (int x = bounds.x; x < bounds.x+bounds.w; ++x)
      this->text_colors[y*this->w + x] = c;
  for (int x = bounds.x; x <= b.x; ++x)
    this->text_colors[y*this->w + x] = c;
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
    text_colors[y*this->w + x] = c;
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
    background_colors[y*this->w + x] = c;
}

void Canvas::render_str(Pos p, const Color *text_color, const Color *background_color, int xclip0, int xclip1, Slice s) {
  if (!s.length)
    return;

  if (xclip1 == -1)
    xclip1 = this->w;

  Utf8char *row = &this->chars[p.y*w];
  Color *text_row = &this->text_colors[p.y*w];
  Color *background_row = &this->background_colors[p.y*w];

  for (Utf8char c : s) {
    if (c == '\t') {
      for (int i = 0; i < G.tab_width; ++i, ++p.x)
        if (p.x >= xclip0 && p.x < xclip1) {
          row[p.x] = ' ';
          if (text_color)
            text_row[p.x] = *text_color;
          if (background_color)
            background_row[p.x] = *background_color;
        }
    }
    else {
      if (p.x >= xclip0 && p.x < xclip1) {
        row[p.x] = c;
        if (text_color)
          text_row[p.x] = *text_color;
        if (background_color)
          background_row[p.x] = *background_color;
      }
      ++p.x;
    }
  }
}

void Canvas::render_str_v(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, va_list args) {
  if (p.x >= w)
    return;
  G.tmp_render_buffer.clear();
  G.tmp_render_buffer.appendv(fmt, args);
  render_str(p, text_color, background_color, x0, x1, G.tmp_render_buffer.str());
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

  // render shadow
  if (draw_shadow) {
    int shadow_offset = 3;
    Color shadow_color = COLOR_BLACK;
    shadow_color.a = 170;
    Color shadow_color2 = COLOR_BLACK;
    shadow_color2.a = 0;

    // right side
    push_quad(
      {(u16)(offset.x + size.x),                 (u16)(offset.y + shadow_offset),          shadow_color},
      {(u16)(offset.x + size.x),                 (u16)(offset.y + size.y),                 shadow_color},
      {(u16)(offset.x + size.x + shadow_offset), (u16)(offset.y + size.y + shadow_offset), shadow_color2},
      {(u16)(offset.x + size.x + shadow_offset), (u16)(offset.y + shadow_offset),          shadow_color2});

    // bottom side
    push_quad(
      {(u16)(offset.x + shadow_offset),          (u16)(offset.y + size.y),                 shadow_color},
      {(u16)(offset.x + shadow_offset),          (u16)(offset.y + size.y + shadow_offset), shadow_color2},
      {(u16)(offset.x + size.x + shadow_offset), (u16)(offset.y + size.y + shadow_offset), shadow_color2},
      {(u16)(offset.x + size.x),                 (u16)(offset.y + size.y),                 shadow_color});
  }

  // render base background
  push_square_quad((u16)offset.x, (u16)(offset.x+size.x), (u16)(offset.y), (u16)(offset.y+size.y), background);
  offset.x += margin;
  offset.y += margin;

  // render background
  for (int y = 0; y < h; ++y) {
    for (int x0 = 0, x1 = 1; x1 <= w; ++x1) {
      if (x1 < w && background_colors[y*w + x1] == background_colors[y*w + x0])
        continue;
      Pos p0 = char2pixel(x0,y) + offset;
      Pos p1 = char2pixel(x1,y+1) + offset;
      const Color c = background_colors[y*w + x0];
      push_square_quad((u16)p0.x, (u16)p1.x, (u16)p0.y, (u16)p1.y, c);
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
      if (x1 < w && text_colors[row*w + x1] == text_colors[row*w + x0])
        continue;
      int x = char2pixelx(x0) + offset.x;
      push_textn(G.tmp_render_buffer.chars + x0, x1 - x0, x, y, false, text_colors[row*w + x0]);
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
  Slice a = Slice::create("hello world");
  Slice b = Slice::create("hello");
  printf("%s %i\n", b(2,-1).chars, b(2,-1).length);
  assert(a.begins_with(0, b));
  assert(a.begins_with(2, b(2, -1)));
  int x;
  b = Slice::create("llo");
  assert(a.find(2, b, &x));
  printf("%i\n", x);
  assert(x == 2);
  assert(a.find(1, b, &x));
  printf("%i\n", x);
  assert(x == 2);
  assert(a.find(0, b, &x));
  printf("%i\n", x);
  assert(x == 2);
}

#if 1
#include <type_traits>
STATIC_ASSERT(std::is_pod<State>::value, state_must_be_pod);
#endif

#ifdef OS_WINDOWS
int wmain(int, const wchar_t *[], wchar_t *[])
#else
int main(int, const char *[])
#endif
{
  state_init();

  test();

  /* open a buffer */
  Buffer *b = Buffer::from_file(Slice::create("./src/cmantic.cpp"));
  G.buffers.push(b);
  G.main_pane.buffer = b;

  for (;;) {
    static u32 ticks = SDL_GetTicks();
    float dt = (float)(SDL_GetTicks() - ticks) / 1000.0f * 60.0f;
    ticks = SDL_GetTicks();

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
          input = (char)((event.key.keysym.mod & KMOD_SHIFT) ? toupper(event.key.keysym.sym) : event.key.keysym.sym);
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

    handle_rendering(dt);

    // the quad and text buffers should be empty, but we flush them just to be safe
    render_quads();
    render_text();

    SDL_GL_SwapWindow(G.window);

    if (G.flags.cursor_dirty)
      puts("Cursor dirty");
    G.flags.cursor_dirty = false;
  }
}

