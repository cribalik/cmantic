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

/**************
 *   GENERAL  *
 *************/

static int graphics_init(SDL_Window **window);
struct Color {
  float r,g,b;

  // returns
  // h [0,360]
  // s [0,1]
  // v [0,1]
  Color hsv() const;

  // h [0,360]
  // s [0,1]
  // v [0,1]
  static Color from_hsl(float h, float s, float l);
  static Color blend(Color back, Color front, float alpha);
};

/**************
 *    TEXT    *
 *************/

static int graphics_text_init(const char *ttf_file, int font_size = 20);
static void render_textatlas(int x, int y, int w, int h);
static void push_textn(const char *str, int n, int pos_x, int pos_y, bool center, Color color);
static void push_text(const char *str, int pos_x, int pos_y, bool center, Color color);
static void push_textf(int pos_x, int pos_y, bool center, Color color, const char *fmt, ...);
static void render_text();

/**************
 *    QUAD    *
 *************/

struct Quad {
  float x,y;
  Color color;
};
static int graphics_quad_init();
static void push_quad(Quad a, Quad b, Quad c, Quad d);
static void render_quads();

#if defined(OS_WINDOWS)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN 1
  #endif
  #include <windows.h>
  #define GLAPI __stdcall
#else
  #define GLAPI
#endif /* OS */

#define GL_GLEXT_LEGACY
#include <GL\gl.h>

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

  static void (*glActiveTexture) (GLenum texture);
  static void (*glEnableVertexAttribArray) (GLuint index);
  static void (*glVertexAttribPointer) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
  static void (*glVertexAttribIPointer) (GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer);
  static void (*glUseProgram) (GLuint program);
  static void (*glUniform1i) (GLint location, GLint v0);
  static GLint (*glGetUniformLocation) (GLuint program, const GLchar *name);
  static GLuint (*glCreateShader) (GLenum type);
  static void (*glShaderSource) (GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
  static void (*glCompileShader) (GLuint shader);
  static void (*glGetShaderiv) (GLuint shader, GLenum pname, GLint *params);
  static void (*glGetShaderInfoLog) (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
  static GLuint (*glCreateProgram) (void);
  static void (*glAttachShader) (GLuint program, GLuint shader);
  static void (*glLinkProgram) (GLuint program);
  static void (*glGetProgramiv) (GLuint program, GLenum pname, GLint *params);
  static void (*glGetProgramInfoLog) (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
  static void (*glDeleteShader) (GLuint shader);
  static void (*glGenerateMipmap) (GLenum target);
  static void (*glGenVertexArrays) (GLsizei n, GLuint *arrays);
  static void (*glBindVertexArray) (GLuint array);
  static void (*glGenBuffers) (GLsizei n, GLuint *buffers);
  static void (*glBindBuffer) (GLenum target, GLuint buffer);
  static void (*glBufferData) (GLenum target, GLsizeiptr size, const void *data, GLenum usage);
  static void (*glUniform1f) (GLint location, GLfloat v0);
  static void (*glUniform2f) (GLint location, GLfloat v0, GLfloat v1);
  static void (*glUniform3f) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
  static void (*glUniform2i) (GLint location, GLint v0, GLint v1);
  static void (*glBufferSubData) (GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
  static void (*glUniform3fv) (GLint location, GLsizei count, const GLfloat *value);


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

bool operator==(Color a, Color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

struct TextVertex {
  int x,y;
  float tx,ty;
  Color color;
};

struct Glyph {
  unsigned short x0, y0, x1, y1; /* Position in image */
  float offset_x, offset_y, advance; /* Glyph offset info */
};

static struct GraphicsTextState {
  bool initialized;
  GLuint shader;
  TextVertex *vertices;
  int num_vertices;
  int _vertex_buffer_size;
  int font_size;
  Texture text_atlas;
  static const int FIRST_CHAR = 32;
  static const int LAST_CHAR = 128;
  GLuint vertex_array, vertex_buffer;
  Glyph glyphs[GraphicsTextState::LAST_CHAR - GraphicsTextState::FIRST_CHAR];
} graphics_text_state;

// only makes sense for mono fonts
static int graphics_get_font_advance() {
  return (int)graphics_text_state.glyphs[0].advance;
}

static struct {
  SDL_Window *window;
  bool initialized;
  int window_width, window_height;
} graphics_state;

// return 0 on success
static int graphics_init(SDL_Window **window) {
  if (SDL_Init(SDL_INIT_EVERYTHING)) {
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
  graphics_state.window = *window = SDL_CreateWindow("cmantic", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 600, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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
  glViewport(0, 0, w, h);
  graphics_state.window_width = w;
  graphics_state.window_height = h;

  if (!SDL_GL_CreateContext(*window)) {
    fprintf(stderr, "Failed to create gl context: %s\n", SDL_GetError());
    return 1;
  }

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
  *(void**) (&glActiveTexture) = (void*)SDL_GL_GetProcAddress("glActiveTexture");
  if (!glActiveTexture) { fprintf(stderr, "Couldn't load gl function \"glActiveTexture\"\n"); return 1;}

  gl_ok_or_die;

  graphics_state.initialized = true;
  return 0;
}

static const char* graphics_strerror(int err) {
#ifdef OS_WINDOWS
  static char buf[128];
  strerror_s(buf, sizeof(buf), err);
  return buf;
#else
  return strerror(err);
#endif
}

static FILE* graphics_fopen(const char *filename, const char *mode) {
#ifdef OS_WINDOWS
  FILE *f;
  if (fopen_s(&f, filename, mode))
    return 0;
  return f;
#else
  return fopen(filename, mode);
#endif
}

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

static int load_font_from_file(const char* filename, GLuint gl_texture, int tex_w, int tex_h, unsigned char first_char, unsigned char last_char, int height, Glyph *out_glyphs) {
  #define BUFFER_SIZE 1024*1024
  unsigned char* ttf_mem;
  unsigned char* bitmap;
  FILE* f;
  int res;

  ttf_mem = (unsigned char*)malloc(BUFFER_SIZE);
  bitmap = (unsigned char*)malloc(tex_w * tex_h);
  if (!ttf_mem || !bitmap) {fprintf(stderr, "Failed to allocate memory for font: %s\n", graphics_strerror(errno)); return 1;}

  f = graphics_fopen(filename, "rb");
  if (!f) {fprintf(stderr, "Failed to open ttf file %s: %s\n", filename, graphics_strerror(errno)); return 1;}
  fread(ttf_mem, 1, BUFFER_SIZE, f);

  res = stbtt_BakeFontBitmap(ttf_mem, 0, (float)height, bitmap, tex_w, tex_h, first_char, last_char - first_char, (stbtt_bakedchar*) out_glyphs);
  if (res <= 0) {fprintf(stderr, "Failed to bake font: %i\n", res); return 1;}

  glBindTexture(GL_TEXTURE_2D, gl_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, tex_w, tex_h, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);

  fclose(f);
  free(ttf_mem);
  free(bitmap);
  return 0;
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

static int graphics_set_font_options(const char *ttf_file, int font_size) {
  graphics_text_state.font_size = font_size;
  if (load_font_from_file(ttf_file, graphics_text_state.text_atlas.id, graphics_text_state.text_atlas.w, graphics_text_state.text_atlas.h, GraphicsTextState::FIRST_CHAR, GraphicsTextState::LAST_CHAR, graphics_text_state.font_size, graphics_text_state.glyphs))
    return 1;
  return 0;
}

static int graphics_text_init(const char *ttf_file, int font_size) {
  assert(graphics_state.initialized);

  graphics_text_state._vertex_buffer_size = 512;
  graphics_text_state.vertices = (TextVertex*)malloc(sizeof(TextVertex) * graphics_text_state._vertex_buffer_size);
  graphics_text_state.num_vertices = 0;
  graphics_text_state.font_size = font_size;
  gl_ok_or_die;

  /* Allocate text buffer */
  glGenVertexArrays(1, &graphics_text_state.vertex_array);
  glGenBuffers(1, &graphics_text_state.vertex_buffer);
  glBindVertexArray(graphics_text_state.vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, graphics_text_state.vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(graphics_text_state.vertices), 0, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glVertexAttribIPointer(0, 2, GL_INT, sizeof(*graphics_text_state.vertices), (void*) 0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(*graphics_text_state.vertices), (void*) offsetof(TextVertex, tx));
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(*graphics_text_state.vertices), (void*) offsetof(TextVertex, color));
  gl_ok_or_die;

  /* Create font texture and read in font from file */
  const int w = 512, h = 512;
  glGenTextures(1, &graphics_text_state.text_atlas.id);
  glBindTexture(GL_TEXTURE_2D, graphics_text_state.text_atlas.id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
  graphics_text_state.text_atlas.w = w;
  graphics_text_state.text_atlas.h = h;
  gl_ok_or_die;

  if (load_font_from_file(ttf_file, graphics_text_state.text_atlas.id, graphics_text_state.text_atlas.w, graphics_text_state.text_atlas.h, GraphicsTextState::FIRST_CHAR, GraphicsTextState::LAST_CHAR, graphics_text_state.font_size, graphics_text_state.glyphs))
    return 1;
  glBindTexture(GL_TEXTURE_2D, graphics_text_state.text_atlas.id);
  glGenerateMipmap(GL_TEXTURE_2D);
  gl_ok_or_die;

  // @shader
  const char *vertex_src = "#version 330 core\n" \
  "layout(location = 0) in ivec2 pos;\n" \
  "layout(location = 1) in vec2 tpos;\n" \
  "layout(location = 2) in vec3 color;\n" \
  "out vec2 ftpos;\n" \
  "out vec3 fcolor;\n" \
  "uniform vec2 screensize;\n" \
  "void main() { \n" \
  "vec2 p = vec2(pos.x*2/screensize.x - 1.0f, (1.0f - pos.y/screensize.y)*2 - 1.0f);\n" \
  "gl_Position = vec4(p, 0, 1);\n" \
  "ftpos = tpos;\n" \
  "fcolor = color;\n" \
  "}";

  const char *fragment_src =
  "#version 330 core\n" \
  "in vec2 ftpos;\n" \
  "in vec3 fcolor;\n" \
  "out vec4 color;\n" \
  "uniform sampler2D tex;\n" \
  "void main () { color = vec4(fcolor, texture(tex, ftpos).x); }\n";

  glEnable(GL_BLEND);
  graphics_text_state.shader = graphics_compile_shader(vertex_src, fragment_src);
  if (!graphics_text_state.shader)
    return 1;
  gl_ok_or_die;

  // gl_ok_or_die;
  glActiveTexture(GL_TEXTURE0);

  graphics_text_state.initialized = true;
  return 0;
}

static void render_textatlas(int x, int y, int w, int h) {
  SDL_GetWindowSize(graphics_state.window, &graphics_state.window_width, &graphics_state.window_height);
  glViewport(0, 0, graphics_state.window_width, graphics_state.window_height);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glActiveTexture(GL_TEXTURE0);
  glUseProgram(graphics_text_state.shader);

  TextVertex vertices[6] = {
    {x, y+h, 0.0f, 0.0f},
    {x+w, y+h, 1.0f, 0.0f},
    {x+w, y, 1.0f, 1.0f},
    {x, y+h, 0.0f, 0.0f},
    {x+w, y, 1.0f, 1.0f},
    {x, y, 0.0f, 1.0f},
  };

  glBindVertexArray(graphics_text_state.vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, graphics_text_state.vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, 6*sizeof(*vertices), vertices, GL_DYNAMIC_DRAW);
  glBindTexture(GL_TEXTURE_2D, graphics_text_state.text_atlas.id);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
}

static void push_textn(const char *str, int n, int pos_x, int pos_y, bool center, Color color) {
  assert(graphics_text_state.initialized);

  if (!str)
    return;

  float ipw,iph, tx0,ty0,tx1,ty1;

  ipw = 1.0f / graphics_text_state.text_atlas.w;
  iph = 1.0f / graphics_text_state.text_atlas.h;

  // allocate new memory if needed
  int newsize = graphics_text_state.num_vertices + 6*strlen(str);
  if (newsize > graphics_text_state._vertex_buffer_size) {
    int newcap = graphics_text_state._vertex_buffer_size * 2;
    while (newcap < newsize)
      newcap *= 2;

    TextVertex *v = (TextVertex*)malloc(sizeof(*v) * newcap);
    memcpy(v, graphics_text_state.vertices, graphics_text_state.num_vertices * sizeof(TextVertex));
    free(graphics_text_state.vertices);

    graphics_text_state.vertices = v;
    graphics_text_state._vertex_buffer_size = newcap;
  }

  if (center) {
    // calc string width
    float strw = 0.0f;
    for (int i = 0; i < n; ++i)
      strw += graphics_text_state.glyphs[str[i] - 32].advance;
    pos_x -= (int)(strw / 2.0f);
    /*pos.y -= height/2.0f;*/ /* Why isn't this working? */
  }

  for (int i = 0; i < n; ++i) {
    Glyph g = graphics_text_state.glyphs[str[i] - 32];

    int x = pos_x + (int)g.offset_x;
    int y = pos_y + (int)g.offset_y;
    int w = (g.x1 - g.x0);
    int h = (g.y1 - g.y0);

    pos_x += (int)g.advance;
    if (x + w <= 0) continue;
    if (x - w >= graphics_state.window_width) break;

    /* scale texture to atlas */
    tx0 = g.x0 * ipw,
    tx1 = g.x1 * ipw;
    ty0 = g.y0 * iph;
    ty1 = g.y1 * iph;

    TextVertex *v = graphics_text_state.vertices + graphics_text_state.num_vertices;

    *v++ = {x, y, tx0, ty0, color};
    *v++ = {x + w, y, tx1, ty0, color};
    *v++ = {x, y+h, tx0, ty1, color};
    *v++ = {x, y+h, tx0, ty1, color};
    *v++ = {x + w, y, tx1, ty0, color};
    *v++ = {x + w, y+h, tx1, ty1, color};

    graphics_text_state.num_vertices += 6;
  }
}

static void push_text(const char *str, int pos_x, int pos_y, bool center, Color color) {
  if (!str) return;
  push_textn(str, strlen(str), pos_x, pos_y, center, color);
}

#define TEXT_IMPLEMENTATION
#include "text.h"

static void push_textf(int pos_x, int pos_y, bool center, Color color, const char *fmt, ...) {
  if (!fmt) return;
  Text t = text_create();
  va_list args;
  va_start(args, fmt);
  text_append_v(&t, fmt, args);
  va_end(args);
  push_text(text_get(t), pos_x, pos_y, center, color);
  text_free(t);
}

static void render_text() {
  SDL_GetWindowSize(graphics_state.window, &graphics_state.window_width, &graphics_state.window_height);
  glViewport(0, 0, graphics_state.window_width, graphics_state.window_height);

  /* draw text */
  gl_ok_or_die;
  glUseProgram(graphics_text_state.shader);

  // set screen size
  GLint loc = glGetUniformLocation(graphics_text_state.shader, "screensize");
  glUniform2f(loc, (float)graphics_state.window_width, (float)graphics_state.window_height);

  // set alpha blending
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // send vertex data
  glBindVertexArray(graphics_text_state.vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, graphics_text_state.vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, graphics_text_state.num_vertices*sizeof(*graphics_text_state.vertices), graphics_text_state.vertices, GL_DYNAMIC_DRAW);

  // set texture
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, graphics_text_state.text_atlas.id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  //draw
  glDrawArrays(GL_TRIANGLES, 0, graphics_text_state.num_vertices);
  glBindVertexArray(0);

  // clear
  graphics_text_state.num_vertices = 0;
  gl_ok_or_die;
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
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void*) 0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void*) offsetof(QuadVertex, color));
  gl_ok_or_die;

  // @shader
  const char *vertex_src = "#version 330 core\n" \
  "layout(location = 0) in vec2 pos;\n" \
  "layout(location = 1) in vec3 color;\n" \
  "out vec3 fcolor;\n" \
  "uniform vec2 screensize;\n" \
  "void main() { \n" \
  "vec2 p = vec2(pos.x*2.0f/screensize.x - 1.0f, (1.0f - pos.y/screensize.y)*2 - 1.0f);\n" \
    "gl_Position = vec4(p, 0, 1);\n" \
    "fcolor = color;\n" \
  "}";

  const char *fragment_src =
  "#version 330 core\n" \
  "in vec3 fcolor;\n" \
  "out vec4 color;\n" \
  "void main () { color = vec4(fcolor, 1.0); }\n";

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

static void push_square_quad(float x0, float x1, float y0, float y1, Color c) {
  push_quad({x0,y0,c}, {x1,y0,c}, {x1,y1,c}, {x0,y1,c});
}

static void render_quads() {
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
}

Color Color::blend(Color back, Color front, float alpha) {
  Color result;

  if (alpha > 0.0001f) {
    result.r = back.r*(1.0f-alpha) + front.r*alpha;
    result.g = back.g*(1.0f-alpha) + front.g*alpha;
    result.b = back.b*(1.0f-alpha) + front.b*alpha;
  }
  else {
    result = back;
  }
  return result;
}

Color Color::hsv() const {
  float max,min, d, h,s,v;
  max = r > g ? r : g;
  max = max > b ? max : b;
  min = r < g ? r : g;
  min = min < b ? min : b;

  d = max - min;
  if (d < 0.00001f)
    return {};
  if (max <= 0.0f)
    return {};

  s = d/max;
  if (max == r)
    h = (g-b)/d;
  else if (max == g)
    h = 2.0f + (b-r)/d;
  else
    h = 4.0f + (r-g)/d;

  h *= 60.0f;

  while (h > 1.0f)
    h -= 360.0f;
  while (h < 0.0f)
    h += 360.0f;

  v = max;
  return  {h,s,v};
}

Color Color::from_hsl(float h, float s, float l) {
  const float c = (1.0f - fabsf(2*l - 1)) * s;
  const float x = c*(1.0f - fabsf(fmodf((h/60.0f), 2.0f) - 1.0f));
  const float m = l - c/2.0f;
  Color result;

  if (h > 300.0f)
    result = {c,0.0f,x};
  else if (h > 240.0f)
    result = {x,0.0f,c};
  else if (h > 180.0f)
    result = {0.0f,x,c};
  else if (h > 120.0f)
    result = {0.0f,c,x};
  else if (h > 60.0f)
    result = {x,c,0.0f};
  else
    result = {c,x,0.0f};

  result.r += m;
  result.g += m;
  result.b += m;
  return result;
}


#endif /* GRAPHICS_H */