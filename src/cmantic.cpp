/*
 * TODO:
 * Texture flicker
 * 'dL' when at end of line
 * Delete multiline string
 * (rel)load buffer that no longer exists
 * visual jump when horizontal scrolling
 * sometimes double indent 
 * Multiline dw
 * Action area 'w' should only apply to current identifier range (not whitespace after it)
 * Paste line puts line below

 * get rid of some modes now that we have proper prompts (MODE_DELETE for example)

 * visual jump to line
 * replace with selection
 * remove string member from stringbuffer, because it is unsafe (string will free the wrong amount of mem)
 * language-dependent autoindent
 * use autoindent to figure out indentation
 * when a long line is under selection, show expansion of that one line
 * Optimize tokenization
 * project file
 * Show current class/function/method/namespace
 * create new file
 * dp on empty ()
 * json language support, and auto formatting (requires language-dependent autoindent)
 * gd for multiple definitions with the same name
 * If a file is only 1 line long, the line will never be read
 * Code folding
 * always distinguish block selection on inner and outer?
 * Goto definition should show entire function parameter list
 * Definition parsing should support templates
 * Fix git blame parsing (git blame doesn't re-output author etc for hashes it's already output)
 * Support for multiple languages
 * Have a global unified list of Locations/cursors
 * listen on file changes and automatically reload (or show a warning?)
 * Search on word only
 * Create an easy-to-use token iterator
 * make 'dp' use tokens instead of chars
 * dropdown_autocomplete should not delete characters (it ruins paste)
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
 */

#if 1
#define DEBUG
#endif


#include "util.hpp"
#include "perf.hpp"
#include "graphics.hpp"
#include "algorithm.hpp"
#include "git.hpp"
#include "parse.hpp"
#include "buffer.hpp"
#include "text_render_utils.hpp"
#include "pane.hpp"
#include <ctime>

#define UTIL_IMPL
#include "util.hpp"

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

enum PromptType {
  PROMPT_STRING,
  PROMPT_INT,
  PROMPT_FLOAT,
  PROMPT_BOOLEAN,
  PROMPT_KEY
};

struct ColorScheme {
  Color syntax_control;
  Color syntax_type;
  Color syntax_specifier;
  Color syntax_definition_keyword;
  Color syntax_definition;
  Color syntax_function;
  Color syntax_macro;
  Color syntax_constant;
  Color syntax_number;
  Color syntax_string;
  Color syntax_comment;
  Color syntax_operator;
  Color syntax_text;
  Color gutter_text;
  Color gutter_background;
  Color line_highlight;
  Color line_highlight_pop;
  Color line_highlight_inactive;
  Color marker_inactive;
  Color search_term_text;
  Color search_term_background;
  Color autocomplete_background;
  Color autocomplete_highlight;
  Color menu_background;
  Color menu_highlight;
  Color background;
  Color git_blame;
};

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
  MODE_GOTO_ALL_DEFINITIONS,
  MODE_CWD,
  MODE_PROMPT,
  MODE_REPLACE,
  MODE_COUNT
};

struct State {
  /* @renderer rendering state */
  SDL_Window *window;
  bool debug_mode;
  float dt; // this will be clamped so it's not too large
  float real_dt; // this is the real dt (not clamped)
  int font_width;
  int font_height;
  int line_margin;
  int line_height;
  StringBuffer tmp_render_buffer;
  int win_height, win_width;
  Path ttf_file;

  /* settings */
  PoppedColor marker_background_color;
  PoppedColor search_term_background_color;
  PoppedColor active_highlight_background_color;
  RotatingColor default_marker_background_color;
  PoppedColor bottom_pane_color;
  PoppedColor visual_jump_color;
  PoppedColor visual_jump_background_color;
  Color* keyword_colors[KEYWORD_COUNT];

  ColorScheme color_scheme;

  /* editor state */
  Path current_working_directory;
  Path install_dir;
  Array<Pane*> editing_panes;
  Pane *bottom_pane;
  Pane *selected_pane; // the pane that the marker currently is on, could be everything from editing pane, to menu pane, to filesearch pane
  Pane *editing_pane; // the pane that is currently being edited on, regardless if you happen to be in filesearch or menu
  Array<BufferData*> buffers;
  Array<String> project_definitions;

  Pane menu_pane;
  BufferData menu_buffer;
  Pane search_pane;
  BufferData search_buffer;
  Pane status_message_pane;
  BufferData status_message_buffer;
  Pane dropdown_pane;
  BufferData dropdown_buffer;
  BufferData null_buffer;
  BufferData build_result_buffer;

  float activation_meter;

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
    Key key;
  } prompt_result;
  void (*prompt_callback)(void);

  /* status message state */
  bool status_message_was_set;

  /* goto state */
  unsigned int goto_line_number; /* unsigned in order to prevent undefined behavior on wrap around */

  /* goto_definition state */
  Pos goto_definition_begin_pos;
  Array<Pos> definition_positions;

  /* search state */
  bool search_failed;
  Pos search_begin_pos;
  Array<TokenInfo> search_term;

  /* file tree state */
  Array<Path> files;
  
  /* visual mode state */
  Location visual_start; // starting position of visual mode
  bool visual_entire_line;

  /* some settings */
  int tab_width; /* how wide are tabs when rendered */
  int default_tab_type; /* 0 for tabs, 1+ for spaces */

  /* build state */
  Array<String> build_command;
  Stream build_result_output;

  /* visual jump */
  Pane *current_visual_jump_pane;
  Array<Pos> visual_jump_positions;

  /* idle game state */
  TextureHandle pet_texture;
  Array<Rect> pet_sprites;
  int pet_index;
};
static State G;

// ensure State is POD
#include <type_traits>
STATIC_ASSERT(std::is_pod<State>::value, state_must_be_pod);

static char visual_jump_highlight_keys[] = {'a', 's', 'd', 'f', 'h', 'j', 'k', 'l', 'q', 'w', 'e', 'r', 'u', 'i', 'o', 'p', ',', '.', 'z', 'x', 'c', 'v', 'm', 'n', 't', 'y', 'b', '/', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '{', '}', '[', ']', '\\', '\'', '-', '=', '?', ':', ';', '"', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'X', 'Y', 'Z'};

#define BUFFER_IMPL
#include "buffer.hpp"
#define TEXT_RENDER_UTIL_IMPL
#include "text_render_utils.hpp"
#define PANE_IMPL
#include "pane.hpp"

static void state_init();
static void do_update(float dt);
static void do_render();
static Key get_input(bool *window_active);
static void test();
static void test_update();
static void handle_pending_removes();
static void handle_input(Key key);

#ifdef OS_WINDOWS
// int wmain(int, const wchar_t *[], wchar_t *[])
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
#else
int main(int, const char *[])
#endif
{
  util_init();
  state_init();

  #ifdef DEBUG
  test();
  #endif

  bool window_active = true;
  for (uint loop_idx = 0;; ++loop_idx) {

    static u32 ticks = SDL_GetTicks();
    G.real_dt = (float)(SDL_GetTicks() - ticks) / 1000.0f * 60.0f, 
    G.dt = at_most(G.real_dt, 3.0f);
    ticks = SDL_GetTicks();

    Key key = get_input(&window_active);
    if (!window_active)
      continue;

    TIMING_BEGIN(TIMING_MAIN_LOOP);

    if (key)
      handle_input(key);

    #ifdef DEBUG
    test_update();
    #endif

    TIMING_BEGIN(TIMING_UPDATE);
    do_update(G.dt);
    TIMING_END(TIMING_UPDATE);

    TIMING_BEGIN(TIMING_RENDER);
    do_render();
    TIMING_END(TIMING_RENDER);

    // do_pet_update_and_draw(dt);
    if (G.debug_mode) {
      static String last_fps;
      if (loop_idx % 100 == 0) {
        util_free(last_fps);
        last_fps = String::createf("fps: %f", 60.0f/G.real_dt);
      }
      int h = G.win_height;
      push_text(last_fps.chars, G.win_width - 100, h, true, COLOR_WHITE, 20), h -= 22;
      if (G.search_term.size)
        push_textn(G.search_term[0].str.chars, G.search_term[0].str.length, G.win_width - 100, h, true, COLOR_WHITE, 20), h -= 22;
    }

    render_quads();
    render_textured_quads(G.pet_texture);
    render_text();

    handle_pending_removes();

    glFinish();
    SDL_GL_SwapWindow(G.window);
    glFinish();

    G.flags.cursor_dirty = false;

    // reset performance timers
    TIMING_END(TIMING_MAIN_LOOP);
    #ifdef DEBUG
      if (loop_idx%100 == 0)
        print_perf_info();
      update_perf_info();
    #endif
  }
}

// Coroutine stuff
#define yield(name) do {step = name; return;} while(0); name:
#define yield_break do {step = {}; return;} while(0)

static void status_message_set(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  G.status_message_pane.buffer.empty();
  G.status_message_buffer[0].clear();
  G.status_message_buffer[0].appendv(fmt, args);

  G.status_message_was_set = true;

  // grab only the first line
  int i;
  if (G.status_message_buffer[0].find('\n', &i)) {
    if (i > 0 && G.status_message_buffer[0][i-1] == '\r')
      --i;
    G.status_message_buffer[0].length = i;
  }
  G.bottom_pane = &G.status_message_pane;
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

static void save_buffer(BufferData *b) {
  FILE* f;
  int i;

  if (!b->is_bound_to_file())
    return;

  if (File::open(&f, b->filename.chars, "wb")) {
    status_message_set("Could not open file {} for writing: %s", (Slice)b->filename.slice, cman_strerror(errno));
    return;
  }

  const int endline_len = strlen(b->endline_string);

  /* TODO: actually write to a temporary file, and when we have succeeded, rename it over the old file */
  for (i = 0; i < b->num_lines(); ++i) {
    unsigned int num_to_write = b->lines[i].length;

    if (num_to_write && File::write(f, b->lines[i].chars, num_to_write)) {
      status_message_set("Failed to write to {}: %s", (Slice)b->filename.slice, cman_strerror(errno));
      goto err;
    }

    // endline
    if (i < b->num_lines()-1) {
      if (File::write(f, b->endline_string, endline_len)) {
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

void editor_exit(int exitcode) {
  SDL_Quit();
  exit(exitcode);
}

static bool dropdown_autocomplete_or_insert_tab(Pane &p) {
  BufferView &b = p.buffer;
  if (G.dropdown_buffer.isempty()) {
    b.insert_tab();
    return false;
  }

  b.action_begin();
  Slice selection = G.dropdown_buffer[G.dropdown_pane.buffer.cursors[0].y].slice;
  for (int i = 0; i < b.cursors.size; ++i) {
    TokenInfo *t = b.data->find_start_of_identifier(b.cursors[i].pos);
    if (!t) {
      b.insert_tab(i);
      continue;
    }
    b.remove_range(t->r, i);
    b.insert(selection, i);
  }
  b.action_end();
  return true;
}

#define CONTROL(c) ((c)|KEY_CONTROL)

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

static bool lines_from_file(Slice filename, Array<StringBuffer> *result, const char **endline_string_result);

static void filetree_init() {
  util_free(G.files);
  _filetree_fill(G.current_working_directory);

  // parse tree
  util_free(G.project_definitions);
  int num_indexed = 0;
  for (Path p : G.files) {
    Language l = language_from_filename(p.string.slice);
    if (l == LANGUAGE_NULL)
      continue;

    Array<StringBuffer> lines;
    if (!lines_from_file(p.string.slice, &lines, 0))
      continue;
    ParseResult pr = parse(lines, l);
    for (Range r : pr.definitions)
      G.project_definitions += lines[r.a.y](r.a.x, r.b.x).copy();
    pr.definitions = {};
    util_free(pr);
    util_free(lines);
    ++num_indexed;
  }
  log_info("Indexed %i files, with %i definitions\n", num_indexed, G.project_definitions.size);
}

static void read_colorscheme_file(const char *path, bool quiet = true) {
  String file;
  if (!File::get_contents(path, &file)) {
    log_warn("Failed to open colorscheme file %s\n", path);
    return;
  }
  int y = 0;
  for (Slice row; row = file.token(&y, '\n'), row.length;) {
    int x = 0;
    Slice key = row.token(&x, ' ');
    if (!key.length)
      continue;
    int ri,gi,bi,ai;

    // hex
    const int color_start = x;
    Slice hex = row.token(&x, " ");
    if (hex.length >= 7 && hex[0] == '#') {
      if (!hex(1,3).toint_from_hex(&ri) || !hex(3,5).toint_from_hex(&gi) || !hex(5,7).toint_from_hex(&bi)) {
        if (!quiet)
          status_message_set("Incorrect syntax in colorscheme file %s", path);
        break;
      }
      ai = 255;
    }
    else {
      x = color_start;
      Slice r = row.token(&x, ' ');
      Slice g = row.token(&x, ' ');
      Slice b = row.token(&x, ' ');
      Slice a = row.token(&x, ' '); // optional
      // rgb
      if (!r.toint(&ri) || !g.toint(&gi) || !b.toint(&bi)) {
        if (!quiet)
          status_message_set("Incorrect syntax in colorscheme file %s", path);
        break;
      }
      if (!a.toint(&ai))
        ai = 255;
    }

    Color c = rgba8_to_linear_color(ri, gi, bi, ai);

    if      (key == "syntax_control")
      G.color_scheme.syntax_control = c;
    else if (key == "syntax_type")
      G.color_scheme.syntax_type = c;
    else if (key == "syntax_specifier")
      G.color_scheme.syntax_specifier = c;
    else if (key == "syntax_definition_keyword")
      G.color_scheme.syntax_definition_keyword = c;
    else if (key == "syntax_definition")
      G.color_scheme.syntax_definition = c;
    else if (key == "syntax_function")
      G.color_scheme.syntax_function = c;
    else if (key == "syntax_macro")
      G.color_scheme.syntax_macro = c;
    else if (key == "syntax_constant")
      G.color_scheme.syntax_constant = c;
    else if (key == "syntax_number")
      G.color_scheme.syntax_number = c;
    else if (key == "syntax_string")
      G.color_scheme.syntax_string = c;
    else if (key == "syntax_comment")
      G.color_scheme.syntax_comment = c;
    else if (key == "syntax_operator")
      G.color_scheme.syntax_operator = c;
    else if (key == "syntax_text")
      G.color_scheme.syntax_text = c;
    else if (key == "gutter_text")
      G.color_scheme.gutter_text = c;
    else if (key == "gutter_background")
      G.color_scheme.gutter_background = c;
    else if (key == "line_highlight")
      G.color_scheme.line_highlight = c;
    else if (key == "line_highlight_inactive")
      G.color_scheme.line_highlight_inactive = c;
    else if (key == "line_highlight_pop")
      G.color_scheme.line_highlight_pop = c;
    else if (key == "marker_inactive")
      G.color_scheme.marker_inactive = c;
    else if (key == "search_term_text")
      G.color_scheme.search_term_text = c;
    else if (key == "search_term_background")
      G.color_scheme.search_term_background = c;
    else if (key == "autocomplete_background")
      G.color_scheme.autocomplete_background = c;
    else if (key == "autocomplete_highlight")
      G.color_scheme.autocomplete_highlight = c;
    else if (key == "menu_background")
      G.color_scheme.menu_background = c;
    else if (key == "menu_highlight")
      G.color_scheme.menu_highlight = c;
    else if (key == "background")
      G.color_scheme.background = c;
    else if (key == "git_blame")
      G.color_scheme.git_blame = c;
    // some special colors
    else if (key == "shadow_color") {
      shadow_color = shadow_color2 = c;
      shadow_color2.a = 0;
    }
    else {
      if (!quiet)
        status_message_set("Unknown color %s in colorscheme file %s\n", key, path);
      break;
    }
  }
  util_free(file);
  if (!quiet)
    status_message_set("Updated color scheme");
}

static Path get_colorscheme_path() {
  Path p = G.install_dir.copy();
  p.push("assets/default.cmantic-colorscheme");
  return p;
}

static void move_to_left_brace(const BufferView &buffer, char leftbrace, char rightbrace, Pos *pos) {
  Pos p = *pos;
  int depth = 0;
  TokenInfo *t = buffer.data->gettoken(p);
  if (!t || t->token == TOKEN_EOF)
    return;
  
  bool allow_inner_blocks = t->token != leftbrace;
  if (t->token == leftbrace && t->a < p) {
    *pos = t->a;
    return;
  }
  if (t->token == leftbrace || t->token == rightbrace)
    --t;
  for (; t >= buffer.data->parser.tokens.begin(); --t) {
    if (t->token == rightbrace)
      ++depth;
    if (t->token == leftbrace) {
      --depth;
      if (depth < 0 || (allow_inner_blocks && depth == 0)) {
        *pos = t->a;
        return;
      }
    }
  }
}

static void move_to_right_brace(const BufferView &buffer, char leftbrace, char rightbrace, Pos *pos) {
  Pos p = *pos;
  int depth = 0;
  TokenInfo *t = buffer.data->gettoken(p);
  if (!t || t->token == TOKEN_EOF)
    return;
  
  bool allow_inner_blocks = t->token != rightbrace;
  if (t && t->token == rightbrace && p < t->a) {
    *pos = t->a;
    return;
  }
  if (t && (t->token == leftbrace || t->token == rightbrace))
    ++t;
  for (; t < buffer.data->parser.tokens.end(); ++t) {
    if (t->token == leftbrace) 
      ++depth;
    if (t->token == rightbrace) {
      --depth;
      if (depth < 0 || (allow_inner_blocks && depth == 0)) {
        *pos = t->a;
        return;
      }
    }
  }
}

static void move_to_left_brace(BufferView &buffer, char leftbrace, char rightbrace) {
  for (int i = 0; i < buffer.cursors.size; ++i) {
    Pos p = buffer.cursors[i].pos;
    move_to_left_brace(buffer, leftbrace, rightbrace, &p);
    buffer.move_to(i, p);
  }
}

static void move_to_right_brace(BufferView &buffer, char leftbrace, char rightbrace) {
  for (int i = 0; i < buffer.cursors.size; ++i) {
    Pos p = buffer.cursors[i].pos;
    move_to_right_brace(buffer, leftbrace, rightbrace, &p);
    buffer.move_to(i, p);
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
      if (!G.search_term.size)
        break;
      G.search_term_background_color.reset();
      buffer.jumplist_push();
      if (!buffer.find_and_move(G.search_term, false)) {
        if (!buffer.find_and_move(G.search_buffer.lines[0].slice, false)) {
          status_message_set("not found");
          break;
        }
      }
      buffer.jumplist_push();
      break;

    case 'N':
      if (!G.search_term.size)
        break;
      G.search_term_background_color.reset();
      buffer.jumplist_push();
      if (!buffer.find_and_move_r(G.search_term, false)) {
        if (!buffer.find_and_move_r(G.search_buffer.lines[0].slice, false)) {
          status_message_set("not found");
          break;
        }
      }
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
      G.search_pane.buffer.empty();
      G.search_pane.buffer.insert(t->str);
      buffer.find_and_move(G.search_term, false);
      break;}

    case '#': {
      TokenInfo *t = buffer.data->gettoken(buffer.cursors[0].pos);
      if (!t)
        break;
      if (t->token != TOKEN_IDENTIFIER)
        break;
      G.search_term_background_color.reset();
      G.search_pane.buffer.empty();
      G.search_pane.buffer.insert(t->str);
      buffer.find_and_move_r(G.search_term, false);
      break;}

    default:
      return false;
  }
  return true;
}

/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                           MODES                            **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/

static void mode_cleanup() {
  G.flags.cursor_dirty = true;

  if (G.mode == MODE_FILESEARCH || G.mode == MODE_CWD)
    G.menu_pane.buffer.empty();

  if (G.mode == MODE_GOTO_DEFINITION)
    util_free(G.definition_positions);

  if (G.mode == MODE_INSERT)
    G.editing_pane->buffer.action_end();
}

static Array<String> get_search_suggestions() {
  Array<String> result;
  TokenInfo *t = G.search_buffer.find_start_of_identifier(G.search_pane.buffer.cursors[0].pos);
  if (!t)
    return {};

  easy_fuzzy_match(t->str, VIEW(G.editing_pane->buffer.data->parser.identifiers, slice), false, &result);
  return result;
}

static void mode_search() {
  mode_cleanup();

  G.mode = MODE_SEARCH;
  G.selected_pane = &G.search_pane;
  G.search_begin_pos = G.editing_pane->buffer.cursors[0].pos;
  G.search_failed = false;

  G.search_pane.menu_init(Slice::create("search"), get_search_suggestions);
  G.search_buffer.language = G.editing_pane->buffer.data->language;
  util_free(G.search_term);

  G.search_pane.buffer.empty();
}

static Array<String> get_cwd_suggestions() {
  // get folder

  // first try relative path, and then try absolute
  Path rel = G.current_working_directory.copy();
  rel.push(G.menu_buffer.lines[0].slice);
  Path rel_dir = rel.copy();
  Path abs = Path::create(G.menu_buffer.lines[0].slice);
  Path abs_dir = abs.copy();

  Path *p = &rel;
  Path *dir = &rel_dir;

  if (rel_dir.string[rel_dir.string.length-1] != Path::separator || !File::isdir(rel_dir)) {
    rel_dir.pop();
    if (rel_dir.string[rel_dir.string.length-1] != Path::separator  || !File::isdir(rel_dir)) {
      p = &abs;
      dir = &abs_dir;
      if (dir->string.length && (dir->string[dir->string.length-1] != Path::separator || !File::isdir(*dir)))
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
  G.menu_pane.menu_init(Slice::create("chdir"), get_cwd_suggestions);
  G.menu_pane.buffer.insert(G.current_working_directory.string.slice);
  G.menu_pane.buffer.insert(Utf8char::create(Path::separator));
  G.menu_pane.update_suggestions();
}

static void mode_prompt(Slice msg, void (*callback)(void), PromptType type) {
  mode_cleanup();

  if (G.prompt_type == PROMPT_STRING)
    util_free(G.prompt_result.string);

  G.mode = MODE_PROMPT;
  G.prompt_type = type;
  G.prompt_callback = callback;
  G.prompt_result = {};

  G.selected_pane = &G.menu_pane;
  G.menu_pane.menu_init(msg);
}

static Array<String> get_goto_definition_suggestions() {
  BufferData &b = *G.editing_pane->buffer.data;
  Array<Slice> defs = {};
  defs.reserve(b.parser.definitions.size);
  for (Range r : b.parser.definitions)
    defs += b.getslice(r);

  Array<int> matches;
  easy_fuzzy_match(G.menu_buffer.lines[0].slice, view(defs), false, &matches);
  util_free(defs);

  Array<String> result = {};
  for (int i : matches) {
    // find entire function definition
    Range start = b.parser.definitions[i];
    TokenInfo *t = b.gettoken(start.b);
    if (t->token != '(') {
      result += b.get_merged_range(start);
      continue;
    }

    int depth = 0;
    for (; t != b.parser.tokens.end(); ++t) {
      if (t->token == '(') ++depth;
      if (t->token == ')') --depth;
      if (depth == 0)
        break;
    }
    if (t == b.parser.tokens.end()) {
      result += b.get_merged_range(start);
      continue;
    }
    result += b.get_merged_range({start.a, t->b});
  }

  G.definition_positions.size = 0;
  for (int i : matches)
    G.definition_positions += b.parser.definitions[i].a;

  util_free(matches);
  return result;
}

static Array<String> get_goto_all_definitions_suggestions() {
  Array<String> result;
  easy_fuzzy_match(G.menu_buffer.lines[0].slice, VIEW(G.project_definitions, slice), false, &result);
  return result;
}

static void mode_goto_all_definitions() {
  mode_cleanup();
  G.mode = MODE_GOTO_ALL_DEFINITIONS;
  G.selected_pane = &G.menu_pane;

  G.editing_pane->buffer.collapse_cursors();
  G.goto_definition_begin_pos = G.editing_pane->buffer.cursors[0].pos;

  G.menu_pane.menu_init(Slice::create("goto def"), get_goto_all_definitions_suggestions);
  G.menu_pane.update_suggestions();
  if (G.definition_positions.size)
    G.editing_pane->buffer.move_to(G.definition_positions[0]);
}

static void mode_goto_definition() {
  mode_cleanup();
  G.mode = MODE_GOTO_DEFINITION;
  G.selected_pane = &G.menu_pane;

  G.editing_pane->buffer.collapse_cursors();
  G.goto_definition_begin_pos = G.editing_pane->buffer.cursors[0].pos;

  G.menu_pane.menu_init(Slice::create("goto def"), get_goto_definition_suggestions);
  G.menu_pane.update_suggestions();
  if (G.definition_positions.size)
    G.editing_pane->buffer.move_to(G.definition_positions[0]);
}

static Array<String> get_filesearch_suggestions() {
  Array<Slice> filenames = {};
  filenames.reserve(G.files.size);
  for (Path p : G.files)
    filenames += p.name();
  Array<int> match;
  easy_fuzzy_match(G.menu_buffer.lines[0].slice, view(filenames), false, &match);
  util_free(filenames);

  Array<String> result = {};
  result.reserve(match.size);
  for (int i : match)
    result += String::create(G.files[i].string.slice(G.current_working_directory.string.length+1, -1));

  util_free(match);
  return result;
}

static void mode_filesearch() {
  mode_cleanup();
  G.mode = MODE_FILESEARCH;
  G.selected_pane = &G.menu_pane;
  G.menu_pane.menu_init(Slice::create("open"), get_filesearch_suggestions);
  G.menu_pane.update_suggestions();
}

static void mode_goto() {
  mode_cleanup();
  G.mode = MODE_GOTO;
  G.goto_line_number = 0;
  status_message_set("goto");
}

static void mode_replace() {
  mode_cleanup();
  G.mode = MODE_REPLACE;
  status_message_set("replace");
}

static void mode_yank() {
  mode_cleanup();
  G.mode = MODE_YANK;
  status_message_set("yank");
}

static void mode_delete() {
  mode_cleanup();
  G.mode = MODE_DELETE;
  status_message_set("delete");
}

static void mode_normal(bool force_set_message = false) {
  mode_cleanup();
  if (G.mode == MODE_INSERT)
    G.default_marker_background_color.jump();

  G.mode = MODE_NORMAL;
  G.bottom_pane = &G.status_message_pane;
  G.selected_pane = G.editing_pane;

  if (!G.status_message_was_set || force_set_message)
    status_message_set("normal");
  G.status_message_was_set = false;
}

static void mode_insert() {
  mode_cleanup();
  G.default_marker_background_color.jump();

  util_free(G.visual_start);

  G.mode = MODE_INSERT;
  G.bottom_pane = &G.status_message_pane;

  G.editing_pane->buffer.action_begin();

  status_message_set("insert");
}

static Array<String> get_menu_suggestions();
static void mode_menu() {
  mode_cleanup();
  G.mode = MODE_MENU;
  G.selected_pane = &G.menu_pane;
  G.menu_pane.menu_init(Slice::create("menu"), get_menu_suggestions);
  G.menu_pane.update_suggestions();
}

static void state_init() {
  srand((uint)time(NULL));
  rand(); rand(); rand();

  #ifdef DEBUG
    perfcheck_data = static_array(my_perfcheck_data, ARRAY_LEN(my_perfcheck_data));
  #endif

  if (!File::cwd(&G.current_working_directory))
    log_err("Failed to find current working directory, something is very wrong\n"), exit(1);
  G.install_dir = G.current_working_directory.copy();
  G.ttf_file = G.current_working_directory.copy();
  G.ttf_file.push("assets/font.ttf");

  // initialize graphics library
  if (graphics_init(&G.window))
    exit(1);
  G.font_height = 14;
  if (graphics_text_init(G.ttf_file.string.chars))
    exit(1);
  if (graphics_quad_init())
    exit(1);
  if (graphics_textured_quad_init())
    exit(1);

  if (!load_texture_from_file("assets/sprites.bmp", &G.pet_texture))
    exit(1);
  G.pet_index = rand() % 3;

  // parse sprite meta data
  {
    String file;
    if (!File::get_contents("assets/sprite_positions", &file)) {
      log_err("Failed to read assets/sprite_positions\n");
      exit(1);
    }
    int i = 0;
    for (Slice row; row = file.token(&i, '\n'), row.length;) {
      Rect r;
      sscanf(row.chars, "%i %i %i %i", &r.x, &r.y, &r.w, &r.h);
      G.pet_sprites += r;
    }
    util_free(file);
  }

  SDL_GetWindowSize(G.window, &G.win_width, &G.win_height);

  // font stuff
  G.font_width = graphics_get_font_advance(G.font_height);
  G.tab_width = 4;
  G.default_tab_type = 4;
  G.line_margin = 0;
  G.line_height = G.font_height + G.line_margin;

  Path colorscheme_path = get_colorscheme_path();
  read_colorscheme_file(colorscheme_path.string.chars, true);
  util_free(colorscheme_path);

  G.keyword_colors[KEYWORD_NONE]        = 0;
  G.keyword_colors[KEYWORD_CONTROL]     = &G.color_scheme.syntax_control;
  G.keyword_colors[KEYWORD_TYPE]        = &G.color_scheme.syntax_type;
  G.keyword_colors[KEYWORD_SPECIFIER]   = &G.color_scheme.syntax_specifier;
  G.keyword_colors[KEYWORD_DEFINITION]  = &G.color_scheme.syntax_definition_keyword;
  G.keyword_colors[KEYWORD_FUNCTION]    = &G.color_scheme.syntax_function;
  G.keyword_colors[KEYWORD_MACRO]       = &G.color_scheme.syntax_macro;
  G.keyword_colors[KEYWORD_CONSTANT]    = &G.color_scheme.syntax_constant;

  G.active_highlight_background_color.base_color = &G.color_scheme.line_highlight;
  G.active_highlight_background_color.popped_color = &G.color_scheme.line_highlight_pop;
  G.active_highlight_background_color.speed = 1.0f;
  G.active_highlight_background_color.cooldown = 1.0f;
  G.active_highlight_background_color.min = 0.0f;
  G.active_highlight_background_color.max = 0.18f;

  G.bottom_pane_color.base_color = &G.color_scheme.menu_background;
  G.bottom_pane_color.popped_color = &G.color_scheme.menu_highlight;
  G.bottom_pane_color.speed = 1.3f;
  G.bottom_pane_color.cooldown = 0.4f;
  G.bottom_pane_color.min = 0.0f;
  G.bottom_pane_color.max = 1.0f;

  G.visual_jump_color.base_color = &COLOR_BLACK;
  G.visual_jump_color.popped_color = &COLOR_BLACK;
  G.visual_jump_color.speed = 1.3f;
  G.visual_jump_color.cooldown = 0.4f;
  G.visual_jump_color.min = 0.0f;
  G.visual_jump_color.max = 0.0f;

  G.visual_jump_background_color.base_color = &G.color_scheme.background;
  G.visual_jump_background_color.popped_color = &COLOR_PINK;
  G.visual_jump_background_color.speed = 1.3f;
  G.visual_jump_background_color.cooldown = 0.4f;
  G.visual_jump_background_color.min = 1.0f;
  G.visual_jump_background_color.max = 1.0f;

  G.default_marker_background_color.speed = 0.2f;
  G.default_marker_background_color.saturation = 0.8f;
  G.default_marker_background_color.light = 0.7f;
  G.default_marker_background_color.hue = 340.0f;

  G.marker_background_color.base_color = &G.color_scheme.line_highlight;
  G.marker_background_color.popped_color = &G.default_marker_background_color.color;
  G.marker_background_color.speed = 1.0f;
  G.marker_background_color.cooldown = 1.0f;
  G.marker_background_color.min = 0.5f;
  G.marker_background_color.max = 1.0f;

  G.search_term_background_color.base_color = &G.color_scheme.background;
  G.search_term_background_color.popped_color = &G.color_scheme.search_term_background;
  G.search_term_background_color.speed = 1.0f;
  G.search_term_background_color.cooldown = 4.0f;
  G.search_term_background_color.min = 0.4f;
  G.search_term_background_color.max = 1.0f;

  // @panes

  // default buffer, and initial pane
  G.null_buffer.init(false, Slice::create("[Scratch]"));
  G.buffers += &G.null_buffer;
  Pane *main_pane = new Pane{};
  Pane::init_edit(*main_pane, &G.null_buffer, &G.color_scheme.background, &G.color_scheme.syntax_text, &G.active_highlight_background_color.color, &G.color_scheme.line_highlight_inactive, true);

  G.build_result_buffer.init(false, Slice::create("[Build Result]"));
  G.build_result_buffer.disable_undo();
  G.buffers += &G.build_result_buffer;

  // menu pane
  G.menu_pane.type = PANETYPE_MENU;
  G.menu_pane.buffer = BufferView::create(&G.menu_buffer);
  G.menu_pane.buffer.empty();
  G.menu_pane.background_color = &G.bottom_pane_color.color;
  G.menu_pane.text_color = &G.color_scheme.syntax_text;
  G.menu_pane.active_highlight_background_color = &G.color_scheme.autocomplete_highlight;
  G.menu_pane.margin = 5;
  G.menu_buffer.disable_undo();

  // search pane
  G.search_pane.type = PANETYPE_MENU;
  G.search_pane.buffer = BufferView::create(&G.search_buffer);
  G.search_pane.buffer.empty();
  G.search_pane.background_color = &G.bottom_pane_color.color;
  G.search_pane.text_color = &G.color_scheme.syntax_text;
  G.search_pane.active_highlight_background_color = &G.color_scheme.autocomplete_highlight;
  G.search_pane.margin = 5;
  G.search_buffer.disable_undo();

  // dropdown pane
  G.dropdown_pane.type = PANETYPE_DROPDOWN;
  G.dropdown_pane.buffer = BufferView::create(&G.dropdown_buffer);
  G.dropdown_pane.buffer.empty();
  G.dropdown_pane.background_color = &G.color_scheme.autocomplete_background;
  G.dropdown_pane.text_color = &G.color_scheme.syntax_text;
  G.dropdown_pane.margin = 5;
  G.dropdown_pane.active_highlight_background_color = &G.color_scheme.autocomplete_highlight;

  // status message pane
  G.status_message_pane.type = PANETYPE_STATUSMESSAGE;
  G.status_message_pane.buffer = BufferView::create(&G.status_message_buffer);
  G.status_message_pane.buffer.empty();
  G.status_message_pane.background_color = &G.bottom_pane_color.color;
  G.status_message_pane.text_color = &G.color_scheme.syntax_text;
  G.status_message_pane.active_highlight_background_color = &G.color_scheme.autocomplete_highlight;
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

static Stream test_async_command_output;
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
  assert(a.begins_with(0, b));
  assert(a.begins_with(2, b(2, -1)));
  int x;
  b = Slice::create("llo");
  assert(a.find(2, b, &x));
  assert(x == 2);
  assert(a.find(1, b, &x));
  assert(x == 2);
  assert(a.find(0, b, &x));
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

  // test async reading
  #ifdef OS_LINUX
  Array<String> command = {};
  command += String::create("sleep");
  command += String::create("3");
  if (!call_async(VIEW(command, slice), &test_async_command_output)) {
    log_err("Call to %s failed\n", command[0]);
    editor_exit(1);
  }
  util_free(command);
  #endif
}

static void test_update() {
  #ifdef OS_LINUX
  while (test_async_command_output) {
    char buf[256];
    int n = test_async_command_output.read(buf, 255);
    if (n <= 0) break;
    log_info(Slice::create(buf, n));
  }
  #endif
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

Pos _pos_to_distance_compare;
static int pos_distance_compare(const void *a, const void *b) {
  return abs(((Pos*)a)->y - _pos_to_distance_compare.y) - abs(((Pos*)b)->y - _pos_to_distance_compare.y);
}
static void do_visual_jump() {
  COROUTINE_BEGIN;

  mode_prompt(Slice::create("jump"), do_visual_jump, PROMPT_KEY);
  yield(wait_for_initial_key);
  if (!G.prompt_success) {
    mode_normal(true);
    yield_break;
  }

  // find the chars to highlight
  {
    char target = (char)G.prompt_result.key;
    Array<Pos> matches = {};
    Rect r = G.editing_pane->buffer_viewport;
    for (BufferRectIter it = G.editing_pane->buffer.data->getrect(r); it.next();)
      if (*it == target)
        matches += it.p;

    // set character as search term, so we can jump to the next one with 'n'
    G.search_pane.buffer.empty();
    G.search_pane.buffer.insert(Utf8char::create(target));

    // now go inside-out
    _pos_to_distance_compare = G.editing_pane->buffer.cursors[0].pos;
    qsort(matches.items, matches.size, sizeof(matches[0]), pos_distance_compare);

    util_free(G.visual_jump_positions);
    for (int i = 0; i < at_most(matches.size, (int)ARRAY_LEN(visual_jump_highlight_keys)); ++i)
      G.visual_jump_positions += matches[i];
    util_free(matches);

    G.current_visual_jump_pane = G.editing_pane;
    G.visual_jump_color.reset();
    G.visual_jump_background_color.reset();
  }

  mode_prompt(Slice::create("jump"), do_visual_jump, PROMPT_KEY);
  yield(wait_for_second_key);
  if (!G.prompt_success) {
    G.current_visual_jump_pane = 0;
    goto done;
  }

  // go to char
  for (int i = 0; i < at_most((int)ARRAY_LEN(visual_jump_highlight_keys), (int)G.visual_jump_positions.size); ++i) {
    if (visual_jump_highlight_keys[i] == G.prompt_result.key) {
      G.editing_pane->buffer.move_to(G.visual_jump_positions[i]);
      goto done;
    }
  }

  done:
  util_free(G.visual_jump_positions);
  G.current_visual_jump_pane = 0;

  mode_normal(true);
  COROUTINE_END;
}

static bool do_paste() {
  BufferView &buffer = G.editing_pane->buffer;
  if (G.editing_pane != G.selected_pane || !SDL_HasClipboardText())
    return false;
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
  return true;
}

static bool get_action_selection(BufferView &buffer, Key key, Array<Range> *result) {
  Array<Range> selections = {};

  switch (key) {
    case ' ':
      for (Cursor c : buffer.cursors)
        selections += Range{{0, c.y}, {0, c.y+1}};
      break;

    case 'p': {
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

        selections += {a, b};
      }
      break;}

    case '}': {
      for (Cursor c : buffer.cursors) {
        Range r = {c.pos, c.pos};
        move_to_right_brace(buffer, '{', '}', &r.b);
        buffer.advance(r.b);
        selections += r;
      }
      break;}

    case ')': {
      for (Cursor c : buffer.cursors) {
        Range r = {c.pos, c.pos};
        move_to_right_brace(buffer, '(', ')', &r.b);
        buffer.advance(r.b);
        selections += r;
      }
      break;}

    case ']': {
      for (Cursor c : buffer.cursors) {
        Range r = {c.pos, c.pos};
        move_to_right_brace(buffer, '[', ']', &r.b);
        buffer.advance(r.b);
        selections += r;
      }
      break;}

    case '"':
      for (Cursor c : buffer.cursors) {
        TokenInfo *t = buffer.data->gettoken(c.pos);
        if (t != buffer.data->parser.tokens.end() && t->token == TOKEN_STRING && t->r.contains(c.pos))
          selections += t->r;
      }
      break;

    default:
      goto fail;
  }
  *result = selections;
  return true;

  fail:
  util_free(selections);
  return false;
}

static bool do_delete_movement(Key key) {
  BufferView &buffer = G.editing_pane->buffer;

  // TODO: is it more performant to check if it is a movement command first?
  buffer.action_begin();
  Array<Cursor> cursors = buffer.cursors.copy_shallow();

  // first try selection (p for parameter, d for line etc)
  Array<Range> selections = {};
  if (get_action_selection(buffer, key, &selections)) {
    for (int i = 0; i < selections.size; ++i)
      buffer.remove_range(selections[i].a, selections[i].b, i);
    util_free(selections);
  }
  else if (movement_default(buffer, key)) {
    if (cursors.size != buffer.cursors.size)
      goto fail;
    // delete movement range
    for (int i = 0; i < cursors.size; ++i) {
      Pos a = cursors[i].pos;
      Pos b = buffer.cursors[i].pos;
      if (b < a)
        swap_range(*buffer.data,a,b);
      buffer.remove_range(a, b, i);
    }
  }
  else {
    goto fail;
  }

  buffer.action_end();
  util_free(cursors);
  return true;

  fail:
  buffer.action_end();
  util_free(cursors);
  return false;
}

static void do_delete_visual() {
  BufferView &buffer = G.editing_pane->buffer;
  buffer.action_begin();

  for (int i = 0; i < G.visual_start.cursors.size; ++i) {
    Pos pa = min(G.visual_start.cursors[i], buffer.cursors[i].pos);
    Pos pb = max(G.visual_start.cursors[i], buffer.cursors[i].pos);

    if (G.visual_entire_line)
      buffer.remove_range(pa.y, pb.y, i);
    else {
      buffer.advance(pb);
      buffer.remove_range(pa, pb, i);
    }
  }

  util_free(G.visual_start);

  buffer.action_end();
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

static void do_build() {
  COROUTINE_BEGIN;

  // if build already running, prompt user
  if (G.build_result_output) {
    mode_prompt(Slice::create("Build is already running, are you sure? [y/n]"), do_build, PROMPT_BOOLEAN);
    yield(wait_for_prompt);
    if (!G.prompt_success || !G.prompt_result.boolean) {
      mode_normal();
      yield_break;
    }
  }

  // build arg list
  if (!G.build_command.size) {
    status_message_set("No build command set. Please set with :set build command");
    mode_normal();
    yield_break;
  }

  // call subprocess
  util_free(G.build_result_output);
  bool success = call_async(VIEW(G.build_command, slice), &G.build_result_output);

  if (!success) {
    status_message_set("Failed to call {}", (Slice)G.build_command[0].slice);
    mode_normal();
    yield_break;
  }

  // reset buffer and pane
  G.build_result_buffer.init(false, Slice::create("[Build Result]"));
  G.build_result_buffer.disable_undo();
  bool exists;
  ARRAY_EXISTS(G.editing_panes, &exists, it->buffer.data == &G.build_result_buffer);
  if (!exists)
    G.editing_pane->add_subpane(&G.build_result_buffer, {});
  else {
    // update all panes showing build result
    for (Pane *p : G.editing_panes) {
      if (p->buffer.data != &G.build_result_buffer)
        continue;
      p->buffer.collapse_cursors();
      p->buffer.cursors[0] = Cursor::create(Pos{});
    }
  }

  mode_normal(true);
  COROUTINE_END;
}

static void toggle_comment(BufferView &buffer, int y0, int y1, int cursor_idx) {
  if (!buffer.data->language)
    return;

  const Slice comment = language_settings[buffer.data->language].line_comment;
  if (!comment.length)
    return;

  for (int y = y0; y <= y1; ++y) {
    int x  = buffer.data->begin_of_line(y);
    Pos a = {x, y};
    if (buffer.data->lines[y].begins_with(x, comment)) {
      Pos b = a;
      b.x += 2;
      while (b.x < buffer.data->lines[y].length && buffer.data->getchar(b).isspace())
        ++b.x;
      buffer.remove_range(a, b, cursor_idx);
    }
    else {
      buffer.insert(a, Slice::create(" "), cursor_idx);
      buffer.insert(a, comment, cursor_idx);
    }
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


/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                           MENU                             **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/

static void menu_option_line_margin() {
  COROUTINE_BEGIN;
  mode_prompt(Slice::create("New line margin"), menu_option_line_margin, PROMPT_INT);
  yield(check_prompt_result);
  if (!G.prompt_success) {
    mode_normal(true);
    yield_break;
  }

  if (G.prompt_result.integer <= 0) {
    status_message_set("line margin must be > 0");
    mode_normal();
    yield_break;
  }

  G.line_margin = G.prompt_result.integer;
  status_message_set("line margin set to %i", G.line_margin);
  mode_normal();

  COROUTINE_END;
}

static void menu_option_save() {
  save_buffer(G.editing_pane->buffer.data);
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

static void menu_option_set_build_command() {
  COROUTINE_BEGIN;

  mode_prompt(Slice::create("Build command"), menu_option_set_build_command, PROMPT_STRING);
  yield(check_prompt_result);
  if (!G.prompt_success) {
    mode_normal(true);
    yield_break;
  }
  
  if (!G.prompt_result.string.length) {
    status_message_set("Command must be non-empty");
    mode_normal();
    yield_break;
  }

  util_free(G.build_command);
  int offset = 0;
  for (Slice arg; arg = G.prompt_result.string.token(&offset, ' '), arg.length;)
    G.build_command += String::create(arg);
  status_message_set("Build command set");

  mode_normal();
  COROUTINE_END;
}

static void menu_option_set_indent() {
  COROUTINE_BEGIN;

  mode_prompt(Slice::create("Set indent"), menu_option_set_indent, PROMPT_INT);
  yield(check_prompt_result);
  if (!G.prompt_success) {
    mode_normal(true);
    yield_break;
  }
  
  if (G.prompt_result.integer < 0) {
    status_message_set("Tab size must be >= 0");
    mode_normal();
    yield_break;
  }
  
  G.default_tab_type = G.prompt_result.integer;
  G.editing_pane->buffer.data->tab_type = G.prompt_result.integer;
  mode_normal();
  COROUTINE_END;
}

static void menu_option_blame() {
  COROUTINE_BEGIN;

  if (!G.editing_pane->buffer.data->is_bound_to_file()) {
    status_message_set("This is not a file");
    mode_normal();
    yield_break;
  }

  // ask the user if it's okay we save before blaming
  if (G.editing_pane->buffer.data->modified()) {
    mode_prompt(Slice::create("Will save buffer in order to blame, ok? [y/n]"), menu_option_blame, PROMPT_BOOLEAN);
    yield(check_prompt_result);
    if (!G.prompt_success || !G.prompt_result.boolean) {
      status_message_set("Cancelling blame");
      mode_normal();
      yield_break;
    }
  }

  BufferData &b = *G.editing_pane->buffer.data;
  if (b.blame.data.size) {
    util_free(b.blame);
    mode_normal();
    yield_break;
  }
  util_free(b.blame);

  if (b.modified())
    save_buffer(&b);

  // call git
  const char* cmd[] = {"git", "blame", b.filename.chars, "--porcelain", NULL};
  String out = {};
  int errcode = 0;
  if (!call(cmd, &errcode, &out)) {
    status_message_set("System call \"{}\" failed", cmd);
    mode_normal();
    yield_break;
  }
  if (errcode || !out.length) {
    // TODO: handle newlines
    if (out.length)
      status_message_set("git blame failed: {}", out.slice(0,-2));
    else
      status_message_set("git blame failed");
    util_free(out);
    mode_normal();
    yield_break;
  }
  b.blame.storage.push();
  git_parse_blame(out, &b.blame.data);
  b.blame.storage.pop();

  if (!b.blame.data.size)
    status_message_set("git blame failed");
  else
    status_message_set("showing git blame");

  util_free(out);

  mode_normal();
  COROUTINE_END;
}

static void menu_option_next_pet() {
  ++G.pet_index;
  status_message_set("Changed to pet %i", (G.pet_index % 3) + 1);
}

static void menu_option_expand_columns() {
  BufferView &b = G.editing_pane->buffer;
  b.action_begin();
  int max_x = 0;
  for (Cursor c : b.cursors)
    max_x = max(max_x, b.data->to_visual_pos(c.pos).x);
  for (int i = 0; i < b.cursors.size; ++i) {
    int n = max_x - b.data->to_visual_pos(b.cursors[i].pos).x;
    while (n--)
      b.insert(Utf8char::create(' '), i);
  }

  status_message_set("Expanded columns");
  b.action_end();
  mode_normal();
}

struct MenuOption {
  Slice name;
  Slice description;
};

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
    Slice::create("show indentation"),
    Slice::create("Show if tab type is spaces or tab"),
    menu_option_show_tab_type
  },
  {
    Slice::create("change directory"),
    Slice::create("Change current working directory"),
    menu_option_chdir
  },
  {
    Slice::create("reload buffer"),
    Slice::create("Reload current buffer"),
    menu_option_reload
  },
  {
    Slice::create("reload all buffers"),
    Slice::create("Reload all buffers"),
    menu_option_reloadall
  },
  {
    Slice::create("close buffer"),
    Slice::create("Close current buffer"),
    menu_option_close
  },
  {
    Slice::create("close all buffers"),
    Slice::create("Close all open buffers"),
    menu_option_closeall
  },
  {
    Slice::create("git blame"),
    Slice::create("git blame on current file"),
    menu_option_blame
  },
  {
    Slice::create("set build command"),
    Slice::create("set build command"),
    menu_option_set_build_command
  },
  {
    Slice::create("set indent"),
    Slice::create("set indentation. > 0 for spaces, 0 for tabs"),
    menu_option_set_indent
  },
  {
    Slice::create("next pet"),
    Slice::create("Switch to the next pet"),
    menu_option_next_pet
  },
  {
    Slice::create("expand to column"),
    Slice::create("Add spaces on the current markers until they align up"),
    menu_option_expand_columns
  },
  {
    Slice::create("set line margin"),
    Slice::create("Set the number of pixels of empty space between lines"),
    menu_option_line_margin
  },
};

static Array<String> get_menu_suggestions() {
  Array<String> result;
  easy_fuzzy_match(G.menu_buffer.lines[0].slice, VIEW_FROM_ARRAY(menu_options, opt.name), false, &result);
  return result;
}


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

    case KEY_BACKSPACE: {
      // if there is only whitespace to the left, go backwards by tabsize
      for (int i = 0; i < b.cursors.size; ++i) {
        if (b.cursors[i].x > 0) {
          const StringBuffer line = b.data->lines[b.cursors[i].y];

          for (int x = b.cursors[i].x-1; x >= 0; --x)
            if (!isspace(line[x]))
              goto default_backspace;

          int indent = b.getindent(b.cursors[i].y); 
          if (indent == 0)
            goto default_backspace;

          b.add_indent(b.cursors[i].y, -1);
          continue;
        }

        default_backspace:
        b.data->delete_char(b.cursors, i);
      }
      return true;
    }
  }

  return false;
}

static void handle_menu_insert(Pane *p, int key) {
  switch (key) {

    case KEY_ARROW_DOWN:
    case CONTROL('j'):
      if (p->menu.is_verbose)
        p->menu.current_suggestion = clamp(p->menu.current_suggestion+1, 0, p->menu.verbose_suggestions.size-1);
      else
        p->menu.current_suggestion = clamp(p->menu.current_suggestion+1, 0, p->menu.suggestions.size-1);
      break;

    case KEY_ARROW_UP:
    case CONTROL('k'):
      if (p->menu.is_verbose)
        p->menu.current_suggestion = clamp(p->menu.current_suggestion-1, 0, p->menu.verbose_suggestions.size-1);
      else
        p->menu.current_suggestion = clamp(p->menu.current_suggestion-1, 0, p->menu.suggestions.size-1);
      break;

    case KEY_TAB: {
      Slice *s = p->menu_get_selection();
      if (s) {
        p->buffer.empty();
        p->buffer.insert(*s);
      } else {
        p->buffer.insert_tab();
      }
      break;}

    default:
      insert_default(*p, key);
      break;
  }
  if (G.flags.cursor_dirty) {
    p->update_suggestions();
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
      mode_normal();
      break;
      err:
      util_free(p);
      mode_normal();
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
      handle_menu_insert(&G.menu_pane, key);
    break;

  case MODE_PROMPT:
    if (key == KEY_RETURN) {
      G.prompt_success = true;
      switch (G.prompt_type) {
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

        case PROMPT_KEY:
          G.prompt_success = false;
          goto prompt_done;
      }
    }
    else if (key == KEY_ESCAPE) {
      G.prompt_success = false;
      goto prompt_done;
    }
    else if (G.prompt_type == PROMPT_BOOLEAN && (key == 'y' || key == 'n' || key == 'Y' || key == 'N')) {
      G.prompt_success = true;
      G.prompt_result.boolean = key == 'y' || key == 'Y';
      goto prompt_done;
    }
    else if (G.prompt_type == PROMPT_KEY) {
      G.prompt_success = true;
      G.prompt_result.key = key;
      goto prompt_done;
    }
    else
      handle_menu_insert(&G.menu_pane, key);

    prompt_keepgoing:
    G.prompt_success = false;
    break;

    prompt_done:
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

        // TODO: what if we have multiple matches?
        Range *def = buffer.data->getdefinition(t->str);
        if (!def)
          break;

        // G.editing_pane->add_subpane(buffer.data, def->a);
        buffer.jumplist_push();
        buffer.move_to(def->a);
        buffer.jumplist_push();
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
        mode_normal();
    }
    else if (key == KEY_ESCAPE)
      mode_normal(true);
    else if (key == KEY_BACKSPACE && G.menu_buffer[0].length == 0)
      mode_normal(true);
    else
      handle_menu_insert(&G.menu_pane, key);
    break;

  case MODE_GOTO_DEFINITION: {
    if (key == KEY_ESCAPE) {
      G.editing_pane->buffer.move_to(G.goto_definition_begin_pos);
      mode_normal(true);
      break;
    }

    if (key == KEY_RETURN) {
      int opt = G.menu_pane.menu_get_selection_idx();
      if (opt == -1) {
        status_message_set("\"{}\": No such file", (Slice)G.menu_buffer[0].slice);
        mode_normal();
        break;
      }

      // G.editing_pane->add_subpane(buffer.data, def->a);
      buffer.jumplist_push();
      buffer.move_to(G.definition_positions[opt]);
      buffer.jumplist_push();
      mode_normal(true);
      break;
    }

    handle_menu_insert(&G.menu_pane, key);

    // update selection
    int i = G.menu_pane.menu_get_selection_idx();
    if (i != -1)
      G.editing_pane->buffer.move_to(G.definition_positions[i]);

    break;}

  case MODE_GOTO_ALL_DEFINITIONS: {
    if (key == KEY_ESCAPE) {
      // G.editing_pane->buffer.move_to(G.goto_definition_begin_pos);
      mode_normal(true);
      break;
    }

    if (key == KEY_RETURN) {
      #if 0
      int opt = G.menu_pane.menu_get_selection_idx();
      if (opt == -1) {
        status_message_set("\"{}\": No such file", (Slice)G.menu_buffer[0].slice);
        mode_normal();
        break;
      }

      // G.editing_pane->add_subpane(buffer.data, def->a);
      buffer.jumplist_push();
      buffer.move_to(G.definition_positions[opt]);
      buffer.jumplist_push();
      #endif
      mode_normal(true);
      break;
    }

    handle_menu_insert(&G.menu_pane, key);

    // update selection
    #if 0
    int i = G.menu_pane.menu_get_selection_idx();
    if (i != -1)
      G.editing_pane->buffer.move_to(G.definition_positions[i]);
    #endif

    break;}

  case MODE_FILESEARCH:
    if (key == KEY_RETURN) {
      Slice *opt = G.menu_pane.menu_get_selection();
      if (!opt) {
        status_message_set("\"{}\": No such file", (Slice)G.menu_buffer[0].slice);
        mode_normal();
        break;
      }

      Slice selection = *opt;
      Path full_path = G.current_working_directory.copy();
      full_path.push(selection);

      // check if buffer is already open
      for (BufferData *b: G.buffers) {
        if (full_path.string.slice == b->filename.slice) {
          status_message_set("Switched to {}", (Slice)b->filename.slice);
          G.editing_pane->switch_buffer(b);
          goto filesearch_done;
        }
      }

      // otherwise open new file
      {
        BufferData *b = new BufferData{};
        if (BufferData::from_file(full_path.string.slice, b)) {
          G.buffers += b;
          G.editing_pane->switch_buffer(b);
          status_message_set("Loaded file {} ({}) (%s)", (Slice)full_path.name(), language_settings[b->language].name, b->endline_string == ENDLINE_UNIX ? "Unix" : "Windows");
        }
        else {
          status_message_set("Failed to load file {}", (Slice)full_path.name());
          free(b);
        }
      }

      filesearch_done:
      util_free(full_path);
      G.menu_pane.buffer.empty();
      mode_normal();
      break;
    }
    else {
      handle_menu_insert(&G.menu_pane, key);
    }
    break;

  case MODE_SEARCH: {
    // RETURN or TAB: accept autocomplete 
    if (key == KEY_RETURN || key == KEY_TAB) {
      Slice *selection = G.search_pane.menu_get_selection();
      if (selection) {
        TokenInfo *t = G.search_buffer.find_start_of_identifier(G.search_pane.buffer.cursors[0].pos);
        if (t) {
          G.search_pane.buffer.remove_range(t->r, 0);
          G.search_pane.buffer.insert(*selection, 0);
        }
      }
    }

    // RETURN or ESCAPE: perform search
    if (key == KEY_RETURN || key == KEY_ESCAPE) {
      if (!G.search_buffer.lines[0].length) {
        util_free(G.search_term);
        mode_normal(true);
      }
      else {
        buffer.jumplist_push();

        // Move out the search token list
        util_free(G.search_term);
        G.search_term = G.search_buffer.parser.tokens;
        G.search_buffer.parser.tokens = {};

        // Drop the eof
        assert(G.search_term.size);
        assert(G.search_term.last().token == TOKEN_EOF);
        --G.search_term.size;

        G.search_failed = !buffer.find_and_move(G.search_term, true);
        if (G.search_failed) {
          // If syntax search failed, try text search. TODO: have separate search modes
          G.search_failed = !buffer.find_and_move(G.search_buffer.lines[0].slice, true);
          if (G.search_failed) {
            // buffer.move_to(G.search_begin_pos);
            status_message_set("'{}' not found", (Slice)G.search_buffer.lines[0].slice);
            mode_normal();
            break;
          }
        }
        buffer.jumplist_push();
        mode_normal(true);
      }
    }
    // insert
    else if (key != KEY_TAB) {
      G.search_term_background_color.reset();
      handle_menu_insert(&G.search_pane, key);
    }
    break;}

  case MODE_REPLACE: {
    buffer.action_begin();
    do_delete_movement(key);
    do_paste();
    buffer.action_end();
    mode_normal(true);
    break;}

  case MODE_YANK: {
    // TODO: is it more performant to check if it is a movement command first?

    // action selection
    Array<Range> selections = {};
    if (get_action_selection(buffer, key, &selections)) {
      range_to_clipboard(*buffer.data, VIEW(selections, a), VIEW(selections, b));

      // highlight
      for (Range r : selections)
        buffer.data->highlight_range(r.a, r.b);

      util_free(selections);
      goto yank_done;
    }

    // movement
    {
      Array<Cursor> prev = buffer.cursors.copy_shallow();
      if (!movement_default(buffer, key))
        goto yank_done;
      if (prev.size != buffer.cursors.size) {
        util_free(prev);
        goto yank_done;
      }

      range_to_clipboard(*buffer.data, VIEW(prev, pos), VIEW(buffer.cursors, pos));

      // highlight
      for (int i = 0; i < prev.size; ++i)
        buffer.data->highlight_range(prev[i].pos, buffer.cursors[i].pos);

      // go back to where we started
      for (int i = 0; i < prev.size; ++i)
        buffer.cursors[i] = prev[i];
      util_free(prev);
    }
  
    yank_done:;
    mode_normal();
    break;}

  case MODE_DELETE: {
    do_delete_movement(key);
    mode_normal(true);
    break;}

  case MODE_INSERT: {
    switch (key) {
      case KEY_RETURN:
        buffer.action_begin();
        buffer.insert_newline();
        buffer.autoindent();
        buffer.action_end();
        break;

      case KEY_TAB:
        if (dropdown_autocomplete_or_insert_tab(*G.editing_pane))
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
        break;}

      case CONTROL('k'): {
        // this move should not dirty cursor
        int f = G.flags.cursor_dirty;
        G.dropdown_pane.buffer.move_y(-1);
        G.flags.cursor_dirty = f;
        break;}

      default:
        insert_default(*G.editing_pane, key);
        break;
    }
    break;}

  case MODE_NORMAL:
    if (movement_default(buffer, key))
      break;

    switch (key) {

    case KEY_ESCAPE:
      buffer.collapse_cursors();
      util_free(G.visual_start.cursors);
      break;

    case CONTROL('G'):
      mode_goto_all_definitions();
      break;

    case CONTROL('g'):
      mode_goto_definition();
      break;

    case 'f':
      do_visual_jump();
      break;

    case 'r':
      if (check_visual_start(buffer)) {
        for (int i = 0; i < buffer.cursors.size; ++i)
          buffer.remove_range(G.visual_start.cursors[i], buffer.cursors[i].pos, i);
        do_paste();
        util_free(G.visual_start);
      }
      mode_replace();
      break;

    case '?':
      G.debug_mode = !G.debug_mode;
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
      }
      else
        buffer.autoindent();
      buffer.action_end();
      break;

    case CONTROL('o'):
      buffer.jumplist_prev();
      break;

    case CONTROL('i'):
      buffer.jumplist_next();
      break;

    case CONTROL(KEY_ARROW_LEFT): {
      Pane *p = G.editing_pane->parent ? G.editing_pane->parent : G.editing_pane;
      float &w = p->width_weight;
      w = (w + 1.0f) / 1.3f - 1.0f;
      break;}

    case CONTROL(KEY_ARROW_RIGHT): {
      Pane *p = G.editing_pane->parent ? G.editing_pane->parent : G.editing_pane;
      float &w = p->width_weight;
      w = (w + 1.0f) * 1.3f - 1.0f;
      break;}

    case CONTROL(KEY_ARROW_DOWN):
      // find first parent with multiple children
      for (Pane *p = G.editing_pane; p; p = p->parent) {
        if (p->parent && p->parent->subpanes.size > 1) {
          if (p->parent->subpanes[0].pane == p)
            p->height_weight = (p->height_weight + 1.0f) * 1.3f - 1.0f;
          else
            p->height_weight = (p->height_weight + 1.0f) / 1.3f - 1.0f;
          break;
        }
      }
      break;

    case CONTROL(KEY_ARROW_UP):
      // find first parent with multiple children
      for (Pane *p = G.editing_pane; p; p = p->parent) {
        if (p->parent && p->parent->subpanes.size > 1) {
          if (p->parent->subpanes[0].pane == p)
            p->height_weight = (p->height_weight + 1.0f) / 1.3f - 1.0f;
          else
            p->height_weight = (p->height_weight + 1.0f) * 1.3f - 1.0f;
          break;
        }
      }
      break;

    case '>':
      buffer.action_begin();
      if (check_visual_start(buffer)) {
        for (int i = 0; i < buffer.cursors.size; ++i) {
          int y0 = min(G.visual_start.cursors[i].y, buffer.cursors[i].y);
          int y1 = max(G.visual_start.cursors[i].y, buffer.cursors[i].y);
          for (int y = y0; y <= y1; ++y)
            buffer.add_indent(y, 1);
        }
      }
      else
        for (int i = 0; i < buffer.cursors.size; ++i)
          buffer.add_indent(buffer.cursors[i].y, 1);
      buffer.action_end();
      break;

    case '<':
      buffer.action_begin();
      if (check_visual_start(buffer)) {
        for (int i = 0; i < buffer.cursors.size; ++i) {
          int y0 = min(G.visual_start.cursors[i].y, buffer.cursors[i].y);
          int y1 = max(G.visual_start.cursors[i].y, buffer.cursors[i].y);
          for (int y = y0; y <= y1; ++y)
            buffer.add_indent(y, -1);
        }
      }
      else
        for (int i = 0; i < buffer.cursors.size; ++i)
          buffer.add_indent(buffer.cursors[i].y, -1);
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
      }
      else
        for (int i = 0; i < buffer.cursors.size; ++i)
          toggle_comment(buffer, buffer.cursors[i].y, buffer.cursors[i].y, i);
      buffer.action_end();

      break;}

    case '+':
      G.font_height = at_most(G.font_height+1, 50);
      graphics_set_font_options(G.ttf_file.string.chars);
      break;

    case '-':
      G.font_height = at_least(G.font_height-1, 7);
      graphics_set_font_options(G.ttf_file.string.chars);
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

    case CONTROL('b'):
      do_build();
      break;

    case CONTROL('s'):
      save_buffer(buffer.data);
      break;

    case 'i':
      mode_insert();
      break;

    case 'p':
      buffer.action_begin();
      if (check_visual_start(buffer))
        do_delete_visual();
      do_paste();
      buffer.action_end();
      break;

    case 'm': {
      if (check_visual_start(buffer)) {
        buffer.collapse_cursors();
        int y0 = min(buffer.cursors[0].y, G.visual_start.cursors[0].y);
        int y1 = max(buffer.cursors[0].y, G.visual_start.cursors[0].y);
        buffer.cursors[0] = Cursor::create(0, y0);
        for (int y = y0+1; y <= y1; ++y)
          buffer.cursors.push(Cursor::create(0, y));
        util_free(G.visual_start);
      }
      else {
        int i = buffer.cursors.size;
        buffer.cursors.push(buffer.cursors[i-1]);
        buffer.move_y(i, 1);
      }
      break;}

    case CONTROL('w'): {
      Pane *p = G.editing_pane->add_subpane(buffer.data, {});
      p->switch_buffer(G.editing_pane->buffer.data);
      p->buffer = G.editing_pane->buffer.copy();
      G.editing_pane = p;
      G.selected_pane = p;
      p->parent->selected_subpane = p->parent->subpanes.size-1;
      break;}

    case CONTROL('q'):
      G.panes_to_remove += G.editing_pane;
      break;

    case CONTROL('l'): {
      if (G.editing_pane->subpanes.size > 0) {
        int i = clamp(G.editing_pane->selected_subpane, 0, G.editing_pane->subpanes.size-1);
        G.editing_pane = G.editing_pane->subpanes[i].pane;
        G.selected_pane = G.editing_pane;
        break;
      }

      /*fallthrough*/}

    case CONTROL('L'): {
      int i;
      for (i = 0; i < G.editing_panes.size; ++i)
        if (G.editing_panes[i] == G.editing_pane)
          break;
      // find next root-level pane
      for (++i; i < G.editing_panes.size; ++i)
        if (!G.editing_panes[i]->parent)
          break;
      if (i != G.editing_panes.size) {
        i = (i+1)%G.editing_panes.size;
        G.editing_pane = G.editing_panes[i];
        G.selected_pane = G.editing_pane;
      }
      break;}

    case CONTROL('j'): {
      // Find sibling below
      Pane *p = G.editing_pane;
      // TODO: go down to same depth as current pane?
      while (p->parent) {
        if (p->parent->selected_subpane < p->parent->subpanes.size-1) {
          ++p->parent->selected_subpane;
          G.editing_pane = p->parent->subpanes[p->parent->selected_subpane].pane;
          G.selected_pane = G.editing_pane;
          break;
        }
        p = p->parent;
      }
      break;}

    case CONTROL('k'): {
      // Find sibling above
      Pane *p = G.editing_pane;
      while (p->parent) {
        if (p->parent->selected_subpane > 0) {
          --p->parent->selected_subpane;
          G.editing_pane = p->parent->subpanes[p->parent->selected_subpane].pane;
          G.selected_pane = G.editing_pane;
          break;
        }
        p = p->parent;
      }
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
      // find next root-level pane
      for (--i; i >= 0; --i)
        if (!G.editing_panes[i]->parent)
          break;
      if (i >= 0) {
        i = (i-1 + G.editing_panes.size)%G.editing_panes.size;
        G.editing_pane = G.editing_panes[i];
        G.selected_pane = G.editing_pane;
      }
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

    case 'c':
      // visual yank?
      if (check_visual_start(buffer)) {
        Array<Pos> destination;
        buffer.action_begin();

        destination = {};
        for (Cursor c : buffer.cursors)
          destination += c.pos;

        for (int i = 0; i < G.visual_start.cursors.size; ++i) {
          if (destination[i] < G.visual_start.cursors[i])
            swap(destination[i], G.visual_start.cursors[i]);
        }

        if (G.visual_entire_line) {
          for (int i = 0; i < G.visual_start.cursors.size; ++i) {
            G.visual_start.cursors[i].x = 0;
            destination[i].x = 0;
            ++destination[i].y;
          }
        }
        else
          for (Pos &p : destination)
            buffer.advance(p);

        range_to_clipboard(*buffer.data, view(G.visual_start.cursors), view(destination));

        for (int i = 0; i < G.visual_start.cursors.size; ++i)
          buffer.data->highlight_range(G.visual_start.cursors[i], destination[i]);

        util_free(destination);
        util_free(G.visual_start);
        buffer.action_end();
        break;
      }

      mode_yank();
      break;

    case ':':
      mode_menu();
      break;

    case 's':
      G.visual_entire_line = false;
      util_free(G.visual_start.cursors);
      for (Cursor c : G.editing_pane->buffer.cursors)
        G.visual_start.cursors += c.pos;
      G.visual_start.buffer = G.editing_pane->buffer.data;
      break;

    case 'd':
      // visual delete?
      if (check_visual_start(buffer)) {
        do_delete_visual();
        break;
      }

      mode_delete();
      break;

    case 'S':
      G.visual_entire_line = true;
      buffer.collapse_cursors();
      util_free(G.visual_start);
      G.visual_start.cursors += buffer.cursors[0].pos;
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

static void do_update(float dt) {
  static uint update_idx;
  ++update_idx;

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
  G.bottom_pane_color.tick(dt);
  G.search_term_background_color.tick(dt);
  G.visual_jump_color.tick(dt);
  G.visual_jump_background_color.tick(dt);

  // update paste highlights
  for (BufferData *b : G.buffers) {
    for (int i = 0; i < b->highlights.size; ++i) {
      b->highlights[i].alpha -= dt*0.06f;

      if (b->highlights[i].alpha < 0.0f)
        b->highlights[i--] = b->highlights[--b->highlights.size];
    }
  }

  // highlight some colors
  G.default_marker_background_color.tick(dt);

  // fetch some build result data
  if (G.build_result_output) {
    BufferData &b = G.build_result_buffer;

    b.description = Slice::create("[Building..]");
    while (1) {
      static char buf[256];
      int n = G.build_result_output.read(buf, 256);
      if (n == -1) {
        b.description = Slice::create("[Build Done]");
        util_free(G.build_result_output);
        break;
      }
      if (n == 0)
        break;

      Array<Cursor> prev = {};
      for (Pane *p : G.editing_panes)
        if (p->buffer.data == &G.build_result_buffer)
          prev += p->buffer.cursors[0];
      Array<Cursor> cursors = {};
      b.insert(cursors, {b.lines.last().length, b.lines.size-1}, Slice::create(buf, n), -1);

      int prev_idx = 0;
      for (Pane *p : G.editing_panes) {
        if (p->buffer.data == &G.build_result_buffer) {
          p->buffer.collapse_cursors();
          p->buffer.cursors[0] = prev[prev_idx++];
        }
      }
      util_free(prev);
    }
  }

  static u64 last_modified;
  if (update_idx%50 == 0) {
    Path colorscheme_path = get_colorscheme_path();
    if (File::was_modified(colorscheme_path.string.chars, &last_modified))
      read_colorscheme_file(colorscheme_path.string.chars, true);
    util_free(colorscheme_path);
  }

  G.activation_meter = at_least(G.activation_meter - dt / 500.0f, 0.0f);
}

static int num_top_level_panes() {
  int x = 0;
  for (Pane *p : G.editing_panes)
    if (!p->parent)
      ++x;
  return x;
}

static void do_render() {
  G.font_width = graphics_get_font_advance(G.font_height);
  G.line_height = G.font_height + G.line_margin;
  SDL_GetWindowSize(G.window, &G.win_width, &G.win_height);

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

  #if 1
  TIMING_BEGIN(TIMING_PANE_RENDER);
  for (Pane *p : G.editing_panes) {
    if (!p->parent)
      p->render();
  }

  G.bottom_pane->render();
  if (G.selected_pane == &G.menu_pane)
    G.menu_pane.render();
  if (G.selected_pane == &G.search_pane)
    G.search_pane.render();
  TIMING_END(TIMING_PANE_RENDER);
  #endif
}

static void handle_pending_removes() {
  while (G.panes_to_remove.size || G.buffers_to_remove.size) {

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
        p->parent->selected_subpane = clamp(p->parent->selected_subpane, 0, p->parent->subpanes.size-1);
      }

      // free pane
      util_free(*p);
      if (p->is_dynamic)
        delete p;

      // remove from global pane list
      int idx = G.editing_panes.find(p) - G.editing_panes.items;
      G.editing_panes.remove_item_slow(p);

      // update global pane pointers
      if (G.editing_pane == p && G.editing_panes.size > 0)
        G.editing_pane = G.editing_panes[clamp(idx, 0, G.editing_panes.size-1)];
      if (G.selected_pane == p && G.editing_panes.size > 0)
        G.selected_pane = G.editing_panes[clamp(idx, 0, G.editing_panes.size-1)];
    }
    G.panes_to_remove.size = 0;

    if (G.editing_panes.size == 0) {
      Pane *main_pane = new Pane{};
      Pane::init_edit(*main_pane, &G.null_buffer, &G.color_scheme.background, &G.color_scheme.syntax_text, &G.active_highlight_background_color.color, &G.color_scheme.line_highlight_inactive, true);
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
      G.buffers.remove_item_slow(b);
      if (b->is_dynamic)
        delete b;

      // reset any panes that were using this buffer
      for (int k = 0; k < G.editing_panes.size; ++k)
        if (G.editing_panes[k]->buffer.data == b)
          G.editing_panes[k]->switch_buffer(&G.null_buffer);
    }
    G.buffers_to_remove.size = 0;
  }
}
