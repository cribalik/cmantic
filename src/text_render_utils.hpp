#ifndef TEXT_RENDER_UTIL_HEADER
#define TEXT_RENDER_UTIL_HEADER

static int calc_num_chars(int i) {
  int result = 0;
  while (i > 0) {
    ++result;
    i /= 10;
  }
  return result;
}

struct TextCanvas {
  Utf8char *chars;
  Color *background_colors;
  Color *text_colors;
  int w, h;
  Color background;
  int margin;
  bool draw_shadow;
  Pos offset;
  int font_size;
  int font_width;
  int line_height;

  void render_strf(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, ...);
  void render(Pos offset);
  void render_str_v(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, va_list args);
  void render_str(Pos p, const Color *text_color, const Color *background_color, int xclip0, int xclip1, Slice s);
  void render_char(Pos p, Color text_color, const Color *background_color, char c);
  void fill(Color text, Color background);
  void fill(Rect r, Color background_color, Color text_color);
  void fill_background(Rect r, Color c);
  void fill_background(Range r, Color c);
  void fill_textcolor(Rect r, Color c);
  void fill_textcolor(Range range, Color c);
  void blend_textcolor(Range range, Color c);
  void blend_textcolor_additive(Range range, Color c);
  void invert_color(Pos p);
  void fill(Utf8char c);
  void resize(int w, int h, int font_size, int line_margin);
  void init(int w, int h, int font_size, int line_margin);
  bool _normalize_range(Pos &a, Pos &b);
  Area _normalize_rect(Rect &r);
  Pos char2pixel(int x, int y) {return Pos{x * font_width, y * line_height};}
  int char2pixelx(int x) {return x * font_width;}
  int char2pixely(int y) {return y * line_height;}
};

void util_free(TextCanvas &c) {
  delete [] c.chars;
  delete [] c.background_colors;
  delete [] c.text_colors;
}

static Pos char2pixel(int x, int y, int font_width, int line_height) {return Pos{x * font_width, y * line_height};}
static Pos char2pixel(Pos p, int font_width, int line_height) {return char2pixel(p.x, p.y, font_width, line_height);}

#endif /* TEXT_RENDER_UTIL_HEADER */








#ifdef TEXT_RENDER_UTIL_IMPL

void TextCanvas::init(int width, int height, int font_size, int line_margin) {
  (*this) = {};
  this->w = width;
  this->h = height;
  this->chars = new Utf8char[w*h]();
  this->background_colors = new Color[w*h]();
  this->text_colors = new Color[w*h]();
  this->font_size = font_size;
  this->font_width = graphics_get_font_advance(font_size);
  this->line_height = this->font_size + line_margin; 
}

void TextCanvas::resize(int width, int height, int font_size, int line_margin) {
  if (this->chars)
    delete [] this->chars;
  if (this->background_colors)
    delete [] this->background_colors;
  if (this->text_colors)
    delete [] this->text_colors;
  this->init(width, height, font_size, line_margin);
}

void TextCanvas::fill(Utf8char c) {
  for (int i = 0; i < w*h; ++i)
    this->chars[i] = c;
}

void TextCanvas::fill(Color text, Color backgrnd) {
  for (int i = 0; i < w*h; ++i)
    background_colors[i] = backgrnd;
  for (int i = 0; i < w*h; ++i)
    text_colors[i] = text;
}

void TextCanvas::invert_color(Pos p) {
  swap(text_colors[p.y*w + p.x], background_colors[p.y*w + p.x]);
}

#if 0
void TextCanvas:fit_range_to_bounds() {
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

bool TextCanvas::_normalize_range(Pos &a, Pos &b) {
  a -= offset;
  b -= offset;
  a.x = clamp(a.x, 0, w-1);
  b.x = clamp(b.x, 0, w-1);
  if (a.y < 0)
    a.y = 0, a.x = 0;
  if (b.y >= h)
    b.y = h-1, b.x = w-1;
  return a.y < h && b.y >= 0 && a.x >= 0;
}

void TextCanvas::blend_textcolor(Range range, Color c) {
  Pos a = range.a;
  Pos b = range.b;
  if (!_normalize_range(a, b))
    return;

  if (a.y == b.y) {
    for (int x = a.x; x < b.x; ++x)
      this->text_colors[a.y*this->w + x] = blend(this->text_colors[a.y*this->w + x], c);
    return;
  }

  int y = a.y;
  for (int x = a.x; x < w; ++x)
    this->text_colors[y*this->w + x] = blend(this->text_colors[y*this->w + x], c);
  for (++y; y < b.y; ++y)
    for (int x = 0; x < w; ++x)
      this->text_colors[y*this->w + x] = blend(this->text_colors[y*this->w + x], c);
  for (int x = 0; x < b.x; ++x)
    this->text_colors[y*this->w + x] = blend(this->text_colors[y*this->w + x], c);
}

void TextCanvas::blend_textcolor_additive(Range range, Color c) {
  Pos a = range.a;
  Pos b = range.b;
  if (!_normalize_range(a, b))
    return;

  if (a.y == b.y) {
    for (int x = a.x; x < b.x; ++x)
      this->text_colors[a.y*this->w + x] = blend_additive(this->text_colors[a.y*this->w + x], c);
    return;
  }

  int y = a.y;
  for (int x = a.x; x < w; ++x)
    this->text_colors[y*this->w + x] = blend_additive(this->text_colors[y*this->w + x], c);
  for (++y; y < b.y; ++y)
    for (int x = 0; x < w; ++x)
      this->text_colors[y*this->w + x] = blend_additive(this->text_colors[y*this->w + x], c);
  for (int x = 0; x < b.x; ++x)
    this->text_colors[y*this->w + x] = blend_additive(this->text_colors[y*this->w + x], c);
}

// fills a to b but only inside the bounds 
void TextCanvas::fill_textcolor(Range range, Color c) {
  Pos a = range.a;
  Pos b = range.b;
  if (!_normalize_range(a, b))
    return;

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
void TextCanvas::fill_textcolor(Rect r, Color c) {
  Area a = _normalize_rect(r);
  for (int y = a.y0; y < a.y1; ++y)
  for (int x = a.x0; x < a.x1; ++x)
    text_colors[y*this->w + x] = c;
}

// fills a to b but only inside the bounds 
void TextCanvas::fill_background(Range range, Color c) {
  Pos a = range.a;
  Pos b = range.b;
  if (!_normalize_range(a, b))
    return;

  if (a.y == b.y) {
    for (int x = a.x; x < b.x; ++x)
      this->background_colors[a.y*this->w + x] = c;
    return;
  }

  int y = a.y;
  for (int x = a.x; x < w; ++x)
    this->background_colors[y*this->w + x] = c;
  for (++y; y < b.y; ++y)
    for (int x = 0; x < w; ++x)
      this->background_colors[y*this->w + x] = c;
  for (int x = 0; x < b.x; ++x)
    this->background_colors[y*this->w + x] = c;
}

Area TextCanvas::_normalize_rect(Rect &r) {
  r.x -= offset.x;
  r.y -= offset.y;
  int x0 = r.x;
  int x1 = r.w == -1 ? w : r.x+r.w;
  int y0 = r.y;
  int y1 = r.h == -1 ? h : r.y+r.h;
  x0 = clamp(x0, 0, w);
  x1 = clamp(x1, 0, w);
  y0 = clamp(y0, 0, h);
  y1 = clamp(y1, 0, h);
  return Area{x0,y0,x1,y1};
}

// w,h: use -1 to say it goes to the end
void TextCanvas::fill_background(Rect r, Color c) {
  Area a = _normalize_rect(r);
  for (int y = a.y0; y < a.y1; ++y)
  for (int x = a.x0; x < a.x1; ++x)
    background_colors[y*this->w + x] = c;
}

void TextCanvas::fill(Rect r, Color background_color, Color text_color) {
  Area a = _normalize_rect(r);
  for (int y = a.y0; y < a.y1; ++y)
  for (int x = a.x0; x < a.x1; ++x)
    background_colors[y*this->w + x] = background_color,
    text_colors[y*this->w + x] = text_color;
}

void TextCanvas::render_char(Pos p, Color text_color, const Color *background_color, char c) {
  p.x -= offset.x;
  p.y -= offset.y;

  if (p.x > w || p.x < 0 || p.y > h || p.y < 0)
    return;

  chars[p.y*w + p.x] = c;
  text_colors[p.y*w + p.x] = text_color;
  if (background_color)
    background_colors[p.y*w + p.x] = *background_color;
}

void TextCanvas::render_str(Pos p, const Color *text_color, const Color *background_color, int xclip0, int xclip1, Slice s) {
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

void TextCanvas::render_str_v(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, va_list args) {
  if (p.x >= w)
    return;
  G.tmp_render_buffer.clear();
  G.tmp_render_buffer.appendv(fmt, args);
  render_str(p, text_color, background_color, x0, x1, G.tmp_render_buffer.slice);
}

void TextCanvas::render_strf(Pos p, const Color *text_color, const Color *background_color, int x0, int x1, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  this->render_str_v(p, text_color, background_color, x0, x1, fmt, args);
  va_end(args);
}

void TextCanvas::render(Pos pos) {
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
  if (draw_shadow)
    render_shadow_bottom_right(pos.x, pos.y, size.x, size.y);

  // render base background
  push_square_quad({pos, size}, background);
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
      push_square_quad({p0, p1-p0}, c);
      x0 = x1;
    }
  }

  // render text
  for (int row = 0; row < h; ++row) {
    G.tmp_render_buffer.clear();
    G.tmp_render_buffer.append(&chars[row*w], w);
    int y = char2pixely(row+1) + pos.y;
    for (int x0 = 0, x1 = 1; x1 <= w; ++x1) {
      if (x1 < w && text_colors[row*w + x1] == text_colors[row*w + x0])
        continue;
      int x = char2pixelx(x0) + pos.x;
      push_textn(G.tmp_render_buffer.chars + x0, x1 - x0, x, y, false, text_colors[row*w + x0], font_size);
      x0 = x1;
    }
  }
}

#endif /* TEXT_RENDER_UTIL_IMPL */
