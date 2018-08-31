/*
 * TODO:
 * If you have the same buffer open in multiple panes, paste_highlights are updated faster
 * move_cursors_on_delete to match move_cursors_on_insert (clean up cursor passing to BufferData methods)
 * listen on file changes and automatically reload
 * 
 * Have a single build buffer
 *
 * Search on word only
 * We don't need to rewrite the whole tokenization on edit, just the y-range that changed
 * Create an easy-to-use token iterator
 * make 'dp' use tokens instead of chars
 * dropdown_autocomplete should not delete characters (it ruins paste)
 * Folding
 * Buffer::advance should return success instead of err
 * project-wide grep
 * project-wide goto definition
 * Add description and better spacing to menus
 * recursive panes
 * Build system
 * parse BOM
 * Size limit on undo/redo
 * Fix memory leak occuring between frames
 * Index entire file tree
 * Syntactical Regex engine (regex with extensions for lexical tokens like identifiers, numbers, and maybe even functions, expressions etc.)
 * Compress undo history?
 * Multiuser editing
 * Listen to changes in current directory and update file list accordingly
 * Update markers in other panes with same buffer (or at least make sure they are in range)
 */

#if 1
#define DEBUG
#endif

// @includes
#include "util.hpp"
#include "graphics.hpp"

template<class T>
struct GroupedData {
  TempAllocator storage;
  T data;
};
template<class T>
void util_free(GroupedData<T> &d) {
  util_free(d.storage);
  d = {};
}
#include "git.cpp"

struct FuzzyMatch {
  Slice str;
  float points;
  int idx;
};

static int fuzzy_cmp(const void *aa, const void *bb) {
  const FuzzyMatch *a = (FuzzyMatch*)aa, *b = (FuzzyMatch*)bb;

  if (a->points != b->points)
    return (int)(b->points - a->points + 0.5f);
  return a->str.length - b->str.length;
}

// returns number of found matches
static int fuzzy_match(Slice string, View<Slice> strings, View<FuzzyMatch> result, bool ignore_identical_strings) {
  int num_results = 0;

  if (string.length == 0) {
    int l = min(result.size, strings.size);
    for (int i = 0; i < l; ++i)
      result[i] = {strings[i], 0.0f, i};
    return l;
  }

  for (int i = 0; i < strings.size; ++i) {
    Slice identifier = strings[i];
    if (ignore_identical_strings && string == identifier)
      continue;

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
      result[num_results].idx = i;
      ++num_results;
    } else {
      /* find worst match and replace */
      FuzzyMatch &worst = result[ARRAY_MIN_BY(result, points)];
      if (points > worst.points) {
        worst.str = identifier;
        worst.points = points;
        worst.idx = i;
      }
    }
  }

  qsort(result.items, num_results, sizeof(result[0]), fuzzy_cmp);
  return num_results;
}

static Array<String> easy_fuzzy_match(Slice input, View<Slice> options, bool ignore_identical_strings) {
  StackArray<FuzzyMatch, 15> matches = {};

  int n = fuzzy_match(input, options, view(matches), ignore_identical_strings);

  Array<String> result = {};
  for (int i = 0; i < n; ++i)
    result += String::create(matches[i].str);
  return result;
}

enum Mode {
  MODE_NORMAL,
  MODE_INSERT,
  MODE_MENU,
  MODE_DELETE,
  MODE_GOTO,
  MODE_SEARCH,
  MODE_YANK,
  MODE_FILESEARCH,
  MODE_GOTO_DEFINITION,
  MODE_CWD,
  MODE_PROMPT,
  MODE_COUNT
};

enum Token {
  TOKEN_NULL                =  0,
  TOKEN_IDENTIFIER          = -2,
  TOKEN_NUMBER              = -3,
  TOKEN_STRING              = -4,
  TOKEN_STRING_BEGIN        = -6,
  TOKEN_BLOCK_COMMENT       = -7,
  TOKEN_BLOCK_COMMENT_BEGIN = -8,
  TOKEN_BLOCK_COMMENT_END   = -9,
  TOKEN_LINE_COMMENT        = -10,
  TOKEN_OPERATOR            = -11,
  TOKEN_EOF                 = -12,
};

struct Pos {
  int x,y;
  bool operator!=(Pos p) {
    return x != p.x || y != p.y;
  }
  void operator+=(Pos p) {
    x += p.x;
    y += p.y;
  }
  void operator-=(Pos p) {
    x -= p.x;
    y -= p.y;
  }
  bool operator==(Pos p) {
    return x == p.x && y == p.y;
  }
  bool operator<(Pos p) {
    if (y == p.y)
      return x < p.x;
    return y < p.y;
  }
  bool operator<=(Pos p) {
    if (y == p.y)
      return x <= p.x;
    return y <= p.y;
  }
};

void util_free(Pos) {}

struct Range {
  Pos a;
  Pos b;
};

struct TokenInfo {
  Token token;
  union {
    struct {
      Pos a;
      Pos b;
    };
    Range r;
  };
  Slice str;
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

static Pos operator+(Pos a, Pos b) {
  return {a.x+b.x, a.y+b.y};
}

static bool is_number_head(char c) {
  return isdigit(c);
}

static bool is_number_tail(char c) {
  return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') || c == 'x';
}

static bool is_number_modifier(char c) {
  return c == 'u' || c == 'l' || c == 'L' || c == 'f';
}

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

static void util_free(Range) {}
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

union Cursor {
  struct {
    int x,y;
  };
  struct {
    Pos pos;
    /* if going from a longer line to a shorter line, remember where we were before clamping to row length. a value of -1 means always go to end of line */
    #define GHOST_EOL -1
    #define GHOST_BOL -2
    int ghost_x;
  };
  bool operator==(Cursor c) {
    return c.pos == pos && c.ghost_x == ghost_x;
  }
};

void util_free(Cursor) {}

const char * const ENDLINE_WINDOWS = "\r\n";
const char * const ENDLINE_UNIX = "\n";

struct TokenResult {
  int tok;
  Pos a;
  Pos b;
  Slice operator_str;
};

struct BufferData;

enum UndoActionType {
  ACTIONTYPE_INSERT,
  ACTIONTYPE_DELETE,
  ACTIONTYPE_CURSOR_SNAPSHOT,
  ACTIONTYPE_GROUP_BEGIN,
  ACTIONTYPE_GROUP_END,
};
struct UndoAction {
  UndoActionType type;
  union {
    // ACTIONTYPE_INSERT
    struct {
      Pos a;
      Pos b;
      String s;
      int cursor_idx;
    } insert;

    // ACTIONTYPE_DELETE
    struct {
      Pos a;
      Pos b;
      String s;
      int cursor_idx;
    } remove;

    // ACTIONTYPE_CURSOR_SNAPSHOT
    struct {
      Array<Cursor> cursors;
    };
  };

  static UndoAction delete_range(Range r, String s, int cursor_idx = -1);
  static UndoAction cursor_snapshot(Array<Cursor> cursors);
  static UndoAction insert_slice(Pos a, Pos b, Slice s, int cursor_idx = -1);
};

static void util_free(UndoAction &a);
static void tokenize(BufferData &b);

struct PasteHighlight {
  Pos a;
  Pos b;
  float alpha; // 1 -> 0
};

struct BufferData {
  String filename;
  Slice description; // only used for special buffers (i.e. if it does not have a filename)
  const char * endline_string; // ENDLINE_WINDOWS or ENDLINE_UNIX

  bool is_dynamic;

  BufferData *buffer;

  Array<StringBuffer> lines;
  GroupedData<Array<BlameData>> blame;

  Array<PasteHighlight> paste_highlights;

  int tab_type; /* 0 for tabs, 1+ for spaces */

  // raw_mode is used when inserting text that is not coming from the keyboard, for example when pasting text or doing undo/redo
  // It disables autoindenting and other automatic formatting stuff
  int _raw_mode_depth;

  /* parser stuff */
  Array<TokenInfo> tokens;
  Array<Range> definitions;
  Array<String> identifiers;

  // methods
  Slice name() const {return filename.chars ? Path::name(filename.slice) : description;}
  bool is_bound_to_file() {return filename.chars;}
  void init(bool is_dynamic, Slice description = {});
  Range* getdefinition(Slice s);
  Slice getslice(Pos a, Pos b) {return lines[a.y](a.x, b.x);} // range is inclusive
  Slice getslice(Range r) {return lines[r.a.y](r.a.x, r.b.x);} // range is inclusive
  bool modified() {return _next_undo_action != _last_save_undo_action;}
  void raw_begin() {++_raw_mode_depth;}
  void raw_end() {--_raw_mode_depth;}
  bool raw_mode() {return _raw_mode_depth;}
  StringBuffer& operator[](int i) {return lines[i];}
  const StringBuffer& operator[](int i) const {return lines[i];}
  int num_lines() const {return lines.size;}
  Slice slice(Pos p, int len) {return lines[p.y](p.x,p.x+len); }
  Pos to_visual_pos(Pos p);
  bool find_r(Slice s, int stay, Pos *pos);
  bool find_r(char c, int stay, Pos *pos);
  bool find(Slice s, bool stay, Pos *pos);
  bool find(char c, bool stay, Pos *pos);
  bool find_start_of_identifier(Pos p, Pos *pout);
  void insert(Array<Cursor> &cursors, Slice s);
  void insert(Array<Cursor> &cursors, Pos p, Slice s, int cursor_idx = -1);
  void insert(Array<Cursor> &cursors, Slice s, int cursor_idx);
  void remove_trailing_whitespace(Array<Cursor> &cursors, int cursor_idx);
  void insert(Array<Cursor> &cursors, Pos p, Utf8char ch);
  void insert(Array<Cursor> &cursors, Utf8char ch, int cursor_idx);
  void insert(Array<Cursor> &cursors, Utf8char ch);
  void delete_line_at(int y);
  void delete_line(Array<Cursor> &cursors);
  void delete_line(Array<Cursor> &cursors, int y);
  void remove_range(Array<Cursor> &cursors, Pos a, Pos b, int cursor_idx = -1);
  void delete_char(Array<Cursor>& cursors);
  void insert_tab(Array<Cursor> &cursors);
  void insert_tab(Array<Cursor> &cursors, Pos pos);
  int getindent(int y);
  int indentdepth(int y, bool *has_statement);
  void autoindent(Array<Cursor> &cursors, const int y);
  void autoindent(Array<Cursor> &cursors);
  void add_indent(Array<Cursor> &cursors, int y, int diff);
  void set_indent(Array<Cursor> &cursors, int y, int target);
  int isempty();
  int advance_r(Pos &p) const;
  int advance(Pos &p) const;
  int advance(int *x, int *y) const;
  void push_line(Array<Cursor> &cursors, Slice s);
  void insert_newline(Array<Cursor> &cursors);
  void insert_newline_below(Array<Cursor> &cursors);
  void guess_tab_type();
  int begin_of_line(int y);
  void empty(Array<Cursor> &cursors);
  Utf8char getchar(Pos p);
  Utf8char getchar(int x, int y);
  StringBuffer range_to_string(Range r);
  // Finds the token at or before given position
  TokenInfo* gettoken(Pos p);
  TokenResult token_read(Pos *p, int y_end);

  // Undo functionality:
  //
  // Every action on the buffer that mutates it (basically insert/delete) will add that action to the undo/redo list.
  // But sometimes you want a series of actions to be grouped together for undo/redo.
  // To do that you can call action_group_begin() and action_group_end() before and after your actions
  //
  // Example:
  //
  // buffer.action_group_begin();
  // .. call methods on buffer that mutate it
  // buffer.action_group_end();
  //
  // TODO: Use a fixed buffer (circular queue) of undo actions
  bool undo_disabled;
  Array<UndoAction> _undo_actions;
  int _next_undo_action;
  int _last_save_undo_action;
  int _action_group_depth;

  void print_undo_actions();
  void redo(Array<Cursor> &cursors);
  void undo(Array<Cursor> &cursors);
  void action_end(Array<Cursor> &cursors);
  void action_begin(Array<Cursor> &cursors);
  void push_undo_action(UndoAction a);

  static bool reload(BufferData *b);
  static bool from_file(Slice filename, BufferData *b);
};

void util_free(BufferData &b);

static void util_free(UndoAction &a) {
  switch (a.type) {
    case ACTIONTYPE_INSERT:
      // puts("Freeing INSERT");
      util_free(a.insert.s);
      break;
    case ACTIONTYPE_DELETE:
      // puts("Freeing DELETE");
      util_free(a.remove.s);
      break;
    case ACTIONTYPE_CURSOR_SNAPSHOT:
      // puts("Freeing CURSOR SNAPSHOT");
      util_free(a.cursors);
      break;
    case ACTIONTYPE_GROUP_BEGIN:
    case ACTIONTYPE_GROUP_END:
      break;
  }
}

UndoAction UndoAction::delete_range(Range r, String s, int cursor_idx) {
  UndoAction a;
  a.type = ACTIONTYPE_DELETE;
  a.remove = {r.a, r.b, s, cursor_idx};
  return a;
}

UndoAction UndoAction::cursor_snapshot(Array<Cursor> cursors) {
  UndoAction a;
  a.type = ACTIONTYPE_CURSOR_SNAPSHOT;
  a.cursors = cursors.copy_shallow();
  return a;
}

UndoAction UndoAction::insert_slice(Pos a, Pos b, Slice s, int cursor_idx) {
  UndoAction act;
  act.type = ACTIONTYPE_INSERT;
  act.insert = {a, b, String::create(s), cursor_idx};
  return act;
}

struct v2 {
  int x;
  int y;
};

struct Canvas {
  Utf8char *chars;
  Color *background_colors;
  Color *text_colors;
  int w, h;
  Color background;
  int margin;
  bool draw_shadow;
  Pos offset;

  void render_strf(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, ...);
  void render(Pos offset);
  void render_str_v(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, va_list args);
  void render_str(Pos p, const Color *text_color, const Color *background_color, int xclip0, int xclip1, Slice s);
  void fill(Color text, Color background);
  void fill_background(Rect r, Color c);
  void fill_textcolor(Rect r, Color c);
  void fill_textcolor(Range range, Color c);
  void blend_textcolor(Range range, Color c);
  void blend_textcolor_additive(Range range, Color c);
  void invert_color(Pos p);
  void fill(Utf8char c);
  void resize(int w, int h);
  void init(int w, int h);
  void _normalize_range(Pos &a, Pos &b);
};

void util_free(Canvas &c) {
  delete [] c.chars;
  delete [] c.background_colors;
  delete [] c.text_colors;
}

enum PaneType {
  PANETYPE_NULL,
  PANETYPE_EDIT,
  PANETYPE_MENU,
  PANETYPE_STATUSMESSAGE,
  PANETYPE_DROPDOWN,
};

struct MenuOption {
  Slice name;
  Slice description;
};

struct BufferView {
  BufferData *data;
  Array<Cursor> cursors;
  Array<Pos> jumplist;
  int jumplist_pos;

  void jumplist_push();
  void jumplist_pop();
  void jumplist_prev();
  void jumplist_next();
  void move_to_y(int marker_idx, int y);
  void move_to_x(int marker_idx, int x);
  void move_to(int marker_idx, Pos p);
  void move_to(int x, int y);
  void move_to(Pos p);
  void move_y(int dy);
  void move_y(int marker_idx, int dy);
  void move_x(int dx);
  void move_x(int marker_idx, int dx);
  void move(int marker_idx, int dx, int dy);
  void action_end() {data->action_end(cursors);}
  void action_begin() {data->action_begin(cursors);}
  void undo() {data->undo(cursors);}
  void redo() {data->redo(cursors);}
  void deduplicate_cursors();
  void collapse_cursors();
  void update();
  bool find_and_move(Slice s, bool stay);
  bool find_and_move(char c, bool stay);
  bool find_and_move_r(Slice s, bool stay);
  bool find_and_move_r(char c, bool stay);
  int advance();
  int advance(Pos &p) {return data->advance(p);}
  int advance(int marker_idx);
  int advance_r();
  int advance_r(Pos &p) {return data->advance_r(p);}
  int advance_r(int marker_idx);
  void goto_endline();
  void goto_endline(int marker_idx);
  void goto_beginline();
  void delete_line() {data->delete_line(cursors);}
  void remove_range(Pos a, Pos b, int cursor_idx) {data->remove_range(cursors, a, b, cursor_idx);}
  void remove_trailing_whitespace();
  void remove_trailing_whitespace(int cursor_idx) {data->remove_trailing_whitespace(cursors, cursor_idx);}
  void autoindent(const int y) {data->autoindent(cursors, y);}
  void autoindent() {data->autoindent(cursors);}
  void push_line(Slice s) {data->push_line(cursors, s);}
  void insert(Slice s) {data->insert(cursors, s);}
  void insert(Pos p, Slice s, int cursor_idx = -1) {data->insert(cursors, p, s, cursor_idx);}
  void insert(Slice s, int cursor_idx) {data->insert(cursors, s, cursor_idx);}
  void insert(Pos p, Utf8char ch) {data->insert(cursors, p, ch);}
  void insert(Utf8char ch, int cursor_idx) {data->insert(cursors, ch, cursor_idx);}
  void insert(Utf8char ch) {data->insert(cursors, ch);}
  void insert_newline() {data->insert_newline(cursors);}
  void insert_newline_below() {data->insert_newline_below(cursors);}
  void insert_tab() {data->insert_tab(cursors);}
  void insert_tab(Pos p) {data->insert_tab(cursors, p);}
  void empty() {data->empty(cursors);}
  void delete_char() {data->delete_char(cursors);}
  Utf8char getchar(int cursor_idx) {return data->getchar(cursors[cursor_idx].pos);}
  Utf8char getchar(Pos p) {return data->getchar(p);}
  int getindent(int y) {return data->getindent(y);}

  static BufferView create(BufferData *data) {
    BufferView b = {data, {}};
    b.cursors += Cursor{};
    return b;
  }
};

void swap_range(BufferData &buffer, Pos &a, Pos &b) {
  Pos tmp = a;
  a = b;
  b = tmp;
  buffer.advance(a);
}

static void util_free(BufferView &b) {
  util_free(b.jumplist);
  util_free(b.cursors);
  b.data = 0;
}

struct Pane;
struct SubPane {
  Pos anchor_pos;
  Pane *pane;
};

struct Pane {
  PaneType type;
  BufferView buffer;

  Rect bounds;
  const Color *background_color;
  const Color *active_highlight_background_color;
  const Color *inactive_highlight_background_color;
  const Color *text_color;
  int _gutter_width;

  bool is_dynamic; // some panes are statically allocated because they are singletons, like the build result pane, status message pane, etc.

  Array<SubPane> subpanes;
  Pane *parent;

  // visual settings
  int margin;

  // type-specific data
  union {
    // PANETYPE_EDIT
    struct {
    };

    // PANETYPE_MENU
    struct {
      // callbacks
      Array<String> (*get_suggestions)();
      Slice prefix;

      int current_suggestion;
      Array<String> suggestions;
    } menu;

    // PANETYPE_DROPDOWN
    struct {
    };
  };

  // methods
  void add_subpane(BufferData *buffer, Pos p);
  void render();
  void update_suggestions();
  void clear_suggestions();
  void switch_buffer(BufferData *d) {
    util_free(buffer);
    buffer = BufferView::create(d);
  }

  // internal methods
  void render_edit();
  void render_as_dropdown();
  void render_goto_definition();
  void render_menu();
  void render_single_line();
  void render_menu_popup();
  void render_syntax_highlight(Canvas &canvas, int y1);
  int calc_top_visible_row() const;
  int calc_left_visible_column() const;
  int calc_max_subpane_depth() const;

  Slice* menu_get_selection() {
    if (!menu.suggestions.size)
      return 0;
    int i = clamp(menu.current_suggestion, 0, menu.suggestions.size);
    return &menu.suggestions[i].slice;
  };

  int numchars_x() const;
  int numchars_y() const;
  Pos slot2pixel(Pos p) const;
  int slot2pixelx(int x) const;
  int slot2pixely(int y) const;
  Pos slot2global(Pos p) const;
  Pos buf2char(Pos p) const;
  Pos buf2pixel(Pos p) const;

  static void init_edit(Pane &p, BufferData *b, Color *background_color, Color *text_color, Color *active_highlight_background_color, Color *inactive_highlight_background_color, bool is_dynamic);
};

void util_free(Pane &p) {
  util_free(p.buffer);
}

void Pane::init_edit(Pane &p,
                      BufferData *b,
                      Color *background_color,
                      Color *text_color,
                      Color *active_highlight_background_color,
                      Color *inactive_highlight_background_color, bool is_dynamic) {
  util_free(p);
  p = {};
  p.type = PANETYPE_EDIT;
  p.buffer = {b, Array<Cursor>{}};
  p.buffer.cursors += {};
  p.background_color = background_color;
  p.text_color = text_color;
  p.active_highlight_background_color = active_highlight_background_color;
  p.inactive_highlight_background_color = inactive_highlight_background_color;
  p.is_dynamic = is_dynamic;
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

struct Location {
  BufferData *buffer;
  Array<Pos> cursors;
};

void util_free(Location &l) {
  util_free(l.cursors);
  l.buffer = 0;
}

enum PromptType {
  PROMPT_STRING,
  PROMPT_INT,
  PROMPT_FLOAT,
  PROMPT_BOOLEAN
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
  Path ttf_file;

  /* settings */
  Color default_background_color;
  Color default_text_color;
  Color default_gutter_text_color;
  Color number_color;
  Color comment_color;
  Color string_color;
  Color operator_color;
  Color default_keyword_color;
  Color inactive_highlight_background_color;
  Color identifier_color;
  Color default_search_term_text_color;
  Color search_term_text_color;
  Color marker_inactive_color;
  PoppedColor marker_background_color;
  PoppedColor search_term_background_color;
  PoppedColor active_highlight_background_color;
  RotatingColor default_marker_background_color;
  PoppedColor bottom_pane_highlight;

  /* editor state */
  Path current_working_directory;
  Array<Pane*> editing_panes;
  Pane *bottom_pane;
  Pane *selected_pane; // the pane that the marker currently is on, could be everything from editing pane, to menu pane, to filesearch pane
  Pane *editing_pane; // the pane that is currently being edited on, regardless if you happen to be in filesearch or menu

  Pane menu_pane;
  BufferData menu_buffer;
  Pane status_message_pane;
  BufferData status_message_buffer;
  Pane dropdown_pane;
  BufferData dropdown_buffer;
  BufferData null_buffer;
  Pane build_result_pane;
  BufferData build_result_buffer;

  float activation_meter;

  String search_term;

  Array<BufferData*> buffers;

  // instead of removing stuff on the spot, which can cause issues, we push them to these lists and delete them at the end of the frame
  Array<BufferData*> buffers_to_remove;
  Array<Pane*> panes_to_remove;

  Mode mode;

  struct {
    bool cursor_dirty;
  } flags;

  /* prompt state */
  PromptType prompt_type;
  bool prompt_success;
  union {
    String string;
    int integer;
    double floating;
    bool boolean;
  } prompt_result;
  void (*prompt_callback)(void);

  /* goto state */
  unsigned int goto_line_number; /* unsigned in order to prevent undefined behavior on wrap around */

  /* search state */
  int search_failed;
  Pos search_begin_pos;

  /* file tree state */
  Array<Path> files;
  
  /* visual mode state */
  Location visual_start; // starting position of visual mode
  bool visual_entire_line;

  /* some settings */
  int tab_width; /* how wide are tabs when rendered */
  int default_tab_type; /* 0 for tabs, 1+ for spaces */
};

State G;

static int get_text_offset_y(int font_height) {
  return (int)(-font_height*3.3f/15.0f); // TODO: get this from truetype?
}

typedef int Key;
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

  G.status_message_pane.buffer.empty();
  G.status_message_buffer[0].clear();
  G.status_message_buffer[0].appendv(fmt, args);

  // grab only the first line
  int i;
  if (G.status_message_buffer[0].find('\n', &i)) {
    if (i > 0 && G.status_message_buffer[0][i-1] == '\r')
      --i;
    G.status_message_buffer[0].length = i;
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
  {(char*)"::", 2},
  {(char*)"--", 2},
  {(char*)"+", 1},
  {(char*)"-", 1},
  {(char*)"*", 1},
  {(char*)"/", 1},
  {(char*)"&", 1},
  {(char*)"%", 1},
  {(char*)"=", 1},
  {(char*)":", 1},
  {(char*)"<", 1},
  {(char*)">", 1},
};

static const Color COLOR_PINK = {236, 64, 122, 255};
static const Color COLOR_YELLOW = {255, 235, 59, 255};
static const Color COLOR_LIGHT_YELLOW = {255, 240, 79, 255};
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
static const Color COLOR_WHITE = {240, 240, 240, 255};
static const Color COLOR_BLUE = {79,195,247, 255};
static const Color COLOR_DARK_BLUE = {124, 173, 213, 255};

enum KeywordType {
  KEYWORD_NONE,
  KEYWORD_CONTROL, // control flow
  KEYWORD_TYPE,
  KEYWORD_SPECIFIER,
  KEYWORD_DEFINITION,
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
  {"null", KEYWORD_CONSTANT},

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
  {"IEnumerator", KEYWORD_TYPE},
  {"byte", KEYWORD_TYPE},

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
  {"public", KEYWORD_SPECIFIER},
  {"private", KEYWORD_SPECIFIER},
  {"in", KEYWORD_SPECIFIER},
  {"delegate", KEYWORD_SPECIFIER},
  {"protected", KEYWORD_SPECIFIER},
  {"override", KEYWORD_SPECIFIER},
  {"virtual", KEYWORD_SPECIFIER},
  {"abstract", KEYWORD_SPECIFIER},

  // declarations

  {"struct", KEYWORD_DEFINITION},
  {"class", KEYWORD_DEFINITION},
  {"union", KEYWORD_DEFINITION},
  {"enum", KEYWORD_DEFINITION},
  {"typedef", KEYWORD_DEFINITION},
  {"template", KEYWORD_DEFINITION},
  {"operator", KEYWORD_DEFINITION},
  {"namespace", KEYWORD_DEFINITION},

  // macro

  {"#include", KEYWORD_MACRO},
  {"#define", KEYWORD_MACRO},
  {"#undef", KEYWORD_MACRO},
  {"#ifdef", KEYWORD_MACRO},
  {"#ifndef", KEYWORD_MACRO},
  {"#endif", KEYWORD_MACRO},
  {"#elif", KEYWORD_MACRO},
  {"#else", KEYWORD_MACRO},
  {"#if", KEYWORD_MACRO},
  {"#error", KEYWORD_MACRO},

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
  {"yield", KEYWORD_CONTROL},
  {"foreach", KEYWORD_CONTROL},
  {"default", KEYWORD_CONTROL},

};


/****** @TOKENIZER ******/

static void tokenize(BufferData &b) {
  Array<TokenInfo> tokens = b.tokens;
  util_free(tokens);
  util_free(b.identifiers);
  int x = 0;
  int y = 0;

  for (;;) {
    TokenInfo t = {TOKEN_NULL, x, y};
    #define NEXT(n) (x += n, c = line[x])
    if (y >= b.lines.size)
      break;
    Slice line = b.lines[y].slice;

    // endline
    char c;
    if (x >= b.lines[y].length) {
      ++y, x = 0;
      continue;
    }
    c = line[x];

    // whitespace
    if (isspace(c)) {
      NEXT(1);
      goto token_done;
    }

    // identifier
    if (is_identifier_head(c)) {
      t.token = TOKEN_IDENTIFIER;
      int identifier_start = x;
      NEXT(1);
      while (is_identifier_tail(c))
        NEXT(1);
      Slice identifier = line(identifier_start, x);
      // check if identifier is already added
      String *s = b.identifiers.find(identifier);
      if (!s)
        b.identifiers += String::create(identifier);
      goto token_done;
    }

    // block comment
    if (line.begins_with(x, "/*")) {
      t.token = TOKEN_BLOCK_COMMENT;
      NEXT(2);
      // goto matching end block
      for (;;) {
        // EOF
        if (y >= b.lines.size)
          goto token_done;
        // EOL
        if (x >= line.length) {
          ++y;
          if (y == b.lines.size)
            break;
          line = b.lines[y].slice;
          x = 0;
          continue;
        }
        // End block
        if (line.begins_with(x, "*/")) {
          NEXT(2);
          break;
        }
        NEXT(1);
      }
      goto token_done;
    }

    // line comment
    if (line.begins_with(x, "//")) {
      t.token = TOKEN_LINE_COMMENT;
      x = line.length;
      goto token_done;
    }

    // number
    if (is_number_head(c)) {
      t.token = TOKEN_NUMBER;
      NEXT(1);
      while (is_number_tail(c))
        NEXT(1);
      if (line[x] == '.' && x+1 < line.length && isdigit(line[x+1])) {
        NEXT(2);
        while (isdigit(c)) NEXT(1);
      }
      while (is_number_modifier(c))
        NEXT(1);
      goto token_done;
    }

    // string
    if (c == '"' || c == '\'') {
      t.token = TOKEN_STRING;
      const char str_char = c;
      NEXT(1);
      while (1) {
        if (x >= line.length)
          break;
        if (c == str_char && (line[x-1] != '\\' || (x >= 2 && line[x-2] == '\\')))
          break;
        NEXT(1);
      }
      if (x < line.length)
        NEXT(1);
      goto token_done;
    }

    // operators
    for (int i = 0; i < (int)ARRAY_LEN(operators); ++i) {
      if (line.begins_with(x, operators[i])) {
        t.token = TOKEN_OPERATOR;
        NEXT(operators[i].length);
        goto token_done;
      }
    }

    // single char token
    t.token = (Token)c;
    NEXT(1);

    token_done:;
    if (t.token != TOKEN_NULL) {
      t.b = {x,y};
      if (t.a.y == t.b.y)
        t.str = b.getslice(t.r);
      tokens += t;
    }
    #undef NEXT
  }

  tokens += {TOKEN_EOF, 0, b.lines.size, 0, b.lines.size};

  b.tokens = tokens;

  // TODO: find definitions
  Array<Range> definitions = b.definitions;
  util_free(definitions);
  for (int i = 0; i < tokens.size; ++i) {
    TokenInfo ti = tokens[i];
    switch (ti.token) {
      case TOKEN_IDENTIFIER: {
        if (i+1 < tokens.size && ti.str == "#define") {
          definitions += tokens[i+1].r;
          break;
        }

        if (i+2 < tokens.size && (ti.str == "struct" || ti.str == "enum" || ti.str == "class") &&
            tokens[i+1].token == TOKEN_IDENTIFIER &&
            tokens[i+2].token == '{') {
          definitions += tokens[i+1].r;
          break;
        }

        // check for function definition
        {
          // is it a keyword, then ignore (things like else if (..) is not a definition)
          for (int j = 0; j < (int)ARRAY_LEN(keywords); ++j)
            if (ti.str == keywords[j].name && keywords[j].type != KEYWORD_TYPE)
              goto no_definition;

          {
            int j = i;
            // skip pointer and references
            for (++j; j < tokens.size && tokens[j].token == TOKEN_OPERATOR; ++j) {
              if (tokens[j].str == "*" || tokens[j].str == "&")
                continue;
              goto no_definition;
            }

            if (j+1 < tokens.size &&
                tokens[j].token == TOKEN_IDENTIFIER &&
                tokens[j+1].token == '(') {
              definitions += {tokens[j].a, tokens[j].b};
            }
            else if (j+3 < tokens.size &&
                     tokens[j].token == TOKEN_IDENTIFIER &&
                     tokens[j+1].token == TOKEN_OPERATOR &&
                     tokens[j+1].str == "::" &&
                     tokens[j+2].token == TOKEN_IDENTIFIER &&
                     tokens[j+3].token == '(') {
              definitions += {tokens[j].a, tokens[j+2].b};
            }
          }
          no_definition:;
        }
        break;}
      default:
        break;
    }
  }
  b.definitions = definitions;
}

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

static void save_buffer(BufferData *b) {
  FILE* f;
  int i;

  if (!b->is_bound_to_file())
    return;

  if (file_open(&f, b->filename.chars, "wb")) {
    status_message_set("Could not open file {} for writing: %s", (Slice)b->filename.slice, cman_strerror(errno));
    return;
  }

  const int endline_len = strlen(b->endline_string);

  /* TODO: actually write to a temporary file, and when we have succeeded, rename it over the old file */
  for (i = 0; i < b->num_lines(); ++i) {
    unsigned int num_to_write = b->lines[i].length;

    if (num_to_write && file_write(f, b->lines[i].chars, num_to_write)) {
      status_message_set("Failed to write to {}: %s", (Slice)b->filename.slice, cman_strerror(errno));
      goto err;
    }

    // endline
    if (i < b->num_lines()-1) {
      if (file_write(f, b->endline_string, endline_len)) {
        status_message_set("Failed to write to {}: %s", (Slice)b->filename.slice, cman_strerror(errno));
        goto err;
      }
    }
  }

  status_message_set("Wrote %i lines to {}", b->num_lines(), (Slice)b->filename.slice);

  b->_last_save_undo_action = b->_next_undo_action;

  err:
  fclose(f);
}

static void menu_option_save() {
  save_buffer(G.editing_pane->buffer.data);
}

void state_free();

void editor_exit(int exitcode) {
  state_free();
  exit(exitcode);
}

static void menu_option_quit() {
  editor_exit(0);
}

static void menu_option_show_tab_type() {
  if (G.editing_pane->buffer.data->tab_type == 0)
    status_message_set("Using tabs");
  else
    status_message_set("Tabs is %i spaces", G.editing_pane->buffer.data->tab_type);
}

static void menu_option_print_definitions() {
  BufferData &b = *G.editing_pane->buffer.data;
  for (Range r : b.definitions) {
    Slice s = b.getslice(r);
    printf("%.*s\n", s.length, s.chars);
  }
}

static void mode_cwd();
static void menu_option_chdir() {
  mode_cwd();
}

static void menu_option_reload() {
  BufferData *b = G.editing_pane->buffer.data;
  if (!b->is_bound_to_file())
    return;

  if (BufferData::reload(b))
    status_message_set("Reloaded {}", (Slice)b->filename.slice);
  else
    status_message_set("Failed to reload {}", (Slice)b->filename.slice);
}

static void menu_option_reloadall() {
  int count = 0;
  for (BufferData *b : G.buffers)
    if (b->is_bound_to_file())
      BufferData::reload(b), ++count;
  status_message_set("Reloaded %i files", count);
}

static void menu_option_close() {
  BufferData *b = G.editing_pane->buffer.data;
  status_message_set("Closed {}", (Slice)b->name());
  G.buffers_to_remove += b;
}

static void menu_option_closeall() {
  status_message_set("Closed all buffers");
  for (BufferData *b : G.buffers)
    G.buffers_to_remove += b;
}

#define yield(name) do {step = name; return;} while(0); name:
#define yield_break do {step = {}; return;} while(0)

static void mode_prompt(Slice msg, void (*callback)(void), PromptType type);
static void menu_option_blame() {
  COROUTINE_BEGIN

  if (!G.editing_pane->buffer.data->is_bound_to_file())
    yield_break;

  // ask the user if it's okay we save before blaming
  if (G.editing_pane->buffer.data->modified()) {
    mode_prompt(Slice::create("Will save buffer in order to blame, ok? [y/n]"), menu_option_blame, PROMPT_BOOLEAN);
    yield(CHECK_PROMPT_RESULT);
    if (!G.prompt_success || !G.prompt_result.boolean) {
      status_message_set("Cancelling blame");
      yield_break;
    }
  }

  BufferData &b = *G.editing_pane->buffer.data;
  if (b.blame.data.size) {
    util_free(b.blame);
    yield_break;
  }
  util_free(b.blame);

  save_buffer(&b);

  // call git
  String cmd = String::createf("git blame {} --porcelain", b.filename.slice);
  String out = {};
  int errcode;
  if (!call(cmd.slice, &errcode, &out)) {
    status_message_set("System call \"{}\" failed", cmd);
    yield_break;
  }
  if (errcode) {
    // TODO: handle newlines
    status_message_set("git blame returned exit code %i: {}", errcode, out.slice(0,-2));
    yield_break;
  }

  b.blame.storage.push();
  git_parse_blame(out, &b.blame.data);
  b.blame.storage.pop();

  util_free(out);
  util_free(cmd);
  yield_break;

  COROUTINE_END
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
  },
  {
    Slice::create("show_definitions"),
    Slice::create("Show all definitions in current buffer"),
    menu_option_print_definitions
  },
  {
    Slice::create("chdir"),
    Slice::create("Change current working directory"),
    menu_option_chdir
  },
  {
    Slice::create("reload"),
    Slice::create("Reload current buffer"),
    menu_option_reload
  },
  {
    Slice::create("reloadall"),
    Slice::create("Reload all buffers"),
    menu_option_reloadall
  },
  {
    Slice::create("close"),
    Slice::create("Close current buffer"),
    menu_option_close
  },
  {
    Slice::create("closeall"),
    Slice::create("Close all open buffers"),
    menu_option_closeall
  },
  {
    Slice::create("blame"),
    Slice::create("git blame on current file"),
    menu_option_blame
  }
};

static Array<String> easy_fuzzy_match(Slice input, View<Slice> options, bool ignore_identical_strings);

static void mode_cleanup() {
  G.flags.cursor_dirty = true;

  if (G.mode == MODE_FILESEARCH || G.mode == MODE_SEARCH || G.mode == MODE_CWD) {
    G.menu_pane.buffer.empty();
  }
  G.menu_pane.clear_suggestions();

  if (G.mode == MODE_INSERT)
    G.editing_pane->buffer.action_end();
}

static Array<String> get_search_suggestions() {
  return easy_fuzzy_match(G.menu_buffer.lines[0].slice, VIEW(G.editing_pane->buffer.data->identifiers, slice), false);
}

static void mode_search() {
  mode_cleanup();

  G.editing_pane->buffer.collapse_cursors();

  G.mode = MODE_SEARCH;
  G.bottom_pane_highlight.reset();
  G.selected_pane = &G.menu_pane;
  G.search_begin_pos = G.editing_pane->buffer.cursors[0].pos;
  G.search_failed = 0;

  G.bottom_pane = &G.menu_pane;
  G.menu_pane.menu.get_suggestions = &get_search_suggestions;
  G.menu_pane.menu.prefix = Slice::create("search");

  G.menu_pane.buffer.empty();
}

static Array<String> get_cwd_suggestions() {
  // get folder

  // first try relative path, and then try absolute
  Path rel = G.current_working_directory.copy(); rel.push(G.menu_buffer.lines[0].slice);
  Path rel_dir = rel.copy();
  Path abs = Path::create(G.menu_buffer.lines[0].slice);
  Path abs_dir = abs.copy();

  Path *p = &rel;
  Path *dir = &rel_dir;

  if (!File::isdir(rel_dir)) {
    rel_dir.pop();
    if (!File::isdir(rel_dir)) {
      p = &abs;
      dir = &abs_dir;
      if (!File::isdir(*dir))
        dir->pop();
    }
  }

  Array<Path> paths = {};
  File::list_files(*dir, &paths);
  for (int i = 0; i < paths.size; ++i) {
    if (!File::isdir(paths[i])) {
      util_free(paths[i]);
      paths[i] = paths[--paths.size];
      --i;
    }
  }

  Array<Slice> match_queries = {};
  for (Path path : paths)
    match_queries += path.name();

  StackArray<FuzzyMatch, 15> matches = {};
  int n = fuzzy_match(p->name(), view(match_queries), view(matches), false);
  Array<String> result = {};

  // StringBuffer sb = {};
  for (int i = 0; i < n; ++i) {
    // sb.length = 0;
    // sb.appendf("{} - %f", matches[i].str.slice, matches[i].points);
    // result += String::create(sb.slice);
    result += String::create(paths[matches[i].idx].string.slice);
  }
  // util_free(sb);

  util_free(match_queries);
  util_free(paths);
  util_free(rel);
  util_free(rel_dir);
  util_free(abs);
  util_free(abs_dir);
  return result;
}

static void mode_cwd() {
  mode_cleanup();
  G.mode = MODE_CWD;

  G.selected_pane = &G.menu_pane;
  G.bottom_pane = &G.menu_pane;
  G.menu_pane.menu.get_suggestions = &get_cwd_suggestions;
  G.menu_pane.menu.prefix = Slice::create("chdir");

  G.menu_pane.buffer.empty();
  G.menu_pane.buffer.insert(G.current_working_directory.string.slice);
  G.menu_pane.buffer.insert(Utf8char::create(Path::separator));
  G.menu_pane.update_suggestions();
}

static void mode_prompt(Slice msg, void (*callback)(void), PromptType type) {
  mode_cleanup();
  G.mode = MODE_PROMPT;
  G.menu_pane.menu.prefix = msg;
  G.prompt_type = type;
  G.prompt_callback = callback;

  G.selected_pane = &G.menu_pane;
  G.bottom_pane = &G.menu_pane;
  G.menu_pane.menu.get_suggestions = 0;

  G.menu_pane.buffer.empty();
}

static Array<String> get_goto_definition_suggestions() {
  BufferData &b = *G.editing_pane->buffer.data;
  Array<Slice> defs = {};
  defs.reserve(b.definitions.size);
  for (Range r : b.definitions)
    defs += b.getslice(r);

  Array<String> result = easy_fuzzy_match(G.menu_buffer.lines[0].slice, view(defs), false);
  util_free(defs);
  return result;
}

static void mode_goto_definition() {
  mode_cleanup();
  G.mode = MODE_GOTO_DEFINITION;
  G.selected_pane = &G.menu_pane;

  G.bottom_pane = &G.menu_pane;
  G.menu_pane.menu.get_suggestions = &get_goto_definition_suggestions;
  G.menu_pane.menu.prefix = Slice::create("goto def");

  G.menu_pane.buffer.empty();
  G.menu_pane.update_suggestions();
}

static Array<String> get_filesearch_suggestions() {
  Array<Slice> filenames = {};
  filenames.reserve(G.files.size);
  for (Path p : G.files)
    filenames += p.name();
  Array<String> match = easy_fuzzy_match(G.menu_buffer.lines[0].slice, view(filenames), false);
  util_free(filenames);
  return match;
}

static void mode_filesearch() {
  mode_cleanup();
  G.mode = MODE_FILESEARCH;
  G.bottom_pane_highlight.reset();
  G.selected_pane = &G.menu_pane;
  G.bottom_pane = &G.menu_pane;
  G.menu_pane.menu.get_suggestions = &get_filesearch_suggestions;
  G.menu_pane.menu.prefix = Slice::create("open");
  G.menu_pane.buffer.empty();
  G.menu_pane.update_suggestions();
}

static void mode_goto() {
  mode_cleanup();
  G.mode = MODE_GOTO;
  G.goto_line_number = 0;
  G.bottom_pane = &G.status_message_pane;
  status_message_set("goto");
}

static void mode_yank() {
  mode_cleanup();
  G.mode = MODE_YANK;
  G.bottom_pane = &G.status_message_pane;
  status_message_set("yank");
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
  G.selected_pane = G.editing_pane;

  if (set_message)
    status_message_set("normal");
}

static void mode_insert() {
  mode_cleanup();
  G.default_marker_background_color.jump();

  util_free(G.visual_start);

  G.mode = MODE_INSERT;
  G.bottom_pane = &G.status_message_pane;

  G.editing_pane->buffer.action_begin();

  // so even if the caller did some changes, and commits them with action_end, it will not have an effect
  // since we do action_begin here. So we need to do things that action_end do manually here
  tokenize(*G.editing_pane->buffer.data);

  status_message_set("insert");
}

static Array<String> get_menu_suggestions() {
  return easy_fuzzy_match(G.menu_buffer.lines[0].slice, VIEW_FROM_ARRAY(menu_options, opt.name), false);
}

static void mode_menu() {
  mode_cleanup();
  G.mode = MODE_MENU;
  G.selected_pane = &G.menu_pane;
  G.bottom_pane_highlight.reset();
  G.bottom_pane = &G.menu_pane;
  G.menu_pane.menu.get_suggestions = &get_menu_suggestions;
  G.menu_pane.menu.prefix = Slice::create("menu");
  G.menu_pane.buffer.empty();
  G.menu_pane.update_suggestions();
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

static int pixel2charx(int x) {
  return x/G.font_width;
}

static int pixel2chary(int y) {
  return y/G.line_height;
}

bool BufferData::find_start_of_identifier(Pos pos, Pos *pout) {
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

static bool dropdown_autocomplete(BufferView &b) {
  if (G.dropdown_buffer.isempty())
    return false;

  Pos start;
  if (!b.data->find_start_of_identifier(b.cursors[0].pos, &start))
    return false;
  b.action_begin();
  for (int i = b.cursors[0].x - start.x; i; --i)
    b.data->delete_char(b.cursors);
  b.insert(G.dropdown_buffer[G.dropdown_pane.buffer.cursors[0].y].slice);
  b.action_end();
  return true;
}

static void render_dropdown(Pane *pane) {
  BufferView &b = pane->buffer;

  // don't show dropdown unless in edit mode
  if (pane->type == PANETYPE_EDIT && G.mode != MODE_INSERT)
    return;

  // we have to be on an identifier
  Pos identifier_start;
  if (!b.data->find_start_of_identifier(b.cursors[0].pos, &identifier_start))
    return;

  // since fuzzy matching is expensive, we only update we moved since last time
  if (G.flags.cursor_dirty) {
    Slice identifier = b.data->gettoken(identifier_start)->str;
    StackArray<FuzzyMatch, 10> best_matches;
    View<Slice> input = VIEW(b.data->identifiers, slice);
    best_matches.size = fuzzy_match(identifier, input, view(best_matches), true);

    G.dropdown_pane.buffer.empty();
    for (int i = 0; i < best_matches.size; ++i)
      G.dropdown_pane.buffer.push_line(best_matches[i].str);
    G.dropdown_pane.buffer.cursors[0] = {};
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

// TODO: use a unified key interface instead of this hack
static bool insert_default(Pane &p, Key key) {
  BufferView &b = p.buffer;

  /* TODO: should not set `modified` if we just enter and exit insert mode */
  if (key == KEY_ESCAPE) {
    mode_normal(true);
    return false;
  }

  if (key >= 0 && key <= 125) {
    b.insert(Utf8char::create((char)key));
    return true;
  }

  switch (key) {
    case KEY_TAB:
      b.insert_tab();
      return true;

    case KEY_BACKSPACE:
      b.data->delete_char(b.cursors);
      return true;
  }

  return false;
}

static void move_to_left_brace(BufferView &buffer, char leftbrace, char rightbrace, bool allow_inner_blocks = false) {
  for (int i = 0; i < buffer.cursors.size; ++i) {
    Pos p = buffer.cursors[i].pos;
    int depth = 0;
    TokenInfo *t = buffer.data->gettoken(p);
    if (t && t->token == leftbrace && t->a < p) {
      buffer.move_to(i, t->a);
      continue;
    }
    if (t && (t->token == leftbrace || t->token == rightbrace))
      --t;
    for (; t >= buffer.data->tokens.begin(); --t) {
      if (t->token == rightbrace)
        ++depth;
      if (t->token == leftbrace) {
        --depth;
        if (depth < 0 || (allow_inner_blocks && depth == 0)) {
          buffer.move_to(i, t->a);
          break;
        }
      }
    }
  }
}

static void move_to_right_brace(BufferView &buffer, char leftbrace, char rightbrace, bool allow_inner_blocks = false) {
  for (int i = 0; i < buffer.cursors.size; ++i) {
    Pos p = buffer.cursors[i].pos;
    int depth = 0;
    TokenInfo *t = buffer.data->gettoken(p);
    if (t && t->token == rightbrace && p < t->a) {
      buffer.move_to(i, t->a);
      continue;
    }
    if (t && (t->token == leftbrace || t->token == rightbrace))
      ++t;
    for (; t < buffer.data->tokens.end(); ++t) {
      if (t->token == leftbrace) 
        ++depth;
      if (t->token == rightbrace) {
        --depth;
        if (depth < 0 || (allow_inner_blocks && depth == 0)) {
          buffer.move_to(i, t->a);
          break;
        }
      }
    }
  }
}

static bool movement_default(BufferView &buffer, int key) {
  switch (key) {
    case 'b': {
      int i;
      for (i = 0; i < buffer.cursors.size; ++i) {
        if (buffer.advance_r(i))
          break;

        Utf8char c = buffer.getchar(i);
        if (c.isspace()) {
          while (c = buffer.getchar(i), c.isspace())
            if (buffer.advance_r(i))
              break;
        }

        if (is_identifier_tail(c) || is_identifier_head(c)) {
          while (c = buffer.getchar(i), is_identifier_tail(c) || is_identifier_head(c))
            if (buffer.advance_r(i))
              break;
        }
        else {
          while (c = buffer.getchar(i), (!is_identifier_head(c) && !c.isspace()))
            if (buffer.advance_r(i))
              break;
        }

        buffer.advance(i);
      }
      break;}

    case 'w': {
      int i;
      for (i = 0; i < buffer.cursors.size; ++i) {
        // if in word, keep going to end of word
        Utf8char c = buffer.getchar(i);
        if (is_identifier_tail(c) || is_identifier_head(c)) {
          while (c = buffer.getchar(i), is_identifier_tail(c) || is_identifier_head(c))
            if (buffer.advance(i))
              break;
          while (c = buffer.getchar(i), c.isspace())
            if (buffer.advance(i))
              break;
        }
        else if (c.isspace()) {
          // go to start of next word
          while (c = buffer.getchar(i), c.isspace())
            if (buffer.advance(i))
              break;
        }
        else {
          while (c = buffer.getchar(i), (!is_identifier_head(c) && !c.isspace()))
            if (buffer.advance(i))
              break;
          while (c = buffer.getchar(i), c.isspace())
            if (buffer.advance(i))
              break;
        }
      }
      break;}

    case 'n':
      G.search_term_background_color.reset();
      buffer.jumplist_push();
      if (!buffer.find_and_move(G.search_term.slice, false))
        status_message_set("'{}' not found", (Slice)G.menu_buffer[0].slice);
      else
        buffer.jumplist_push();
      break;

    case 'J':
      for (int i = 0; i < buffer.cursors.size; ++i) {
        int prev_indent = buffer.getindent(buffer.cursors[i].y);
        status_message_set("jumping to next line of indent %i", prev_indent);
        bool different_indent_encountered = false;
        for (int y = buffer.cursors[i].y+1; y < buffer.data->lines.size; ++y) {
          if (buffer.data->lines[y].length == 0 || buffer.getindent(y) != prev_indent) {
            different_indent_encountered = true;
            continue;
          }
          if (different_indent_encountered) {
            buffer.move_to_y(i, y);
            break;
          }
        }
      }
      break;

    case 'K':
      for (int i = 0; i < buffer.cursors.size; ++i) {
        int prev_indent = buffer.getindent(buffer.cursors[i].y);
        status_message_set("jumping to next line of indent %i", prev_indent);
        bool different_indent_encountered = false;
        for (int y = buffer.cursors[i].y-1; y >= 0; --y) {
          if (buffer.data->lines[y].length == 0 || buffer.getindent(y) != prev_indent) {
            different_indent_encountered = true;
            continue;
          }
          if (different_indent_encountered) {
            buffer.move_to_y(i, y);
            break;
          }
        }
      }
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

    case 'N':
      G.search_term_background_color.reset();
      buffer.jumplist_push();
      if (!buffer.find_and_move_r(G.search_term.slice, false))
        status_message_set("'{}' not found", (Slice)G.search_term.slice);
      else
        buffer.jumplist_push();
      break;

    case '{':
      move_to_left_brace(buffer, '{', '}');
      break;

    case '}':
      move_to_right_brace(buffer, '{', '}');
      break;

    case '[':
      move_to_left_brace(buffer, '[', ']');
      break;

    case ']':
      move_to_right_brace(buffer, '[', ']');
      break;

    case '(':
      move_to_left_brace(buffer, '(', ')');
      break;

    case ')':
      move_to_right_brace(buffer, '(', ')');
      break;

    case '*': {
      TokenInfo *t = buffer.data->gettoken(buffer.cursors[0].pos);
      if (!t)
        break;
      if (t->token != TOKEN_IDENTIFIER)
        break;
      G.search_term_background_color.reset();
      util_free(G.search_term);
      G.search_term = String::create(t->str);
      buffer.find_and_move(G.search_term.slice, false);
      break;}

    case '#': {
      TokenInfo *t = buffer.data->gettoken(buffer.cursors[0].pos);
      if (!t)
        break;
      if (t->token != TOKEN_IDENTIFIER)
        break;
      G.search_term_background_color.reset();
      util_free(G.search_term);
      G.search_term = String::create(t->str);
      buffer.find_and_move_r(G.search_term.slice, false);
      break;}

    default:
      return false;
  }
  return true;
}

static void _filetree_fill(Path path) {
  Array<Path> files = {};
  if (!File::list_files(path, &files))
    goto err;

  for (Path f : files) {
    if (File::filetype(f) == FILETYPE_DIR)
      _filetree_fill(f);
    else {
      G.files += f.copy();
    }
  }

  err:
  util_free(files);
}

static void filetree_init() {
  util_free(G.files);
  Path cwd = {};
  if (File::cwd(&cwd))
    _filetree_fill(cwd);
  util_free(cwd);
}

static void state_init() {

  if (!File::cwd(&G.current_working_directory))
    fprintf(stderr, "Failed to find current working directory, something is very wrong\n"), exit(1);
  G.ttf_file = G.current_working_directory.copy();
  G.ttf_file.push("font.ttf");

  // initialize graphics library
  if (graphics_init(&G.window))
    exit(1);
  G.font_height = 14;
  if (graphics_text_init(G.ttf_file.string.chars, G.font_height))
    exit(1);
  if (graphics_quad_init())
    exit(1);

  SDL_GetWindowSize(G.window, &G.win_width, &G.win_height);

  // font stuff
  G.font_width = graphics_get_font_advance(G.font_height);
  G.tab_width = 4;
  G.default_tab_type = 4;
  G.line_margin = 0;
  G.line_height = G.font_height + G.line_margin;

  // @colors!
  G.default_text_color = COLOR_WHITE;
  G.default_background_color = {45, 45, 45, 255};
  G.default_gutter_text_color = {127, 127, 127, 255};
  G.number_color = COLOR_RED;
  G.string_color =                          COLOR_RED;
  G.operator_color =                        COLOR_WHITE; // COLOR_RED;
  G.comment_color = COLOR_BLUEGREY;
  G.identifier_color = COLOR_GREEN;
  G.default_search_term_text_color = G.search_term_text_color = COLOR_WHITE;
  G.marker_inactive_color = COLOR_LIGHT_GREY;
  G.inactive_highlight_background_color = Color::blend(G.default_background_color, COLOR_WHITE, 0.1f);

  keyword_colors[KEYWORD_NONE]        = {};
  keyword_colors[KEYWORD_CONTROL]     = COLOR_DARK_BLUE;
  keyword_colors[KEYWORD_TYPE]        = COLOR_DARK_BLUE;
  keyword_colors[KEYWORD_SPECIFIER]   = COLOR_DARK_BLUE;
  keyword_colors[KEYWORD_DEFINITION] = COLOR_PINK;
  keyword_colors[KEYWORD_FUNCTION]    = COLOR_BLUE;
  keyword_colors[KEYWORD_MACRO]       = COLOR_DEEP_ORANGE;
  keyword_colors[KEYWORD_CONSTANT]    = G.number_color;

  G.active_highlight_background_color.base_color = G.default_background_color;
  G.active_highlight_background_color.popped_color = COLOR_WHITE;
  G.active_highlight_background_color.speed = 1.0f;
  G.active_highlight_background_color.cooldown = 1.0f;
  G.active_highlight_background_color.min = 0.1f;
  G.active_highlight_background_color.max = 0.18f;

  G.bottom_pane_highlight.base_color = G.default_background_color;
  G.bottom_pane_highlight.popped_color = COLOR_WHITE;
  G.bottom_pane_highlight.speed = 1.3f;
  G.bottom_pane_highlight.cooldown = 0.4f;
  G.bottom_pane_highlight.min = 0.05f;
  G.bottom_pane_highlight.max = 0.15f;

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

  // default buffer, and initial pane
  G.null_buffer.init(false, Slice::create("[Scratch]"));
  G.buffers += &G.null_buffer;
  Pane *main_pane = new Pane{};
  Pane::init_edit(*main_pane, &G.null_buffer, &G.default_background_color, &G.default_text_color, &G.active_highlight_background_color.color, &G.inactive_highlight_background_color, true);

  G.build_result_buffer.init(false, Slice::create("[Build Result]"));
  G.buffers += &G.build_result_buffer;

  // search pane
  G.menu_pane.type = PANETYPE_MENU;
  G.menu_pane.buffer = BufferView::create(&G.menu_buffer);
  G.menu_pane.buffer.empty();
  G.menu_pane.background_color = &G.bottom_pane_highlight.color;
  G.menu_pane.text_color = &G.default_text_color;
  G.menu_pane.active_highlight_background_color = &COLOR_DEEP_PURPLE;
  G.menu_pane.margin = 5;

  // dropdown pane
  G.dropdown_pane.type = PANETYPE_DROPDOWN;
  G.dropdown_pane.buffer = BufferView::create(&G.dropdown_buffer);
  G.dropdown_pane.buffer.empty();
  G.dropdown_pane.background_color = &COLOR_GREY;
  G.dropdown_pane.text_color = &COLOR_WHITE;
  G.dropdown_pane.margin = 5;
  G.dropdown_pane.active_highlight_background_color = &COLOR_DEEP_PURPLE;

  // status message pane
  G.status_message_pane.type = PANETYPE_STATUSMESSAGE;
  G.status_message_pane.buffer = BufferView::create(&G.status_message_buffer);
  G.status_message_pane.buffer.empty();
  G.status_message_pane.background_color = &G.bottom_pane_highlight.color;
  G.status_message_pane.text_color = &G.default_text_color;
  G.status_message_pane.active_highlight_background_color = &G.active_highlight_background_color.color;
  G.status_message_pane.margin = 5;
  G.status_message_pane.menu.prefix = Slice::create("status");

  G.editing_pane = main_pane;
  G.bottom_pane = &G.status_message_pane;
  G.selected_pane = main_pane;
  G.editing_pane = main_pane;

  G.editing_panes += main_pane;

  filetree_init();
  status_message_set("Welcome!");
}

void state_free() {
  // really none of this matters, since all resouces are freed when program exits
  #if 0
  util_free(G.buffers);
  util_free(G.null_buffer);
  util_free(G.menu_buffer);
  util_free(G.dropdown_buffer);
  util_free(G.status_message_buffer);
  util_free(G.files);

  util_free(G.menu_pane);
  util_free(G.status_message_pane);
  util_free(G.dropdown_pane);
  #endif
  SDL_Quit();
}

static Pos char2pixel(Pos p) {
  return char2pixel(p.x, p.y);
}

static void handle_menu_insert(int key) {
    switch (key) {

    case KEY_ARROW_DOWN:
    case CONTROL('j'):
      G.menu_pane.menu.current_suggestion = clamp(G.menu_pane.menu.current_suggestion+1, 0, G.menu_pane.menu.suggestions.size-1);
      break;

    case KEY_ARROW_UP:
    case CONTROL('k'):
      G.menu_pane.menu.current_suggestion = clamp(G.menu_pane.menu.current_suggestion-1, 0, G.menu_pane.menu.suggestions.size-1);
      break;

    case KEY_TAB: {
      Slice *s = G.menu_pane.menu_get_selection();
      if (s) {
        G.menu_pane.buffer.empty();
        G.menu_pane.buffer.insert(*s);
      } else {
        G.menu_pane.buffer.insert_tab();
      }
      break;}

    default:
      insert_default(G.menu_pane, key);
      break;
  }
  if (G.flags.cursor_dirty) {
    G.menu_pane.update_suggestions();
  }
}

static void range_to_clipboard(BufferData &buffer, View<Pos> from, View<Pos> to) {
  if (from.size != to.size)
    return;

  StringBuffer sb = {};
  for (int i = 0; i < from.size; ++i) {
    Pos a = from[i];
    Pos b = to[i];
    if (b < a)
      swap_range(buffer,a,b);

    StringBuffer s = buffer.range_to_string({a,b});
    sb += s;
    if (i < to.size-1)
      sb += '\n';
    util_free(s);
  }
  SDL_SetClipboardText(sb.chars);
  util_free(sb);
}

static void move_cursor_due_to_remove(Pos a, Pos b, Pos *pos) {
  if (b <= a)
    return;
  // All cursors that are inside range should be moved to beginning of range
  if (a <= *pos && *pos <= b)
    *pos = a;
  // If lines were deleted, all cursors below b.y should move up
  else if (b.y > a.y && pos->y > b.y)
    pos->y -= b.y-a.y;
  // All cursors that are on the same row as b, but after b should be merged onto line a
  else if (pos->y == b.y && pos->x >= b.x-1) {
    pos->y = a.y;
    pos->x = a.x + pos->x - b.x;
  }
}

// frees visual_start if invalid
static bool check_visual_start(BufferView &buffer) {
  if (G.editing_pane->buffer.data != G.visual_start.buffer)
    return false;
  if (buffer.cursors.size != G.visual_start.cursors.size) {
    util_free(G.visual_start);
    return false;
  }
  return true;
}

static void toggle_comment(BufferView &buffer, int y0, int y1, int cursor_idx) {
  for (int y = y0; y <= y1; ++y) {
    int x  = buffer.data->begin_of_line(y);
    Pos a = {x, y};
    if (buffer.data->lines[y].begins_with(x, Slice::create("//"))) {
      Pos b = a;
      b.x += 2;
      while (b.x < buffer.data->lines[y].length && buffer.data->getchar(b).isspace())
        ++b.x;
      buffer.remove_range(a, b, cursor_idx);
    }
    else
      buffer.insert(a, Slice::create("// "), cursor_idx);
  }
}

static void handle_input(Key key) { 
  BufferView &buffer = G.editing_pane->buffer;

  switch (G.mode) {
  case MODE_CWD:
    if (key == KEY_RETURN) {
      Path p = Path::create(G.menu_buffer.lines[0](0, -2));
      // if what the user wrote is not a directory, try the autocomplete
      if (!File::isdir(p)) {
        Slice *s = G.menu_pane.menu_get_selection();
        if (!s) {
          status_message_set("'{}' is not a directory", (Slice)p.string.slice);
          goto err;
        }
        util_free(p);
        p = Path::create(*s);
        if (!File::isdir(p)) {
          status_message_set("'{}' is not a directory", (Slice)p.string.slice);
          goto err;
        }
      }

      if (!File::change_dir(p)) {
        status_message_set("Failed to change directory to '{}'", (Slice)p.string.slice);
        goto err;
      }

      status_message_set("Changed directory to '{}'", (Slice)p.string.slice);
      util_free(G.current_working_directory);
      G.current_working_directory = p;
      filetree_init();
      mode_normal(false);
      break;
      err:
      util_free(p);
      mode_normal(false);
    }
    else if (key == KEY_BACKSPACE) {
      Path p = Path::create(G.menu_buffer.lines[0](0, -2));
      p.pop();
      G.menu_pane.buffer.empty();
      G.menu_pane.buffer.insert(p.string.slice);
      G.menu_pane.buffer.insert(Utf8char::create(Path::separator));
      util_free(p);
      G.menu_pane.update_suggestions();
    }
    else if (key == KEY_TAB) {
      Slice *s = G.menu_pane.menu_get_selection();
      if (s) {
        G.menu_pane.buffer.empty();
        G.menu_pane.buffer.insert(*s);
        G.menu_pane.buffer.insert(Utf8char::create(Path::separator));
      }
      G.menu_pane.update_suggestions();
    }
    else
      handle_menu_insert(key);
    break;

  case MODE_PROMPT:
    if (key == KEY_RETURN) {
      G.prompt_success = true;
      switch (G.prompt_type) {
        // TODO: implement
        case PROMPT_STRING:
          G.prompt_result.string = String::create(G.menu_buffer.lines[0].slice);
          goto prompt_done;

        case PROMPT_INT:
          if (!G.menu_buffer.lines[0].toint(&G.prompt_result.integer)) {
            G.menu_pane.buffer.empty();
            G.menu_pane.menu.prefix = Slice::create("Please enter a valid number");
            goto prompt_keepgoing;
          }
          goto prompt_done;

        case PROMPT_FLOAT:
          if (!G.menu_buffer.lines[0].tofloat(&G.prompt_result.floating)) {
            G.menu_pane.buffer.empty();
            G.menu_pane.menu.prefix = Slice::create("Please enter a valid number");
            goto prompt_keepgoing;
          }
          goto prompt_done;

        case PROMPT_BOOLEAN:
          if (G.menu_buffer.lines[0].slice == "y")
            G.prompt_result.boolean = true;
          else if (G.menu_buffer.lines[0].slice == "n")
            G.prompt_result.boolean = false;
          else {
            G.menu_pane.buffer.empty();
            G.menu_pane.menu.prefix = Slice::create("Invalid bool. Please enter y or n");
            goto prompt_keepgoing;
          }
          goto prompt_done;
      }
    }
    else if (key == KEY_ESCAPE) {
      G.prompt_success = false;
      goto prompt_done;
    }
    else if (G.prompt_type == PROMPT_BOOLEAN && (key == 'y' || key == 'n')) {
      G.prompt_success = true;
      G.prompt_result.boolean = key == 'y';
      goto prompt_done;
    }
    else
      handle_menu_insert(key);

    prompt_keepgoing:
    G.prompt_success = false;
    break;

    prompt_done:
    mode_normal(false);
    G.prompt_callback();
    break;

  case MODE_GOTO:
    buffer.collapse_cursors();
    if (key >= '0' && key <= '9') {
      buffer.jumplist_push();
      G.goto_line_number *= 10;
      G.goto_line_number += key - '0';
      buffer.move_to_y(0, G.goto_line_number-1);
      status_message_set("goto %u", G.goto_line_number);
      buffer.jumplist_push();
      break;
    }

    switch (key) {
      case 't':
        buffer.jumplist_push();
        buffer.move_to(0, 0);
        buffer.jumplist_push();
        break;
      case 'b':
        buffer.jumplist_push();
        buffer.move_to(0, buffer.data->num_lines()-1);
        buffer.jumplist_push();
        break;
      case 'd': {
        // goto definition
        TokenInfo *t = buffer.data->gettoken(buffer.cursors[0].pos);
        if (!t)
          break;
        if (t->token != TOKEN_IDENTIFIER)
          break;

        Range *def = buffer.data->getdefinition(t->str);
        if (!def)
          break;

        G.editing_pane->add_subpane(buffer.data, def->a);
        // buffer.jumplist_push();
        // buffer.move_to(def->a);
        // buffer.jumplist_push();
        break;}
    }
    G.goto_line_number = 0;
    mode_normal(true);
    break;

  case MODE_COUNT:
    break;

  case MODE_MENU:
    if (key == KEY_RETURN) {
      if (G.menu_buffer.isempty()) {
        mode_normal(true);
        break;
      }

      Slice *s = G.menu_pane.menu_get_selection();
      if (!s)
        s = &G.menu_buffer[0].slice;

      foreach(menu_options) {
        if (*s == it->opt.name) {
          it->fun();
          goto done;
        }
      }
      status_message_set("Unknown option '{}'", (Slice)*s);
      done:
      // if the menu option changed the mode, leave it
      if (G.mode == MODE_MENU)
        mode_normal(false);
    }
    else if (key == KEY_ESCAPE)
      mode_normal(true);
    else
      handle_menu_insert(key);
    /* FIXME: if backspace so that menu_buffer is empty, exit menu mode */
    break;

  case MODE_GOTO_DEFINITION: {
    if (key == KEY_ESCAPE) {
      mode_normal(true);
      break;
    }

    if (key == KEY_RETURN) {
      Slice* opt = G.menu_pane.menu_get_selection();
      if (!opt) {
        status_message_set("\"{}\": No such file", (Slice)G.menu_buffer[0].slice);
        mode_normal(false);
        break;
      }

      Range *def = buffer.data->getdefinition(*opt);
      if (!def) {
        mode_normal(true);
        break;
      }

      G.editing_pane->add_subpane(buffer.data, def->a);
      // buffer.jumplist_push();
      // buffer.move_to(def->a);
      // buffer.jumplist_push();
      mode_normal(true);
      break;
    }

    handle_menu_insert(key);
    break;}

  case MODE_FILESEARCH:
    if (key == KEY_RETURN) {
      Slice *opt = G.menu_pane.menu_get_selection();
      if (!opt) {
        status_message_set("\"{}\": No such file", (Slice)G.menu_buffer[0].slice);
        mode_normal(false);
        break;
      }

      Slice filename = *opt;
      // TODO: selections should have metadata baked in, and not hack it like this (what if there are miultiple files with the same name?)
      for (Path p : G.files) {
        if (filename == p.name()) {
          filename = p.string.slice;
          break;
        }
      }

      BufferData *b = 0;
      for (BufferData *bb : G.buffers) {
        if (filename == bb->filename.slice) {
          b = bb;
          filename = bb->filename.slice;
          break;
        }
      }
      if (b) {
        status_message_set("Switched to {}", (Slice)filename);
        G.editing_pane->switch_buffer(b);
      }
      else {
        b = new BufferData{};
        if (BufferData::from_file(filename, b)) {
          G.buffers += b;
          G.editing_pane->switch_buffer(b);
          status_message_set("Loaded file {} (%s)", (Slice)filename, b->endline_string == ENDLINE_UNIX ? "Unix" : "Windows");
        } else {
          status_message_set("Failed to load file {}", (Slice)filename);
          free(b);
        }
      }
      G.menu_pane.buffer.empty();
      mode_normal(false);
      break;
    }
    else {
      handle_menu_insert(key);
    }
    break;

  case MODE_SEARCH: {
    if (key == KEY_RETURN || key == KEY_ESCAPE) {
      buffer.jumplist_push();
      G.search_failed = !buffer.find_and_move(G.search_term.slice, true);
      if (G.search_failed) {
        buffer.move_to(G.search_begin_pos);
        status_message_set("'{}' not found", (Slice)G.search_term.slice);
        mode_normal(false);
      }
      else {
        buffer.jumplist_push();
        mode_normal(true);
      }
    }
    else {
      G.search_term_background_color.reset();
      handle_menu_insert(key);

      if (G.flags.cursor_dirty) {
        util_free(G.search_term);
        Slice *s = G.menu_pane.menu_get_selection();
        if (s)
          G.search_term = String::create(*s);
        else
          G.search_term = String::create(G.menu_buffer.lines[0].slice);
      }
    }
  } break;

  case MODE_YANK: {
    // TODO: is it more performant to check if it is a movement command first?
    buffer.action_begin();

    Array<Cursor> cursors = buffer.cursors.copy_shallow();
    if (!movement_default(buffer, key))
      goto yank_done;
    if (cursors.size != buffer.cursors.size)
      goto yank_done;

    // some movements we want to include the end position
    // for (Cursor &c : cursors)
      // buffer.data->advance(c.pos);

    range_to_clipboard(*buffer.data, VIEW(cursors, pos), VIEW(buffer.cursors, pos));

    // go back to where we started
    for (int i = 0; i < cursors.size; ++i)
      buffer.cursors[i].pos = cursors[i].pos;

    yank_done:;
    buffer.action_end();
    util_free(cursors);
    mode_normal(true);
    break;}

  case MODE_DELETE: {
    // TODO: is it more performant to check if it is a movement command first?
    buffer.action_begin();
    Array<Cursor> cursors = buffer.cursors.copy_shallow();
    switch (key) {
      // delete line
      case 'd':
        buffer.delete_line();
        break;

      // delete block
      case 'b': {
        buffer.action_begin();
        Array<Cursor> prev = buffer.cursors.copy_shallow();
        move_to_right_brace(buffer, '{', '}', true);
        buffer.advance();
        for (int i = 0; i < buffer.cursors.size; ++i) {
          if (buffer.getchar(i) == ';')
            buffer.advance(i);
          buffer.remove_range(prev[i].pos, buffer.cursors[i].pos, i);
        }
        util_free(prev);
        buffer.action_end();
        break;}

      case 'p': {
        buffer.action_begin();
        for (int i = 0; i < buffer.cursors.size; ++i) {
          int depth = 0;
          Utf8char c;
          // find beginning
          Pos a = buffer.cursors[i].pos;
          depth = 0;
          c = buffer.getchar(a);
          bool left_was_brace = false;
          if (c == '(' || c == '[' || c == '{') {
            buffer.advance(a);
            left_was_brace = true;
          }
          else if (c == ',') {}
          else {
            while(!buffer.advance_r(a)) {
              c = buffer.getchar(a);
              if (c == ')' || c == ']' || c == '}')
                --depth;
              else if (c == '(' || c == '[' || c == '{')
                ++depth;
              else if (c == ',' && depth == 0)
                break;
              if (depth > 0) {
                left_was_brace = true;
                buffer.advance(a);
                break;
              }
            }
          }

          // find end
          Pos b = buffer.cursors[i].pos;
          depth = 0;
          c = buffer.getchar(b);
          if (c != ')' && c != ']' && c != '}') {
            while (!buffer.advance(b)) {
              c = buffer.getchar(b);
              if (c == ')' || c == ']' || c == '}')
                --depth;
              else if (c == '(' || c == '[' || c == '{')
                ++depth;
              else if (c == ',' && depth == 0) {
                // if left was brace, consume comma
                if (left_was_brace) {
                  buffer.advance(b);
                  while (c = buffer.getchar(b), c.isspace())
                    if (buffer.advance(b))
                      break;
                }
                break;
              }
              if (depth < 0)
                break;
            }
          }

          buffer.remove_range(a, b, i);
        }
        buffer.action_end();
        break;}

      default:
        if (!movement_default(buffer, key))
          break;
        if (cursors.size != buffer.cursors.size)
          break;
        // delete movement range
        for (int i = 0; i < cursors.size; ++i) {
          Pos a = cursors[i].pos;
          Pos b = buffer.cursors[i].pos;
          if (b < a) {
            swap_range(*buffer.data,a,b);
          }
          // else
          //   if (b.x != buffer.data->lines[b.y].length)
          //     buffer.advance(b);
          buffer.remove_range(a, b, i);
        }
        break;
    }
    buffer.action_end();

    util_free(cursors);
    mode_normal(true);
    break;}

  case MODE_INSERT: {
    bool insert_occured = true;
    switch (key) {
      case KEY_RETURN:
        buffer.action_begin();
        buffer.insert_newline();
        buffer.autoindent();
        buffer.action_end();
        break;

      case KEY_TAB:
        if (dropdown_autocomplete(buffer))
          break;
        insert_default(*G.editing_pane, key);
        break;

      case KEY_ESCAPE:
        buffer.remove_trailing_whitespace();
        mode_normal(true);
        break;

      case CONTROL('j'): {
        // this move should not dirty cursor
        int f = G.flags.cursor_dirty;
        G.dropdown_pane.buffer.move_y(1);
        G.flags.cursor_dirty = f;
        insert_occured = false;
        break;}

      case CONTROL('k'): {
        // this move should not dirty cursor
        int f = G.flags.cursor_dirty;
        G.dropdown_pane.buffer.move_y(-1);
        G.flags.cursor_dirty = f;
        insert_occured = false;
        break;}

      default:
        insert_occured = insert_default(*G.editing_pane, key);
        break;
    }
    if (insert_occured)
      tokenize(*buffer.data);
    break;}

  case MODE_NORMAL:
    if (movement_default(buffer, key))
      break;

    switch (key) {

    case KEY_ESCAPE:
      buffer.collapse_cursors();
      util_free(G.visual_start.cursors);
      break;

    case CONTROL('g'):
      mode_goto_definition();
      break;

    case '?':
      buffer.data->print_undo_actions();
      log_info("%i %i\n", buffer.data->_next_undo_action, buffer.data->_last_save_undo_action);
      break;

    case 'x':
      buffer.action_begin();
      buffer.advance();
      buffer.delete_char();
      buffer.action_end();
      break;

    case 'D':
      buffer.delete_line();
      break;

    case '=':
      buffer.action_begin();
      if (check_visual_start(buffer)) {
        for (int i = 0; i < buffer.cursors.size; ++i) {
          int y0 = min(G.visual_start.cursors[i].y, buffer.cursors[i].y);
          int y1 = max(G.visual_start.cursors[i].y, buffer.cursors[i].y);
          for (int y = y0; y <= y1; ++y)
            buffer.autoindent(y);
        }
        util_free(G.visual_start);
      }
      buffer.action_end();
      buffer.autoindent();
      break;

    case CONTROL('o'):
      buffer.jumplist_prev();
      break;

    case CONTROL('i'):
      buffer.jumplist_next();
      break;

    case '>':
      buffer.action_begin();
      for (int i = 0; i < buffer.cursors.size; ++i)
        buffer.insert_tab({0, buffer.cursors[i].y});
      buffer.action_end();
      break;

    case '/': {
      buffer.action_begin();
      if (check_visual_start(buffer)) {
        for (int i = 0; i < buffer.cursors.size; ++i) {
          int y0 = min(G.visual_start.cursors[i].y, buffer.cursors[i].y);
          int y1 = max(G.visual_start.cursors[i].y, buffer.cursors[i].y);
          toggle_comment(buffer, y0, y1, i);
        }
        util_free(G.visual_start);
      }
      else
        for (int i = 0; i < buffer.cursors.size; ++i)
          toggle_comment(buffer, buffer.cursors[i].y, buffer.cursors[i].y, i);
      buffer.action_end();

      break;}

    case '+':
      G.font_height = at_most(G.font_height+1, 50);
      graphics_set_font_options(G.ttf_file.string.chars, G.font_height);
      break;

    case '-':
      G.font_height = at_least(G.font_height-1, 7);
      graphics_set_font_options(G.ttf_file.string.chars, G.font_height);
      break;

    case 'q': {
      // check if there are any unsaved buffers
      for (BufferData *b : G.buffers) {
        if (b->is_bound_to_file() && b->modified()) {
          G.editing_pane->switch_buffer(b);
          status_message_set("{} has unsaved changes. If you really want to exit, use :quit", (Slice)b->name());
          goto quit_done;
        }
      }
      // otherwise exit
      editor_exit(0);

      quit_done:
      break;}

    case CONTROL('b'): {
      int errcode;
      String output = {};
      if (!call("build.bat", &errcode, &output)) {
        status_message_set("Failed to call build.bat");
        goto build_done;
      }
      output.convert_to_unix_endlines();

      if (!G.editing_panes.find(&G.build_result_pane))
        G.editing_panes += &G.build_result_pane;
      G.build_result_buffer.init(false, Slice::create("[Build Result]"));
      Pane::init_edit(G.build_result_pane, &G.build_result_buffer, &G.default_background_color, &G.default_text_color, &G.active_highlight_background_color.color, &G.inactive_highlight_background_color, false);
      G.build_result_pane.buffer.insert(output.slice);

      build_done:
      util_free(output);
      break;}

    case CONTROL('s'):
      save_buffer(buffer.data);
      break;

    case 'i':
      mode_insert();
      break;

    case 'p':
      if (G.editing_pane == G.selected_pane && SDL_HasClipboardText()) {
        char *s = SDL_GetClipboardText();
        // if we're on windows, remove \r
        #ifdef OS_WINDOWS
          char *out = s;
          for (char *in = s; *in; ++in)
            if (*in != '\r')
              *out++ = *in;
          *out = '\0';
        #endif

        int num_endlines = 0;
        for (char *t = s; *t; ++t)
          num_endlines += (*t == '\n');

        buffer.action_begin();
        buffer.data->raw_begin();

        // split clipboard among cursors
        if (num_endlines == buffer.cursors.size-1) {
          char *start = s;
          char *end = start;
          for (int i = 0; i < buffer.cursors.size; ++i) {
            start = end;
            while (*end && *end != '\n')
              ++end;
            buffer.insert(Slice::create(start, end-start), i);
            ++end;
          }
        }
        // otherwise just paste out the whole thing for all cursors
        else {
          Slice sl = Slice::create(s);
          if (sl.chars[sl.length-1] == '\n') {
            buffer.insert_newline_below();
            --sl.length;
          }
          buffer.insert(sl);
        }

        buffer.data->raw_end();
        buffer.action_end();
        SDL_free(s);
      }
      break;

    case 'm': {
      int i = buffer.cursors.size;
      buffer.cursors.push(buffer.cursors[i-1]);
      buffer.move_y(i, 1);
      break;}

    case CONTROL('w'): {
      Pane *p = new Pane{};
      Pane::init_edit(*p, &G.null_buffer, &G.default_background_color, &G.default_text_color, &G.active_highlight_background_color.color, &G.inactive_highlight_background_color, true);
      G.editing_panes += p;
      break;}

    case CONTROL('q'):
      G.panes_to_remove += G.editing_pane;
      break;

    case CONTROL('l'): {
      if (G.editing_pane->subpanes.size > 0) {
        G.editing_pane = G.editing_pane->subpanes[0].pane;
        G.selected_pane = G.editing_pane;
        break;
      }

      /*fallthrough*/}

    case CONTROL('L'): {
      int i;
      for (i = 0; i < G.editing_panes.size; ++i)
        if (G.editing_panes[i] == G.editing_pane)
          break;
      i = (i+1)%G.editing_panes.size;
      G.editing_pane = G.editing_panes[i];
      G.selected_pane = G.editing_pane;
      break;}

    case CONTROL('j'): {
      // Find sibling below
      Pane *p = G.editing_pane;
      while (p->parent) {
        for (int i = 0; i < p->parent->subpanes.size-1; ++i) {
          if (p->parent->subpanes[i].pane == p) {
            G.editing_pane = p->parent->subpanes[i+1].pane;
            G.selected_pane = p->parent->subpanes[i+1].pane;
            goto sibling_below_done;
          }
        }
        p = p->parent;
      }
      sibling_below_done:
      break;}

    case CONTROL('k'): {
      // Find sibling above
      Pane *p = G.editing_pane;
      while (p->parent) {
        for (int i = 1; i < p->parent->subpanes.size; ++i) {
          if (p->parent->subpanes[i].pane == p) {
            G.editing_pane = p->parent->subpanes[i-1].pane;
            G.selected_pane = p->parent->subpanes[i-1].pane;
            goto sibling_above_done;
          }
        }
        p = p->parent;
      }
      sibling_above_done:
      break;}

    case CONTROL('h'): {
      if (G.editing_pane->parent) {
        G.editing_pane = G.editing_pane->parent;
        G.selected_pane = G.editing_pane;
        break;
      }

      /*fallthrough*/}

    case CONTROL('H'): {
      int i;
      for (i = 0; i < G.editing_panes.size; ++i)
        if (G.editing_panes[i] == G.editing_pane)
          break;
      i = (i-1+G.editing_panes.size)%G.editing_panes.size;
      G.editing_pane = G.editing_panes[i];
      G.selected_pane = G.editing_pane;
      break;}

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
      buffer.action_begin();
      buffer.insert_newline_below();
      buffer.autoindent();
      mode_insert();
      buffer.action_end();
      break;

    case 'y':
      // visual yank?
      if (check_visual_start(buffer)) {
        Array<Pos> destination;
        buffer.action_begin();

        destination = {};
        for (Cursor c : buffer.cursors)
          destination += c.pos;

        if (G.visual_entire_line) {
          for (int i = 0; i < G.visual_start.cursors.size; ++i) {
            if (destination[i] < G.visual_start.cursors[i])
              swap(destination[i], G.visual_start.cursors[i]);
            G.visual_start.cursors[i].x = 0;
            destination[i].x = 0;
            ++destination[i].y;
          }
        }

        range_to_clipboard(*buffer.data, view(G.visual_start.cursors), view(destination));
        util_free(destination);
        util_free(G.visual_start);
        buffer.action_end();
        break;
      }

      mode_yank();
      break;

    case 'Y': {
      StringBuffer s = {};
      for (int i = 0; i < buffer.cursors.size; ++i) {
        s += StringBuffer::create(buffer.data->lines[buffer.cursors[i].y].slice);
        s += '\n';
        if (i < buffer.cursors.size-1)
          s += '\n';
      }
      SDL_SetClipboardText(s.chars);
      util_free(s);
      break;}

    case ':':
      mode_menu();
      break;

    #if 1
    case 'v':
      G.visual_entire_line = false;
      util_free(G.visual_start.cursors);
      for (Cursor c : G.editing_pane->buffer.cursors)
        G.visual_start.cursors += c.pos;
      G.visual_start.buffer = G.editing_pane->buffer.data;
      break;
    #endif

    case 'd':
      // visual delete?
      if (check_visual_start(buffer)) {
        buffer.action_begin();

        for (int i = 0; i < G.visual_start.cursors.size; ++i) {
          Pos pa = G.visual_start.cursors[i];
          Pos pb = buffer.cursors[i].pos;
          if (pb < pa)
            swap_range(*buffer.data, pa, pb);
          if (G.visual_entire_line) {
            pa.x = 0;
            pb.x = 0;
            ++pb.y;
          }
          buffer.remove_range(pa, pb, i);
          for (Pos &p : G.visual_start.cursors)
            move_cursor_due_to_remove(pa, pb, &p);
        }

        util_free(G.visual_start);

        buffer.action_end();
        break;
      }

      mode_delete();
      break;

    case 'V':
      G.visual_entire_line = true;
      util_free(G.visual_start);
      for (Cursor c : G.editing_pane->buffer.cursors)
        G.visual_start.cursors += c.pos;
      G.visual_start.buffer = G.editing_pane->buffer.data;
      break;

    case CONTROL('z'):
      buffer.undo();
      break;

    case CONTROL('Z'):
      buffer.redo();
      break;

    }
    break;
  }
  buffer.deduplicate_cursors();
}

static int num_top_level_panes() {
  int x = 0;
  for (Pane *p : G.editing_panes)
    if (!p->parent)
      ++x;
  return x;
}

static void handle_rendering(float dt) {
  // boost marker when you move or change modes
  static Pos prev_pos;
  static Mode prev_mode;
  if (prev_pos != G.editing_pane->buffer.cursors[0].pos || (prev_mode != G.mode && G.mode != MODE_SEARCH && G.mode != MODE_MENU)) {
    G.marker_background_color.reset();
    G.active_highlight_background_color.reset();
  }
  prev_pos = G.editing_pane->buffer.cursors[0].pos;
  prev_mode = G.mode;

  // update colors
  G.marker_background_color.tick(dt);
  G.active_highlight_background_color.tick(dt);
  G.bottom_pane_highlight.tick(dt);
  G.search_term_background_color.tick(dt);

  // update paste highlights
  for (BufferData *b : G.buffers) {
    for (int i = 0; i < b->paste_highlights.size; ++i) {
      b->paste_highlights[i].alpha -= dt*0.03f;

      if (b->paste_highlights[i].alpha < 0)
        b->paste_highlights[i--] = b->paste_highlights[--b->paste_highlights.size];
    }
  }

  // highlight some colors
  G.default_marker_background_color.tick(dt);
  G.marker_background_color.popped_color = G.default_marker_background_color.color;

  // render
  G.font_width = graphics_get_font_advance(G.font_height);
  G.line_height = G.font_height + G.line_margin;
  SDL_GetWindowSize(G.window, &G.win_width, &G.win_height);
  glClearColor(G.default_background_color.r/255.0f, G.default_background_color.g/255.0f, G.default_background_color.g/255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // reflow top level panes
  G.bottom_pane->margin = 3;
  G.bottom_pane->bounds.h = G.line_height + 2*G.bottom_pane->margin;

  int x = 0;
  for (Pane *p : G.editing_panes) {
    if (p->parent)
      continue;
    const int w = G.win_width / num_top_level_panes();
    const int h = G.win_height - G.bottom_pane->bounds.h;
    p->bounds = {x, 0, w, h};
    x += w;
  }
  G.bottom_pane->bounds = {0, G.win_height - G.bottom_pane->bounds.h, G.win_width, G.bottom_pane->bounds.h};

  for (Pane *p : G.editing_panes) {
    if (!p->parent)
      p->render();
  }
  G.bottom_pane->render();
}

Pos BufferData::to_visual_pos(Pos p) {
  p.x = lines[p.y].visual_offset(p.x, G.tab_width);
  return p;
}

void BufferView::move_to_y(int marker_idx, int y) {
  G.flags.cursor_dirty = true;

  y = clamp(y, 0, data->lines.size-1);
  int x = cursors[marker_idx].ghost_x;
  if (x == GHOST_EOL)
    x = data->lines[y].length;
  else if (x == GHOST_BOL)
    x = data->begin_of_line(y);

  x = clamp(x, 0, data->lines[y].length);
  cursors[marker_idx].y = y;
  cursors[marker_idx].x = x;
}

void BufferView::move_to_x(int marker_idx, int x) {
  G.flags.cursor_dirty = true;

  x = clamp(x, 0, data->lines[cursors[marker_idx].y].length);
  cursors[marker_idx].x = x;
  cursors[marker_idx].ghost_x = data->lines[cursors[marker_idx].y].visual_offset(x, G.tab_width);
}

void BufferView::move_to(int x, int y) {
  collapse_cursors();
  move_to_y(0, y);
  move_to_x(0, x);
}

void BufferView::move_to(Pos p) {
  collapse_cursors();
  move_to_y(0, p.y);
  move_to_x(0, p.x);
}

// NOTE: you must call deduplicate_cursors after this
void BufferView::move_to(int marker_idx, Pos p) {
  move_to_y(marker_idx, p.y);
  move_to_x(marker_idx, p.x);
}

void BufferView::move_y(int marker_idx, int dy) {
  if (!dy)
    return;

  G.flags.cursor_dirty = true;

  Pos &pos = cursors[marker_idx].pos;
  int ghost_x = cursors[marker_idx].ghost_x;

  pos.y = clamp(pos.y + dy, 0, data->lines.size - 1);

  if (ghost_x == GHOST_EOL)
    pos.x = data->lines[pos.y].length;
  else if (ghost_x == GHOST_BOL)
    pos.x = data->begin_of_line(pos.y);
  else
    pos.x = data->lines[pos.y].from_visual_offset(ghost_x, G.tab_width);
}

void BufferView::move_x(int marker_idx, int dx) {
  if (!dx)
    return;
  G.flags.cursor_dirty = true;

  Pos &pos = cursors[marker_idx].pos;
  if (dx > 0)
    for (; dx > 0; --dx)
      pos.x = data->lines[pos.y].next(pos.x);
  if (dx < 0)
    for (; dx < 0; ++dx)
      pos.x = data->lines[pos.y].prev(pos.x);
  pos.x = clamp(pos.x, 0, data->lines[pos.y].length);
  cursors[marker_idx].ghost_x = data->lines[pos.y].visual_offset(pos.x, G.tab_width);
}

void BufferView::move_x(int dx) {
  for (int i = 0; i < cursors.size; ++i)
    move_x(i, dx);
}

void BufferView::move_y(int dy) {
  for (int i = 0; i < cursors.size; ++i)
    move_y(i, dy);
}

void util_free(BufferData &b) {
  util_free(b.lines);
  util_free(b.filename);
  util_free(b.tokens);
  util_free(b.identifiers);
  util_free(b.definitions);
  util_free(b._undo_actions);
  b.paste_highlights.free_shallow();
}

bool BufferData::reload(BufferData *b) {
  if (!b->filename.chars)
    return false;
  String filename = String::create(b->filename.slice);
  util_free(*b);
  bool succ = BufferData::from_file(filename.slice, b);
  util_free(filename);
  return succ;
}

bool BufferData::from_file(Slice filename, BufferData *buffer) {
  FILE* f = 0;
  int num_lines = 0;
  *buffer = {};
  BufferData &b = *buffer;
  b.endline_string = ENDLINE_UNIX;

  b.filename = filename.copy();
  if (file_open(&f, b.filename.chars, "rb"))
    goto err;

  // get line count
  num_lines = 0;
  for (char c; (c = (char)fgetc(f)), !feof(f);)
    num_lines += c == '\n';
  if (ferror(f))
    goto err;
  fseek(f, 0, SEEK_SET);

  // read content
  if (num_lines > 0) {
    b.lines.resize(num_lines+1);
    b.lines.zero();

    char c = 0;
    for (int i = 0; i <= num_lines; ++i) {
      while (1) {
        c = (char)fgetc(f);
        if (feof(f)) {
          if (ferror(f))
            goto err;
          goto last_line;
        }
        if (c == '\r') {
          c = (char)fgetc(f);
          b.endline_string = ENDLINE_WINDOWS;
        }
        if (c == '\n')
          break;
        b[i] += c;
      }
    }
    last_line:;
    assert(feof(f));
  } else {
    b.lines += {};
  }

  // token type
  tokenize(b);

  // guess tab type
  b.guess_tab_type();

  fclose(f);
  return true;

  err:
  if (buffer) {
    util_free(*buffer);
    free(buffer);
  }
  if (f)
    fclose(f);
  return false;
}

StringBuffer BufferData::range_to_string(const Range r) {
    // turn range into a string with endlines in it
  // TODO: We could probably do some compression on this
  if (r.a.y == r.b.y)
    return StringBuffer::create(lines[r.a.y](r.a.x, r.b.x));
  else {
    // first row
    StringBuffer s = {};
    s += lines[r.a.y](r.a.x, -1);
    s += '\n';
    for (int y = r.a.y+1; y < r.b.y; ++y) {
      s += lines[y].slice;
      s += '\n';
    }
    if (r.b.x)
      s += lines[r.b.y](0, r.b.x);
    return s;
  }
}

Range* BufferData::getdefinition(Slice s) {
  for (Range &r : definitions)
    if (getslice(r) == s)
      return &r;
  return 0;
}

TokenInfo* BufferData::gettoken(Pos p) {
  for (int i = 0; i < tokens.size; ++i)
    if (p < tokens[i].b)
      return &tokens[i];
  return tokens.end();
}

#if 0
TokenInfo* BufferData::gettoken(Pos p) {
  int a = 0, b = tokens.size-1;

  while (a <= b) {
    int mid = (a+b)/2;
    if (tokens[mid].a <= p && p < tokens[mid].b)
      return &tokens[mid];
    if (tokens[mid].a < p)
      a = mid+1;
    else
      b = mid-1;
  }
  return 0;
}
#endif

void BufferData::push_undo_action(UndoAction a) {
  if (undo_disabled)
    return;

  // remove any redos we might have
  if (_next_undo_action < _undo_actions.size) {
    for (int i = _next_undo_action; i < _undo_actions.size; ++i)
      util_free(_undo_actions[i]);

    // invalidate the save position
    if (_last_save_undo_action > _next_undo_action)
      _last_save_undo_action = -1;
  }
  _undo_actions.size = _next_undo_action;
  _undo_actions += a;
  ++_next_undo_action;
}

void BufferData::action_begin(Array<Cursor> &cursors) {
  util_free(blame);

  if (undo_disabled)
    return;

  if (_action_group_depth == 0) {
    push_undo_action({ACTIONTYPE_GROUP_BEGIN});
    push_undo_action(UndoAction::cursor_snapshot(cursors));
  }
  // TODO: check if anything actually happened between begin and end
  ++_action_group_depth;
}

void BufferData::action_end(Array<Cursor> &cursors) {
  if (undo_disabled)
    return;

  --_action_group_depth;
  if (_action_group_depth == 0) {
    // check if something actually happened
    bool changed = true;
    bool buffer_changed = true;
    if (_undo_actions[_next_undo_action-1].type == ACTIONTYPE_CURSOR_SNAPSHOT && _undo_actions[_next_undo_action-2].type == ACTIONTYPE_GROUP_BEGIN) {
      changed = false;
      buffer_changed = false;
      UndoAction a = _undo_actions[_next_undo_action-1];
      // check if cursors changed
      if (a.cursors.size != cursors.size)
        changed = true;
      else {
        for (int i = 0; i < a.cursors.size; ++i) {
          if (cursors[i] == a.cursors[i])
            continue;
          changed = true;
          break;
        }
      }
    }

    // if nothing happened, remove group
    if (!changed) {
      assert(_undo_actions[_next_undo_action-1].type == ACTIONTYPE_CURSOR_SNAPSHOT);
      util_free(_undo_actions[_next_undo_action-1]);
      assert(_undo_actions[_next_undo_action-2].type == ACTIONTYPE_GROUP_BEGIN);
      _next_undo_action -= 2;
      _undo_actions.size -= 2;
      return;
    }

    if (this == G.editing_pane->buffer.data)
      G.activation_meter += 1.0f;

    push_undo_action(UndoAction::cursor_snapshot(cursors));
    push_undo_action({ACTIONTYPE_GROUP_END});

    // retokenize
    if (buffer_changed)
      tokenize(*this);

    // build up clipboard from removed strings
    Array<StringBuffer> clips = {};
    // find start of group
    UndoAction *a = &_undo_actions[_next_undo_action-1];
    assert(a->type == ACTIONTYPE_GROUP_END);
    while (a[-1].type != ACTIONTYPE_GROUP_BEGIN)
      --a;
    assert(a->type == ACTIONTYPE_CURSOR_SNAPSHOT);
    clips.resize(a->cursors.size);
    clips.zero();
    // find every delete action, and if there is a cursor for that, add that delete that cursors 
    bool clip_filled = false;
    for (; a->type != ACTIONTYPE_GROUP_END; ++a) {
      if (a->type != ACTIONTYPE_DELETE)
        continue;
      if (a->remove.cursor_idx == -1)
        continue;

      clips[a->remove.cursor_idx] += a->remove.s;
      clip_filled = true;
    }

    if (clip_filled) {
      StringBuffer clip = {};
      for (int i = 0; i < clips.size; ++i) {
        clip += clips[i];
        if (i < clips.size-1)
          clip += '\n';
      }
      clip += '\0';
      SDL_SetClipboardText(clip.chars);
      util_free(clip);
    }
    util_free(clips);
  }
}

void BufferData::undo(Array<Cursor> &cursors) {
  util_free(blame);

  if (undo_disabled)
    return;
  if (!_next_undo_action)
    return;

  undo_disabled = true;
  raw_begin();
  --_next_undo_action;
  assert(_undo_actions[_next_undo_action].type == ACTIONTYPE_GROUP_END);
  --_next_undo_action;
  for (; _undo_actions[_next_undo_action].type != ACTIONTYPE_GROUP_BEGIN; --_next_undo_action) {
    UndoAction a = _undo_actions[_next_undo_action];
    // printf("undo action: %i\n", a.type);
    switch (a.type) {
      case ACTIONTYPE_INSERT:
        // printf("Removing {%i %i}, {%i %i}\n", a.insert.a.x, a.insert.a.y, a.insert.b.x, a.insert.b.y);
        remove_range(cursors, a.insert.a, a.insert.b);
        break;
      case ACTIONTYPE_DELETE:
        // printf("Inserting '%.*s' at {%i %i}\n", a.remove.s.slice.length, a.remove.s.slice.chars, a.remove.a.x, a.remove.a.y);
        insert(cursors, a.remove.a, a.remove.s.slice);
        break;
      case ACTIONTYPE_CURSOR_SNAPSHOT:
        util_free(cursors);
        cursors = a.cursors.copy_shallow();
        break;
      case ACTIONTYPE_GROUP_BEGIN:
      case ACTIONTYPE_GROUP_END:
        break;
    }
  }
  // printf("cursors: %i\n", cursors.size);
  undo_disabled = false;

  // TODO: @hack should we be able to do action_begin and action_end here so we don't need to hardcode in retokenization?
  tokenize(*this);
  raw_end();
}

void BufferData::redo(Array<Cursor> &cursors) {
  util_free(blame);

  if (undo_disabled)
    return;

  if (_next_undo_action == _undo_actions.size)
    return;

  undo_disabled = true;
  raw_begin();
  assert(_undo_actions[_next_undo_action].type == ACTIONTYPE_GROUP_BEGIN);
  ++_next_undo_action;
  for (; _undo_actions[_next_undo_action].type != ACTIONTYPE_GROUP_END; ++_next_undo_action) {
    UndoAction a = _undo_actions[_next_undo_action];
    // printf("redo action: %i\n", a.type);
    switch (a.type) {
      case ACTIONTYPE_INSERT:
        // printf("Inserting '%s' at {%i %i}\n", a.insert.s.slice.chars, a.insert.a.x, a.insert.a.y);
        insert(cursors, a.insert.a, a.insert.s.slice);
        break;
      case ACTIONTYPE_DELETE:
        // printf("Removing {%i %i}, {%i %i}\n", a.remove.a.x, a.remove.a.y, a.remove.b.x, a.remove.b.y);
        remove_range(cursors, a.remove.a, a.remove.b);
        break;
      case ACTIONTYPE_CURSOR_SNAPSHOT:
        util_free(cursors);
        cursors = a.cursors.copy_shallow();
        break;
      case ACTIONTYPE_GROUP_BEGIN:
      case ACTIONTYPE_GROUP_END:
        break;
    }
  }
  ++_next_undo_action;
  undo_disabled = false;
  // TODO: @hack should we be able to do action_begin and action_end here so we don't need to hardcode in retokenization?
  tokenize(*this);
  raw_end();
}

void BufferData::print_undo_actions() {
  log_info("#########################\n");
  for (UndoAction &a : _undo_actions) {
    switch (a.type) {
      case ACTIONTYPE_INSERT:
        log_info("   INSERT\n");
        break;
      case ACTIONTYPE_DELETE:
        log_info("   DELETE\n");
        break;
      case ACTIONTYPE_CURSOR_SNAPSHOT:
        log_info("   CURSORS\n");
        break;
      case ACTIONTYPE_GROUP_BEGIN:
        log_info(">>>\n");
        break;
      case ACTIONTYPE_GROUP_END:
        log_info("<<<\n");
        break;
    }
  }
}

void BufferView::jumplist_push() {
  if (jumplist_pos > 0 && jumplist[jumplist_pos-1] == cursors[0].pos)
    return;
  jumplist.resize(++jumplist_pos);
  jumplist.last() = cursors[0].pos;
}

void BufferView::jumplist_prev() {
  if (!jumplist.size || !jumplist_pos)
    return;

  collapse_cursors();
  Pos p = jumplist[--jumplist_pos];
  while (p == cursors[0].pos && jumplist_pos > 0)
    p = jumplist[--jumplist_pos];
  cursors[0].pos = p;
  cursors[0].ghost_x = cursors[0].x;
}

void BufferView::jumplist_next() {
  if (jumplist_pos >= jumplist.size)
    return;

  collapse_cursors();
  Pos p = jumplist[jumplist_pos++];
  while (p == cursors[0].pos && jumplist_pos < jumplist.size)
    p = jumplist[jumplist_pos++];
  cursors[0].pos = p;
  cursors[0].ghost_x = cursors[0].x;
}

void BufferView::move(int marker_idx, int dx, int dy) {
  if (dy)
    move_y(marker_idx, dy);
  if (dx)
    move_x(marker_idx, dx);
}

void BufferView::deduplicate_cursors() {
  for (int i = 0; i < cursors.size; ++i)
  for (int j = 0; j < cursors.size; ++j)
    if (i != j && cursors[i].pos == cursors[j].pos) {
      cursors[j] = cursors[--cursors.size],
      --j;
      G.flags.cursor_dirty = true;
    }
}

void BufferView::collapse_cursors() {
  cursors.size = 1;
}

void BufferView::update() {
  for (int i = 0; i < cursors.size; ++i)
    move(i, 0, 0);
}

bool BufferData::find_r(Slice s, int stay, Pos *p) {
  if (!s.length)
    return false;

  int x = p->x;
  int y = p->y;
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

bool BufferData::find_r(char s, int stay, Pos *p) {
  int x = p->x;
  int y = p->y;
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

bool BufferData::find(char s, bool stay, Pos *p) {
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

bool BufferData::find(Slice s, bool stay, Pos *p) {
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

bool BufferView::find_and_move_r(char c, bool stay) {
  collapse_cursors();
  Pos p = cursors[0].pos;
  if (!data->find_r(c, stay, &p))
    return false;
  move_to(p);
  return true;
}

bool BufferView::find_and_move_r(Slice s, bool stay) {
  collapse_cursors();
  Pos p = cursors[0].pos;
  if (!data->find_r(s, stay, &p))
    return false;
  move_to(p);
  return true;
}

bool BufferView::find_and_move(char c, bool stay) {
  collapse_cursors();
  Pos p = cursors[0].pos;
  if (!data->find(c, stay, &p))
    return false;
  move_to(p);
  return true;
}

bool BufferView::find_and_move(Slice s, bool stay) {
  collapse_cursors();
  Pos p = cursors[0].pos;
  if (!data->find(s, stay, &p))
    return false;
  move_to(p);
  return true;
}

void BufferView::remove_trailing_whitespace() {
  data->action_begin(cursors);
  for (int i = 0; i < cursors.size; ++i)
    remove_trailing_whitespace(i);
  data->action_end(cursors);
}

void BufferData::remove_trailing_whitespace(Array<Cursor> &cursors, int cursor_idx) {
  int y = cursors[cursor_idx].y;
  if (lines[y].length == 0)
    return;

  int x = lines[y].length - 1;
  if (!getchar(x,y).isspace())
    return;

  // TODO: @utf8
  while (x > 0 && getchar(x, y).isspace())
    --x;
  remove_range(cursors, {x, y}, {lines[y].length, y}, cursor_idx);
}

void BufferData::delete_line(Array<Cursor> &cursors, int y) {
  remove_range(cursors, {0, y}, {0, y+1});
}

void BufferData::delete_line(Array<Cursor> &cursors) {
  action_begin(cursors);
  for (int i = 0; i < cursors.size; ++i)
    remove_range(cursors, {0, cursors[i].y}, {0, cursors[i].y+1}, i);
  action_end(cursors);
}

void BufferData::remove_range(Array<Cursor> &cursors, Pos a, Pos b, int cursor_idx) {
  if (b <= a)
    return;

  action_begin(cursors);
  G.flags.cursor_dirty = true;

  if (!undo_disabled) {
    UndoAction act = UndoAction::delete_range({a,b}, range_to_string({a,b}).string, cursor_idx);
    push_undo_action(act);
  }

  for (Cursor &c : cursors) {
    move_cursor_due_to_remove(a, b, &c.pos);
    c.ghost_x = c.x;
  }

  if (a.y == b.y)
    lines[a.y].remove(a.x, b.x-a.x);
  else {
    // append end of b onto a
    lines[a.y].length = a.x;
    if (b.y < lines.size)
      lines[a.y] += lines[b.y](b.x, -1);
    // delete lines a+1 to and including b
    lines.remove_slow_and_free(a.y+1, at_most(b.y - a.y, lines.size - a.y - 1));
  }

  action_end(cursors);
}

void BufferData::delete_char(Array<Cursor> &cursors) {
  action_begin(cursors);
  G.flags.cursor_dirty = true;

  for (int i = 0; i < cursors.size; ++i) {
    Pos pos = cursors[i].pos;
    if (pos.x == 0) {
      if (pos.y == 0)
        return;
      remove_range(cursors, {lines[pos.y-1].length, pos.y-1}, {0, pos.y}, i);
    }
    else {
      Pos p = pos;
      advance_r(p);
      remove_range(cursors, p, pos, i);
    }
  }

  action_end(cursors);
}

int BufferData::getindent(int y) {
  if (y < 0 || y >= lines.size)
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

int BufferData::indentdepth(int y, bool *has_statement) {
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

void BufferData::autoindent(Array<Cursor> &cursors) {
  action_begin(cursors);

  for (Cursor c : cursors)
    autoindent(cursors, c.y);

  action_end(cursors);
}

void BufferData::autoindent(Array<Cursor> &cursors, const int y) {
  action_begin(cursors);

  int y_above = y-1;
  // skip empty lines
  while (y_above >= 0 && lines[y_above].length == 0)
    --y_above;

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

  set_indent(cursors, y, target_indent);
  action_end(cursors);
}

void BufferData::add_indent(Array<Cursor> &cursors, int y, int diff) {
  action_begin(cursors);
  const char tab_char = tab_type ? ' ' : '\t';
  const int tab_size = tab_type ? tab_type : 1;
  const int current_indent = getindent(y);

  diff *= tab_size;

  if (diff < -current_indent*tab_size)
    diff = -current_indent*tab_size;

  if (diff < 0)
    remove_range(cursors, {0, y}, {-diff, y});
  if (diff > 0)
    for (int i = 0; i < diff; ++i)
      insert(cursors, Pos{0, y}, Utf8char::create(tab_char));

  action_end(cursors);
}

void BufferData::set_indent(Array<Cursor> &cursors, int y, int target) {
  add_indent(cursors, y, target - getindent(y));
}

int BufferData::isempty() {
  return lines.size == 1 && lines[0].length == 0;
}

void BufferData::push_line(Array<Cursor> &cursors, Slice s) {
  action_begin(cursors);

  if (lines.size > 1 || lines[0].length > 0)
    insert(cursors, {lines[lines.size-1].length, lines.size-1}, Utf8char::create('\n'));
  insert(cursors, {0, lines.size-1}, s);

  action_end(cursors);
}

void BufferData::insert(Array<Cursor> &cursors, Utf8char ch, int cursor_idx) {
  action_begin(cursors);

  // TODO: @utf8
  char c = ch.ansi();
  insert(cursors, Slice{&c, 1}, cursor_idx);

  if (ch == '}' || ch == ')' || ch == ']' || ch == '>')
    autoindent(cursors, cursors[cursor_idx].y);

  action_end(cursors);
}

void BufferData::insert(Array<Cursor> &cursors, Slice s, int cursor_idx) {
  insert(cursors, cursors[cursor_idx].pos, s, cursor_idx);
}

static void move_on_insert(Pos &p, Pos a, Pos b) {
  if (p.y == a.y && p.x >= a.x) {
    p.y += b.y - a.y;
    p.x = b.x + p.x - a.x;
  }
  else if (p.y > a.y) {
    p.y += b.y - a.y;
  }
}

static void move_cursors_on_insert(BufferData *buffer, Pos a, Pos b) {
  if (buffer == &G.status_message_buffer) {
    move_on_insert(G.status_message_pane.buffer.cursors[0].pos, a, b);
    return;
  }
  if (buffer == &G.menu_buffer) {
    move_on_insert(G.menu_pane.buffer.cursors[0].pos, a, b);
    return;
  }

  // move cursors
  for (Pane *pane : G.editing_panes) {
    if (pane->buffer.data != buffer)
      continue;

    // cursors
    for (Cursor &c : pane->buffer.cursors) {
      move_on_insert(c.pos, a, b);
      c.ghost_x = c.x;
    }

    // jumplist
    for (Pos &pos : pane->buffer.jumplist)
      move_on_insert(pos, a, b);

    for (PasteHighlight &ph : buffer->paste_highlights) {
      move_on_insert(ph.a, a, b);
      move_on_insert(ph.b, a, b);
    }
  }
}

void BufferData::insert(Array<Cursor> &cursors, const Pos a, Slice s, int cursor_index_hint) {
  if (!s.length)
    return;

  action_begin(cursors);
  G.flags.cursor_dirty = true;

  int num_lines = 0;
  int last_endline = 0;
  for (int i = 0; i < s.length; ++i) {
    if (s[i] == '\n') {
      last_endline = i;
      ++num_lines;
    }
  }

  Pos b;
  if (num_lines > 0)
    b = {s.length - last_endline - 1, a.y + num_lines};
  else
    b = {a.x + s.length, a.y};

  // if (this == G.editing_pane->buffer.data)
  //   printf("insert: {%i %i} {%i %i} '%.*s'\n", a.x, a.y, b.x, b.y, s.length, s.chars);

  if (!undo_disabled)
    push_undo_action(UndoAction::insert_slice(a, b, s, cursor_index_hint));

  if (num_lines == 0)
    lines[a.y].insert(a.x, s);
  else {
    lines.insertz(a.y+1, num_lines);
    // construct last line
    lines[b.y] += s(last_endline+1, -1);
    lines[b.y] += lines[a.y](a.x, -1);

    // resize first line
    lines[a.y].length = a.x;

    // first line
    int ai = 0, bi;
    s.find('\n', &bi);
    lines[a.y] += s(0, bi);
    ++bi;
    ai = bi;

    // all lines in between
    int y = a.y+1;
    while (s.find(ai, '\n', &bi)) {
      lines[y++] += s(ai, bi);
      ++bi;
      ai = bi;
    }
  }

  move_cursors_on_insert(this, a, b);

  paste_highlights += PasteHighlight{a, b, 1.0f};
  action_end(cursors);
}

void BufferData::insert(Array<Cursor> &cursors, Slice s) {
  action_begin(cursors);

  for (int i = 0; i < cursors.size; ++i)
    insert(cursors, cursors[i].pos, s);

  action_end(cursors);
}

void BufferData::insert(Array<Cursor> &cursors, Pos pos, Utf8char ch) {
  action_begin(cursors);

  // TODO: @utf8
  char c = ch.ansi();
  insert(cursors, pos, Slice{&c, 1});

  if (ch == '}' || ch == ')' || ch == ']' || ch == '>')
    autoindent(cursors, pos.y);

  action_end(cursors);
}

void BufferData::insert(Array<Cursor> &cursors, Utf8char ch) {
  action_begin(cursors);

  for (int i = 0; i < cursors.size; ++i)
    insert(cursors, cursors[i].pos, ch);

  action_end(cursors);
}

void BufferData::insert_tab(Array<Cursor> &cursors, Pos p) {
  action_begin(cursors);

  if (tab_type == 0)
    insert(cursors, p, Utf8char::create('\t'));
  else
    for (int i =0 ; i < tab_type; ++i)
      insert(cursors, p, Utf8char::create(' '));

  action_end(cursors);
}

void BufferData::insert_tab(Array<Cursor> &cursors) {
  action_begin(cursors);

  if (tab_type == 0)
    insert(cursors, Utf8char::create('\t'));
  else
    for (int i = 0; i < tab_type; ++i)
      insert(cursors, Utf8char::create(' '));

  action_end(cursors);
}

void BufferData::insert_newline(Array<Cursor> &cursors) {
  action_begin(cursors);

  for (int i = 0; i < cursors.size; ++i) {
    remove_trailing_whitespace(cursors, i);
    insert(cursors, cursors[i].pos, Utf8char::create('\n'));
  }

  action_end(cursors);
}

void BufferData::insert_newline_below(Array<Cursor> &cursors) {
  action_begin(cursors);

  for (Cursor &c : cursors) {
    c.x = lines[c.y].length;
    insert(cursors, c.pos, Utf8char::create('\n'));
  }

  action_end(cursors);
}

void BufferData::guess_tab_type() {
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

void BufferView::goto_endline(int marker_idx) {
  Pos pos = cursors[marker_idx].pos;
  move_to_x(marker_idx, data->lines[pos.y].length);
  cursors[marker_idx].ghost_x = GHOST_EOL;
}

void BufferView::goto_endline() {
  for (int i = 0; i < cursors.size; ++i)
    goto_endline(i);
}

int BufferData::begin_of_line(int y) {
  int x;

  x = 0;
  while (x < lines[y].length && (isspace(lines[y][x])))
    ++x;
  return x;
}

void BufferView::goto_beginline() {
  for (int i = 0; i < cursors.size; ++i) {
    int x = data->begin_of_line(cursors[i].y);
    move_to_x(i, x);
    cursors[i].ghost_x = GHOST_BOL;
  }
}

void BufferData::init(bool buffer_is_dynamic, Slice descr) {
  util_free(*this);
  *this = {};
  this->description = descr;
  this->is_dynamic = buffer_is_dynamic;
  lines += {};
}

void BufferData::empty(Array<Cursor> &cursors) {
  if (cursors.size == 1 && lines.size == 1 && lines[0].length == 0)
    return;

  G.flags.cursor_dirty = true;

  if (!cursors.size)
    cursors += {};

  action_begin(cursors);

  cursors.size = 1;

  if (lines.size == 0)
    lines += {};
  while (lines.size > 1)
    delete_line(cursors, 0);
  if (lines[0].length > 0)
    remove_range(cursors, {0, 0}, {lines[0].length, 0});

  cursors[0] = {};

  action_end(cursors);
}

int BufferData::advance(int *x, int *y) const {
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

int BufferData::advance(Pos &p) const {
  return advance(&p.x, &p.y);
}

int BufferView::advance(int marker_idx) {
  Pos &pos = cursors[marker_idx].pos;
  int err = data->advance(&pos.x, &pos.y);
  if (err)
    return err;
  cursors[marker_idx].ghost_x = data->lines[pos.y].visual_offset(pos.x, G.tab_width);
  return 0;
}

int BufferData::advance_r(Pos &p) const {
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

int BufferView::advance() {
  int r = 1;
  for (int i = 0; i < cursors.size; ++i)
    r &= advance(i);
  return r;
}

int BufferView::advance_r() {
  int r = 1;
  for (int i = 0; i < cursors.size; ++i)
    r &= advance_r(i);
  return r;
}

int BufferView::advance_r(int marker_idx) {
  Pos &pos = cursors[marker_idx].pos;
  int err = data->advance_r(pos);
  if (err)
    return err;
  cursors[marker_idx].ghost_x = data->lines[cursors[marker_idx].y].visual_offset(pos.x, G.tab_width);
  return 0;
}

// TODO, FIXME: properly implement @utf8
Utf8char BufferData::getchar(int x, int y) {
  return Utf8char::create(x >= lines[y].length ? '\n' : lines[y][x]);
}

Utf8char BufferData::getchar(Pos p) {
  return getchar(p.x, p.y);
}

TokenResult BufferData::token_read(Pos *p, int y_end) {
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
      x = 0;
      ++y;
      continue;
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
      result.tok = TOKEN_LINE_COMMENT;
      result.a = {x, y};
      result.b = {lines[y].length-1, y};
      x = lines[y].length;
      goto done;
    }

    /* number */
    if (is_number_head(c)) {
      result.tok = TOKEN_NUMBER;
      result.a = {x, y};
      while (x < lines[y].length) {
        result.b = {x, y};
        c = lines[y][++x];
        if (!is_number_tail(c))
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
        result.operator_str = lines[y](x, x + operators[i].length);
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

#if 0
void Canvas:fit_range_to_bounds() {
  Pos a = range.a;
  Pos b = range.b;
  a -= offset;
  b -= offset;
  if (bounds.w == -1)
    bounds.w = w - bounds.x;
  if (bounds.h == -1)
    bounds.h = h - bounds.y;

  bounds.x = clamp(bounds.x, 0, w-1);
  bounds.w = clamp(bounds.w, 0, w-bounds.x);
  bounds.y = clamp(bounds.y, 0, h-1);
  bounds.h = clamp(bounds.h, 0, h-bounds.y);

  if (a.y >= bounds.y + bounds.h || b.y < bounds.y)
    return;

  a.x = clamp(a.x, bounds.x, bounds.w);
  if (a.y < bounds.y) {
    a.y = bounds.y;
    a.x = 0;
  }

  b.x = clamp(b.x, bounds.x, bounds.w);
  if (b.y >= bounds.y + bounds.h) {
    b.y = bounds.y + bounds.h - 1;
    b.x = bounds.x + bounds.w - 1;
  }
}
#endif

void Canvas::_normalize_range(Pos &a, Pos &b) {
  a -= offset;
  b -= offset;
  a.x = clamp(a.x, 0, w-1);
  a.y = clamp(a.y, 0, h-1);
  b.x = clamp(b.x, 0, w-1);
  b.y = clamp(b.y, 0, h-1);
}

void Canvas::blend_textcolor(Range range, Color c) {
  Pos a = range.a;
  Pos b = range.b;
  _normalize_range(a, b);

  if (a.y == b.y) {
    for (int x = a.x; x < b.x; ++x)
      this->text_colors[a.y*this->w + x] = Color::blend(this->text_colors[a.y*this->w + x], c);
    return;
  }

  int y = a.y;
  for (int x = a.x; x < w; ++x)
    this->text_colors[y*this->w + x] = Color::blend(this->text_colors[y*this->w + x], c);
  for (++y; y < b.y; ++y)
    for (int x = 0; x < w; ++x)
      this->text_colors[y*this->w + x] = Color::blend(this->text_colors[y*this->w + x], c);
  for (int x = 0; x < b.x; ++x)
    this->text_colors[y*this->w + x] = Color::blend(this->text_colors[y*this->w + x], c);
}

void Canvas::blend_textcolor_additive(Range range, Color c) {
  Pos a = range.a;
  Pos b = range.b;
  _normalize_range(a, b);

  if (a.y == b.y) {
    for (int x = a.x; x < b.x; ++x)
      this->text_colors[a.y*this->w + x] = Color::blend_additive(this->text_colors[a.y*this->w + x], c);
    return;
  }

  int y = a.y;
  for (int x = a.x; x < w; ++x)
    this->text_colors[y*this->w + x] = Color::blend_additive(this->text_colors[y*this->w + x], c);
  for (++y; y < b.y; ++y)
    for (int x = 0; x < w; ++x)
      this->text_colors[y*this->w + x] = Color::blend_additive(this->text_colors[y*this->w + x], c);
  for (int x = 0; x < b.x; ++x)
    this->text_colors[y*this->w + x] = Color::blend_additive(this->text_colors[y*this->w + x], c);
}

// fills a to b but only inside the bounds 
void Canvas::fill_textcolor(Range range, Color c) {
  Pos a = range.a;
  Pos b = range.b;
  _normalize_range(a, b);

  if (a.y == b.y) {
    for (int x = a.x; x < b.x; ++x)
      this->text_colors[a.y*this->w + x] = c;
    return;
  }

  int y = a.y;
  for (int x = a.x; x < w; ++x)
    this->text_colors[y*this->w + x] = c;
  for (++y; y < b.y; ++y)
    for (int x = 0; x < w; ++x)
      this->text_colors[y*this->w + x] = c;
  for (int x = 0; x < b.x; ++x)
    this->text_colors[y*this->w + x] = c;
}

// w,h: use -1 to say it goes to the end
void Canvas::fill_textcolor(Rect r, Color c) {
  r.x -= offset.x;
  r.y -= offset.y;
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
  r.x -= offset.x;
  r.y -= offset.y;
  if (r.w == -1)
    r.w = this->w - r.x;
  if (r.h == -1)
    r.h = this->h - r.y;
  r.w = at_most(r.w, this->w - r.x);
  r.h = at_most(r.h, this->h - r.y);
  if (r.x < 0) {
    r.w += r.x;
    r.x = 0;
  }

  if (r.y < 0) {
    r.h += r.y;
    r.y = 0;
  }

  if (r.w < 0 || r.h < 0 || r.x > this->w || r.y > this->h)
    return;

  for (int y = r.y; y < r.y+r.h; ++y)
  for (int x = r.x; x < r.x+r.w; ++x)
    background_colors[y*this->w + x] = c;
}

void Canvas::render_str(Pos p, const Color *text_color, const Color *background_color, int xclip0, int xclip1, Slice s) {
  if (!s.length)
    return;

  p.x -= offset.x;
  p.y -= offset.y;

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
  render_str(p, text_color, background_color, x0, x1, G.tmp_render_buffer.slice);
}

void Canvas::render_strf(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  this->render_str_v(p, text_color, background_color, x0, x1, fmt, args);
  va_end(args);
}

void Canvas::render(Pos pos) {
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
      {(u16)(pos.x + size.x),                 (u16)(pos.y + shadow_offset),          shadow_color},
      {(u16)(pos.x + size.x),                 (u16)(pos.y + size.y),                 shadow_color},
      {(u16)(pos.x + size.x + shadow_offset), (u16)(pos.y + size.y + shadow_offset), shadow_color2},
      {(u16)(pos.x + size.x + shadow_offset), (u16)(pos.y + shadow_offset),          shadow_color2});

    // bottom side
    push_quad(
      {(u16)(pos.x + shadow_offset),          (u16)(pos.y + size.y),                 shadow_color},
      {(u16)(pos.x + shadow_offset),          (u16)(pos.y + size.y + shadow_offset), shadow_color2},
      {(u16)(pos.x + size.x + shadow_offset), (u16)(pos.y + size.y + shadow_offset), shadow_color2},
      {(u16)(pos.x + size.x),                 (u16)(pos.y + size.y),                 shadow_color});
  }

  // render base background
  push_square_quad((u16)pos.x, (u16)(pos.x+size.x), (u16)(pos.y), (u16)(pos.y+size.y), background);
  pos.x += margin;
  pos.y += margin;

  // render background
  for (int y = 0; y < h; ++y) {
    for (int x0 = 0, x1 = 1; x1 <= w; ++x1) {
      if (x1 < w && background_colors[y*w + x1] == background_colors[y*w + x0])
        continue;
      Pos p0 = char2pixel(x0,y) + pos;
      Pos p1 = char2pixel(x1,y+1) + pos;
      const Color c = background_colors[y*w + x0];
      push_square_quad((u16)p0.x, (u16)p1.x, (u16)p0.y, (u16)p1.y, c);
      x0 = x1;
    }
  }

  // render text
  const int text_offset_y = get_text_offset_y(G.font_height);
  for (int row = 0; row < h; ++row) {
    G.tmp_render_buffer.clear();
    G.tmp_render_buffer.append(&chars[row*w], w);
    int y = char2pixely(row+1) + text_offset_y + pos.y;
    for (int x0 = 0, x1 = 1; x1 <= w; ++x1) {
      if (x1 < w && text_colors[row*w + x1] == text_colors[row*w + x0])
        continue;
      int x = char2pixelx(x0) + pos.x;
      push_textn(G.tmp_render_buffer.chars + x0, x1 - x0, x, y, false, text_colors[row*w + x0]);
      x0 = x1;
    }
  }
}

void Pane::render_as_dropdown() {
  BufferView &b = buffer;

  Canvas canvas;
  int y_max = this->numchars_y();
  canvas.init(this->numchars_x(), this->numchars_y());
  canvas.background = *this->background_color;
  canvas.fill(Utf8char{' '});
  canvas.fill(*this->text_color, *this->background_color);
  canvas.draw_shadow = true;
  canvas.margin = this->margin;

  // draw each line 
  for (int y = 0; y < at_most(b.data->lines.size, y_max); ++y)
    canvas.render_str({0, y}, this->text_color, NULL, 0, -1, b.data->lines[y].slice);

  // highlight the line you're on
  if (active_highlight_background_color) {
    for (int i = 0; i < b.cursors.size; ++i)
      canvas.fill_background({0, b.cursors[i].y, {-1, 1}}, *active_highlight_background_color);
  }

  canvas.render(this->bounds.p);

  util_free(canvas);
  render_quads();
  render_text();
}

void Pane::render_syntax_highlight(Canvas &canvas, int y1) {
  #define render_highlight(color) canvas.fill_textcolor(Range{b.data->to_visual_pos(t->a), b.data->to_visual_pos(t->b)}, color)

  BufferView &b = this->buffer;
  // syntax @highlighting
  int y0 = canvas.offset.y;
  Pos pos = {0, canvas.offset.y};
  TokenInfo *t = b.data->gettoken(pos);

  if (t) {
    for (; t < b.data->tokens.end() && t->a.y <= y1; ++t) {
      if (t->token == TOKEN_NULL)
        break;
      switch (t->token) {

        case TOKEN_NUMBER:
          render_highlight(G.number_color);
          break;

        case TOKEN_BLOCK_COMMENT_BEGIN:
          render_highlight(G.comment_color);
          break;

        case TOKEN_BLOCK_COMMENT:
        case TOKEN_BLOCK_COMMENT_END:
        case TOKEN_LINE_COMMENT:
          render_highlight(G.comment_color);
          break;

        case TOKEN_STRING:
        case TOKEN_STRING_BEGIN:
          render_highlight(G.string_color);
          break;

        case TOKEN_OPERATOR:
          render_highlight(G.operator_color);
          break;

        case TOKEN_IDENTIFIER: {
          // check for keywords
          if (t->str.length > 0 && t->str[0] == '#')
            render_highlight(keyword_colors[KEYWORD_MACRO]);
          else {
            for (int i = 0; i < (int)ARRAY_LEN(keywords); ++i) {
              if (t->str == keywords[i].name) {
                render_highlight(keyword_colors[keywords[i].type]);
                break;
              }
            }
          }
        break;}

        default:
          break;
      }
    }
  }

  // syntax highlight definitions
  for (Range r : buffer.data->definitions) {
    if (r.a.y > y1)
      break;
    if (r.b.y < y0)
      continue;
    canvas.fill_textcolor(Range{b.data->to_visual_pos(r.a), b.data->to_visual_pos(r.b)}, G.identifier_color);
  }
}

void Pane::add_subpane(BufferData *b, Pos pos) {
  Pane *p = new Pane{};
  Pane::init_edit(*p, b, (Color*)&G.default_background_color, &G.default_text_color, &G.active_highlight_background_color.color, &G.inactive_highlight_background_color, true);
  p->buffer.move_to(pos);
  p->parent = this;
  subpanes += {buffer.cursors[0].pos, p};
  G.editing_panes += p;
}

void Pane::render() {
  switch (type) {
    case PANETYPE_NULL:
      break;
    case PANETYPE_EDIT:
      render_edit();
      break;
    case PANETYPE_MENU:
      render_menu();
      break;
    case PANETYPE_STATUSMESSAGE:
      render_single_line();
      break;
    case PANETYPE_DROPDOWN:
      render_as_dropdown();
      break;
  }
}

void Pane::update_suggestions() {
  if (!G.flags.cursor_dirty)
    return;
  if (!menu.get_suggestions)
    return;
  util_free(menu.suggestions);
  menu.suggestions = menu.get_suggestions();
  menu.current_suggestion = 0;
}

void Pane::clear_suggestions() {
  util_free(menu.suggestions);
  menu.current_suggestion = 0;
}

void Pane::render_single_line() {
  BufferView &b = buffer;
  // TODO: scrolling in x
  Pos buf_offset = {menu.prefix.length+2, 0};

  // render the editing line
  Canvas canvas;
  canvas.init(this->numchars_x(), 1);
  canvas.fill(*this->text_color, *this->background_color);
  canvas.background = *this->background_color;
  canvas.margin = this->margin;

  // draw prefix
  canvas.render_strf({0, 0}, &G.default_gutter_text_color, NULL, 0, -1, "{}: ", (Slice)menu.prefix);

  // draw buffer
  canvas.render_str(buf_offset, this->text_color, NULL, 0, -1, b.data->lines[0].slice);

  // draw marker
  if (G.selected_pane == this)
    canvas.fill_background({b.cursors[0].pos + buf_offset, {1, 1}}, G.marker_background_color.color);
  else if (G.bottom_pane != this)
    canvas.fill_background({b.cursors[0].pos + buf_offset, {1, 1}}, G.marker_inactive_color);

  canvas.render(this->bounds.p);

  util_free(canvas);
}

void Pane::render_menu_popup() {
  if (!menu.suggestions.size)
    return;

  // resize dropdown pane
  int width = ARRAY_MAXVAL_BY(menu.suggestions, length);
  int height = at_most(menu.suggestions.size, pixel2chary(G.win_height) - 10);
  if (height <= 0)
    return;

  Canvas canvas;
  canvas.init(width, height);
  canvas.fill(*text_color, *background_color);
  canvas.background = *this->background_color;
  canvas.margin = this->margin;

  // position this pane
  Pos p = this->bounds.p;
  p.y -= char2pixely(height) + margin;

  for (int i = 0; i < at_most(menu.suggestions.size, height); ++i)
    canvas.render_str({0, i}, text_color, NULL, 0, -1, menu.suggestions[i].slice);

  // highlight
  canvas.fill_background({0, menu.current_suggestion, {-1, 1}}, *this->active_highlight_background_color);

  canvas.render(p);
  util_free(canvas);

  render_quads();
  render_text();
}

void Pane::render_menu() {
  render_single_line();
  render_menu_popup();
}

void Pane::render_edit() {
  BufferView &b = buffer;
  BufferData &d = *buffer.data;

  Rect orig_bounds = bounds;
  const int header_height = 25;
  bounds.y += header_height;
  bounds.h -= header_height;

  // recalculate the bounds of this pane depending on the number of subpanes
  int subpane_depth = calc_max_subpane_depth();
  int total_width = orig_bounds.w;
  bounds.w = total_width/(subpane_depth+1);

  // calc buffer bound
  Pos buf_offset = {this->calc_left_visible_column(), this->calc_top_visible_row()};
  int buf_y1 = at_most(buf_offset.y + this->numchars_y(), d.lines.size);

  // draw gutter
  this->_gutter_width = at_least(calc_num_chars(buf_y1) + 3, 6);
  {
    Canvas gutter;
    gutter.init(_gutter_width, this->numchars_y());
    gutter.background = *this->background_color;
    for (int y = 0, line = buf_offset.y; line < buf_y1; ++y, ++line)
      if (line < d.lines.size)
        gutter.render_strf({0, y}, &G.default_gutter_text_color, background_color, 0, _gutter_width, " %i", line + 1);
      else
        gutter.render_str({0, y}, &G.default_gutter_text_color, background_color, 0, _gutter_width, Slice::create(" ~"));
    gutter.render(this->bounds.p);
    util_free(gutter);
  }

  Canvas canvas;
  canvas.init(this->numchars_x()-_gutter_width, this->numchars_y());
  canvas.fill(*this->text_color, *this->background_color);
  canvas.background = *this->background_color;
  canvas.margin = this->margin;
  canvas.offset = buf_offset;

  // draw each line 
  for (int y = buf_offset.y; y < buf_y1; ++y)
    canvas.render_str({0, y}, this->text_color, NULL, 0, -1, d.lines[y].slice);

  // draw syntax highlighting
  this->render_syntax_highlight(canvas, buf_y1);

  // draw paste highlight
  #if 1
  for (PasteHighlight ph : d.paste_highlights) {
    if (ph.b.y < buf_offset.y || ph.a.y > buf_y1)
      continue;
    Color color = COLOR_LIGHT_YELLOW;
    color.a = (u8)(ph.alpha*255.0f);
    canvas.blend_textcolor(Range{d.to_visual_pos(ph.a), d.to_visual_pos(ph.b)}, color);
  }
  #endif

  // draw blame
  if (d.blame.data.size) {
    Slice last_hash = {};
    StringBuffer msg = {};
    int i = 0;
    for (int y = buf_offset.y; y < buf_y1; ++y) {
      while (i < d.blame.data.size && d.blame.data[i].line <= y)
        ++i;
      BlameData bd = d.blame.data[i-1];
      if (last_hash == bd.hash)
        continue;
      last_hash = Slice::create(bd.hash);

      Pos p = d.to_visual_pos({d.lines[y].length, y});
      p.x = at_least(p.x+2, 30);
      msg.length = 0;
      msg.appendf("%s - %s - %s", bd.hash, bd.author, bd.summary);
      canvas.render_str(p, &COLOR_DEEP_ORANGE, NULL, p.x, -1, msg.slice);
    }
    util_free(msg);
  }

  // highlight the line you're on
  const Color *highlight_background_color = G.editing_pane == this ? this->active_highlight_background_color : this->inactive_highlight_background_color;
  if (highlight_background_color)
    for (Cursor c : b.cursors)
      canvas.fill_background({d.to_visual_pos(Pos{0, c.y}), {-1, 1}}, *highlight_background_color);

  // highlight visual start
  if (G.visual_start.buffer == buffer.data && G.visual_entire_line)
    for (Pos pos : G.visual_start.cursors)
      canvas.fill_background({d.to_visual_pos({0, pos.y}), {-1, 1}}, *this->inactive_highlight_background_color);

  // if there is a search term, highlight that as well
  if (G.search_term.length > 0) {
    Pos pos = {0, buf_offset.y};
    while (d.find(G.search_term.slice, false, &pos) && pos.y < buf_y1)
      canvas.fill_background({d.to_visual_pos(pos), G.search_term.length, 1}, G.search_term_background_color.color);
  }

  // draw visual start marker
  if (G.visual_start.buffer == buffer.data)
    for (Pos pos : G.visual_start.cursors)
      canvas.fill_background({d.to_visual_pos(pos), {1, 1}}, G.default_marker_background_color.color);

  // draw marker
  for (Cursor c : b.cursors) {
    if (G.selected_pane == this)
      // canvas.fill_background({buf2char(pos), {1, 1}}, Color::from_hsl(fmodf(i*360.0f/b.markers.size, 360.0f), 0.7f, 0.7f));
      canvas.fill_background({d.to_visual_pos(c.pos), {1, 1}}, G.default_marker_background_color.color);
    else if (G.bottom_pane != this)
      canvas.fill_background({d.to_visual_pos(c.pos), {1, 1}}, G.marker_inactive_color);
  }

  canvas.render(this->bounds.p + Pos{_gutter_width*G.font_width, 0});

  // render filename
  // render filename
  const Slice filename = d.name();
  const int header_text_size = header_height - 6;
  push_text(filename.chars, bounds.x + G.font_width, bounds.y + get_text_offset_y(header_text_size) - 3, false, d.modified() ? COLOR_ORANGE : COLOR_WHITE, header_text_size);

  util_free(canvas);
  render_quads();
  render_text();

  if (G.editing_pane == this)
    render_dropdown(this);

  // reflow and render subpanes
  {
    float w,h,x,y;
    int num_panes_in_sight = 0;
    int subpane_margin = 0;
    int total_margin = 0;
    num_panes_in_sight = subpanes.size;
    if (!num_panes_in_sight)
      goto subpanes_done;

    // total_margin = subpane_margin*(num_panes_in_sight+1);
    // subpane_margin = total_margin / (num_panes_in_sight+1);

    w = (float)total_width - bounds.w;
    h = (float)(orig_bounds.h - total_margin)/num_panes_in_sight;
    x = (float)orig_bounds.x + bounds.w;
    y = (float)orig_bounds.y + subpane_margin;
    for (SubPane p : subpanes) {
      // if (p.anchor_pos.y < buf_offset.y || p.anchor_pos.y > buf_y1)
        // continue;
      p.pane->bounds = {(int)x, (int)y, (int)w, (int)h};
      p.pane->render();
      y += h + subpane_margin;
    }

    subpanes_done:;
  }

  bounds.w = total_width;
  bounds.y -= header_height;
  bounds.h += header_height;
}

Pos Pane::buf2pixel(Pos p) const {
  p = buffer.data->to_visual_pos(p);
  p.x -= this->calc_left_visible_column();
  p.y -= this->calc_top_visible_row();
  p = char2pixel(p) + this->bounds.p;
  p.x += this->_gutter_width * G.font_width;
  return p;
}

Pos Pane::buf2char(Pos p) const {
  p = buffer.data->to_visual_pos(p);
  p.x -= this->calc_left_visible_column();
  p.y -= this->calc_top_visible_row();
  return p;
}

int Pane::calc_top_visible_row() const {
  return at_least(0, this->buffer.cursors[0].y - this->numchars_y()/2);
}

int Pane::calc_left_visible_column() const {
  int x = this->buffer.cursors[0].x;
  x = this->buffer.data->lines[this->buffer.cursors[0].y].visual_offset(x, G.tab_width);
  x -= this->numchars_x()*6/7;
  return at_least(x, 0);
}

int Pane::calc_max_subpane_depth() const {
  int result = 0;
  for (SubPane p : subpanes) {
    int d = p.pane->calc_max_subpane_depth()+1;
    result = max(result, d);
  }
  return result;
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
  // return (this->bounds.h - 2*this->margin) / G.line_height + 1;
  const int header_height = type == PANETYPE_EDIT ? G.line_height : 0;
  return ((this->bounds.h - header_height - 2*this->margin) / G.line_height) + 2;
}

static Key get_input(bool *window_active) {
  if (!*window_active)
    SDL_WaitEvent(NULL);

  for (SDL_Event event; SDL_PollEvent(&event);) {
    Utf8char input = {};
    SpecialKey special_key = KEY_NONE;
    bool ctrl = false;

    switch (event.type) {
    case SDL_WINDOWEVENT:
      switch (event.window.event) {
        case SDL_WINDOWEVENT_CLOSE:
          editor_exit(0);
          break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
          *window_active = true;
          break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
          *window_active = false;
          break;
        default:
          break;
      }
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
        if (ctrl)
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

    // handle input
    // TODO: insert utf8 characters
    if ((input.code && input.is_ansi()) || special_key) {
      Key key = special_key ? special_key : input.ansi();
      if (ctrl)
        key |= KEY_CONTROL;
      return key;
    }
  }
  return KEY_NONE;
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

  #ifdef OS_WINDOWS
  Path p = Path::create(Slice::create("hello"));
  p.push("..");
  puts(p.string.chars);
  assert(p.string.slice == "hello\\..");
  p.push("some_file");
  puts(p.string.chars);
  assert(p.string.slice == "some_file");
  p.push("some\\path\\..\\h");
  puts(p.string.chars);
  assert(p.string.slice == "some_file\\some\\h");
  #endif
}

#if 1
#include <type_traits>
STATIC_ASSERT(std::is_pod<State>::value, state_must_be_pod);
#endif

static void handle_pending_removes() {
  // remove panes
  for (int pane_idx = 0; pane_idx < G.panes_to_remove.size; ++pane_idx) {
    Pane *p = G.panes_to_remove[pane_idx];

    // remove from parent subpane list
    if (p->parent) {
      Pane *parent = p->parent;
      for (int i = 0; i < parent->subpanes.size; ++i) {
        if (parent->subpanes[i].pane == p) {
          parent->subpanes.remove_slow(i);
          Pane *next = parent->subpanes.size == 0 ? parent : parent->subpanes[clamp(i, 0, parent->subpanes.size-1)].pane;
          if (G.editing_pane == p)
            G.editing_pane = next;
          if (G.selected_pane == p)
            G.selected_pane = next;
          break;
        }
      }
    }

    // remove subpanes
    for (SubPane sp : p->subpanes) {
      sp.pane->parent = 0;
      G.panes_to_remove += sp.pane;
    }
    p->subpanes.free_shallow();

    // free pane
    util_free(*p);
    if (p->is_dynamic)
      delete p;

    // remove from global pane list
    int idx = G.editing_panes.find(p) - G.editing_panes.items;
    G.editing_panes.remove_slow(p);

    // update global pane pointers
    if (G.editing_pane == p)
      G.editing_pane = G.editing_panes[clamp(idx, 0, G.editing_panes.size-1)];
    if (G.selected_pane == p)
      G.selected_pane = G.editing_panes[clamp(idx, 0, G.editing_panes.size-1)];
  }
  G.panes_to_remove.size = 0;

  if (G.editing_panes.size == 0) {
    Pane *main_pane = new Pane{};
    Pane::init_edit(*main_pane, &G.null_buffer, &G.default_background_color, &G.default_text_color, &G.active_highlight_background_color.color, &G.inactive_highlight_background_color, true);
    G.editing_panes += main_pane;
    G.selected_pane = main_pane;
    G.editing_pane = main_pane;
  }

  // remove buffers
  for (BufferData *b : G.buffers_to_remove) {
    if (!b->is_bound_to_file())
      continue;

    // free buffer
    util_free(*b);
    G.buffers.remove_slow(b);
    if (b->is_dynamic)
      delete b;

    // reset any panes that were using this buffer
    for (int k = 0; k < G.editing_panes.size; ++k) {
      if (G.editing_panes[k]->buffer.data == b) {
        util_free(*G.editing_panes[k]);
        Pane::init_edit(*G.editing_panes[k], &G.null_buffer, &G.default_background_color, &G.default_text_color, &G.active_highlight_background_color.color, &G.inactive_highlight_background_color, true);
      }
    }
  }
  G.buffers_to_remove.size = 0;
}

#ifdef OS_WINDOWS
int wmain(int, const wchar_t *[], wchar_t *[])
#else
int main(int, const char *[])
#endif
{
  util_init();
  state_init();
  test();

  bool window_active = true;
  for (;;) {
    static u32 ticks = SDL_GetTicks();
    const float dt = clamp((float)(SDL_GetTicks() - ticks) / 1000.0f * 60.0f, 0.3f, 3.0f);
    ticks = SDL_GetTicks();

    Key key = get_input(&window_active);
    if (!window_active)
      continue;

    if (key)
      handle_input(key);
    handle_rendering(dt);

    // draw activation meter
    {
      StringBuffer sb = {};
      sb.appendf("%i", (int)G.activation_meter);
      Color c = COLOR_WHITE;
      if (G.activation_meter < 10.0f)
        c = COLOR_WHITE;
      else if (G.activation_meter < 20.0f)
        c = COLOR_BLUE;
      else if (G.activation_meter < 30.0f)
        c = COLOR_ORANGE;
      else
        c = COLOR_DEEP_ORANGE;

      push_text(sb.chars, G.win_width - G.font_width*3, G.win_height - G.font_height*2, true, c);
      util_free(sb);
    }

    render_quads();
    render_text();

    G.activation_meter = at_least(G.activation_meter - dt / 5000.0f, 0.0f);

    SDL_GL_SwapWindow(G.window);

    handle_pending_removes();

    G.flags.cursor_dirty = false;

    if (G.mode != MODE_INSERT)
      assert(G.editing_pane->buffer.data->_action_group_depth == 0);
  }
}
