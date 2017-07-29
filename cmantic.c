/* CURRENT:
 *
 * fix scrolling
 *
 * TODO:
 *
 * handle tabs
 * fuzzy search actions
 *
 * load files
 * movement (word, parenthises, block)
 * actions (Delete, Yank, ...)
 * add folders
 */

#define _POSIX_C_SOURCE 200112L
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include "array.h"

/* @debug */
#if 0
  #define DEBUG(stmt) stmt
  static void* debug_malloc(unsigned long size, int line, const char* file) {
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
  #define DEBUG(stmt)
#endif

#define STATIC_ASSERT(expr, name) typedef char static_assert_##name[expr?1:-1]

static void term_reset_to_default_settings();
static void panic() {
  term_reset_to_default_settings();
  abort();
}

static int maxi(int a, int b) {
  return a < b ? b : a;
}
static int mini(int a, int b) {
  return b < a ? b : a;
}
/* a,b inclusive */
static int clampi(int x, int a, int b) {
  return x < a ? a : (b < x ? b : x);
}
#define arrcount(arr) (sizeof(arr)/sizeof(*arr))

typedef enum Key {
  KEY_UNKNOWN = 0,
  KEY_ESCAPE = '\x1b',
  KEY_RETURN = '\r',

  KEY_SPECIAL = 1000, /* so we can do c >= KEY_SPECIAL to check for special keys */
  KEY_ARROW_UP,
  KEY_ARROW_DOWN,
  KEY_ARROW_LEFT,
  KEY_ARROW_RIGHT,
  KEY_END,
  KEY_HOME
} Key;

static int key_is_special(Key key) {
  return key >= KEY_SPECIAL;
}

typedef struct Pos {
  int x,y;
} Pos;

typedef struct Rect {
  int x,y,w,h;
} Rect;

typedef struct Buffer {
  Array(Array(char)) lines;
  const char* filename;

  int pos_x, pos_y;

  #define GHOST_EOL -1
  int ghost_x; /* if going from a longer line to a shorter line, remember where we were before clamping to row length. a value of -1 means always go to end of line */

  int modified;
} Buffer;

typedef struct Pane {
  int gutter_width;
  Rect bounds;
  Buffer buffer;
} Pane;

typedef enum Mode {
  MODE_NORMAL,
  MODE_INSERT,
  MODE_MENU,
  MODE_COUNT
} Mode;

static struct State {
  /* some rendering memory */
  char *render_buffer, *render_buffer_prev, *row_buffer;
  int term_width, term_height;

  /* some editor state */
  Pane main_pane;
  Mode mode;
  Array(char) menu_buffer;

  /* some settings */
  struct termios orig_termios;
  int read_timeout_ms;
  int tab_width;
} G;

static void render_strn(int x0, int x1, int y, const char* str, int n) {
  char * const out = &G.render_buffer[G.term_width*y];

  while (n-- && x0 < x1) {
    if (*str == '\t') {
      int num_to_write = mini(G.tab_width, x1 - x0);
      memset(out + x0, ' ', num_to_write);
      x0 += num_to_write;
    }
    else
      out[x0++] = *str;
    ++str;
  }
}

static void render_str_v(int x0, int x1, int y, const char* fmt, va_list args) {
  int n = vsnprintf(G.row_buffer, G.term_width, fmt, args);
  render_strn(x0, x1, y, G.row_buffer, n);
}

static void render_str(int x0, int x1, int y, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  render_str_v(x0, x1, y, fmt, args);
  va_end(args);
}

static int to_visual_offset(Array(char) line, int x) {
  int result = 0;
  int i;

  if (!line) return result;

  for (i = 0; i < x; ++i) {
    ++result;
    if (line[i] == '\t')
      result += G.tab_width-1;
  }
  return result;
}

/* returns the logical index located visually at x */
static int from_visual_offset(Array(char) line, int x) {
  int visual = 0;
  int i;

  if (!line) return 0;

  for (i = 0; i < array_len(line); ++i) {
    ++visual;
    if (line[i] == '\t')
      visual += G.tab_width-1;
    if (visual > x)
      return i;
  }

  return i;

  return 0;
}

static int pane_calc_top_visible_row(Pane *pane) {
  return maxi(0, pane->buffer.pos_y - pane->bounds.h/2);
}

static int pane_calc_bottom_visible_row(Pane *pane) {
  return mini(pane_calc_top_visible_row(pane) + pane->bounds.h, array_len(pane->buffer.lines)) - 1;
}

static Pos pane_to_screen_pos(Pane *pane) {
  Pos result;
  Array(char) line = pane->buffer.lines[pane->buffer.pos_y];
  result.x = pane->bounds.x + to_visual_offset(line, pane->buffer.pos_x) + pane->gutter_width;
  /* try to center */
  result.y = pane->bounds.y + pane->buffer.pos_y - pane_calc_top_visible_row(pane);
  return result;
}

static void move_to(Buffer *b, int x, int y) {
  Array(char) old_line;
  Array(char) new_line;
  int old_x, dx;

  old_line = b->lines[b->pos_y];
  old_x = b->pos_x;
  dx = x - old_x;

  /* y is easy */
  b->pos_y = clampi(y, 0, array_len(b->lines) - 1);

  new_line = b->lines[b->pos_y];

  /* use ghost pos? */
  if (dx == 0) {
    if (b->ghost_x == GHOST_EOL)
      b->pos_x = array_len(new_line);
    else
      b->pos_x = from_visual_offset(new_line, b->ghost_x);
  }
  else {
    int old_vis_x;

    old_vis_x = to_visual_offset(old_line, old_x);

    b->pos_x = from_visual_offset(new_line, old_vis_x) + dx;
    b->pos_x = clampi(b->pos_x, 0, array_len(new_line));

    b->ghost_x = to_visual_offset(new_line, b->pos_x);
  }

}

static void move(Buffer *b, int x, int y) {
  move_to(b, b->pos_x + x, b->pos_y + y);
}

static void status_message_set(const char *fmt, ...) {
  int res;
  va_list args;

  va_start(args, fmt);
  res = vsnprintf(G.menu_buffer, array_len(G.menu_buffer), fmt, args);
  va_end(args);


  if (res >= array_len(G.menu_buffer)) {
    array_resize(G.menu_buffer, res+1);
    va_start(args, fmt);
    res = vsnprintf(G.menu_buffer, array_len(G.menu_buffer), fmt, args);
    va_end(args);
  }

  array_resize(G.menu_buffer, res);
}


/****** TERMINAL STUFF !!DO NOT USE OUTSIDE OF RENDERER!! ******/

static void term_clear_screen() {
  if (write(STDOUT_FILENO, "\x1b[2J", 4) != 4) panic();
}

static void term_hide_cursor() {
  if (write(STDOUT_FILENO, "\x1b[?25l", 6) != 6) panic();
}

static void term_show_cursor() {
  if (write(STDOUT_FILENO, "\x1b[?25h", 6) != 6) panic();
}

static void term_cursor_move(int x, int y) {
  char buf[32];
  int n = sprintf(buf, "\x1b[%i;%iH", y+1, x+1);
  if (write(STDOUT_FILENO, buf, n+1) != n+1) panic();
}

static void term_cursor_move_p(Pos p) {
  term_cursor_move(p.x, p.y);
}

static void term_inverse_video(int state) {
  static int inv = 0;
  int val;
  if (state == -1) inv = !inv;
  val = state == -1 ? inv : state;
  if (val) {
    if (write(STDOUT_FILENO, "\x1b[7m", 4) != 4) panic();
  } else {
    if (write(STDOUT_FILENO, "\x1b[m", 3) != 3) panic();
  }
}

static int term_get_dimensions(int *w, int *h) {
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
  tcgetattr(STDIN_FILENO, &G.orig_termios);
  new_termios = G.orig_termios;

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
}

static void term_reset_to_default_settings() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &G.orig_termios);
  term_clear_screen();
  term_show_cursor();
  term_cursor_move(0,0);
}

/* returns:
 * -1 on error
 * 0 on timedout
 * number of bytes read on success
 */
static int read_timeout(unsigned char *buf, int n, int ms) {
  #if 1
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
  else
  #endif
    return read(STDIN_FILENO, buf, n);
}

static int calc_num_chars(int i) {
  int result = 0;
  while (i > 0) {
    ++result;
    i /= 10;
  }
  return result;
}

/* grabs ownership of filename */
static int open_file(const char* filename, Buffer *buffer_out) {
  int result = -1;
  int num_lines = 0;
  FILE* f;

  f = fopen(filename, "r");
  if (!f) goto done;

  /* get line count */
  {
    /* TODO: optimize ? */
    while (!feof(f) && !ferror(f)) num_lines += fgetc(f) == '\n';
    if (ferror(f)) goto done;
    DEBUG(display_message("File has %i rows", num_lines));
  }

  {
    int row = 0;
    Buffer buffer = {0};
    buffer.filename = filename;
    array_resize(buffer.lines, num_lines);
    fseek(f, 0, SEEK_SET);
    for (row = 0; row < array_len(buffer.lines); ++row) {
      buffer.lines[row] = 0;
      while(1) {
        char c = fgetc(f);
        if (c == EOF) {
          if (ferror(f))
            goto done;
          goto last_line;
        }
        if (c == '\r') c = fgetc(f);
        if (c == '\n') break;
        array_push(buffer.lines[row], c);
      }
    }
    last_line:;
    assert(fgetc(f) == EOF);
    result = 0;
    *buffer_out = buffer;
  }

  done:
  /* TODO: free buffer memory if we failed */
  #if 0
  if (result) {
    if (result.buffer_out.lines) {
      int i;
      for (i = 0; i < result.buffer_out.num_lines; ++i)
        blocklist_free(result.buffer_out.lines[i]);
      free(buffer_out.lines);
    }
  }
  #endif
  if (f) fclose(f);
  return result;
}

static void render_clear(int x0, int x1, int y) {
  memset(&G.render_buffer[y*G.term_width + x0], ' ', x1 - x0);
}

/* x,y: screen bounds for pane */
static void render_pane(Pane *p) {
  Buffer *b;
  int i, i_end;
  int x0,x1,y;

  b = &p->buffer;
  x0 = p->bounds.x;
  x1 = p->bounds.x + p->bounds.w;
  y = p->bounds.y;

  /* recalc gutter width */
  p->gutter_width = calc_num_chars(array_len(b->lines)) + 1;

  /* calc where we start */
  i = pane_calc_top_visible_row(p);
  i_end = pane_calc_bottom_visible_row(p);
  for (; i <= i_end; ++i, ++y) {
    Array(char) line;

    if (i >= array_len(b->lines))
      render_clear(x0, x1, y);
    else {
      line = b->lines[i];

      /* gutter */
      render_str(x0, x0+p->gutter_width, y, "%i", i+1);

      if (!line) continue;

      /* text */
      /*render_clear(x0, x1, y);*/
      render_strn(x0 + p->gutter_width, x1, y, line, array_len(line));
    }
  }
}

static void save_buffer(Buffer *b) {
  FILE* f;
  int i;

  assert(b->filename);

  f = fopen(b->filename, "wb");
  if (!f) {
    status_message_set("Could not open file %s for writing: %s", b->filename, strerror(errno));
    return;
  }

  /* TODO: actually write to a temporary file, and when we have succeeded, rename it over the old file */
  for (i = 0; i < array_len(b->lines); ++i) {
    unsigned int num_to_write = array_len(b->lines[i]);
    if (fwrite(b->lines[i], 1, num_to_write, f) != num_to_write) {
      status_message_set("Failed to write to %s: %s", b->filename, strerror(errno));
      return;
    }
    /* TODO: windows endlines option */
    fputc('\n', f);
  }

  b->modified = 0;

  fclose(f);
}

void render_flush() {
  int i;
  /* basically write the G.render_buffer to the terminal */
  term_hide_cursor();
  term_cursor_move(0, 0);
  for (i = 0; i < G.term_height; ++i) {
    char *row = &G.render_buffer[i*G.term_width];
    char *oldrow = &G.render_buffer_prev[i*G.term_width];
    int rowsize = G.term_width;
    /* check if we actually need to re-render */
    int dirty = memcmp(row, oldrow, rowsize);

    if (!dirty) continue;

    term_cursor_move(0, i);
    if (write(STDOUT_FILENO, row, rowsize) != rowsize)
      panic();
  }
  term_show_cursor();

  term_cursor_move_p(pane_to_screen_pos(&G.main_pane));
  fflush(stdout);

  /* swap new and old buffer */
  {
    void* tmp = G.render_buffer_prev;
    G.render_buffer_prev = G.render_buffer;
    G.render_buffer = tmp;
  }
}


int main(int argc, const char** argv) {
  int err;
  (void) argc, (void)argv;

  if (argc < 2) {
    fprintf(stderr, "Usage: cedit <file>\n");
    return 1;
  }

  /* set up terminal */
  term_enable_raw_mode();

  /* read terminal dimensions */
  err = term_get_dimensions(&G.term_width, &G.term_height);
  if (err) panic();
  DEBUG(fprintf(stderr, "terminal dimensions: %i %i\n", G.term_width, G.term_height););

  /* set default settings */
  G.read_timeout_ms = 1;
  G.tab_width = 8;

  /* allocate memory */
  G.render_buffer = malloc(G.term_width * G.term_height);
  G.render_buffer_prev = malloc(G.term_width * G.term_height);
  memset(G.render_buffer, ' ', sizeof(char) * G.term_width * G.term_height);
  G.row_buffer = malloc(sizeof(char) * G.term_width);

  /* open a buffer */
  {
    const char* filename = argv[1];
    err = open_file(filename, &G.main_pane.buffer);
    if (err) status_message_set("Could not open file %s: %s\n", filename, strerror(errno));
    if (!err) status_message_set("loaded %s, %i lines", filename, array_len(G.main_pane.buffer.lines));
    G.main_pane.bounds.x = 0;
    G.main_pane.bounds.y = 0;
    G.main_pane.bounds.w = G.term_width - 1;
    G.main_pane.bounds.h = G.term_height - 2; /* leave room for menu */
  }

  while (1) {

    /* clear rendering buffer */
    memset(G.render_buffer, ' ', G.term_width * G.term_height * sizeof(char));

    /* draw document */
    render_pane(&G.main_pane);

    /* Draw menu/status bar */
    render_strn(0, G.term_width - 1, G.term_height - 1, G.menu_buffer, mini(G.term_width, array_len(G.menu_buffer)));

    /* render to screen */
    render_flush();

    /* get and process input */
    {
      int input;

      /* get input */
      {
        unsigned char c;
        int nread;
        while ((nread = read_timeout(&c, 1, 0)) != 1 && nread != 0)
          if (nread == -1 && errno != EAGAIN)
            goto done;

        /* escape sequence? */
        if (c == '\x1b') {
          /* read '[' */
          nread = read_timeout(&c, 1, G.read_timeout_ms);
          if (nread == -1 && errno != EAGAIN) goto done;
          if (nread == 0 || c != '[') {
            input = KEY_ESCAPE;
            goto input_done;
          }

          /* read actual character */
          nread = read_timeout(&c, 1, G.read_timeout_ms);
          if (nread == -1 && errno != EAGAIN) goto done;
          if (nread == 0) {
            input = KEY_UNKNOWN;
            goto input_done;
          }

          DEBUG(fprintf(stderr, "escape %c\n", c);)

          if (c >= '0' && c <= '9') {
            switch (c) {
              case '1': input = KEY_HOME; break;
              case '4': input = KEY_END; break;
            }

            nread = read_timeout(&c, 1, G.read_timeout_ms);
            if (nread == -1 && errno != EAGAIN) goto done;
            if (nread == 0 && c != '~') {
              input = KEY_UNKNOWN;
              goto input_done;
            }
          } else {
            switch (c) {
              case 'A': input = KEY_ARROW_UP; break;
              case 'B': input = KEY_ARROW_DOWN; break;
              case 'C': input = KEY_ARROW_RIGHT; break;
              case 'D': input = KEY_ARROW_LEFT; break;
              case 'F': input = KEY_END; break;
              case 'H': input = KEY_HOME; break;
              default: input = 0; break;
            }
          }
        }
        else {
          input = c;
          DEBUG(fprintf(stderr, "%c\n", c);)
          goto input_done;
        }
      }

      input_done:;

      /* process input */
      switch (G.mode) {
        case MODE_MENU:
          if (input == KEY_ESCAPE || input == KEY_RETURN) {
            #define IS_OPTION(str) strncmp(G.menu_buffer+1, str, array_len(G.menu_buffer)-1) == 0

            assert(G.menu_buffer[0] == ':');

            /* FIXME: this might buffer overrun */
            if (IS_OPTION("inv")) {
              term_inverse_video(-1);
              status_message_set("");
            }
            else if (IS_OPTION("s")) {
              save_buffer(&G.main_pane.buffer);
              /* TODO: write out absolute path, instead of relative */
              status_message_set("Wrote %i lines to %s", array_len(G.main_pane.buffer.lines), G.main_pane.buffer.filename);
            }
            else if (IS_OPTION("q"))
              goto done;
            G.mode = MODE_NORMAL;

            #undef IS_OPTION
          }
          else if (!key_is_special(input))
            array_push(G.menu_buffer, input);
          /* FIXME: if backspace so that menu_buffer is empty, exit menu mode */
          break;

        case MODE_NORMAL:
          switch (input) {
            case KEY_UNKNOWN:
              break;

            case KEY_ESCAPE:
            case 'q':
              if (G.main_pane.buffer.modified)
                status_message_set("You have unsaved changes. If you really want to exit, use :q");
              else
                goto done;
              break;

            case 'k':
            case KEY_ARROW_UP:
              move(&G.main_pane.buffer, 0, -1);
              break;

            case 'j':
            case KEY_ARROW_DOWN:
              move(&G.main_pane.buffer, 0, 1);
              break;

            case 'h':
            case KEY_ARROW_LEFT:
              move(&G.main_pane.buffer, -1, 0);
              break;

            case 'l':
            case KEY_ARROW_RIGHT:
              move(&G.main_pane.buffer, 1, 0);
              break;

            case 'L':
            case KEY_END:
              G.main_pane.buffer.ghost_x = -1;
              break;

            case 'H':
            case KEY_HOME:
              move_to(&G.main_pane.buffer, 0, G.main_pane.buffer.pos_y);
              break;

            case 'o':
              array_insert(G.main_pane.buffer.lines, G.main_pane.buffer.pos_y+1, 0);
              G.mode = MODE_INSERT;
              move(&G.main_pane.buffer, 0, 1);
              break;

            case 'i':
              G.mode = MODE_INSERT;
              break;

            case ':':
              status_message_set(":");
              G.mode = MODE_MENU;
              break;
          }
          break;

        case MODE_INSERT:
          /* TODO: should not set `modifier` if we just enter and exit insert mode */
          if (input == KEY_ESCAPE)
            G.mode = MODE_NORMAL;
          else if (!key_is_special(input)) {
            Buffer *b = &G.main_pane.buffer;

            G.main_pane.buffer.modified = 1;
            array_insert(b->lines[b->pos_y], G.main_pane.buffer.pos_x, input);
            move(&G.main_pane.buffer, 1, 0);
          }
          break;

        case MODE_COUNT:
          break;
      }

      move(&G.main_pane.buffer, 0, 0); /* update cursor in case anything happened (like changing modes) */
    }

  }

  done:
  term_reset_to_default_settings();
  return 0;
}
