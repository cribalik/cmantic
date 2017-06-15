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

static void reset_termios_settings();
static void panic() {
  reset_termios_settings();
  abort();
}

int maxi(int a, int b) {
  return a < b ? b : a;
}
int mini(int a, int b) {
  return b < a ? b : a;
}
int clampi(int x, int a, int b) {
  return x < a ? a : (b < x ? b : x);
}
#define arrcount(arr) (sizeof(arr)/sizeof(*arr))

typedef enum {
  KEY_UNKNOWN = 0,
  KEY_ESCAPE = '\x1b',
  KEY_ARROW_UP = 1000,
  KEY_ARROW_DOWN,
  KEY_ARROW_LEFT,
  KEY_ARROW_RIGHT,
  KEY_END,
  KEY_HOME
} Key;

typedef struct {
  int x,y;
} Pos;

static Pos pos_create(int x, int y) {
  Pos result;
  result.x = x;
  result.y = y;
  return result;
}

typedef struct Buffer {
  int num_lines;
  char** lines;
  char* filename;
  int offset_x, offset_y;
} Buffer;

static struct State {
  /* some rendering memory */
  char *render_buffer, *render_buffer_prev, *row_buffer;
  int term_width, term_height;
  int pane_offset_x, pane_offset_y;
  int gutter_width;

  /* some editor state */
  Buffer current_buffer;
  int pos_x, pos_y;
  int ghost_x; /* if going from a longer line to a shorter line, remember where we were before clamping to row length. a value of -1 means always go to end of line */

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

static Pos buffer_to_screen_pos(int x, int y) {
  Pos result;
  result.x = x - G.pane_offset_x + G.gutter_width;
  result.y = y - G.pane_offset_y;
  return result;
}

static Pos screen_to_buffer_pos(int x, int y) {
  Pos result;
  result.x = x + G.pane_offset_x - G.gutter_width;
  result.y = y + G.pane_offset_y;
  return result;
}

static int line_visual_width(char *line) {
  int result = 0;
  if (!line)
    return result;
  for (; *line; ++line) {
    ++result;
    if (*line == '\t') result += G.tab_width;
  }
  return result;
}

static void render_line(int x, int y, char* line) {
  if (line) {
    int line_remaining = line_visual_width(line) - screen_to_buffer_pos(x, y).x;
    int screen_remaining = G.term_width - x;
    int num_to_write = mini(line_remaining, screen_remaining);
    if (num_to_write > 0)
      render_strn(x, y, line, num_to_write);
  }
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
  G.pos_y = clampi(G.pos_y, 0, G.current_buffer.num_lines-1);
  line_last_pos = maxi(line_visual_width(G.current_buffer.lines[G.pos_y])-1, 0);
  G.pos_x = clampi(G.pos_x, 0, line_last_pos);
  if (dx != 0) G.ghost_x = G.pos_x;
  if (dx == 0 && G.ghost_x == -1) G.pos_x = line_last_pos;

  /* TODO: then offset the window so that it contains the new positions */
  G.pane_offset_y = 0; /* TODO: scrolling */
  G.pane_offset_x = 0;

}

static void move(int x, int y) {
  move_to(G.pos_x + x, G.pos_y + y);
}


/****** TERMINAL STUFF !!DO NOT USE OUTSIDE OF RENDERER!! ******/

static void clear_screen() {
  if (write(STDOUT_FILENO, "\x1b[2J", 4) != 4) panic();
}

static void hide_cursor() {
  if (write(STDOUT_FILENO, "\x1b[?25l", 6) != 6) panic();
}

static void show_cursor() {
  if (write(STDOUT_FILENO, "\x1b[?25h", 6) != 6) panic();
}

static void cursor_move(int x, int y) {
  char buf[32];
  int n = sprintf(buf, "\x1b[%i;%iH", y+1, x+1);
  if (write(STDOUT_FILENO, buf, n+1) != n+1) panic();
}

static void cursor_move_p(Pos p) {
  cursor_move(p.x, p.y);
}

static void inverse_video(int state) {
  if (state) {
    if (write(STDOUT_FILENO, "\x1b[7m", 4) != 4) panic();
  } else {
    if (write(STDOUT_FILENO, "\x1b[m", 3) != 3) panic();
  }
}

static void render() {
  int r;
  /* check if we actually need to re-render */
  hide_cursor();
  cursor_move(0, 0);
  for (r = 0; r < G.term_height; ++r) {
    char *row = &G.render_buffer[r*G.term_width];
    char *oldrow = &G.render_buffer_prev[r*G.term_width];
    int rowsize = G.term_width;
    int dirty = memcmp(row, oldrow, rowsize);
    cursor_move(0, r);
    if (dirty) {
      if (write(STDOUT_FILENO, row, rowsize) != rowsize) panic();
    }
  }
  show_cursor();

  cursor_move_p(buffer_to_screen_pos(G.pos_x, G.pos_y));
  fflush(stdout);

  /* swap new and old buffer */
  {
    void* tmp = G.render_buffer_prev;
    G.render_buffer_prev = G.render_buffer;
    G.render_buffer = tmp;
  }
}

static void setup_termios_settings() {
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

static void reset_termios_settings() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &G.orig_termios);
  clear_screen();
  show_cursor();
  cursor_move(0,0);
}

static int read_terminal_dimensions(int *w, int *h) {
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

/* returns:
 * -1 on error
 * 0 on timedout
 * number of bytes read on success
 */
int read_timeout(unsigned char *buf, int n, int ms) {
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

int calc_num_chars(int i) {
  int result = 0;
  while (i > 0) {
    ++result;
    i /= 10;
  }
  return result;
}

int open_file(const char* filename, Buffer *buffer_out) {
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
    buffer.lines = calloc(buffer.num_lines, sizeof(*buffer.lines));
    fseek(f, 0, SEEK_SET);
    for (row = 0; row < buffer.num_lines; ++row) {
      int col;
      for (col = 0; !feof(f); ++col) {
        char c = fgetc(f);
        if (ferror(f)) {
          display_error("Error when reading from %s: %s", filename, strerror(errno));
          goto done;
        }
        if (c == '\n' || c == '\r') break;
        array_push(buffer.lines[row], c);
        if (feof(f)) goto last_line;
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

int main(int argc, const char** argv) {
  int err;
  (void) argc, (void)argv;

  /* set up terminal */
  setup_termios_settings();

  /* read terminal dimensions */
  err = read_terminal_dimensions(&G.term_width, &G.term_height);
  if (err) panic();
  DEBUG(fprintf(stderr, "terminal dimensions: %i %i\n", G.term_width, G.term_height););

  /* set default settings */
  G.read_timeout_ms = 1;
  G.tab_width = 10;

  /* allocate memory */
  G.render_buffer = malloc(sizeof(char) * G.term_width * G.term_height);
  G.render_buffer_prev = malloc(sizeof(char) * G.term_width * G.term_height);
  memset(G.render_buffer, ' ', sizeof(char) * G.term_width * G.term_height);
  G.row_buffer = malloc(sizeof(char) * G.term_width);

  /* open a buffer */
  {
    const char* filename = "cedit.c";
    open_file(filename, &G.current_buffer);
    if (!err) display_message("%s", filename);
  }

  while (1) {

    /* clear rendering buffer */
    memset(G.render_buffer, ' ', G.term_width * G.term_height * sizeof(char));

    /* render gutter */
    {
      int i;
      G.gutter_width = calc_num_chars(G.current_buffer.num_lines) + 1;
      for (i = 0; i < mini(G.term_height, G.current_buffer.num_lines); ++i) {
        render_str(0, i, "%i", i+1);
        render_line(G.gutter_width, i, G.current_buffer.lines[i]);
      }
    }

    /* render body */
    /*render_str(0, G.term_height - 1, "cursor: %i %i", G.pos_x, G.pos_y);*/
    {
      Pos sp = buffer_to_screen_pos(G.pos_x, G.pos_y);
      render_str(0, G.term_height-1, "x %i y %i - sx %i sy %i", G.pos_x, G.pos_y, sp.x, sp.y);
    }

    /* render to screen */
    render();

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

          fprintf(stderr, "escape %c\n", c);

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
          fprintf(stderr, "%c\n", c);
          goto input_done;
        }
      }
      input_done:;

      /* process input */
      {
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
        }
      }
    }

  }

  done:
  reset_termios_settings();
  return 0;
}
