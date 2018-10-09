#ifndef GRAPHICS_H
#define GRAPHICS_H

#ifdef _MSC_VER
  #ifndef OS_WINDOWS
    #define OS_WINDOWS 1
  #endif
#else
  #ifndef OS_LINUX
    #define OS_LINUX 1
  #endif
#endif

#include <SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/**************
 *   GENERAL  *
 *************/

struct Pos {
  int x;
  int y;

  bool operator!=(Pos p) {return x != p.x || y != p.y; }
  void operator+=(Pos p) {x += p.x; y += p.y; }
  void operator-=(Pos p) {x -= p.x; y -= p.y; }
  bool operator==(Pos p) {return x == p.x && y == p.y; }
  bool operator<(Pos p) {if (y == p.y) return x < p.x; return y < p.y; }
  bool operator<=(Pos p) {if (y == p.y) return x <= p.x; return y <= p.y; }
  bool operator>(Pos p) {return p < *this; }
  bool operator>=(Pos p) {return p <= *this; }
  Pos operator-(Pos p) {return Pos{x-p.x, y-p.y};}
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

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned short u16;
#define U16_MAX 0xFFFF
#define U32_MAX 0xFFFFFFFF

static int graphics_init(SDL_Window **window);


/***************
 *    COLOR    *
 **************/

static float to_srgb(float val);
static float to_linear(float val);
struct Color;
struct Color8;

struct Color {
  float r;
  float g;
  float b;
  float a;
};
static Color  hsl_to_linear_color(float h, float s, float l); // (h,s,v) in range ([0,360], [0,1], [0,1])
static Color  blend(Color back, Color front);
static Color  blend(Color back, Color front, float alpha);
static Color  blend_additive(Color back, Color front);
static Color  blend_additive(Color back, Color front, float alpha);
static Color8 to_srgb(Color c);
static Color  invert(Color c);
static bool   operator==(Color, Color);

struct Color8 {
  u8 r;
  u8 g;
  u8 b;
  u8 a;
};
static Color  to_linear(Color8);
static bool   operator==(Color8, Color8);

/**************
 *    TEXT    *
 *************/

static int graphics_text_init(const char *ttf_file, int default_font_size);
static void render_textatlas(int x, int y, int w, int h);
static void push_textn(const char *str, int n, int pos_x, int pos_y, bool center, Color color, int font_size = 0);
static void push_text(const char *str, int pos_x, int pos_y, bool center, Color color, int font_size = 0);
static void push_textf(int pos_x, int pos_y, bool center, Color color, const char *fmt, ...);
static void render_text();

/**************
 *    QUAD    *
 *************/

struct Quad {
  u16 x,y;
  Color color;
};
static Quad quad(int x, int y, Color c) {return {(u16)x, (u16)y, c};}
static int graphics_quad_init();
static void push_quad(Quad a, Quad b, Quad c, Quad d);
static void render_quads();
static void push_square_quad(Rect r, Color topleft, Color topright, Color bottomleft, Color bottomright);
static void push_square_quad(Rect r, Color c);

/**********************
 *    TexturedQuad    *
 *********************/

struct TexturedQuad {
  u16 x,y;
  u16 tx,ty;
};
struct TextureHandle {
  uint value;
};
static TexturedQuad tquad(int x, int y, int tx, int ty) {return {(u16)x, (u16)y, (u16)tx, (u16)ty};}
static int graphics_textured_quad_init();
static void push_tquad(TexturedQuad a, TexturedQuad b, TexturedQuad c, TexturedQuad d);
static void push_square_tquad(Rect pos, Rect tex);
static void render_textured_quads(TextureHandle texture);
static bool load_texture_from_file(const char *filename, TextureHandle *result);


#ifdef OS_WINDOWS
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN 1
  #endif
  #include <windows.h>
  #define GLAPIENTRY __stdcall
#else
  #define GLAPIENTRY
#endif /* OS */

#define GL_GLEXT_LEGACY
#include <GL/gl.h>

#define GL_GLEXT_VERSION 20171125

#include <stddef.h>

/* GL defines */

  typedef char GLchar;
  typedef ptrdiff_t GLsizeiptr;
  typedef ptrdiff_t GLintptr;

  #define GL_DYNAMIC_DRAW                   0x88E8
  #define GL_ARRAY_BUFFER                   0x8892
  #define GL_LINK_STATUS                    0x8B82
  #define GL_INVALID_FRAMEBUFFER_OPERATION  0x0506
  #define GL_VERTEX_SHADER                  0x8B31
  #define GL_COMPILE_STATUS                 0x8B81
  #define GL_FRAGMENT_SHADER                0x8B30
  #define GL_MIRRORED_REPEAT                0x8370

  #define GL_TEXTURE0                       0x84C0
  #define GL_TEXTURE1                       0x84C1
  #define GL_TEXTURE2                       0x84C2
  #define GL_TEXTURE3                       0x84C3
  #define GL_TEXTURE4                       0x84C4
  #define GL_TEXTURE5                       0x84C5
  #define GL_TEXTURE6                       0x84C6
  #define GL_TEXTURE7                       0x84C7
  #define GL_TEXTURE8                       0x84C8
  #define GL_TEXTURE9                       0x84C9
  #define GL_TEXTURE10                      0x84CA
  #define GL_TEXTURE11                      0x84CB
  #define GL_TEXTURE12                      0x84CC
  #define GL_TEXTURE13                      0x84CD
  #define GL_TEXTURE14                      0x84CE
  #define GL_TEXTURE15                      0x84CF
  #define GL_TEXTURE16                      0x84D0
  #define GL_TEXTURE17                      0x84D1
  #define GL_TEXTURE18                      0x84D2
  #define GL_TEXTURE19                      0x84D3
  #define GL_TEXTURE20                      0x84D4
  #define GL_TEXTURE21                      0x84D5
  #define GL_TEXTURE22                      0x84D6
  #define GL_TEXTURE23                      0x84D7
  #define GL_TEXTURE24                      0x84D8
  #define GL_TEXTURE25                      0x84D9
  #define GL_TEXTURE26                      0x84DA
  #define GL_TEXTURE27                      0x84DB
  #define GL_TEXTURE28                      0x84DC
  #define GL_TEXTURE29                      0x84DD
  #define GL_TEXTURE30                      0x84DE
  #define GL_TEXTURE31                      0x84DF

  static void (GLAPIENTRY *glEnableVertexAttribArray) (GLuint index);
  static void (GLAPIENTRY *glVertexAttribPointer) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
  static void (GLAPIENTRY *glVertexAttribIPointer) (GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer);
  static void (GLAPIENTRY *glUseProgram) (GLuint program);
  static void (GLAPIENTRY *glUniform1i) (GLint location, GLint v0);
  static GLint (GLAPIENTRY *glGetUniformLocation) (GLuint program, const GLchar *name);
  static GLuint (GLAPIENTRY *glCreateShader) (GLenum type);
  static void (GLAPIENTRY *glShaderSource) (GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
  static void (GLAPIENTRY *glCompileShader) (GLuint shader);
  static void (GLAPIENTRY *glGetShaderiv) (GLuint shader, GLenum pname, GLint *params);
  static void (GLAPIENTRY *glGetShaderInfoLog) (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
  static GLuint (GLAPIENTRY *glCreateProgram) (void);
  static void (GLAPIENTRY *glAttachShader) (GLuint program, GLuint shader);
  static void (GLAPIENTRY *glLinkProgram) (GLuint program);
  static void (GLAPIENTRY *glGetProgramiv) (GLuint program, GLenum pname, GLint *params);
  static void (GLAPIENTRY *glGetProgramInfoLog) (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
  static void (GLAPIENTRY *glDeleteShader) (GLuint shader);
  static void (GLAPIENTRY *glGenerateMipmap) (GLenum target);
  static void (GLAPIENTRY *glGenVertexArrays) (GLsizei n, GLuint *arrays);
  static void (GLAPIENTRY *glBindVertexArray) (GLuint array);
  static void (GLAPIENTRY *glGenBuffers) (GLsizei n, GLuint *buffers);
  static void (GLAPIENTRY *glBindBuffer) (GLenum target, GLuint buffer);
  static void (GLAPIENTRY *glBufferData) (GLenum target, GLsizeiptr size, const void *data, GLenum usage);
  static void (GLAPIENTRY *glUniform1f) (GLint location, GLfloat v0);
  static void (GLAPIENTRY *glUniform2f) (GLint location, GLfloat v0, GLfloat v1);
  static void (GLAPIENTRY *glUniform3f) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
  static void (GLAPIENTRY *glUniform2i) (GLint location, GLint v0, GLint v1);
  static void (GLAPIENTRY *glBufferSubData) (GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
  static void (GLAPIENTRY *glUniform3fv) (GLint location, GLsizei count, const GLfloat *value);
  #ifdef OS_WINDOWS
  static void (GLAPIENTRY *glActiveTexture) (GLenum);
  #endif


#ifndef gl_ok_or_die
#define gl_ok_or_die _gl_ok_or_die(__FILE__, __LINE__)
static void _gl_ok_or_die(const char* file, int line) {
  GLenum error_code;
  const char* error;

  error_code = glGetError();

  if (error_code == GL_NO_ERROR) return;

  switch (error_code) {
    case GL_INVALID_ENUM:                  error = "INVALID_ENUM"; break;
    case GL_INVALID_VALUE:                 error = "INVALID_VALUE"; break;
    case GL_INVALID_OPERATION:             error = "INVALID_OPERATION"; break;
    case GL_STACK_OVERFLOW:                error = "STACK_OVERFLOW"; break;
    case GL_STACK_UNDERFLOW:               error = "STACK_UNDERFLOW"; break;
    case GL_OUT_OF_MEMORY:                 error = "OUT_OF_MEMORY"; break;
    case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
    default: error = "unknown error";
  };
  fprintf(stderr, "GL error at %s:%u: (%u) %s\n", file, line, error_code, error);
  abort();
}
#endif

struct Texture {
  GLuint id;
  int w,h;
};

struct TextVertex {
  u16 x,y;
  u16 tx,ty;
  Color color;
};

struct Glyph {
  unsigned short x0, y0, x1, y1; /* Position in image */
  float offset_x, offset_y, advance; /* Glyph offset info */
};

static struct GraphicsTextState {
  static const int FIRST_CHAR = 32;
  static const int LAST_CHAR = 128;
  bool initialized;
  String font_file;
  int font_size;
  struct FontData {
    int font_size;
    Texture atlas;
    Glyph glyphs[GraphicsTextState::LAST_CHAR - GraphicsTextState::FIRST_CHAR];
    Array<TextVertex> vertices;
  };
  Array<FontData> fonts;
  GLuint vertex_array, vertex_buffer;
  GLuint shader;
} graphics_text_state;

static void graphics_set_font_options(const char *font_file = 0, int font_size = 0) {
  if (font_file) {
    util_free(graphics_text_state.font_file);
    graphics_text_state.font_file = String::create(font_file);
  }
  if (font_size)
    graphics_text_state.font_size = font_size;
}

// only makes sense for mono fonts
static bool get_or_create_fontdata(Slice ttf_file, int font_size, GraphicsTextState::FontData **result);
static int graphics_get_font_advance(int font_size) {
  for (auto &d : graphics_text_state.fonts)
    if (d.font_size == font_size)
      return (int)d.glyphs[0].advance;
  GraphicsTextState::FontData *d;
  get_or_create_fontdata(graphics_text_state.font_file.slice, font_size, &d);
  return (int)d->glyphs[0].advance;
}

static struct {
  SDL_Window *window;
  bool initialized;
  int window_width, window_height;
} graphics_state;

// return 0 on success
static int graphics_init(SDL_Window **window) {
  if (SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "Failed to init SDL\n");
    return 1;
  }

  if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE)) {
    fprintf(stderr, "Failed to set core profile: %s\n", SDL_GetError());
    return 1;
  }
  if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3)) {
    fprintf(stderr, "Failed to set minor version to 3: %s\n", SDL_GetError());
    return 1;
  }
  if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3)) {
    fprintf(stderr, "Failed to set major version to 3: %s\n", SDL_GetError());
    return 1;
  }

  // #ifdef DEBUG
  graphics_state.window = *window = SDL_CreateWindow("cmantic", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  // #else
  // TODO: How de we make this not change the resolution on linux?
  // SDL_Window *window = SDL_CreateWindow("cmantic", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
  // #endif
  if (!*window) {
    fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
    return 1;
  }

  int w,h;
  SDL_GetWindowSize(*window, &w, &h);
  graphics_state.window_width = w;
  graphics_state.window_height = h;

  if (!SDL_GL_CreateContext(*window)) {
    fprintf(stderr, "Failed to create gl context: %s\n", SDL_GetError());
    return 1;
  }
  gl_ok_or_die;

  SDL_GL_SetSwapInterval(1);

  // load gl functions
  *(void**) (&glEnableVertexAttribArray) = (void*)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
  if (!glEnableVertexAttribArray) { fprintf(stderr, "Couldn't load gl function \"glEnableVertexAttribArray\"\n"); return 1;}
  *(void**) (&glVertexAttribPointer) = (void*)SDL_GL_GetProcAddress("glVertexAttribPointer");
  if (!glVertexAttribPointer) { fprintf(stderr, "Couldn't load gl function \"glVertexAttribPointer\"\n"); return 1;}
  *(void**) (&glVertexAttribIPointer) = (void*)SDL_GL_GetProcAddress("glVertexAttribIPointer");
  if (!glVertexAttribIPointer) { fprintf(stderr, "Couldn't load gl function \"glVertexAttribIPointer\"\n"); return 1;}
  *(void**) (&glUseProgram) = (void*)SDL_GL_GetProcAddress("glUseProgram");
  if (!glUseProgram) { fprintf(stderr, "Couldn't load gl function \"glUseProgram\"\n"); return 1;}
  *(void**) (&glUniform1i) = (void*)SDL_GL_GetProcAddress("glUniform1i");
  if (!glUniform1i) { fprintf(stderr, "Couldn't load gl function \"glUniform1i\"\n"); return 1;}
  *(void**) (&glGetUniformLocation) = (void*)SDL_GL_GetProcAddress("glGetUniformLocation");
  if (!glGetUniformLocation) { fprintf(stderr, "Couldn't load gl function \"glGetUniformLocation\"\n"); return 1;}
  *(void**) (&glCreateShader) = (void*)SDL_GL_GetProcAddress("glCreateShader");
  if (!glCreateShader) { fprintf(stderr, "Couldn't load gl function \"glCreateShader\"\n"); return 1;}
  *(void**) (&glShaderSource) = (void*)SDL_GL_GetProcAddress("glShaderSource");
  if (!glShaderSource) { fprintf(stderr, "Couldn't load gl function \"glShaderSource\"\n"); return 1;}
  *(void**) (&glCompileShader) = (void*)SDL_GL_GetProcAddress("glCompileShader");
  if (!glCompileShader) { fprintf(stderr, "Couldn't load gl function \"glCompileShader\"\n"); return 1;}
  *(void**) (&glGetShaderiv) = (void*)SDL_GL_GetProcAddress("glGetShaderiv");
  if (!glGetShaderiv) { fprintf(stderr, "Couldn't load gl function \"glGetShaderiv\"\n"); return 1;}
  *(void**) (&glGetShaderInfoLog) = (void*)SDL_GL_GetProcAddress("glGetShaderInfoLog");
  if (!glGetShaderInfoLog) { fprintf(stderr, "Couldn't load gl function \"glGetShaderInfoLog\"\n"); return 1;}
  *(void**) (&glCreateProgram) = (void*)SDL_GL_GetProcAddress("glCreateProgram");
  if (!glCreateProgram) { fprintf(stderr, "Couldn't load gl function \"glCreateProgram\"\n"); return 1;}
  *(void**) (&glAttachShader) = (void*)SDL_GL_GetProcAddress("glAttachShader");
  if (!glAttachShader) { fprintf(stderr, "Couldn't load gl function \"glAttachShader\"\n"); return 1;}
  *(void**) (&glLinkProgram) = (void*)SDL_GL_GetProcAddress("glLinkProgram");
  if (!glLinkProgram) { fprintf(stderr, "Couldn't load gl function \"glLinkProgram\"\n"); return 1;}
  *(void**) (&glGetProgramiv) = (void*)SDL_GL_GetProcAddress("glGetProgramiv");
  if (!glGetProgramiv) { fprintf(stderr, "Couldn't load gl function \"glGetProgramiv\"\n"); return 1;}
  *(void**) (&glGetProgramInfoLog) = (void*)SDL_GL_GetProcAddress("glGetProgramInfoLog");
  if (!glGetProgramInfoLog) { fprintf(stderr, "Couldn't load gl function \"glGetProgramInfoLog\"\n"); return 1;}
  *(void**) (&glDeleteShader) = (void*)SDL_GL_GetProcAddress("glDeleteShader");
  if (!glDeleteShader) { fprintf(stderr, "Couldn't load gl function \"glDeleteShader\"\n"); return 1;}
  *(void**) (&glGenerateMipmap) = (void*)SDL_GL_GetProcAddress("glGenerateMipmap");
  if (!glGenerateMipmap) { fprintf(stderr, "Couldn't load gl function \"glGenerateMipmap\"\n"); return 1;}
  *(void**) (&glGenVertexArrays) = (void*)SDL_GL_GetProcAddress("glGenVertexArrays");
  if (!glGenVertexArrays) { fprintf(stderr, "Couldn't load gl function \"glGenVertexArrays\"\n"); return 1;}
  *(void**) (&glBindVertexArray) = (void*)SDL_GL_GetProcAddress("glBindVertexArray");
  if (!glBindVertexArray) { fprintf(stderr, "Couldn't load gl function \"glBindVertexArray\"\n"); return 1;}
  *(void**) (&glGenBuffers) = (void*)SDL_GL_GetProcAddress("glGenBuffers");
  if (!glGenBuffers) { fprintf(stderr, "Couldn't load gl function \"glGenBuffers\"\n"); return 1;}
  *(void**) (&glBindBuffer) = (void*)SDL_GL_GetProcAddress("glBindBuffer");
  if (!glBindBuffer) { fprintf(stderr, "Couldn't load gl function \"glBindBuffer\"\n"); return 1;}
  *(void**) (&glBufferData) = (void*)SDL_GL_GetProcAddress("glBufferData");
  if (!glBufferData) { fprintf(stderr, "Couldn't load gl function \"glBufferData\"\n"); return 1;}
  *(void**) (&glUniform1f) = (void*)SDL_GL_GetProcAddress("glUniform1f");
  if (!glUniform1f) { fprintf(stderr, "Couldn't load gl function \"glUniform1f\"\n"); return 1;}
  *(void**) (&glUniform2f) = (void*)SDL_GL_GetProcAddress("glUniform2f");
  if (!glUniform2f) { fprintf(stderr, "Couldn't load gl function \"glUniform2f\"\n"); return 1;}
  *(void**) (&glUniform3f) = (void*)SDL_GL_GetProcAddress("glUniform3f");
  if (!glUniform3f) { fprintf(stderr, "Couldn't load gl function \"glUniform3f\"\n"); return 1;}
  *(void**) (&glUniform2i) = (void*)SDL_GL_GetProcAddress("glUniform2i");
  if (!glUniform2i) { fprintf(stderr, "Couldn't load gl function \"glUniform2i\"\n"); return 1;}
  *(void**) (&glBufferSubData) = (void*)SDL_GL_GetProcAddress("glBufferSubData");
  if (!glBufferSubData) { fprintf(stderr, "Couldn't load gl function \"glBufferSubData\"\n"); return 1;}
  *(void**) (&glUniform3fv) = (void*)SDL_GL_GetProcAddress("glUniform3fv");
  if (!glUniform3fv) { fprintf(stderr, "Couldn't load gl function \"glUniform3fv\"\n"); return 1;}
  #ifdef OS_WINDOWS
  *(void**) (&glActiveTexture) = (void*)SDL_GL_GetProcAddress("glActiveTexture");
  if (!glActiveTexture) { fprintf(stderr, "Couldn't load gl function \"glActiveTexture\"\n"); return 1;}
  #endif

  glViewport(0, 0, w, h);

  gl_ok_or_die;

  graphics_state.initialized = true;
  return 0;
}

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

static const char* graphics_strerror(int err) {
#ifdef OS_WINDOWS
  static char buf[128];
  strerror_s(buf, sizeof(buf), err);
  return buf;
#else
  return strerror(err);
#endif
}

static bool load_font_from_file(Slice filename, int height, GraphicsTextState::FontData **result) {
  GraphicsTextState::FontData font_data = {};
  font_data.font_size = height;

  // Create font texture and read in font from file
  const int w = 512, h = 512;
  glGenTextures(1, &font_data.atlas.id);
  glBindTexture(GL_TEXTURE_2D, font_data.atlas.id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
  font_data.atlas.w = w;
  font_data.atlas.h = h;
  gl_ok_or_die;

  glGenerateMipmap(GL_TEXTURE_2D);
  gl_ok_or_die;

  Array<u8> data;
  if (!File::get_contents(filename.chars, &data)) {
    log_err("Failed to open ttf file {}: %s\n", (Slice)filename, graphics_strerror(errno));
    return false;
  }

  u8 *bitmap = (u8*)malloc(w * h);
  if (!bitmap) {
    log_err("Failed to allocate memory for font\n");
    return false;
  }

  if (stbtt_BakeFontBitmap(data.items, 0, (float)height, bitmap, w, h, GraphicsTextState::FIRST_CHAR, GraphicsTextState::LAST_CHAR - GraphicsTextState::FIRST_CHAR, (stbtt_bakedchar*) font_data.glyphs) <= 0) {
    log_err("Failed to bake font\n");
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, font_data.atlas.id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);

  data.free_shallow();
  free(bitmap);
  graphics_text_state.fonts += font_data;
  *result = &graphics_text_state.fonts.last();
  return true;
}

static bool get_or_create_fontdata(Slice ttf_file, int font_size, GraphicsTextState::FontData **result) {
  for (GraphicsTextState::FontData &d : graphics_text_state.fonts) {
    if (d.font_size == font_size) {
      *result = &d;
      return true;
    }
  }
  return load_font_from_file(ttf_file, font_size, result);
}

static FILE* graphics_fopen(const char *filename, const char *mode) {
#ifdef OS_WINDOWS
  FILE *f;
  if (!filename) {
    fprintf(stderr, "graphics::graphics_fopen: Please define a filename\n");
    return 0;
  }
  if (fopen_s(&f, filename, mode))
    return 0;
  return f;
#else
  return fopen(filename, mode);
#endif
}

static GLuint graphics_compile_shader(const char *vertex_shader_src, const char *fragment_shader_src) {
  GLuint result = 0;
  const char * shader_src_list;
  char info_log[512];
  GLint success;
  GLuint vertex_shader, fragment_shader;

  vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  shader_src_list = vertex_shader_src;
  glShaderSource(vertex_shader, 1, &shader_src_list, 0);
  glCompileShader(vertex_shader);
  glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vertex_shader, sizeof(info_log), 0, info_log);
    fprintf(stderr, "Could not compile vertex shader: %s\n", info_log);
    return 0;
  }

  fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  shader_src_list = fragment_shader_src;
  glShaderSource(fragment_shader, 1, &shader_src_list, 0);
  glCompileShader(fragment_shader);
  glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragment_shader, sizeof(info_log), 0, info_log);
    fprintf(stderr, "Could not compile fragment shader: %s\n", info_log);
    return 0;
  }

  result = glCreateProgram();
  glAttachShader(result, vertex_shader);
  glAttachShader(result, fragment_shader);
  glLinkProgram(result);
  glGetProgramiv(result, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(result, 512, 0, info_log);
    fprintf(stderr, "Could not link shader: %s\n", info_log);
    return 0;
  }

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  return result;
}

static int graphics_text_init(const char *ttf_file, int default_font_size) {
  assert(graphics_state.initialized);
  graphics_text_state.initialized = true;

  // Allocate text buffer
  glGenVertexArrays(1, &graphics_text_state.vertex_array);
  glGenBuffers(1, &graphics_text_state.vertex_buffer);
  glBindVertexArray(graphics_text_state.vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, graphics_text_state.vertex_buffer);
  // glBufferData(GL_ARRAY_BUFFER, 1024, 0, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glVertexAttribIPointer(0, 2, GL_UNSIGNED_SHORT, sizeof(TextVertex), (void*) 0);
  glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, sizeof(TextVertex), (void*) offsetof(TextVertex, tx));
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*) offsetof(TextVertex, color));
  gl_ok_or_die;

  // @shader
  const char *vertex_src = R"STRING(
  #version 330 core

  layout(location = 0) in ivec2 pos;
  layout(location = 1) in ivec2 tpos;
  layout(location = 2) in vec4 color;

  out vec2 ftpos;
  out vec4 fcolor;

  uniform vec2 screensize;
  uniform vec2 texture_size;

  void main() { 
    vec2 p = vec2(pos.x*2/screensize.x - 1.0f, (1.0f - pos.y/screensize.y)*2 - 1.0f);
    gl_Position = vec4(p, 0, 1);
    ftpos = tpos / texture_size;
    fcolor = color;
  }

  )STRING";

  const char *fragment_src = R"STRING(
  #version 330 core

  in vec2 ftpos;
  in vec4 fcolor;

  out vec4 color;

  uniform sampler2D tex;

  float to_srgbf(float val) {
    return val < 0.0031308f ? val*12.92f : 1.055f * pow(val, 1.0f/2.4f) - 0.055f;
  }

  vec3 to_srgb(vec3 v) {
    return vec3(to_srgbf(v.x), to_srgbf(v.y), to_srgbf(v.z));
  }

  void main () {
    vec4 c = clamp(fcolor, 0.0, 1.0);
    float alpha = c.w * texture(tex, ftpos).x;
    alpha = pow(alpha, 0.73);
    color = vec4(to_srgb(c.xyz), alpha);
  }

  )STRING";

  glEnable(GL_BLEND);
  graphics_text_state.shader = graphics_compile_shader(vertex_src, fragment_src);
  if (!graphics_text_state.shader)
    return 1;
  gl_ok_or_die;

  // gl_ok_or_die;
  glActiveTexture(GL_TEXTURE0);

  graphics_set_font_options(ttf_file, default_font_size);
  return 0;
}

static GraphicsTextState::FontData* get_fontdata(int font_size) {
  for (GraphicsTextState::FontData &d : graphics_text_state.fonts)
    if (d.font_size == font_size)
      return &d;
  return 0;
}

static void render_textatlas(int x, int y, int w, int h, int font_size) {
  GraphicsTextState::FontData *d = get_fontdata(font_size);
  if (!d)
    return;

  SDL_GetWindowSize(graphics_state.window, &graphics_state.window_width, &graphics_state.window_height);
  glViewport(0, 0, graphics_state.window_width, graphics_state.window_height);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glActiveTexture(GL_TEXTURE0);
  glUseProgram(graphics_text_state.shader);

  TextVertex vertices[6] = {
    {(u16)x,     (u16)(y+h), 0,       0},
    {(u16)(x+w), (u16)(y+h), U16_MAX, 0},
    {(u16)(x+w), (u16)(y),   U16_MAX, U16_MAX},
    {(u16)(x),   (u16)(y+h), 0,       0},
    {(u16)(x+w), (u16)(y),   U16_MAX, U16_MAX},
    {(u16)(x),   (u16)(y),   0,       U16_MAX},
  };

  glBindVertexArray(graphics_text_state.vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, graphics_text_state.vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, 6*sizeof(*vertices), vertices, GL_DYNAMIC_DRAW);
  glBindTexture(GL_TEXTURE_2D, d->atlas.id);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
}

static void push_textn(const char *str, int n, int pos_x, int pos_y, bool center, Color color, int font_size) {
  assert(graphics_text_state.initialized);
  if (!font_size)
    font_size = graphics_text_state.font_size;

  GraphicsTextState::FontData *font_data;
  if (!get_or_create_fontdata(graphics_text_state.font_file.slice, font_size, &font_data))
    return;

  if (!str)
    return;

  // allocate new memory if needed

  if (center) {
    // calc string width
    float strw = 0.0f;
    for (int i = 0; i < n; ++i)
      strw += font_data->glyphs[str[i] - 32].advance;
    pos_x -= (int)(strw / 2.0f);
    /*pos.y -= height/2.0f;*/ /* Why isn't this working? */
  }

  for (int i = 0; i < n; ++i) {
    // assert(str[i] >= GraphicsTextState::FIRST_CHAR && str[i] <= GraphicsTextState::LAST_CHAR);
    char chr = str[i];
    if (!chr)
      chr = ' ';
    else if (chr < GraphicsTextState::FIRST_CHAR || chr > GraphicsTextState::LAST_CHAR)
      chr = '?';
    Glyph g = font_data->glyphs[chr - 32];

    u16 x = (u16)(pos_x + (int)g.offset_x);
    u16 y = (u16)(pos_y + (int)g.offset_y);
    u16 w = (g.x1 - g.x0);
    u16 h = (g.y1 - g.y0);

    pos_x += (int)g.advance;
    if (pos_x >= graphics_state.window_width)
      break;

    u16 x0 = x;
    u16 x1 = x+w;
    u16 y0 = y;
    u16 y1 = y+h;

    TextVertex a = {x0, y0, g.x0, g.y0, color};
    TextVertex b = {x1, y0, g.x1, g.y0, color};
    TextVertex c = {x1, y1, g.x1, g.y1, color};
    TextVertex d = {x0, y1, g.x0, g.y1, color};
    font_data->vertices += a;
    font_data->vertices += b;
    font_data->vertices += c;
    font_data->vertices += a;
    font_data->vertices += c;
    font_data->vertices += d;
  }
}

static void push_text(const char *str, int pos_x, int pos_y, bool center, Color color, int font_size) {
  if (!str) return;
  push_textn(str, strlen(str), pos_x, pos_y, center, color, font_size);
}

static void render_text() {
  TIMING_BEGIN(TIMING_PANE_PUSH_TEXT_QUADS);

  SDL_GetWindowSize(graphics_state.window, &graphics_state.window_width, &graphics_state.window_height);
  glViewport(0, 0, graphics_state.window_width, graphics_state.window_height);

  // draw text
  gl_ok_or_die;
  glUseProgram(graphics_text_state.shader);

  // set alpha blending
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // send vertex data
  glBindVertexArray(graphics_text_state.vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, graphics_text_state.vertex_buffer);

  // set screen size
  glUniform2f(glGetUniformLocation(graphics_text_state.shader, "screensize"), (float)graphics_state.window_width, (float)graphics_state.window_height);

  // set texture
  glActiveTexture(GL_TEXTURE0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  for (GraphicsTextState::FontData &font_data : graphics_text_state.fonts) {
    glUniform2f(glGetUniformLocation(graphics_text_state.shader, "texture_size"), (float)font_data.atlas.w, (float)font_data.atlas.h);
    glBindTexture(GL_TEXTURE_2D, font_data.atlas.id);
    glBufferData(GL_ARRAY_BUFFER, font_data.vertices.size*sizeof(*font_data.vertices.items), font_data.vertices, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, font_data.vertices.size);
    font_data.vertices.size = 0;
  }

  glBindVertexArray(0);

  // clear
  gl_ok_or_die;
  TIMING_END(TIMING_PANE_PUSH_TEXT_QUADS);
}

static GLuint graphics_compile_shader_from_file(const char* vertex_filename, const char* fragment_filename) {
  char shader_src[2][2048];
  FILE *vertex_file = 0, *fragment_file = 0;
  int num_read;

  vertex_file = graphics_fopen(vertex_filename, "r");
  if (!vertex_file) {fprintf(stderr, "Could not open vertex shader file %s: %s\n", vertex_filename, graphics_strerror(errno)); return 0;};
  num_read = fread(shader_src[0], 1, sizeof(shader_src[0]), vertex_file);
  shader_src[0][num_read] = 0;
  if (!feof(vertex_file)) {fprintf(stderr, "File larger than buffer (which is %lu)\n", sizeof(shader_src[0])); return 0;}
  else if (ferror(vertex_file)) {fprintf(stderr, "Error While reading vertex shader %s: %s\n", vertex_filename, graphics_strerror(errno)); return 0;}

  fragment_file = graphics_fopen(fragment_filename, "r");
  if (!fragment_file) {fprintf(stderr, "Could not open fragment shader file %s: %s\n", fragment_filename, graphics_strerror(errno)); return 0;}
  num_read = fread(shader_src[1], 1, sizeof(shader_src[1]), fragment_file);
  shader_src[1][num_read] = 0;
  if (!feof(fragment_file)) {fprintf(stderr, "File larger than buffer\n"); return 0;}
  else if (ferror(fragment_file)) {fprintf(stderr, "While reading fragment shader %s: %s\n", fragment_filename, graphics_strerror(errno)); return 0;}

  GLuint result = graphics_compile_shader(shader_src[0], shader_src[1]);
  fclose(vertex_file);
  fclose(fragment_file);
  return result;
}

typedef Quad QuadVertex;

static struct {
  bool initialized;
  GLuint shader;
  QuadVertex *vertices;
  int num_vertices, _vertex_buffer_size;
  GLuint vertex_array, vertex_buffer;
} graphics_quad_state;

static int graphics_quad_init() {
  assert(graphics_state.initialized);

  graphics_quad_state._vertex_buffer_size = 512;
  graphics_quad_state.vertices = (QuadVertex*)malloc(sizeof(QuadVertex) * graphics_quad_state._vertex_buffer_size);
  graphics_quad_state.num_vertices = 0;
  gl_ok_or_die;

  /* Allocate quad buffer */
  glGenVertexArrays(1, &graphics_quad_state.vertex_array);
  glGenBuffers(1, &graphics_quad_state.vertex_buffer);
  glBindVertexArray(graphics_quad_state.vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, graphics_quad_state.vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(graphics_quad_state.vertices), 0, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribIPointer(0, 2, GL_UNSIGNED_SHORT, sizeof(QuadVertex), (void*) 0);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void*) offsetof(QuadVertex, color));
  gl_ok_or_die;

  // @shader
  const char *vertex_src = R"SHADER(
  #version 330 core

  layout(location = 0) in ivec2 pos;
  layout(location = 1) in vec4 color;

  out vec4 fcolor;

  uniform vec2 screensize;

  void main() { 
    vec2 p = vec2(pos.x*2.0f/screensize.x - 1.0f, (1.0f - pos.y/screensize.y)*2 - 1.0f);
    gl_Position = vec4(p, 0, 1);
    fcolor = color;
  }

  )SHADER";

  const char *fragment_src = R"SHADER(
  #version 330 core

  in vec4 fcolor;

  out vec4 color;

  float to_srgbf(float val) {
    return val < 0.0031308f ? val*12.92f : 1.055f * pow(val, 1.0f/2.4f) - 0.055f;
  }

  vec3 to_srgb(vec3 v) {
    return vec3(to_srgbf(v.x), to_srgbf(v.y), to_srgbf(v.z));
  }

  void main () {
    vec4 c = clamp(fcolor, 0.0, 1.0);
    color = vec4(to_srgb(c.xyz), c.w);
  }

  )SHADER";

  graphics_quad_state.shader = graphics_compile_shader(vertex_src, fragment_src);
  if (!graphics_quad_state.shader)
    return 1;
  gl_ok_or_die;

  graphics_quad_state.initialized = true;
  return 0;
}

static void push_quad(Quad a, Quad b, Quad c, Quad d) {
  assert(graphics_quad_state.initialized);

  // allocate new memory if needed
  int newsize = graphics_quad_state.num_vertices + 6;
  if (newsize > graphics_quad_state._vertex_buffer_size) {
    int newcap = graphics_quad_state._vertex_buffer_size * 2;
    while (newcap < newsize)
      newcap *= 2;

    QuadVertex *v = (QuadVertex*)malloc(sizeof(*v) * newcap);
    memcpy(v, graphics_quad_state.vertices, graphics_quad_state.num_vertices * sizeof(QuadVertex));
    free(graphics_quad_state.vertices);

    graphics_quad_state.vertices = v;
    graphics_quad_state._vertex_buffer_size = newcap;
  }

  QuadVertex *v = graphics_quad_state.vertices + graphics_quad_state.num_vertices;

  *v++ = a;
  *v++ = b;
  *v++ = c;
  *v++ = a;
  *v++ = c;
  *v++ = d;
  graphics_quad_state.num_vertices += 6;
}

static void push_square_quad(Rect r, Color topleft, Color topright, Color bottomleft, Color bottomright) {
  push_quad(quad(r.x,r.y,topleft), quad(r.x+r.w,r.y,topright), quad(r.x+r.w,r.y+r.h,bottomright), quad(r.x,r.y+r.h,bottomleft));
}

static void push_square_quad(Rect r, Color c) {
  push_quad(quad(r.x,r.y,c), quad(r.x+r.w,r.y,c), quad(r.x+r.w,r.y+r.h,c), quad(r.x,r.y+r.h,c));
}

static void render_quads() {
  TIMING_BEGIN(TIMING_PANE_PUSH_QUADS);
  SDL_GetWindowSize(graphics_state.window, &graphics_state.window_width, &graphics_state.window_height);
  glViewport(0, 0, graphics_state.window_width, graphics_state.window_height);

  glUseProgram(graphics_quad_state.shader);

  // set screen size
  GLint loc = glGetUniformLocation(graphics_quad_state.shader, "screensize");
  glUniform2f(loc, (float)graphics_state.window_width, (float)graphics_state.window_height);

  // set alpha blending
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // send vertex data
  glBindVertexArray(graphics_quad_state.vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, graphics_quad_state.vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, graphics_quad_state.num_vertices*sizeof(*graphics_quad_state.vertices), graphics_quad_state.vertices, GL_DYNAMIC_DRAW);

  //draw
  glDrawArrays(GL_TRIANGLES, 0, graphics_quad_state.num_vertices);
  glBindVertexArray(0);

  // clear
  graphics_quad_state.num_vertices = 0;
  gl_ok_or_die;

  TIMING_END(TIMING_PANE_PUSH_QUADS);
}

static Color shadow_color = {0.0f, 0.0f, 0.0f, 0.314f};
static Color shadow_color2 = {0.0f, 0.0f, 0.0f, 0.0f};

static void render_shadow_bottom_right(int x, int y, int w, int h, int shadow_size = 3) {
  // right side
  push_quad(
    quad(x + w, y + shadow_size, shadow_color),
    quad(x + w, y + h, shadow_color),
    quad(x + w + shadow_size, y + h + shadow_size, shadow_color2),
    quad(x + w + shadow_size, y + shadow_size, shadow_color2)
  );
  
  // bottom side
  push_quad(
    quad(x + shadow_size, y + h, shadow_color),
    quad(x + shadow_size, y + h + shadow_size, shadow_color2),
    quad(x + w + shadow_size, y + h + shadow_size, shadow_color2),
    quad(x + w, y + h, shadow_color)
  );
}

static void render_shadow_top(int x, int y, int w, int shadow_size = 3) {
  push_quad(
    quad(x + w, y,               shadow_color),
    quad(x + w, y - shadow_size, shadow_color2),
    quad(x,     y - shadow_size, shadow_color2),
    quad(x,     y,               shadow_color)
  );
}

static void render_shadow_bottom(int x, int y, int w, int shadow_size = 3) {
  push_quad(
    quad(x + w, y + shadow_size, shadow_color2),
    quad(x + w, y,               shadow_color),
    quad(x,     y,               shadow_color),
    quad(x,     y + shadow_size, shadow_color2)
  );
}

static void render_shadow_left(int x, int y, int h, int shadow_size = 3) {
  push_quad(
    quad(x,               y,     shadow_color),
    quad(x - shadow_size, y,     shadow_color2),
    quad(x - shadow_size, y + h, shadow_color2),
    quad(x,               y + h, shadow_color)
  );
}

/**********************
 *    TexturedQuad    *
 *********************/

typedef TexturedQuad TexturedQuadVertex;

static struct {
  bool initialized;
  GLuint shader;
  GLuint texture;
  Array<TexturedQuadVertex> vertices;
  Array<GLuint> loaded_textures;
  GLuint vertex_array, vertex_buffer;
} graphics_tquad_state;

static int graphics_textured_quad_init() {
  assert(graphics_state.initialized);

  graphics_tquad_state.vertices.resize(512);

  /* Allocate quad buffer */
  gl_ok_or_die;
  glGenVertexArrays(1, &graphics_tquad_state.vertex_array);
  glGenBuffers(1, &graphics_tquad_state.vertex_buffer);
  glBindVertexArray(graphics_tquad_state.vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, graphics_tquad_state.vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(graphics_tquad_state.vertices), 0, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribIPointer(0, 2, GL_UNSIGNED_SHORT, sizeof(TexturedQuadVertex), (void*) 0);
  glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, sizeof(TexturedQuadVertex), (void*) offsetof(TexturedQuadVertex, tx));
  gl_ok_or_die;

  // @shader
  const char *vertex_src = R"STRING(
  #version 330 core

  layout(location = 0) in ivec2 pos;
  layout(location = 1) in ivec2 tex;

  out vec2 fpos;
  out vec2 ftex;

  uniform vec2 screensize;
  uniform vec2 texsize;

  void main() { 
    vec2 p = vec2(pos.x*2.0f/screensize.x - 1.0f, (1.0f - pos.y/screensize.y)*2 - 1.0f);
    gl_Position = vec4(p, 0, 1);
    vec2 tp = vec2(tex.x/texsize.x, 1.0f - tex.y/texsize.y);
    ftex = tp;
  }
  )STRING";

  const char *fragment_src = R"STRING(
  #version 330 core

  in vec2 fpos;
  in vec2 ftex;

  out vec4 color;

  uniform sampler2D u_texture;

  float to_srgbf(float val) {
    if(val < 0.0031308f)
      val = val * 12.92f;
    else 
      val = 1.055f * pow(val, 1.0f/2.4f) - 0.055f;
    return val;
  }

  vec3 to_srgb(vec3 v) {
    return vec3(to_srgbf(v.x), to_srgbf(v.y), to_srgbf(v.z));
  }

  void main () {
    vec3 c = texture(u_texture, ftex).xyz;
    if (c == vec3(1.0, 0.0, 1.0) || c == vec3(0.0, 1.0, 1.0))
      discard;
    color = vec4(c, 1.0);
  }
  )STRING";

  graphics_tquad_state.shader = graphics_compile_shader(vertex_src, fragment_src);
  if (!graphics_tquad_state.shader)
    return 1;
  gl_ok_or_die;

  graphics_tquad_state.initialized = true;
  return 0;
}

static void push_tquad(TexturedQuad a, TexturedQuad b, TexturedQuad c, TexturedQuad d) {
  assert(graphics_tquad_state.initialized);

  // allocate new memory if needed
  graphics_tquad_state.vertices.reserve(graphics_tquad_state.vertices.size + 6);

  TexturedQuadVertex *v = graphics_tquad_state.vertices.end();

  v[0] = a;
  v[1] = b;
  v[2] = c;
  v[3] = a;
  v[4] = c;
  v[5] = d;

  graphics_tquad_state.vertices.size += 6;
}

static void push_square_tquad(Rect pos, Rect tex) {
  push_tquad(
    tquad(pos.x,         pos.y,         tex.x,         tex.y),
    tquad(pos.x + pos.w, pos.y,         tex.x + tex.w, tex.y),
    tquad(pos.x + pos.w, pos.y + pos.h, tex.x + tex.w, tex.y + tex.h),
    tquad(pos.x,         pos.y + pos.h, tex.x,         tex.y + tex.h)
  );
}

static void render_textured_quads(TextureHandle texture) {
  SDL_GetWindowSize(graphics_state.window, &graphics_state.window_width, &graphics_state.window_height);
  glViewport(0, 0, graphics_state.window_width, graphics_state.window_height);

  glUseProgram(graphics_tquad_state.shader);

  // set texture size
  int w, h;
  glBindTexture(GL_TEXTURE_2D, texture.value);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
  glUniform2f(glGetUniformLocation(graphics_tquad_state.shader, "texsize"), (float)w, (float)h);
  glUniform1i(glGetUniformLocation(graphics_tquad_state.shader, "u_texture"), 0);

  // set screen size
  glUniform2f(glGetUniformLocation(graphics_tquad_state.shader, "screensize"), (float)graphics_state.window_width, (float)graphics_state.window_height);

  // set alpha blending
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // send vertex data
  glBindVertexArray(graphics_tquad_state.vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, graphics_tquad_state.vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, graphics_tquad_state.vertices.size*sizeof(graphics_tquad_state.vertices[0]), graphics_tquad_state.vertices, GL_DYNAMIC_DRAW);

  //draw
  glDrawArrays(GL_TRIANGLES, 0, graphics_tquad_state.vertices.size);
  glBindVertexArray(0);

  // clear
  graphics_tquad_state.vertices.size = 0;
  gl_ok_or_die;
}

static bool load_texture_from_file(const char *filename, TextureHandle *result) {
  GLuint t;

  int channels = 3;
  // load file
  stbi_set_flip_vertically_on_load(1);
  int w,h;
  unsigned char *data = stbi_load(filename, &w, &h, &channels, 0);
  if (!data)
    return false;

  // put into gltexture
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // free
  stbi_image_free(data);
  gl_ok_or_die;

  result->value = t;
  return true;
}


static float to_linear(float val) {
  return val < 0.04045f ? val/12.92f : powf((val + 0.055f)/1.055f, 2.4f);
}

static float to_srgb(float val) {
  return val < 0.0031308f ? val * 12.92f : (1.055f * powf(val, 1.0f/2.4f) - 0.055f);
}

// Color8
static Color to_linear(Color8 c) {
  float r = to_linear(c.r/255.0f);
  float g = to_linear(c.g/255.0f);
  float b = to_linear(c.b/255.0f);
  return {r,g,b, c.a/255.0f};
}

bool operator==(Color8 a, Color8 b) {
  return *(u32*)&a == *(u32*)&b;
}

// Color
static Color blend(Color back, Color front, float alpha) {
  if (alpha < 0.0001f)
    return back;

  float a = alpha + back.a*(1.0f-alpha);
  float r = (back.r*back.a*(1.0f-alpha) + front.r*alpha) / a;
  float g = (back.g*back.a*(1.0f-alpha) + front.g*alpha) / a;
  float b = (back.b*back.a*(1.0f-alpha) + front.b*alpha) / a;
  return {r, g, b, a};
}

static Color blend_additive(Color back, Color front, float alpha) {
  return {
    back.r + front.r*alpha,
    back.g + front.g*alpha,
    back.b + front.b*alpha,
    back.a,
  };
}

static Color blend_additive(Color back, Color front) {
  return blend_additive(back, front, front.a);
}

static Color blend(Color back, Color front) {
  return blend(back, front, front.a);
}

static Color rgb8_to_linear_color(int r, int g, int b) {
  return {
    to_linear(r/255.0f),
    to_linear(g/255.0f),
    to_linear(b/255.0f),
    to_linear(1.0f),
  };
}

static Color rgba8_to_linear_color(int r, int g, int b, int a) {
  return {
    to_linear(r/255.0f),
    to_linear(g/255.0f),
    to_linear(b/255.0f),
    to_linear(a/255.0f),
  };
}

static Color hsl_to_linear_color(float h, float s, float l) {
  const float c = (1.0f - fabsf(2*l - 1)) * s;
  const float x = c*(1.0f - fabsf(fmodf((h/60.0f), 2.0f) - 1.0f));
  const float m = l - c/2.0f;
  float r,g,b;

  if (h > 300.0f)
    r = c,
    g = 0.0f,
    b = x;
  else if (h > 240.0f)
    r = x,
    g = 0.0f,
    b = c;
  else if (h > 180.0f)
    r = 0.0f,
    g = x,
    b = c;
  else if (h > 120.0f)
    r = 0.0f,
    g = c,
    b = x;
  else if (h > 60.0f)
    r = x,
    g = c,
    b = 0.0f;
  else
    r = c,
    g = x,
    b = 0.0f;

  r += m;
  g += m;
  b += m;

  return Color{to_linear(r), to_linear(g), to_linear(b), to_linear(1.0f)};
}

static Color8 to_srgb(Color c) {
  float r = to_srgb(c.r);
  float g = to_srgb(c.g);
  float b = to_srgb(c.b);
  return {
    (u8)(r*255),
    (u8)(g*255),
    (u8)(b*255),
    (u8)(c.a*255),
  };
}

static Color invert(Color c) {
  return {
    1.0f - c.r,
    1.0f - c.g,
    1.0f - c.b,
    c.a
  };
}

bool operator==(Color a, Color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

#endif /* GRAPHICS_H */