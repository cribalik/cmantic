#ifndef PANE_HEADER
#define PANE_HEADER

struct Pane;
struct SubPane {
  Pos anchor_pos;
  Pane *pane;
};

struct MenuSuggestion {
  String value;
  String description;
};
void util_free(MenuSuggestion &m) {
  util_free(m.value);
  util_free(m.description);
  m = {};
}

enum PaneType {
  PANETYPE_NULL,
  PANETYPE_EDIT,
  PANETYPE_MENU,
  PANETYPE_STATUSMESSAGE,
  PANETYPE_DROPDOWN,
};

struct Pane {
  PaneType type;
  BufferView buffer;

  Rect bounds;
  const Color *background_color;
  const Color *active_highlight_background_color;
  const Color *line_highlight_inactive;
  const Color *text_color;
  int _gutter_width;

  bool is_dynamic; // some panes are statically allocated because they are singletons, like the build result pane, status message pane, etc.

  float width_weight;
  float height_weight;
  Array<SubPane> subpanes;
  int selected_subpane;
  Pane *parent;

  // visual settings
  int margin;

  // type-specific data
  union {
    // PANETYPE_EDIT
    struct {
      // The part of the buffer that is visible
      Rect buffer_viewport;
    };

    // PANETYPE_MENU
    struct {
      bool is_verbose;
      // callbacks
      Array<String> (*get_suggestions)();
      Array<MenuSuggestion> (*get_verbose_suggestions)();
      Slice prefix;

      int current_suggestion;
      Array<String> suggestions;
      Array<MenuSuggestion> verbose_suggestions;
    } menu;

    // PANETYPE_DROPDOWN
    struct {
    };
  };

  // methods
  void menu_init(Slice prefix) {
    menu.get_suggestions = 0;
    menu.get_verbose_suggestions = 0;
    util_free(menu.suggestions);
    util_free(menu.verbose_suggestions);
    menu.current_suggestion = 0;
    menu.prefix = prefix;
    buffer.empty();
  }

  void menu_init(Slice prefix, Array<String> (*get_suggestions_fn)()) {
    menu_init(prefix);
    menu.is_verbose = false;
    menu.get_suggestions = get_suggestions_fn;
  }

  void menu_init(Slice prefix, Array<MenuSuggestion> (*get_suggestions_fn)()) {
    menu_init(prefix);
    menu.is_verbose = true;
    menu.get_verbose_suggestions = get_suggestions_fn;
  }

  Pane* add_subpane(BufferData *buffer, Pos p);
  void render();
  Rect get_buffer_visibility_rect();
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
  void render_single_line(int font_size);
  void render_syntax_highlight(TextCanvas &canvas, int y1);
  int calc_top_visible_row() const;
  int calc_left_visible_column() const;
  int calc_max_subpane_depth() const;

  Slice* menu_get_selection() {
    if (!menu.suggestions.size)
      return 0;
    int i = clamp(menu.current_suggestion, 0, menu.suggestions.size);
    return &menu.suggestions[i].slice;
  };

  int menu_get_selection_idx() {
    if (!menu.suggestions.size)
      return -1;
    return clamp(menu.current_suggestion, 0, menu.suggestions.size);
  };

  int numchars_x() const;
  int numchars_y() const;
  Pos slot2pixel(Pos p) const;
  int slot2pixelx(int x) const;
  int slot2pixely(int y) const;
  Pos slot2global(Pos p) const;
  Pos buf2char(Pos p) const;
  Pos buf2pixel(Pos p, int font_width, int line_height) const;

  static void init_edit(Pane &p, BufferData *b, Color *background_color, Color *text_color, Color *active_highlight_background_color, Color *line_highlight_inactive, bool is_dynamic);
};

static void util_free(Pane&);

#endif /* PANE_HEADER */










#ifdef PANE_IMPL

void Pane::init_edit(Pane &p,
                      BufferData *b,
                      Color *background_color,
                      Color *text_color,
                      Color *active_highlight_background_color,
                      Color *line_highlight_inactive, bool is_dynamic) {
  Pane *old_parent = p.parent;
  util_free(p);
  p.type = PANETYPE_EDIT;
  p.buffer = {b, Array<Cursor>{}};
  p.buffer.cursors += {};
  p.background_color = background_color;
  p.text_color = text_color;
  p.active_highlight_background_color = active_highlight_background_color;
  p.line_highlight_inactive = line_highlight_inactive;
  p.is_dynamic = is_dynamic;
  p.parent = old_parent;
}


void Pane::render_as_dropdown() {
  BufferView &b = buffer;

  TextCanvas canvas;
  int y_max = this->numchars_y();
  canvas.init(this->numchars_x(), this->numchars_y(), G.font_height, G.line_margin);
  canvas.background = *this->background_color;
  canvas.fill(Utf8char{' '});
  canvas.fill(*this->text_color, *this->background_color);
  canvas.draw_shadow = true;
  canvas.margin = this->margin;

  // draw each line 
  for (int y = 0; y < at_most(b.data->lines.size, y_max); ++y)
    canvas.render_str({0, y}, this->text_color, NULL, 0, -1, b.data->lines[y].slice);

  // highlight the line you're on
  for (int i = 0; i < b.cursors.size; ++i)
    canvas.fill_background(Rect{0, b.cursors[i].y, {-1, 1}}, *active_highlight_background_color);

  canvas.render(this->bounds.p);

  util_free(canvas);
  render_quads();
  render_text();
}

void Pane::render_syntax_highlight(TextCanvas &canvas, int y1) {
  #define render_highlight(color) canvas.fill_textcolor(Range{b.data->to_visual_pos(t->a), b.data->to_visual_pos(t->b)}, color)


  BufferView &b = this->buffer;

  if (!b.data->language)
    return;

  // syntax @highlighting
  int y0 = canvas.offset.y;
  const Pos pos = {0, canvas.offset.y};
  TokenInfo *t = b.data->gettoken(pos);

  if (t) {
    for (; t < b.data->parser.tokens.end() && t->a.y < y1; ++t) {
      if (t->token == TOKEN_NULL)
        break;
      switch (t->token) {

        case TOKEN_NUMBER:
          render_highlight(G.color_scheme.syntax_number);
          break;

        case TOKEN_BLOCK_COMMENT:
        case TOKEN_LINE_COMMENT:
          render_highlight(G.color_scheme.syntax_comment);
          break;

        case TOKEN_STRING:
        case TOKEN_STRING_BEGIN:
          render_highlight(G.color_scheme.syntax_string);
          break;

        case TOKEN_OPERATOR:
          render_highlight(G.color_scheme.syntax_operator);
          break;

        case TOKEN_IDENTIFIER: {
          // check for keywords
          if (t->str.length > 0 && t->str[0] == '#')
            render_highlight(*G.keyword_colors[KEYWORD_MACRO]);
          else {
            for (Keyword keyword : language_settings[b.data->language].keywords) {
              if (t->str == keyword.name) {
                render_highlight(*G.keyword_colors[keyword.type]);
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

  // we hack this here until we have language-specific syntax highlighting
  if (b.data->language == LANGUAGE_CMANTIC_COLORSCHEME) {
    t = b.data->gettoken(pos);
    while (t < b.data->parser.tokens.end()) {
      if (t->token != TOKEN_IDENTIFIER) {
        ++t;
        continue;
      }
      Range r = {t->a};
      ++t;

      int ri,gi,bi;

      // hex
      if (t < b.data->parser.tokens.end() && t[0].token == TOKEN_IDENTIFIER) {
        Slice hex = b.data->getslice(t[0].r);
        if (hex.length < 7 || hex[0] != '#')
          continue;
        bool success = true;
        success &= hex(1,3).toint_from_hex(&ri);
        success &= hex(3,5).toint_from_hex(&gi);
        success &= hex(5,7).toint_from_hex(&bi);
        if (!success)
          continue;
        ++t;
      }
      else if (t + 2 < b.data->parser.tokens.end() && t[0].token == TOKEN_NUMBER && t[1].token == TOKEN_NUMBER && t[2].token == TOKEN_NUMBER) {
        bool success = true;
        success &= b.data->getslice(t[0].r).toint(&ri);
        success &= b.data->getslice(t[1].r).toint(&gi);
        success &= b.data->getslice(t[2].r).toint(&bi);
        if (!success)
          continue;

        while (t < b.data->parser.tokens.end() && t->token == TOKEN_NUMBER)
          ++t;
      }
      else
        continue;

      Color color = rgb8_to_linear_color(ri, gi, bi);
      r.b = t < b.data->parser.tokens.end() ? t->b : t[-1].b;
      r.a = b.data->to_visual_pos(r.a);
      r.b = b.data->to_visual_pos(r.b);

      canvas.fill_textcolor(r, invert(color));
      canvas.fill_background(r, color);
    }
  }

  // syntax highlight definitions
  for (Range r : buffer.data->parser.definitions) {
    if (r.a.y >= y1)
      break;
    if (r.b.y < y0)
      continue;
    canvas.fill_textcolor(Range{b.data->to_visual_pos(r.a), b.data->to_visual_pos(r.b)}, G.color_scheme.syntax_definition);
  }
}

Pane* Pane::add_subpane(BufferData *b, Pos pos) {
  Pane *p = new Pane{};
  Pane::init_edit(*p, b, (Color*)&G.color_scheme.background, &G.color_scheme.syntax_text, &G.active_highlight_background_color.color, &G.color_scheme.line_highlight_inactive, true);
  p->buffer.move_to(pos);
  p->parent = this;
  subpanes += {buffer.cursors[0].pos, p};
  G.editing_panes += p;
  return p;
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
      render_single_line(G.font_height);
      break;
    case PANETYPE_DROPDOWN:
      render_as_dropdown();
      break;
  }
}

void Pane::update_suggestions() {
  if (!G.flags.cursor_dirty)
    return;
  if (!menu.get_suggestions && !menu.get_verbose_suggestions)
    return;
  util_free(menu.suggestions);
  util_free(menu.verbose_suggestions);
  if (menu.get_suggestions)
    menu.suggestions = menu.get_suggestions();
  else
    menu.verbose_suggestions = menu.get_verbose_suggestions();
  menu.current_suggestion = 0;
}

void Pane::render_single_line(int font_size) {
  BufferView &b = buffer;
  // TODO: scrolling in x
  Pos buf_offset = {menu.prefix.length+2, 0};

  // render the editing line
  TextCanvas canvas;
  canvas.init(this->numchars_x(), 1, font_size, G.line_margin);
  canvas.fill(*this->text_color, *this->background_color);
  canvas.background = *this->background_color;
  canvas.margin = this->margin;

  // draw prefix
  canvas.render_strf({0, 0}, &G.color_scheme.gutter_text, NULL, 0, -1, "{}: ", (Slice)menu.prefix);

  // draw buffer
  canvas.render_str(buf_offset, text_color, NULL, 0, -1, b.data->lines[0].slice);

  // draw marker
  if (G.selected_pane == this)
    canvas.fill_background(Rect{b.cursors[0].pos + buf_offset, {1, 1}}, G.marker_background_color.color);
  else if (G.bottom_pane != this)
    canvas.fill_background(Rect{b.cursors[0].pos + buf_offset, {1, 1}}, G.color_scheme.marker_inactive);

  canvas.render(this->bounds.p);

  util_free(canvas);
}

void Pane::render_menu() {
  const int font_height = 14;
  margin = 8;
  const int line_height = font_height + margin;
  const int font_width = graphics_get_font_advance(font_height);
  int num_lines = at_most(menu.suggestions.size, (G.win_height - 10)/line_height) + 1;
  int width = clamp((int)(G.win_width*0.8), 5, font_width * 120);
  const int num_chars = (width - 2*margin)/font_width;
  if (num_chars < 0)
    return;
  int height = num_lines*line_height + margin;
  int x = G.win_width/2 - width/2;
  int y = G.win_height * 0.1f;

  // draw background
  push_square_quad(Rect{x, y, width, height}, *background_color);
  render_shadow_bottom_right(x, y, width, height, 5);
  y += margin;
  x += margin;
  width -= 2*margin;
  height -= 2*margin;

  // render menu prefix
  int _x = x;
  String prefix = String::createf("{}: ", (Slice)menu.prefix);
  int n = min(prefix.length, num_chars);
  push_textn(prefix.chars, n, _x, y + font_height, false, G.color_scheme.gutter_text, font_height);
  _x += n * font_width;
  util_free(prefix);

  // render input line
  n = min(buffer.data->lines[0].length, num_chars - _x/font_width);
  push_textn(buffer.data->lines[0].chars, n, _x, y + font_height, false, *text_color, font_height);
  _x += n * font_width;

  // render marker
  push_square_quad(Rect{_x, y, font_width, font_height}, G.marker_background_color.color);
  height -= line_height;
  --num_lines;
  y += line_height;

  // render popup
  if (menu.is_verbose) {
    if (!menu.verbose_suggestions.size)
      return;
    // TODO: implement
  }
  else {
    if (!menu.suggestions.size)
      return;

    // draw highlighted line
    push_square_quad(Rect{x - margin/2, y + menu.current_suggestion*line_height - margin/2, width + margin, line_height}, *active_highlight_background_color);

    // draw text
    y += font_height;
    for (int i = 0, n = at_most(menu.suggestions.size, num_lines); i < n; ++i) {
      push_textn(menu.suggestions[i].chars, min(menu.suggestions[i].length, num_chars), x, y, false, *text_color, font_height);
      // draw hairline
      // if (i < n-1)
        // push_square_quad(Rect{x, y, width, 1}, blend(*background_color, COLOR_WHITE, 0.1));

      y += line_height;
    }

  }

  render_quads();
  render_text();
}

static void render_dropdown(Pane *pane) {
  BufferView &b = pane->buffer;

  // don't show dropdown unless in edit mode
  if (pane->type == PANETYPE_EDIT && G.mode != MODE_INSERT)
    return;

  // we have to be on an identifier
  Pos identifier_start;
  TokenInfo *t = b.data->find_start_of_identifier(b.cursors[0].pos);
  if (!t)
    return;
  identifier_start = t->a;

  // since fuzzy matching is expensive, we only update we moved since last time
  if (G.flags.cursor_dirty) {
    Slice identifier = t->str;
    StackArray<FuzzyMatch, 10> best_matches;
    View<Slice> input = VIEW(b.data->parser.identifiers, slice);
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

  G.dropdown_pane.bounds.size = char2pixel(max_width, G.dropdown_buffer.lines.size-1, G.font_width, G.line_height) + Pos{G.dropdown_pane.margin*2, G.dropdown_pane.margin*2};
  G.dropdown_pane.bounds.h = at_most(G.dropdown_pane.bounds.h, G.win_height - 10*G.line_height);

  // position pane
  Pos p = pane->buf2pixel(identifier_start, G.font_width, G.line_height);
  p.y += G.line_height;
  G.dropdown_pane.bounds.p = p;

  // move it up if it would go outside the screen
  if (G.dropdown_pane.bounds.y + G.dropdown_pane.bounds.h > G.win_height)
    G.dropdown_pane.bounds.y -= G.dropdown_pane.bounds.h + 2*G.line_height;

  G.dropdown_pane.bounds.x = clamp(G.dropdown_pane.bounds.x, 0, G.win_width - G.dropdown_pane.bounds.w);

  G.dropdown_pane.render();
}


void Pane::render_edit() {
  BufferView &b = buffer;
  BufferData &d = *buffer.data;

  // TODO: cleanup. this is super hacky, since we change the bounds, and then call numchars_x which depends on bounds
  Rect orig_bounds = bounds;
  const int header_height = 20;
  bounds.y += header_height;
  bounds.h -= header_height;

  // recalculate the bounds of this pane depending on the number of subpanes
  const int subpane_depth = calc_max_subpane_depth();
  const int total_width = orig_bounds.w;
  bounds.w = (int)(total_width*(width_weight+1)/(subpane_depth+width_weight+1));

  // calc buffer bound
  Pos buf_offset = {this->calc_left_visible_column(), this->calc_top_visible_row()};
  int buf_y1 = at_most(buf_offset.y + this->numchars_y(), d.lines.size);

  // draw gutter
  TIMING_BEGIN(TIMING_PANE_GUTTER);
  this->_gutter_width = at_least(calc_num_chars(buf_y1) + 3, 6);
  if (_gutter_width && numchars_y()) {
    TextCanvas gutter;
    gutter.init(_gutter_width, numchars_y(), G.font_height, G.line_margin);
    gutter.background = *background_color;
    for (int y = 0, line = buf_offset.y; line < buf_y1; ++y, ++line)
      if (line < d.lines.size)
        gutter.render_strf({0, y}, &G.color_scheme.gutter_text, &G.color_scheme.gutter_background, 0, _gutter_width, " %i", line + 1);
      else
        gutter.render_str({0, y}, &G.color_scheme.gutter_text, &G.color_scheme.gutter_background, 0, _gutter_width, Slice::create(" ~"));
    gutter.render(bounds.p);
    util_free(gutter);
  }
  TIMING_END(TIMING_PANE_GUTTER);

  // record buffer viewport (mainly used for visual jump)
  buffer_viewport = {buf_offset.x, buf_offset.y, numchars_x()-_gutter_width, buf_y1 - buf_offset.y-1};

  // render buffer
  // TODO: The reason we don't use buffer_viewport.h here is because we might be at the end of the buffer,
  //       causing the background to never render.. This is kind of depressing. We should probably just render a big
  //       background first, and then use buffer_viewport.h for the canvas
  TIMING_BEGIN(TIMING_PANE_BUFFER);
  if (buffer_viewport.w > 0 && numchars_y() > 0) {
    TextCanvas canvas;
    canvas.init(buffer_viewport.w, numchars_y(), G.font_height, G.line_margin);
    canvas.fill(*text_color, *background_color);
    canvas.background = *background_color;
    canvas.margin = margin;
    canvas.offset = buf_offset;

    // draw each line 
    for (int y = buf_offset.y; y < buf_y1; ++y)
      canvas.render_str({0, y}, text_color, NULL, 0, -1, d.lines[y].slice);

    // highlight the line you're on
    const Color *highlight_background_color = G.editing_pane == this ? active_highlight_background_color : line_highlight_inactive;
    if (highlight_background_color)
      for (Cursor c : b.cursors)
        canvas.fill_background(Rect{d.to_visual_pos(Pos{0, c.y}), {-1, 1}}, *highlight_background_color);

    // highlight visual start
    if (G.visual_start.buffer == buffer.data && G.visual_entire_line)
      for (Pos pos : G.visual_start.cursors)
        canvas.fill_background(Rect{d.to_visual_pos({0, pos.y}), {-1, 1}}, *line_highlight_inactive);

    // draw syntax highlighting
    render_syntax_highlight(canvas, buf_y1);

    // draw paste highlight
    for (BufferHighlight ph : d.highlights) {
      if (ph.b.y < buf_offset.y || ph.a.y > buf_y1)
        continue;
      Color color = COLOR_YELLOW;
      color.a = clamp(ph.alpha, 0.0f, 1.0f);
      canvas.blend_textcolor(Range{d.to_visual_pos(ph.a), d.to_visual_pos(ph.b)}, color);
    }

    // draw blame
    if (d.blame.data.size) {
      Slice last_hash = {};
      StringBuffer msg = {};
      int i = 0;
      for (int y = buf_offset.y; y < buf_y1; ++y) {
        // TODO: binary search
        while (i < d.blame.data.size && d.blame.data[i].line <= y)
          ++i;
        BlameData bd = d.blame.data[i-1];
        if (last_hash == bd.hash)
          continue;
        last_hash = Slice::create(bd.hash);

        Pos p = d.to_visual_pos({d.lines[y].length, y});
        p.x = at_least(p.x+2, 30);
        msg.length = 0;
        msg.appendf("{} - %s - %s", Slice::create(bd.hash, 8), bd.author, bd.summary);
        canvas.render_str(p, &G.color_scheme.git_blame, NULL, p.x, -1, msg.slice);
      }
      util_free(msg);
    }

    // if there is a search term, highlight that as well
    if (G.search_term.length > 0) {
      Pos pos = {0, buf_offset.y};
      while (d.find(G.search_term.slice, false, &pos) && pos.y < buf_y1)
        canvas.fill_background(Rect{d.to_visual_pos(pos), G.search_term.length, 1}, G.search_term_background_color.color);
    }

    // draw visual start marker
    if (G.visual_start.buffer == buffer.data)
      for (Pos pos : G.visual_start.cursors)
        canvas.fill(Rect{d.to_visual_pos(pos), {1, 1}}, G.default_marker_background_color.color, *background_color);

    // draw marker
    for (Cursor c : b.cursors) {
      if (G.selected_pane == this)
        // canvas.fill_background(Rect{buf2char(pos), {1, 1}}, from_hsl(fmodf(i*360.0f/b.markers.size, 360.0f), 0.7f, 0.7f));
        canvas.fill(Rect{d.to_visual_pos(c.pos), {1, 1}}, G.default_marker_background_color.color, *background_color);
      else if (G.bottom_pane != this)
        canvas.fill_background(Rect{d.to_visual_pos(c.pos), {1, 1}}, G.color_scheme.marker_inactive);
    }

    // draw visual jump
    if (G.current_visual_jump_pane == this) {
      for (int i = 0; i < G.visual_jump_positions.size; ++i) {
        Pos p = G.visual_jump_positions[i];
        char placeholder = visual_jump_highlight_keys[i];
        canvas.render_char(d.to_visual_pos(p), G.visual_jump_color.color, &G.visual_jump_background_color.color, placeholder);
      }
    }

    canvas.render(bounds.p + Pos{_gutter_width*G.font_width, 0});
    util_free(canvas);
  }
  TIMING_END(TIMING_PANE_BUFFER);

  // render filename
  const Slice filename = d.name();
  const int header_text_size = header_height - 6;
  push_text(filename.chars, bounds.x + G.font_width, bounds.y - 3, false, d.modified() ? COLOR_ORANGE : COLOR_WHITE, header_text_size);
  push_square_quad({bounds.p, {bounds.w, -header_height}}, G.color_scheme.menu_background);

  // shadow
  if (bounds.x >= 7)
    render_shadow_left(bounds.x, bounds.y - header_height, bounds.h + header_height, 7);
  render_shadow_bottom(bounds.x, bounds.y, bounds.w, 4);

  render_quads();
  render_text();

  if (G.editing_pane == this)
    render_dropdown(this);

  // reflow and render subpanes
  if (subpanes.size) {
    float height_weight_sum = 0.0f;
    for (SubPane p : subpanes)
      height_weight_sum += p.pane->height_weight+1;

    float w = (float)total_width - bounds.w;
    float x = (float)orig_bounds.x + bounds.w;
    float y = (float)orig_bounds.y;

    for (SubPane p : subpanes) {
      float h = orig_bounds.h * (p.pane->height_weight+1) / height_weight_sum;
      if ((int)h <= 0)
        continue;
      p.pane->bounds = {(int)x, (int)y, (int)w, (int)h};
      p.pane->render();
      y += h;
    }
  }

  bounds.w = total_width;
  bounds.y -= header_height;
  bounds.h += header_height;
}

Pos Pane::buf2pixel(Pos p, int font_width, int line_height) const {
  p = buffer.data->to_visual_pos(p);
  p.x -= this->calc_left_visible_column();
  p.y -= this->calc_top_visible_row();
  p = char2pixel(p, font_width, line_height) + this->bounds.p;
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
  const int header_height = type == PANETYPE_EDIT ? G.line_height : 0;
  return ((this->bounds.h - header_height - 2*this->margin) / G.line_height) + 2;
}

void util_free(Pane &p) {
  util_free(p.buffer);

  // free subpanes
  for (SubPane sp : p.subpanes) {
    sp.pane->parent = 0;
    G.panes_to_remove += sp.pane;
  }
  p.subpanes.free_shallow();

  p = {};
}

#endif /* PANE_IMPL */