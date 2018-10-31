#ifndef BUFFER_HEADER
#define BUFFER_HEADER

const char * const ENDLINE_WINDOWS = "\r\n";
const char * const ENDLINE_UNIX = "\n";

static void util_free(Pos) {}

static bool lines_from_file(Slice filename, Array<StringBuffer> *result, const char **endline_string_result);

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

  static Cursor create(Pos p) {
    return create(p.x, p.y);
  }

  static Cursor create(int x, int y) {
    Cursor c;
    c.x = x;
    c.y = y;
    c.ghost_x = x;
    return c;
  }
};

void util_free(Cursor) {}

static void move_on_insert(Pos &p, Pos a, Pos b) {
  if (p.y == a.y && p.x >= a.x) {
    p.y += b.y - a.y;
    p.x = b.x + p.x - a.x;
  }
  else if (p.y > a.y) {
    p.y += b.y - a.y;
  }
}

static void move_on_insert(Cursor &c, Pos a, Pos b) {
  move_on_insert(c.pos, a, b);
  c.ghost_x = c.x;
}

static void move_on_delete(Pos &p, Pos a, Pos b) {
  if (b <= a)
    return;
  // All cursors that are inside range should be moved to beginning of range
  if (a <= p && p <= b)
    p = a;
  // If lines were deleted, all cursors below b.y should move up
  else if (b.y > a.y && p.y > b.y)
    p.y -= b.y-a.y;
  // All cursors that are on the same row as b, but after b should be merged onto line a
  else if (p.y == b.y && p.x >= b.x-1) {
    p.y = a.y;
    p.x = a.x + p.x - b.x;
  }
}

static void move_on_delete(Cursor &c, Pos a, Pos b) {
  move_on_delete(c.pos, a, b);
  c.ghost_x = c.x;
}

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

struct BufferHighlight {
  Pos a;
  Pos b;
  float alpha; // 1 -> 0
};
static BufferHighlight buffer_highlight(Pos a, Pos b) {
  return {a, b, 2.0f};
}

struct BufferRectIter {
  Pos p;
  Array<StringBuffer> lines;
  Rect r; // y+h will never be more than lines.size-1

  bool next() {
    ++p.x;
    int w = at_most(r.x+r.w, lines[p.y].length);
    if (p.x > w) {
      ++p.y;
      p.x = r.x;
    }
    return p.y <= r.y+r.h;
  }

  char operator*() {
    if (p.x == lines[p.y].length)
      return '\n';
    return lines[p.y][p.x];
  }
};

struct BufferData {
  String filename;
  Slice description; // only used for special buffers (i.e. if it does not have a filename)
  const char * endline_string; // ENDLINE_WINDOWS or ENDLINE_UNIX
  Language language;

  bool is_dynamic;

  BufferData *buffer;

  Array<StringBuffer> lines;
  GroupedData<Array<BlameData>> blame;

  Array<BufferHighlight> highlights;

  int tab_type; /* 0 for tabs, 1+ for spaces */

  // raw_mode is used when inserting text that is not coming from the keyboard, for example when pasting text or doing undo/redo
  // It disables autoindenting and other automatic formatting stuff
  int _raw_mode_depth;

  /* parser stuff */
  ParseResult parser;

  // methods
  Slice name() const {return filename.chars ? Path::name(filename.slice) : description;}
  void parse() {util_free(parser); parser = ::parse(lines, language);}
  bool is_bound_to_file() {return filename.chars;}
  void init(bool is_dynamic, Slice description = {});
  Range* getdefinition(Slice s);
  Slice getslice(Pos a, Pos b) {return lines[a.y](a.x, b.x);} // range is inclusive
  Slice getslice(Range r) const {return lines[r.a.y](r.a.x, r.b.x);} // range is inclusive
  String get_merged_range(Range r) const;
  BufferRectIter getrect(Rect r) {return BufferRectIter{r.p, lines, {r.x, r.y, r.w, at_most(r.y+r.h, lines.size-1) - r.y}};}
  bool modified() {return is_bound_to_file() && _next_undo_action != _last_save_undo_action;}
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
  TokenInfo* find_start_of_identifier(Pos p);
  void insert(Array<Cursor> &cursors, Slice s);
  void insert(Array<Cursor> &cursors, Pos p, Slice s, int cursor_idx = -1, bool re_parse = true);
  void insert(Array<Cursor> &cursors, Slice s, int cursor_idx);
  void remove_trailing_whitespace(Array<Cursor> &cursors, int cursor_idx);
  void insert(Array<Cursor> &cursors, Pos p, Utf8char ch);
  void insert(Array<Cursor> &cursors, Utf8char ch, int cursor_idx);
  void insert(Array<Cursor> &cursors, Utf8char ch);
  void delete_line_at(int y);
  void delete_line(Array<Cursor> &cursors);
  void delete_line(Array<Cursor> &cursors, int y);
  void remove_range(Array<Cursor> &cursors, Pos a, Pos b, int cursor_idx = -1, bool re_parse = true);
  void delete_char(Array<Cursor>& cursors);
  void delete_char(Array<Cursor> &cursors, int cursor_idx);
  void insert_tab(Array<Cursor> &cursors);
  void insert_tab(Array<Cursor> &cursors, int cursor_idx);
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
  void highlight_range(Pos a, Pos b);

  // Undo functionality:
  //
  // Every action on the buffer that mutates it (basically insert/delete) will add that action to the undo/redo list.
  // But sometimes you want a series of actions to be grouped together for undo/redo.
  // To do that you can call action_begin() and action_end() before and after your actions
  //
  // Example:
  //
  // buffer.action_begin(cursors);
  // .. call methods on buffer that mutate it
  // buffer.action_end(cursors);
  //
  // TODO: Use a fixed buffer (circular queue) of undo actions
  int undo_disabled;
  void disable_undo() {++undo_disabled;}
  void enable_undo() {--undo_disabled;}
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

struct BufferView {
  BufferData *data;
  Array<Cursor> cursors;
  Array<Pos> jumplist;
  int jumplist_pos;

  BufferView copy() {
    BufferView bv = {};
    bv.data = data;
    bv.cursors = cursors.copy_shallow();
    bv.jumplist = jumplist.copy_shallow();
    bv.jumplist_pos = jumplist_pos;
    return bv;
  }

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
  void remove_range(int y0, int y1, int cursor_idx) {data->remove_range(cursors, Pos{0, min(y0,y1)}, Pos{0, max(y1,y0)+1}, cursor_idx);}
  void remove_range(Range r, int cursor_idx) {remove_range(r.a, r.b, cursor_idx);}
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
  void insert_tab(int cursor_idx) {data->insert_tab(cursors, cursor_idx);}
  void empty() {data->empty(cursors);}
  void delete_char() {data->delete_char(cursors);}
  Utf8char getchar(int cursor_idx) {return data->getchar(cursors[cursor_idx].pos);}
  Utf8char getchar(Pos p) {return data->getchar(p);}
  int getindent(int y) {return data->getindent(y);}
  void add_indent(int y, int diff) {return data->add_indent(cursors, y, diff);}

  static BufferView create(BufferData *data) {
    BufferView b = {data, {}};
    b.cursors += Cursor{};
    return b;
  }
};
static void util_free(BufferView &b);

struct Location {
  BufferData *buffer;
  Array<Pos> cursors;
};

void util_free(Location &l) {
  util_free(l.cursors);
  l.buffer = 0;
}

Language language_from_filename(Slice filename);

#endif /* BUFFER_HEADER */




/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                       IMPLEMENTATION                       **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/

#ifdef BUFFER_IMPL

static void util_free(BufferView &b) {
  util_free(b.jumplist);
  util_free(b.cursors);
  b.data = 0;
}

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

void swap_range(BufferData &buffer, Pos &a, Pos &b) {
  Pos tmp = a;
  a = b;
  b = tmp;
  buffer.advance(a);
}

static void clamp_cursor(Pos &p, Pos, Pos);
static void clamp_cursor(Cursor &c, Pos a, Pos b);

#define UPDATE_CURSORS(name, move_function) \
static void name(BufferData *buffer, Pos a, Pos b) { \
  if (buffer == &G.status_message_buffer) { \
    move_function(G.status_message_pane.buffer.cursors[0], a, b); \
    return; \
  } \
  \
  if (buffer == &G.menu_buffer) { \
    move_function(G.menu_pane.buffer.cursors[0], a, b); \
    return; \
  } \
  \
  if (G.visual_start.buffer == buffer) \
    for (Pos &p : G.visual_start.cursors) \
      move_function(p, a, b); \
  \
  /* move cursors */ \
  for (Pane *pane : G.editing_panes) {  \
    if (pane->buffer.data != buffer)  \
      continue;  \
  \
    /* cursors */ \
    for (Cursor &c : pane->buffer.cursors) {  \
      move_function(c, a, b); \
    } \
  \
    /* jumplist */ \
    for (Pos &pos : pane->buffer.jumplist) \
      move_function(pos, a, b); \
  \
  } \
  \
  /* paste highlights */ \
  for (BufferHighlight &ph : buffer->highlights) { \
    move_function(ph.a, a, b); \
    move_function(ph.b, a, b); \
  } \
}

UPDATE_CURSORS(move_cursors_on_insert, move_on_insert)
UPDATE_CURSORS(move_cursors_on_delete, move_on_delete)
UPDATE_CURSORS(clamp_cursors, clamp_cursor)

TokenInfo* BufferData::find_start_of_identifier(Pos pos) {
  advance_r(pos);
  TokenInfo *t = gettoken(pos);
  if (t == parser.tokens.end() || t->token != TOKEN_IDENTIFIER || !t->r.contains(pos))
    return 0;
  return t;
}

void BufferData::remove_range(Array<Cursor> &cursors, Pos a, Pos b, int cursor_idx, bool re_parse) {
  // log_info("before: (%i %i) (%i %i)\n", a.x, a.y, b.x, b.y);
  if (b <= a)
    swap_range(*this, a, b);
  // log_info("after:  (%i %i) (%i %i)\n", a.x, a.y, b.x, b.y);

  action_begin(cursors);
  G.flags.cursor_dirty = true;

  if (!undo_disabled)
    push_undo_action(UndoAction::delete_range({a,b}, range_to_string({a,b}).string, cursor_idx));

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

  if (re_parse)
    parse();

  move_cursors_on_delete(this, a, b);

  action_end(cursors);
}

void BufferData::delete_char(Array<Cursor> &cursors, int cursor_idx) {
  action_begin(cursors);
  G.flags.cursor_dirty = true;

  Pos pos = cursors[cursor_idx].pos;
  if (pos.x == 0) {
    if (pos.y == 0)
      return;
    remove_range(cursors, {lines[pos.y-1].length, pos.y-1}, {0, pos.y}, cursor_idx);
  }
  else {
    Pos p = pos;
    advance_r(p);
    remove_range(cursors, p, pos, cursor_idx);
  }

  action_end(cursors);
}

void BufferData::delete_char(Array<Cursor> &cursors) {
  action_begin(cursors);
  G.flags.cursor_dirty = true;

  for (int i = 0; i < cursors.size; ++i)
    delete_char(cursors, i);

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
  for (TokenInfo *t = gettoken(p); t != parser.tokens.end() && t->b.y == y; ++t) {
    if      (t->token == '{' || t->token == '[' || t->token == '(') ++depth;
    else if (t->token == '}' || t->token == ']' || t->token == ')') --depth;
    else if (t->token == TOKEN_IDENTIFIER) {
      if (first && (
          t->str == "for" ||
          t->str == "if" ||
          t->str == "while" ||
          t->str == "else"
          )) {
        if (has_statement)
          *has_statement = true;
      }
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

  // TODO: handle correctly if y=0
  int y_above = at_least(y-1, 0);
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

void BufferData::insert(Array<Cursor> &cursors, const Pos a, Slice s, int cursor_index_hint, bool re_parse) {
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
    int ai = 0, bi = 0;
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

  if (re_parse)
    parse();
  move_cursors_on_insert(this, a, b);

  highlight_range(a,b);
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

void BufferData::insert_tab(Array<Cursor> &cursors, int cursor_idx) {
  action_begin(cursors);

  if (tab_type == 0)
    insert(cursors, Utf8char::create('\t'), cursor_idx);
  else
    for (int i = 0; i < tab_type; ++i)
      insert(cursors, Utf8char::create(' '), cursor_idx);

  action_end(cursors);
}

void BufferData::insert_tab(Array<Cursor> &cursors) {
  action_begin(cursors);

  for (int i = 0; i < cursors.size; ++i)
    insert_tab(cursors, i);

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


Language language_from_filename(Slice filename) {
  Language language = LANGUAGE_NULL;
  if (filename.ends_with(".cpp") || filename.ends_with(".h") || filename.ends_with(".hpp") || filename.ends_with(".c"))
    language = LANGUAGE_C;
  else if (filename.ends_with(".cs"))
    language = LANGUAGE_CSHARP;
  else if (filename.ends_with(".py"))
    language = LANGUAGE_PYTHON;
  else if (filename.ends_with(".jl"))
    language = LANGUAGE_JULIA;
  else if (filename.ends_with(".sh"))
    language = LANGUAGE_BASH;
  else if (Path::name(filename) == "Makefile" || Path::name(filename) == "makefile")
    language = LANGUAGE_BASH; // bash will have to do for makefiles
  else if (filename.ends_with(".cmantic-colorscheme"))
    language = LANGUAGE_CMANTIC_COLORSCHEME;
  else if (filename.ends_with(".go"))
    language = LANGUAGE_GOLANG;
  return language;
}

// filename must be absolute
bool BufferData::from_file(Slice filename, BufferData *buffer) {
  *buffer = {};
  BufferData &b = *buffer;

  b.language = language_from_filename(filename);

  b.endline_string = ENDLINE_UNIX;

  b.filename = filename.copy();
  if (!lines_from_file(filename, &b.lines, &b.endline_string))
    goto err;

  // token type
  b.parse();

  // guess tab type
  b.guess_tab_type();

  return true;

  err:
  if (buffer) {
    util_free(*buffer);
    free(buffer);
  }
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
  for (Range &r : parser.definitions)
    if (getslice(r) == s)
      return &r;
  return 0;
}

#if 0
// if no token at pos, gets the next token
TokenInfo* BufferData::gettoken(Pos p) {
  for (int i = 0; i < parser.tokens.size; ++i)
    if (p < parser.tokens[i].b)
      return &parser.tokens[i];
  return parser.tokens.end();
}
#else
TokenInfo* BufferData::gettoken(Pos p) {
  int a = 0, b = parser.tokens.size-1;

  while (a <= b) {
    int mid = (a+b)/2;
    if ((mid == 0 || parser.tokens[mid-1].b <= p) && p < parser.tokens[mid].b)
      return &parser.tokens[mid];
    if (parser.tokens[mid].a < p)
      a = mid+1;
    else
      b = mid-1;
  }
  return parser.tokens.end();
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
    if (_undo_actions[_next_undo_action-1].type == ACTIONTYPE_CURSOR_SNAPSHOT && _undo_actions[_next_undo_action-2].type == ACTIONTYPE_GROUP_BEGIN) {
      changed = false;
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

    // CLIPBOARD
    {
      Array<StringBuffer> clips = {};

      // find start of group
      UndoAction *a = &_undo_actions[_next_undo_action-1];
      assert(a->type == ACTIONTYPE_GROUP_END);
      while (a[-1].type != ACTIONTYPE_GROUP_BEGIN) --a;
      assert(a->type == ACTIONTYPE_CURSOR_SNAPSHOT);

      // only do clipboard if no inserts were done
      for (UndoAction *act = a; act->type != ACTIONTYPE_GROUP_END; ++act)
        if (act->type == ACTIONTYPE_INSERT)
          goto clipboard_done;

      // create stringbuffers
      clips.resize(a->cursors.size);
      clips.zero();

      // find every delete action, and if there is a cursor for that, add that delete that cursors 
      {
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
      }

      clipboard_done:
      util_free(clips);
    }
  }
}

void BufferData::undo(Array<Cursor> &cursors) {
  util_free(blame);

  if (undo_disabled)
    return;
  if (!_next_undo_action)
    return;

  ++undo_disabled;
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
        remove_range(cursors, a.insert.a, a.insert.b, -1, false);
        break;
      case ACTIONTYPE_DELETE:
        // printf("Inserting '%.*s' at {%i %i}\n", a.remove.s.slice.length, a.remove.s.slice.chars, a.remove.a.x, a.remove.a.y);
        insert(cursors, a.remove.a, a.remove.s.slice, -1, false);
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
  --undo_disabled;
  parse();

  raw_end();
}

void BufferData::redo(Array<Cursor> &cursors) {
  util_free(blame);

  if (undo_disabled)
    return;

  if (_next_undo_action == _undo_actions.size)
    return;

  ++undo_disabled;
  raw_begin();
  assert(_undo_actions[_next_undo_action].type == ACTIONTYPE_GROUP_BEGIN);
  ++_next_undo_action;
  for (; _undo_actions[_next_undo_action].type != ACTIONTYPE_GROUP_END; ++_next_undo_action) {
    UndoAction a = _undo_actions[_next_undo_action];
    // printf("redo action: %i\n", a.type);
    switch (a.type) {
      case ACTIONTYPE_INSERT:
        // printf("Inserting '%s' at {%i %i}\n", a.insert.s.slice.chars, a.insert.a.x, a.insert.a.y);
        insert(cursors, a.insert.a, a.insert.s.slice, -1, false);
        break;
      case ACTIONTYPE_DELETE:
        // printf("Removing {%i %i}, {%i %i}\n", a.remove.a.x, a.remove.a.y, a.remove.b.x, a.remove.b.y);
        remove_range(cursors, a.remove.a, a.remove.b, -1, false);
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
  parse();
  --undo_disabled;
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
  bool success = false;
  for (int i = 0; i < cursors.size; ++i) {
    Pos p = cursors[i].pos;
    if (!data->find_r(c, stay, &p))
      continue;
    success = true;
    move_to(i, p);
  }
  return success;
}

bool BufferView::find_and_move_r(Slice s, bool stay) {
  bool success = false;
  for (int i = 0; i < cursors.size; ++i) {
    Pos p = cursors[i].pos;
    if (!data->find_r(s, stay, &p))
      continue;
    success = true;
    move_to(i, p);
  }
  return success;
}

// returns success if at least one cursor jumps
bool BufferView::find_and_move(char c, bool stay) {
  bool success = false;
  for (int i = 0; i < cursors.size; ++i) {
    Pos p = cursors[i].pos;
    if (!data->find(c, stay, &p))
      continue;
    success = true;
    move_to(i, p);
  }
  return success;
}

bool BufferView::find_and_move(Slice s, bool stay) {
  bool success = false;
  for (int i = 0; i < cursors.size; ++i) {
    Pos p = cursors[i].pos;
    if (!data->find(s, stay, &p))
      continue;
    success = true;
    move_to(i, p);
  }
  return success;
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
  for (int x = 0; x < lines[y].length; ++x)
    if (!getchar(x,y).isspace())
      return;

  remove_range(cursors, {0, y}, {lines[y].length, y}, cursor_idx);
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

static bool lines_from_file(Slice filename, Array<StringBuffer> *result, const char **endline_string_result) {
  Array<StringBuffer> lines = {};
  int num_lines;

  FILE* f = 0;
  if (File::open(&f, filename.chars, "rb"))
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
    lines.resize(num_lines+1);
    lines.zero();

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
          if (endline_string_result)
            *endline_string_result = ENDLINE_WINDOWS;
        }
        if (c == '\n')
          break;
        lines[i] += c;
      }
    }
    last_line:;
    assert(feof(f));
  } else {
    lines += {};
  }

  *result = lines;
  fclose(f);
  return true;

  err:
  util_free(lines);
  if (f)
    fclose(f);
  return false;
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
  util_free(b.parser);
  util_free(b._undo_actions);
  b.highlights.free_shallow();
}

String BufferData::get_merged_range(Range r) const {
  if (r.a.y == r.b.y)
    return String::create(getslice(r));

  StringBuffer sb = {};
  sb += lines[r.a.y](r.a.x, -1);
  sb += ' ';
  for (int y = r.a.y+1; y < r.b.y; ++y) {
    sb += lines[y](0, -1);
    sb += ' ';
  }
  sb += lines[r.b.y](0, r.b.x);

  // remove whitespaces
  char *out = sb.chars;
  bool in_space = false;
  for (int i = 0; i < sb.length; ++i) {
    if (!isspace(sb[i])) {
      *out++ = sb[i];
      in_space = false;
    }
    else {
      if (!in_space)
        *out++ = ' ';
      in_space = true;
    }
  }
  sb.length = out - sb.chars;
  String result = String::create(sb.slice);
  util_free(sb);
  return result;
}

static BufferData *_clamp_cursor_current_buffer;
static void clamp_cursor(Pos &p, Pos, Pos) {
  p.y = clamp(p.y, 0, _clamp_cursor_current_buffer->lines.size-1);
  p.x = clamp(p.x, 0, _clamp_cursor_current_buffer->lines[p.y].length);
}
static void clamp_cursor(Cursor &c, Pos a, Pos b) {
  clamp_cursor(c.pos, a, b);
  c.ghost_x = c.x;
}

bool BufferData::reload(BufferData *b) {
  if (!b->filename.chars)
    return false;
  String filename = String::create(b->filename.slice);
  util_free(*b);
  bool succ = BufferData::from_file(filename.slice, b);
  util_free(filename);

  _clamp_cursor_current_buffer = b;
  clamp_cursors(b, {}, {});

  return succ;
}

void BufferData::highlight_range(Pos a, Pos b) {
  if (b < a)
    swap_range(*this, a,b);
  highlights += buffer_highlight(a,b);
}

#endif /* BUFFER_IMPL */