/* TODO:
 *
 * status bar
 * menu
 * save and load files
 * movement (word, parenthises, block)
 * actions (Delete, Yank, ...)
 * fuzzy search actions
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
static int clampi(int x, int a, int b) {
  return x < a ? a : (b < x ? b : x);
}
#define arrcount(arr) (sizeof(arr)/sizeof(*arr))

typedef enum {
  KEY_UNKNOWN = 0,
  KEY_ESCAPE = '\x1b',

  KEY_SPECIAL = 1000, /* so we can do c >= KEY_SPECIAL to check for special keys */
  KEY_ARROW_UP,
  KEY_ARROW_DOWN,
  KEY_ARROW_LEFT,
  KEY_ARROW_RIGHT,
  KEY_END,
  KEY_HOME,
  KEY_RETURN
} Key;

static int key_is_special(Key key) {return key >= KEY_SPECIAL;}

typedef struct {
  int x,y;
} Pos;

static Pos pos_create(int x, int y) {
  Pos result;
  result.x = x;
  result.y = y;
  return result;
}

typedef struct {
  int num_lines;
  char** lines;
  char* filename;
  int offset_x, offset_y;
} Buffer;

typedef struct {
  int gutter_width;
  int x, y, w, h;
  Buffer buffer;
} Pane;

typedef enum {
  MODE_NORMAL,
  MODE_INSERT,
  MODE_MENU,
  MODE_COUNT
} Mode;

static struct State {
  /* some rendering memory */
  char *render_buffer, *render_buffer_prev, *row_buffer;
  int term_width, term_height;
  int gutter_width;

  /* some editor state */
  Pane main_pane;
  int pos_x, pos_y;
  int ghost_x; /* if going from a longer line to a shorter line, remember where we were before clamping to row length. a value of -1 means always go to end of line */
  Mode mode;
  char* menu_buffer;

  /* some settings */
  struct termios orig_termios;
  int read_timeout_ms;
  int tab_width;
} G;

static void render_strn(int x, int y, const char* str, int n) {
  if (str)
    memcpy(&G.render_buffer[G.term_width*y + x], str, n);
}

static void render_str_v(int x, int y, const char* fmt, va_list args) {
  int n = vsnprintf(G.row_buffer, G.term_width, fmt, args);
  render_strn(x, y, G.row_buffer, mini(G.term_width - x, n));
}

static void render_str(int x, int y, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  render_str_v(x, y, fmt, args);
  va_end(args);
}

static Pos pane_to_screen_pos(Pane *pane, int x, int y) {
  Pos result;
  result.x = x + pane->gutter_width;
  result.y = y;
  return result;
}

static int line_visual_width(char *line) {
  int result = 0;
  int num_chars = array_len(line);
  if (!line) return result;
  while (num_chars--) {
    ++result;
    if (*line == '\t') result += G.tab_width;
  }
  return result;
}

static void display_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  render_str_v(G.term_width/2, G.term_height/2, fmt, args);
  va_end(args);
}

static void display_message(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  render_str_v(G.term_width/2, G.term_height/2, fmt, args);
  va_end(args);
}

static void move_to(int x, int y) {
  int dx = x - G.pos_x;
  int line_last_pos;
  G.pos_x = x;
  G.pos_y = y;

  if (dx == 0) G.pos_x = G.ghost_x;

  /* clamp cursor to the buffer dimensions */
  G.pos_y = clampi(G.pos_y, 0, G.main_pane.buffer.num_lines-1);
  line_last_pos = line_visual_width(G.main_pane.buffer.lines[G.pos_y]) - 1;
  /* in insert mode, allow cursor to go one past the end of line */
  if (G.mode == MODE_INSERT)
    ++line_last_pos;
  line_last_pos = maxi(line_last_pos, 0);
  G.pos_x = clampi(G.pos_x, 0, line_last_pos);
  if (dx != 0) G.ghost_x = G.pos_x;
  if (dx == 0 && G.ghost_x == -1) G.pos_x = line_last_pos;

  /* TODO: then offset the window so that it contains the new positions */
}

static void move(int x, int y) {
  move_to(G.pos_x + x, G.pos_y + y);
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
  if (state) {
    if (write(STDOUT_FILENO, "\x1b[7m", 4) != 4) panic();
  } else {
    if (write(STDOUT_FILENO, "\x1b[m", 3) != 3) panic();
  }
}

static int term_get_dimensions(int *w, int *h) {
  struct winsize ws;
  int res = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
  if (res == -1 || !ws.ws_col) {
    int r;
    /* fall back to moving the cursor to the bottom right and querying its position */
    printf("%s", "\x1b[999C\x1b[999B\x1b[6n");
    fflush(stdout);
    r = scanf("\x1b[%d;%dR", h, w);
    if (r != 2 || !*w || !*h) return -1;
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

static int open_file(const char* filename, Buffer *buffer_out) {
  int result = -1;
  int num_lines = 1;
  FILE* f = 0;
  f = fopen(filename, "r");
  if (!f) {
    display_error("Could not open %s: %s", filename, strerror(errno));
    goto done;
  }

  /* get line count */
  {
    /* TODO: optimize ? */
    while (!feof(f) && !ferror(f)) num_lines += fgetc(f) == '\n';
    if (ferror(f)) {
      display_error("Could not open %s: %s", filename, strerror(errno));
      goto done;
    }
    DEBUG(display_message("File has %i rows", num_lines));
  }

  {
    int row = 0;
    Buffer buffer = {0};
    buffer.num_lines = num_lines;
    buffer.lines = 0;
    array_reserve(buffer.lines, buffer.num_lines);
    fseek(f, 0, SEEK_SET);
    for (row = 0; row < buffer.num_lines; ++row) {
      buffer.lines[row] = 0;
      while(1) {
        char c = fgetc(f);
        if (c == EOF) {
          if (ferror(f)) {
            display_error("Error when reading from %s: %s", filename, strerror(errno));
            goto done;
          }
          goto last_line;
        }
        if (c == '\r') fgetc(f);
        if (c == '\n') break;
        array_push(buffer.lines[row], c);
      }
    }
    last_line:;
    assert(feof(f));
    result = 0;
    *buffer_out = buffer;
  }

  done:
  /* TODO: free buffer_out if we failed */
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

static void render_pane(Pane *p, int x0, int x1, int y0, int y1) {
  Buffer *b;
  int line_num;
  b = &p->buffer;

  x1 = mini(x1, G.term_width-1);

  p->gutter_width = calc_num_chars(b->num_lines) + 1;
  for (line_num = 0; line_num < mini(y1 - y0, b->num_lines); ++line_num) {
    int num_to_write;
    int x,y;
    char *line;

    x = x0;
    y = y0+line_num;
    line = b->lines[line_num];

    /* draw gutter */
    render_str(x, y, "%i", line_num);

    if (!line) continue;
    x += p->gutter_width;

    num_to_write = mini(x1 - x, line_visual_width(line));
    if (num_to_write > 0)
      render_strn(x, y, line, num_to_write);
  }
}

static void status_message_set(const char *str) {
  int len = strlen(str);
  array_reserve(G.menu_buffer, len);
  memcpy(G.menu_buffer, str, len);
}


int main(int argc, const char** argv) {
  int err;
  (void) argc, (void)argv;

  /* set up terminal */
  term_enable_raw_mode();

  /* read terminal dimensions */
  err = term_get_dimensions(&G.term_width, &G.term_height);
  if (err) panic();
  DEBUG(fprintf(stderr, "terminal dimensions: %i %i\n", G.term_width, G.term_height););

  /* set default settings */
  G.read_timeout_ms = 1;
  G.tab_width = 10;

  /* allocate memory */
  G.render_buffer = malloc(G.term_width * G.term_height);
  G.render_buffer_prev = malloc(G.term_width * G.term_height);
  memset(G.render_buffer, ' ', sizeof(char) * G.term_width * G.term_height);
  G.row_buffer = malloc(sizeof(char) * G.term_width);

  /* open a buffer */
  {
    const char* filename = "cedit.c";
    open_file(filename, &G.main_pane.buffer);
    if (!err) display_message("%s", filename);
  }

  while (1) {

    /* clear rendering buffer */
    memset(G.render_buffer, ' ', G.term_width * G.term_height * sizeof(char));

    /* draw document */
    render_pane(&G.main_pane, 0, G.term_width - 1, 0, G.term_height - 2);

    /* TODO: draw status bar */

    /* render to screen */
    {
      int r;
      /* basically write the G.render_buffer to the terminal */
      term_hide_cursor();
      term_cursor_move(0, 0);
      for (r = 0; r < G.term_height; ++r) {
        char *row = &G.render_buffer[r*G.term_width];
        char *oldrow = &G.render_buffer_prev[r*G.term_width];
        int rowsize = G.term_width;
        /* check if we actually need to re-render */
        int dirty = memcmp(row, oldrow, rowsize);
        term_cursor_move(0, r);
        if (dirty) {
          if (write(STDOUT_FILENO, row, rowsize) != rowsize) panic();
        }
      }
      term_show_cursor();

      term_cursor_move_p(pane_to_screen_pos(&G.main_pane, G.pos_x, G.pos_y));
      fflush(stdout);

      /* swap new and old buffer */
      {
        void* tmp = G.render_buffer_prev;
        G.render_buffer_prev = G.render_buffer;
        G.render_buffer = tmp;
      }
    }

    /* get and process input */
    {
      int input;

      /* get input */
      {
        unsigned char c;
        int nread;
        while ((nread = read_timeout(&c, 1, 0)) != 1 && nread != 0) {
          if (nread == -1 && errno != EAGAIN) goto done;
        }

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
          if (input == KEY_ESCAPE)
            G.mode = MODE_NORMAL;
          else if (input == KEY_RETURN)
            status_message_set("Menu not yet supported");
          else if (!key_is_special(input))
            array_push(G.menu_buffer, input);
          break;

        case MODE_NORMAL:
          switch (input) {
            case KEY_UNKNOWN: break;

            case KEY_ESCAPE:
            case 'q': goto done;

            case 'k':
            case KEY_ARROW_UP: move(0, -1); break;
            case 'j':
            case KEY_ARROW_DOWN: move(0, 1); break;
            case 'h':
            case KEY_ARROW_LEFT: move(-1, 0); break;
            case 'l':
            case KEY_ARROW_RIGHT: move(1, 0); break;

            case 'L':
            case KEY_END: G.ghost_x = -1; move(0, 0); break;
            case 'H':
            case KEY_HOME: move_to(0, G.pos_y); break;

            case 'o':

              G.mode = MODE_INSERT;
              break;

            case 'i': G.mode = MODE_INSERT; break;

            case ':': G.mode = MODE_MENU; break;
          }
          break;

        case MODE_INSERT:
          if (input == KEY_ESCAPE) G.mode = MODE_NORMAL;
          else if (!key_is_special(input)) {
            Buffer *b = &G.main_pane.buffer;
            array_insert(b->lines[G.pos_y], G.pos_x, input);
            /*
            array_push(b->lines[G.pos_y], 0);
            memmove(b->lines[G.pos_y] + G.pos_x + 1, b->lines[G.pos_y] + G.pos_x, array_len(b->lines[G.pos_y])-G.pos_x-1);
            b->lines[G.pos_y][G.pos_x] = input;
            */
            move(1, 0);
          }
          break;

        case MODE_COUNT: break;
      }

      move(0, 0); /* update cursor in case anything happened (like changing modes) */
    }

  }

  done:
  term_reset_to_default_settings();
  return 0;
}
