#include "cmantic.cpp"

static int style_cmp(Style a, Style b) {
  return a.fcolor != b.fcolor || a.bcolor != b.bcolor || a.bold != b.bold || a.italic != b.italic || a.inverse != b.inverse;
}

enum Key {
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


struct Rect {
  int x,y,w,h;
};

Rect rect_create(int x, int y, int w, int h) {
  Rect result;
  result.x = x;
  result.y = y;
  result.w = w;
  result.h = h;
  return result;
}

enum Mode {
  MODE_NORMAL,
  MODE_INSERT,
  MODE_MENU,
  MODE_DELETE,
  MODE_GOTO,
  MODE_SEARCH,
  MODE_COUNT
};

typedef struct {
  unsigned int fcolor: 4;
  unsigned int bcolor: 4;
  unsigned int bold: 1;
  unsigned int italic: 1;
  unsigned int inverse: 1;
} Style;

typedef struct {
  char c[4]; /* should be enough to hold any utf8 char that we care about */
  Style style;
} Pixel;

enum TermColor {
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

struct TermPane {
  Pixel *screen_buffer;
  int gutter_width;
  Rect bounds;
  Buffer *buffer;
  Style style;
};

static struct TermState {
	/* some settings */
  struct termios orig_termios;
  int read_timeout_ms;
} Term;

static void screen_buffer_reset() {
  int i;
  for (i = 0; i < G.term_width * G.term_height; ++i) {
    char_to_wide(' ', G.screen_buffer[i].c);
    G.screen_buffer[i].style = G.default_style;
  }
}


static int read_timeout(unsigned char *buf, int n, int ms);

static int read_char(unsigned char *c) {
  int err;

  for (;;) {
    err = read_timeout(c, 1, 0);
    if (err == 1)
      return 0;
    if (err == -1 && errno != EAGAIN)
      return 1;
  }
}

static int process_input() {
  Key special_key = KEY_NONE;
  /* utf8 input */
  unsigned char input[4] = {0};

  /* get input */

  {
    int nread;

    if (read_char(input))
      return 1;

    if (IS_UTF8_HEAD(*input)) {
      unsigned char *c = input+1;
      unsigned char x;

      for (x = *input << 1; x&0x80; x <<= 1, ++c)
        if (read_char(c))
          return 1;
    }

    /* some special chars */
    switch (*input) {
      case '\t':
        special_key = KEY_TAB;
        break;
      case '\r':
        special_key = KEY_RETURN;
        break;
      case KEY_BACKSPACE:
        special_key = KEY_BACKSPACE;
        break;
      default:
        break;
    }

    if (*input != '\x1b')
      goto input_done;

    /* escape sequence? */
    /* read '[' */
    nread = read_timeout(input, 1, G.read_timeout_ms);
    if (nread == -1 && errno != EAGAIN)
      return 1;
    if (nread == 0 || *input != '[') {
      special_key = KEY_ESCAPE;
      goto input_done;
    }

    /* read actual character */
    nread = read_timeout(input, 1, G.read_timeout_ms);
    if (nread == -1 && errno != EAGAIN)
      return 1;
    if (nread == 0) {
      special_key = KEY_UNKNOWN;
      goto input_done;
    }

    IF_DEBUG(fprintf(stderr, "escape %c\n", *input);)

    if (*input >= '0' && *input <= '9') {
      switch (*input) {
        case '1': special_key = KEY_HOME; break;
        case '4': special_key = KEY_END; break;
      }

      nread = read_timeout(input, 1, G.read_timeout_ms);
      if (nread == -1 && errno != EAGAIN)
        return 1;
      if (nread == 0 && *input != '~') {
        special_key = KEY_UNKNOWN;
        goto input_done;
      }
    } else {
      switch (*input) {
        case 'A': special_key = KEY_ARROW_UP; break;
        case 'B': special_key = KEY_ARROW_DOWN; break;
        case 'C': special_key = KEY_ARROW_RIGHT; break;
        case 'D': special_key = KEY_ARROW_LEFT; break;
        case 'F': special_key = KEY_END; break;
        case 'H': special_key = KEY_HOME; break;
        default: special_key = KEY_UNKNOWN; break;
      }
    }
  }

  input_done:;

  /* process input */
  switch (G.mode) {
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
            return 1;
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

    case MODE_NORMAL:
      switch (special_key ? special_key : input[0]) {
        case KEY_UNKNOWN:
          break;

        case KEY_ESCAPE:
          if (G.main_pane.buffer->modified)
            status_message_set("You have unsaved changes. If you really want to exit, use :quit");
          else
            return 1;
          break;

        case 'q':
          /*jumplist_prev(G.main_pane.buffer);*/
          break;

        case 'w':
          /*jumplist_next(G.main_pane.buffer);*/
          break;

        case 'k':
        case KEY_ARROW_UP:
          buffer_move_y(G.main_pane.buffer, -1);
          break;

        case 'j':
        case KEY_ARROW_DOWN:
          buffer_move_y(G.main_pane.buffer, 1);
          break;

        case 'h':
        case KEY_ARROW_LEFT:
          buffer_move_x(G.main_pane.buffer, -1);
          break;

        case 'l':
        case KEY_ARROW_RIGHT:
          buffer_move_x(G.main_pane.buffer, 1);
          break;

        case 'L':
        case KEY_END:
          buffer_goto_endline(G.main_pane.buffer);
          break;

        case 'H':
        case KEY_HOME:
          buffer_goto_beginline(G.main_pane.buffer);
          break;

        case 'n': {
          int err;
          Pos prev;

          prev = G.main_pane.buffer->pos;

          err = buffer_find(G.main_pane.buffer, G.search_buffer[0], G.search_buffer[0].size, 0);
          if (err) {
            status_message_set("'%.*s' not found", G.search_buffer[0].size, G.search_buffer[0].data);
            break;
          }
          /*jumplist_push(prev);*/
        } break;

        case 'N': {
          int err;
          Pos prev;

          prev = G.main_pane.buffer->pos;

          err = buffer_find_r(G.main_pane.buffer, G.search_buffer[0], G.search_buffer[0].size, 0);
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
          buffer_insert_newline_below(G.main_pane.buffer);
          mode_insert();
          break;

        case 'i':
          mode_insert();
          break;

        case ':':
          mode_menu();
          break;

        case 'd':
          mode_delete();
          break;

        case 'J':
          buffer_move_y(G.main_pane.buffer, G.main_pane.bounds.h/2);
          break;

        case 'K':
          buffer_move_y(G.main_pane.buffer, -G.main_pane.bounds.h/2);
          break;

        case '}': {
          Buffer *b;
          int x,y,yend,t;
          Pos p;

          b = G.main_pane.buffer;
          x = b->pos.x;
          y = b->pos.y;
          yend = b->num_lines();

          for (;;) {
            t = token_read(b, &x, &y, yend, &p, 0);
            /* if we're already on one, ignore */
            if (t == '}' && p.x == b->pos.x && p.y == b->pos.y)
              continue;
            if (t == '{' || t == '}' || t == TOKEN_NULL)
              break;
          }

          if (t == '}')
            buffer_move_to(b, p.x, p.y);
          else if (t == '{') {
            int depth = 1;
            for (;;) {
              t = token_read(b, &x, &y, yend, &p, 0);
              if (t == TOKEN_NULL)
                break;
              if (t == '}')
                --depth;
              if (t == '{')
                ++depth;
              if (depth == 0) {
                buffer_move_to(b, p.x, p.y);
                break;
              }
            }
          }
        } break;

      }
      break;

    case MODE_GOTO:
      if (isdigit(input[0])) {
        G.goto_line_number *= 10;
        G.goto_line_number += input[0] - '0';
        buffer_move_to_y(G.main_pane.buffer, G.goto_line_number-1);
        status_message_set("goto %u", G.goto_line_number);
        break;
      }

      switch (special_key ? special_key : input[0]) {
        case 't':
          buffer_move_to(G.main_pane.buffer, 0, 0);
          break;
        case 'b':
          buffer_move_to(G.main_pane.buffer, 0, G.main_pane.buffer->num_lines()-1);
          break;
      }
      mode_normal(1);
      break;

    case MODE_SEARCH: {
      Array<char> search;

      G.bottom_pane.buffer = &G.search_buffer;
      if (special_key == KEY_ESCAPE)
        dropdown_autocomplete(&G.search_buffer);
      else
        insert_default(&G.bottom_pane, special_key, input);

      search = G.search_buffer.lines[0];
      G.search_failed = buffer_find(G.main_pane.buffer,
                               search,
                               search.size,
                               1);

      if (special_key == KEY_RETURN || special_key == KEY_ESCAPE) {
        if (G.dropdown_visible) {
          dropdown_get_first_line();
          G.search_failed = buffer_find(G.main_pane.buffer,
                                   search,
                                   search.size,
                                   1);
        }
        if (G.search_failed) {
          G.main_pane.buffer->pos = G.search_begin_pos;
          status_message_set("'%.*s' not found", G.search_buffer[0].size, G.search_buffer.lines[0].data);
          mode_normal(0);
        } else
          mode_normal(1);
      }

    } break;

    case MODE_DELETE:
      switch (special_key ? special_key : input[0]) {
        case 'd':
          buffer_delete_line(G.main_pane.buffer);
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

    case MODE_COUNT:
      break;
  }

  /* just in case */
  G.main_pane.buffer->pos.y = clampi(G.main_pane.buffer->pos.y, 0, G.main_pane.buffer->num_lines()-1);
  G.main_pane.buffer->pos.x = clampi(G.main_pane.buffer->pos.x, 0, G.main_pane.buffer->lines[G.main_pane.buffer->pos.y].size);
  return 0;
}


static void term_reset_to_default_settings();

static void render_flush() {
  Style style;

  array_resize(G.tmp_render_buffer, 0);

  style = G.default_style;
  term_apply_style_slow(&G.tmp_render_buffer, style);

  term_cursor_move(3, 3);

  for (int y = 0; y < G.term_height; ++y)
  for (int x = 0; x < G.term_width; ++x) {
    Pixel p = Term.screen_buffer[y*G.term_width + x];

    if (style_cmp(p.style, style)) {
      /* flush buffer */
      file_write(stdout, G.tmp_render_buffer.data, G.tmp_render_buffer.size);
      array_resize(G.tmp_render_buffer, 0);

      /* apply style */
      term_apply_style(&G.tmp_render_buffer, p.style, style);
      style = p.style;
    }
    array_push(G.tmp_render_buffer, p.c[0]);
    if (!IS_UTF8_TRAIL(p.c[1]))
      continue;
    array_push(G.tmp_render_buffer, p.c[1]);
    if (!IS_UTF8_TRAIL(p.c[2]))
      continue;
    array_push(G.tmp_render_buffer, p.c[2]);
    if (!IS_UTF8_TRAIL(p.c[3]))
      continue;
    array_push(G.tmp_render_buffer, p.c[3]);
  }
  /* flush buffer */
  file_write(stdout, G.tmp_render_buffer.data, G.tmp_render_buffer.size);
  G.tmp_render_buffer.size = 0;

  fflush(stdout);

  screen_buffer_reset();
}

static void terminal_check_resize() {
  int w,h;
  term_get_dimensions(&w, &h);
  if (w == G.term_width && h == G.term_height)
    return;

  G.term_width = w;
  G.term_height = h;
  free(Term.screen_buffer);
  Term.screen_buffer = (Pixel*)malloc(G.term_width * G.term_height * sizeof(*Term.screen_buffer));
  screen_buffer_reset();
  G.bottom_pane.bounds = rect_create(0, G.term_height-1, G.term_width-1, 1);
  G.main_pane.bounds = rect_create(0, 0, G.term_width-1, G.term_height-1);
}

static void render_strn(int x0, int x1, int y, const char *str, int n) {
  Pixel *row = &Term.screen_buffer[G.term_width*y];

  if (!str)
    return;

  for (const char *end = str+n; str != end && x0 < x1;) {
    if (*str == '\t') {
      int e = x0 + at_most(G.tab_width, x1 - x0);
      for (; x0 < e; ++x0)
        char_to_wide(' ', row[x0].c);
      ++str;
    }
    else {
      str += utf8_to_wide(str, end, row[x0].c);
      ++x0;
    }
  }
  /* pad with spaces to x1 */
  for (; x0 < x1; ++x0)
    char_to_wide(' ', row[x0].c);
}

static void render_str_v(int x0, int x1, int y, const char *fmt, va_list args) {
  int n;

  array_resize(G.tmp_render_buffer, x1-x0+1);
  n = vsnprintf(G.tmp_render_buffer, x1-x0+1, fmt, args);
  render_strn(x0, x1, y, G.tmp_render_buffer, n);
}

static void render_str(int x0, int x1, int y, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  render_str_v(x0, x1, y, fmt, args);
  va_end(args);
}


static int pane_calc_top_visible_row(TermPane *pane) {
  return at_least(0, pane->buffer->pos.y - pane->bounds.h/2);
}

static int pane_calc_left_visible_column(TermPane *pane) {
  return at_least(0, to_visual_offset(pane->buffer->lines[pane->buffer->pos.y], pane->buffer->pos.x) - (pane->bounds.w - pane->gutter_width - 3));
}

static Pos pane_to_screen_pos(TermPane *pane) {
  return pane_to_screen_pos_xy(pane, pane->buffer->pos.x, pane->buffer->pos.y);
}
static Pos pane_to_screen_pos_xy(TermPane *pane, int x, int y) {
  Pos result = {0};
  Array<char> line;

  line = pane->buffer->lines[y];
  result.x = pane->bounds.x + pane->gutter_width + to_visual_offset(line, x) - pane_calc_left_visible_column(pane);
  /* try to center */
  result.y = pane->bounds.y + y - pane_calc_top_visible_row(pane);
  return result;
}


/****** TERMINAL STUFF !!DO NOT USE OUTSIDE OF RENDERER!! ******/

typedef struct {const char *str; int len;} TermCommand;
static TermCommand term_fcolors[] = {
  {"\x1b[30m", 5}, /* COLOR_BLACK  */
  {"\x1b[31m", 5}, /* COLOR_RED    */
  {"\x1b[32m", 5}, /* COLOR_GREEN  */
  {"\x1b[33m", 5}, /* COLOR_YELLOW */
  {"\x1b[34m", 5}, /* COLOR_BLUE   */
  {"\x1b[35m", 5}, /* COLOR_MAGENTA*/
  {"\x1b[36m", 5}, /* COLOR_CYAN   */
  {"\x1b[37m", 5}, /* COLOR_WHITE  */
};
static TermCommand term_bcolors[] = {
  {"\x1b[40m", 5}, /* COLOR_BLACK  */
  {"\x1b[41m", 5}, /* COLOR_RED    */
  {"\x1b[42m", 5}, /* COLOR_GREEN  */
  {"\x1b[43m", 5}, /* COLOR_YELLOW */
  {"\x1b[44m", 5}, /* COLOR_BLUE   */
  {"\x1b[45m", 5}, /* COLOR_MAGENTA*/
  {"\x1b[46m", 5}, /* COLOR_CYAN   */
  {"\x1b[47m", 5}, /* COLOR_WHITE  */
};
STATIC_ASSERT(ARRAY_LEN(term_fcolors) == COLOR_COUNT, all_term_font_colors_defined);
STATIC_ASSERT(ARRAY_LEN(term_bcolors) == COLOR_COUNT, all_term_background_colors_defined);

static void term_bold(Array<char> *buf, int bold) {
  if (bold)
    array_push_str(buf, "\x1b[1m");
  else
    array_push_str(buf, "\x1b[22m");
}

static void term_inverse_video(Array<char> *buf, int inverse) {
  if (inverse)
    array_push_str(buf, "\x1b[7m");
  else
    array_push_str(buf, "\x1b[27m");
}

static void term_apply_style_slow(Array<char> *buf, Style style) {
  array_push_str(buf, term_fcolors[style.fcolor].str);
  array_push_str(buf, term_bcolors[style.bcolor].str);
  term_bold(buf, style.bold);
  term_inverse_video(buf, style.inverse);
}

static void term_apply_style(Array<char> *buf, Style style, Style old_style) {
  if (style.fcolor != old_style.fcolor)
    array_push_str(buf, term_fcolors[style.fcolor].str);
  if (style.bcolor != old_style.bcolor)
    array_push_str(buf, term_bcolors[style.bcolor].str);
  if (style.inverse != old_style.inverse)
    term_inverse_video(buf, style.inverse);
  if (style.bold != old_style.bold)
    term_bold(buf, style.bold);
}

static void render_dropdown(TermPane *active_pane) {
	fill_dropdown_buffer();

  /* position pane */
  Pos p = pane_to_screen_pos_xy(active_pane, G.dropdown_pos.x, G.dropdown_pos.y);
  G.dropdown_pane.bounds.x = p.x;
  G.dropdown_pane.bounds.y = p.y+1;
  G.dropdown_pane.bounds.h = at_most(num_best_matches, (G.term_height-1)/2);
  G.dropdown_pane.bounds.w = at_least(max_width, 30);
  int overflow = G.dropdown_pane.bounds.x + G.dropdown_pane.bounds.w - (G.term_width - 1);
  if (overflow > 0)
    G.dropdown_pane.bounds.x -= overflow;
  G.dropdown_backwards = G.dropdown_pane.bounds.y + G.dropdown_pane.bounds.h >= G.term_height;
  if (G.dropdown_backwards)
    G.dropdown_pane.bounds.y -= G.dropdown_pane.bounds.h+1;

  if (G.dropdown_visible && !buffer_isempty(&G.dropdown_buffer))
    render_pane(&G.dropdown_pane, 0, 0);
}

static void term_clear_screen() {
  if (file_write(stdout, "\x1b[2J", 4) != 4) panic();
}

static void term_hide_cursor() {
  if (file_write(stdout, "\x1b[?25l", 6) != 6) panic();
}

static void term_clear_line() {
  if (file_write(stdout, "\x1b[2K", 4) != 4) panic();
}

static void term_show_cursor() {
  if (file_write(stdout, "\x1b[?25h", 6) != 6) panic();
}

static void term_cursor_move(int x, int y) {
  char buf[32];
  unsigned int n = sprintf_s(buf, "\x1b[%i;%iH", y+1, x+1);
  if (file_write(stdout, buf, n+1) != n+1) panic();
}

static void term_reset_video() {
  if (file_write(stdout, "\x1b[0m", 4) != 4) panic();
}

static int term_get_dimensions(int *w, int *h) {
#ifdef OS_LINUX
  struct winsize ws;
  int res = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
  if (res == -1 || !ws.ws_col) {
    /* fall back to moving the cursor to the bottom right and querying its position */
    printf("%s", "\x1b[999C\x1b[999B\x1b[6n");
    fflush(stdout);
    res = scanf("\x1b[%d;%dR", h, w);
    if (res != 2 || !*w || !*h) return -1;
  } else {
    *w = ws.ws_col;
    *h = ws.ws_row;
  }
  return 0;
}

static void term_enable_raw_mode() {
  struct termios new_termios;
  tcgetattr(STDIN_FILENO, &Term.orig_termios);
  new_termios = Term.orig_termios;

  new_termios.c_iflag &= ~(IXON /* Disable Control-s and Control-q */
                           | ICRNL /* Let Control-m and Enter be read as carriage returns ('\r') */ );
  new_termios.c_oflag &= ~(OPOST /* Disable that '\n' output is turned into "\r\n" */ );
  new_termios.c_lflag &= ~(ECHO /* Don't echo the input */
                           | ICANON /* Get input byte by byte, instead of waiting for endline */
                           /*| ISIG */ /* Don't catch control-c */
                           | IEXTEN /* Disable Control-v */);

  /* These should already be set, but we set them anyway just in case */
  new_termios.c_cflag |= (CS8); /* set character to be 8 bits, should already be set */
  new_termios.c_iflag &= ~(INPCK /* No parity checking */
                           | ISTRIP /* don't strip the 8th bit of each input byte */);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios);
  term_hide_cursor();
}

static void term_reset_to_default_settings() {
  static int panicking = 0;
  /* to prevent recursion in term_* functions */
  if (panicking)
    return;
  panicking = 1;
  term_clear_screen();
  term_show_cursor();
  term_cursor_move(0,0);
  term_reset_video();
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &Term.orig_termios);
}

/* returns:
 * -1 on error
 * 0 on timedout
 * number of bytes read on success
 */
static int read_timeout(unsigned char *buf, int n, int ms) {
  fd_set files;
  struct timeval timeout = {0};
  int res;
  FD_ZERO(&files);
  FD_SET(STDIN_FILENO, &files);

  timeout.tv_sec = 0;
  timeout.tv_usec = ms*1000;

  res = select(STDIN_FILENO+1, &files, 0, 0, ms ? &timeout : 0);
  if (res == -1) return res; /* error */
  else if (res == 0) return res; /* timeout */
  else return read(STDIN_FILENO, buf, n);
}

static void render_clear(int x0, int x1, int y) {
  for (; x0 < x1; ++x0) {
    char_to_wide(' ', Term.screen_buffer[y*G.term_width + x0].c);
    Term.screen_buffer[y*G.term_width + x0].style = G.default_style;
  }
}

static void render_set_style_block(Style style, int x0, int x1, int y0, int y1) {
  int x,y;
  for (y = y0; y < y1; ++y)
    for (x = x0; x < x1; ++x)
      Term.screen_buffer[y*G.term_width + x].style = style;
}


static void render_set_style_text(Style style, Pos a, Pos b, Rect bounds) {
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
      Term.screen_buffer[y*G.term_width + x].style = style;
    return;
  }

  for (x = x0 + a.x, y = y0; x < x1 && y < y1; ++x)
    Term.screen_buffer[y*G.term_width + x].style = style;
  for (++y; y < y1; ++y)
    for (x = x0; x < x1; ++x)
      Term.screen_buffer[y*G.term_width + x].style = style;
  for (x = x0; x <= x0 + b.x; ++x)
    Term.screen_buffer[y*G.term_width + x].style = style;
}

static void render_cursor(TermPane *p) {
  Pos pos = pane_to_screen_pos(p);
  if (G.mode == MODE_INSERT) {
    Term.screen_buffer[pos.y*G.term_width + pos.x].style.bcolor = COLOR_RED;
    Term.screen_buffer[pos.y*G.term_width + pos.x].style.fcolor = COLOR_BLACK;
  }
  else
    Term.screen_buffer[pos.y*G.term_width + pos.x].style.inverse = 1;
}

/* x,y: screen bounds for pane */
static void render_pane(TermPane *p, int draw_gutter, int highlight) {
  Buffer *b;
  int buf_y, buf_y0, buf_y1;
  int x,y;
  int x0, x1, y0, y1;
  int buf_x0;
  Style style;

  /* calc bounds */
  b = p->buffer;
  x0 = clampi(p->bounds.x, 0, G.term_width-1);
  x1 = clampi(p->bounds.x + p->bounds.w, 0, G.term_width);
  y0 = clampi(p->bounds.y, 0, G.term_height);
  y1 = clampi(y0 + p->bounds.h, 0, G.term_height);
  buf_y0 = pane_calc_top_visible_row(p);
  buf_y1 = at_most(buf_y0 + y1-y0, b->lines.size);
  buf_x0 = pane_calc_left_visible_column(p);

  /* calc gutter width */
  if (draw_gutter)
    p->gutter_width = at_least(calc_num_chars(buf_y1) + 1, 4);
  else
    p->gutter_width = 0;

  /* set some styles */
  render_set_style_block(p->style, x0, x1, y0, y1);
  style = p->style;
  style.fcolor = COLOR_YELLOW;
  render_set_style_block(style, x0, x0+p->gutter_width-1, y0, y1);

  /* draw each line */
  for (y = y0, buf_y = buf_y0; y < y1; ++buf_y, ++y) {
    Array<char> line;

    line = b->lines[buf_y];

    /* beyond buffer ? */
    if (buf_y >= b->num_lines()) {
      render_clear(x0, x1, y);
      continue;
    }

    /* gutter */
    render_str(x0, x0+p->gutter_width, y, "%*i", p->gutter_width-1, buf_y+1);

    if (line.size <= buf_x0)
      continue;

    /* text */
    render_strn(x0 + p->gutter_width, x1, y, line + buf_x0, line.size - buf_x0);
  }

  /* color highlighting */
  if (highlight) {
    int token;

    y = buf_y0;
    x = 0;
    for (;;) {
      Pos prev, next;
      int do_render;

      token = token_read(b, &x, &y, buf_y1, &prev, &next);

      check_token:

      do_render = 0;

      if (token == TOKEN_NULL)
        break;
      switch (token) {

        case TOKEN_NUMBER:
          do_render = 1;
          style = G.number_style;
          break;

        case TOKEN_BLOCK_COMMENT_END:
          do_render = 1;
          style = G.comment_style;
          prev = pos_create(buf_x0, buf_y0);
          break;

        case TOKEN_BLOCK_COMMENT_BEGIN:
          do_render = 1;
          style = G.comment_style;
          next = pos_create(b->lines[buf_y1-1].size, buf_y1-1);
          break;

        case TOKEN_BLOCK_COMMENT:
          do_render = 1;
          style = G.comment_style;
          break;

        case TOKEN_STRING:
          do_render = 1;
          style = G.string_style;
          break;

        case TOKEN_STRING_BEGIN:
          do_render = 1;
          style = G.string_style;
          break;

        case TOKEN_IDENTIFIER: {
          Pos prev_tmp, next_tmp;
          int i, left_of_line;

          /* check for keywords */
          left_of_line = b->lines[prev.y].size - prev.x;
          for (i = 0; i < (int)ARRAY_LEN(keywords); ++i) {
            int keyword_len = strlen(keywords[i]);
            if (keyword_len <= left_of_line && strncmp(buffer_getstr_p(b, prev), keywords[i], keyword_len) == 0) {
              style = G.keyword_style;
              goto done;
            }
          }

          /* otherwise check for functions */
          /* should not be indented */
          if (isspace(buffer_getchar(b, 0, prev.y)))
            break;
          token = token_read(b, &x, &y, buf_y1, &prev_tmp, &next_tmp);
          if (token != '(') {
            prev = prev_tmp;
            next = next_tmp;
            goto check_token;
          }
          style = G.identifier_style;

          done:
          do_render = 1;
        } break;

        case '#':
          do_render = 1;
          style = G.keyword_style;
          break;


        default:
          break;
      }
      if (do_render) {
        Rect bounds;

        prev = to_visual_pos(b, prev);
        next = to_visual_pos(b, next);

        bounds = p->bounds;
        bounds.x += p->gutter_width;
        bounds.w -= p->gutter_width;

        prev.x -= buf_x0;
        prev.y -= buf_y0;
        next.x -= buf_x0;
        next.y -= buf_y0;

        render_set_style_text(style, prev, next, bounds);
      }

      if (y > buf_y1)
        break;
    }
  }
}

static void term_init() {
  /* read terminal dimensions */
  err = term_get_dimensions(&Term.term_width, &Term.term_height);
  if (err) panic();
  IF_DEBUG(fprintf(stderr, "terminal dimensions: %i %i\n", Term.term_width, Term.term_height););

  /* set default settings */
  Term.read_timeout_ms = 1;

  /* init screen buffer */
  Term.screen_buffer = (Pixel*)malloc(Term.term_width * Term.term_height * sizeof(*Term.screen_buffer));
  screen_buffer_reset();

  G.bottom_pane.style.bcolor = COLOR_MAGENTA;
  G.bottom_pane.style.fcolor = COLOR_BLACK;
  G.bottom_pane.bounds = rect_create(0, G.term_height-1, G.term_width-1, 1);
  G.bottom_pane.buffer = &G.message_buffer;

  G.main_pane.bounds = rect_create(0, 0, G.term_width-1, G.term_height-1);
}

int main(int argc, const char **argv) {
  int err;

  if (argc < 2) {
    fprintf(stderr, "Usage: cedit <file>\n");
    return 1;
  }

  IF_DEBUG(test());

  /* set up terminal */
  term_enable_raw_mode();
  atexit(term_reset_to_default_settings);

  /* initialize state */
  state_init();
  term_init();

  /* open a buffer */
  {
    const char *filename = argv[1];
    G.main_pane.buffer = (Buffer*)malloc(sizeof(*G.main_pane.buffer));
    err = file_open(filename, G.main_pane.buffer);
    if (err) {
      // status_message_set("Could not open file %s: %s\n", filename, cman_strerror(errno));
      fprintf(stderr, "Could not open file %s: %s\n", filename, cman_strerror(errno));
      exit(1);
    }
    if (!err)
      status_message_set("loaded %s, %i lines", filename, G.main_pane.buffer->num_lines());
  }

  while (1) {

    /* draw document */
    render_pane(&G.main_pane, 1, 1);

    /* draw menu/status bar */
    render_pane(&G.bottom_pane, 0, 0);

    /* draw dropdown ? */
    G.search_buffer.identifiers = G.main_pane.buffer->identifiers;
    if (G.mode == MODE_SEARCH || G.mode == MODE_MENU)
      render_dropdown(&G.bottom_pane);
    else
      render_dropdown(&G.main_pane);

    /* Draw cursors */
    render_cursor(&G.main_pane);
    if (G.mode == MODE_SEARCH || G.mode == MODE_MENU)
      render_cursor(&G.bottom_pane);

    /* render to screen */
    render_flush();

    /* get and process input */
    err = process_input();
    if (err)
      break;

    // terminal_check_resize();
  }

  return 0;
}