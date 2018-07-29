#ifdef _MSC_VER
  #define OS_WINDOWS 1
#else
  #define OS_LINUX 1
#endif

#ifdef OS_LINUX
  #define _POSIX_C_SOURCE 200112L
  #include <unistd.h>
  #include <termios.h>
  #include <sys/ioctl.h>
  #include <sys/select.h>
  #include <sys/types.h>
  #include <dirent.h>
#else
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN 1
  #endif
  #define NOMINMAX
  #include <windows.h>
  #include <direct.h>
  #include <sys/types.h>
#endif
#include <cstddef>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <sys/stat.h>

/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                           COMMON                           **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/


#ifdef _MSC_VER
  typedef __int8 i8;
  typedef __int16 i16;
  typedef __int32 i32;
  typedef __int64 i64;
  typedef unsigned __int8 u8;
  typedef unsigned __int16 u16;
  typedef unsigned __int32 u32;
  typedef unsigned __int64 u64;
#else
  /* let's hope stdint has us covered */
  #include <stdint.h>
  typedef int8_t i8;
  typedef int16_t i16;
  typedef int32_t i32;
  typedef int64_t i64;
  typedef uint8_t u8;
  typedef uint16_t u16;
  typedef uint32_t u32;
  typedef uint64_t u64;
#endif

#ifdef DEBUG
  #define IF_DEBUG(stmt) stmt
#else
  #define IF_DEBUG(stmt)
#endif

// An annotation for function parameters that a variable is optional
#define OPTIONAL

struct Path;
struct Slice;
union String;
union StringBuffer;
void util_free(Path &p);
void util_free(StringBuffer &s);
void util_free(String &s);
void util_free(Slice &s);
template<class T> union Array;
template<class T> struct View;
template<class T> union StrideView;
template<class T> void util_free(Array<T> &);
#define ALIGN(ptr, n) (void*)(((long)(ptr)+((n)-1)) & ~((n)-1))
#define ALIGNI(ptr, n) (((long)(ptr)+((n)-1)) & ~((n)-1))
#define CONTAINEROF(ptr, type, member) (((type)*)((char*)ptr - offsetof(type, member)))
#define CAST(type, val) (*(type*)(&(val)))
#define STATIC_ASSERT(expr, name) typedef char static_assert_##name[expr?1:-1]
#define ARRAY_LEN(a) (sizeof(a)/sizeof(*a))
#define foreach(a) for (auto it = a; it < (a)+ARRAY_LEN(a); ++it)
typedef unsigned int u32;
STATIC_ASSERT(sizeof(u32) == 4, u32_is_4_bytes);

#define at_least(a, b) max((a),(b))
#define at_most(a, b) min((a),(b))

/* a,b inclusive */
template<class T>
static T clamp(T x, T a, T b) {
  return x < a ? a : (b < x ? b : x);
}

template<class T>
T max(T a, T b) {return a < b ? b : a;}
template<class T>
T min(T a, T b) {return b < a ? b : a;}

float angle_to_range(float v, float a, float b) {
  return (sinf(v)*0.5f + 0.5f)*(b-a) + a;
}

template<class T>
void swap(T &a, T &b) {
  T tmp;
  tmp = a;
  a = b;
  b = tmp;
}


/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                          ALLOCATION                        **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/

// All the util libraries uses this stack of allocators.
// If you want a util library to use your own allocator, you push it on to the stack before using the util stuff
// See ALLOCATORS for some allocator implementations
struct Allocator {
  void *(*alloc)  (void *alloc_data, size_t size, size_t align);
  void *(*realloc)(void *alloc_data, void *prev, size_t prev_size, size_t size, size_t align);
  void (*dealloc) (void *alloc_data, void*, size_t size);
  void *alloc_data;
};

static void* default_alloc(void*, size_t size, size_t) {
  return malloc(size);
}
static void default_dealloc(void*, void *mem, size_t) {
  free(mem);
}
static void* default_realloc(void *, void *prev, size_t, size_t size, size_t) {
  return realloc(prev, size);
}

// You shall not have an allocator stack greater than 64 or I will personally come and slap you for writing shit code (i.e. have so little discipline in your control flow that you let it happen).
static Allocator allocators[64] = {{default_alloc, default_realloc, default_dealloc, 0}};
static int num_allocators = 1;
static void *(*current_alloc)(void *alloc_data, size_t size, size_t align) = default_alloc;
static void *(*current_realloc)(void *alloc_data, void *prev, size_t prev_size, size_t size, size_t align) = default_realloc;
static void (*current_dealloc)(void *alloc_data, void*, size_t size) = default_dealloc;
static void *current_alloc_data;

template<class T>
static T* alloc() {
  return (T*)current_alloc(current_alloc_data, sizeof(T), alignof(T));
}

static void* alloc(size_t size, size_t align = alignof(max_align_t)) {
  return current_alloc(current_alloc_data, size, align);
}

template<class T>
static T* alloc_array(size_t num) {
  return (T*)current_alloc(current_alloc_data, sizeof(T)*num, alignof(T));
}

static void* realloc(void *prev, size_t prev_size, size_t size, size_t align = alignof(max_align_t)) {
  return current_realloc(current_alloc_data, prev, prev_size, size, align);
}

template<class T>
static T* realloc(T *prev, size_t prev_num, size_t num) {
  return (T*)current_realloc(current_alloc_data, prev, sizeof(T)*prev_num, sizeof(T)*num, alignof(T));
}

template<class T>
static void dealloc(T *t) {
  current_dealloc(current_alloc_data, t, sizeof(T));
}

template<class T>
static void dealloc_array(T *t, size_t num) {
  current_dealloc(current_alloc_data, t, sizeof(T)*num);
}

static void dealloc(void *mem, size_t size) {
  current_dealloc(current_alloc_data, mem, size);
}

static void push_allocator(Allocator a) {
  allocators[num_allocators++] = a;
  current_alloc = a.alloc;
  current_dealloc = a.dealloc;
  current_alloc_data = a.alloc_data;
}

static void pop_allocator() {
  --num_allocators;
  current_alloc = allocators[num_allocators-1].alloc;
  current_dealloc = allocators[num_allocators-1].dealloc;
  current_alloc_data = allocators[num_allocators-1].alloc_data;
}


/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                           ARRAY                            **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/





#ifndef UTIL_ARRAY
#define UTIL_ARRAY

#ifndef ARRAY_INITIAL_SIZE
  #define ARRAY_INITIAL_SIZE 4
#endif

template<class T>
struct View {
  T *items;
  int size;
  int stride;

  const T& operator[](int i) const {return *(T*)((char*)items + stride*i);}
  T& operator[](int i) {return *(T*)((char*)items + stride*i);}

  const T& get(int i) const {return (*this)[i];}
  T& get(int i) {return (*this)[i];}

  int min() {
    assert(size);
    int r = 0;
    for (int i = 1; i < size; ++i)
      if (get(i) < get(r))
        r = i;
    return r;
  }

  T maxval() {
    assert(size);
    T t = get(0);
    for (int i = 1; i < size; ++i)
      if (t < get(i))
        t = get(i);
    return t;
  }

  template<class U>
  void free(int offset) {
    Array<U> a = {(U*)((char*)items - offset), size, size};
    util_free(a);
  }
};

template<class T>
View<T> view(T *items, int size, int stride = sizeof(T)) {
  return {items, size, stride};
}


#define VIEW_FREE(view, original_type, field) view.free<original_type>(offsetof(original_type, field))
#define VIEW(array, field) view(&array.items[0].field, array.size, sizeof(*array.items))
#define VIEW_FROM_ARRAY(array, field) view(&array[0].field, ARRAY_LEN(array), sizeof(*array))

template<class T, int N>
union StackArray {
  struct {
    T *items;
    int size;
    T buf[N];
  };

  StackArray() : items(buf), size(N) {}

  const T& operator[](int i) const {return items[i];}
  T& operator[](int i) {return items[i];}
};

template<class T, int N>
View<T> view(StackArray<T, N> a) {
  return {a.items, a.size, sizeof(T)};
}

template<class T>
union Array {
  struct {
    T *items;
    int size;
    int cap;
  };

  T* begin() {
    return items;
  }

  T* end() {
    return items + size;
  }

  const T* begin() const {
    return items;
  }

  const T* end() const {
    return items + size;
  }

  T& operator[](int i) {return items[i];}
  const T& operator[](int i) const {return items[i];}
  operator T*() {return items;}
  operator const T*() const {return items;}

  T& last() {
    return items[size-1];
  }

  Array<T> copy_shallow() {
    Array<T> a = {};
    a.resize(size);
    memcpy(a.items, items, size * sizeof(T));
    return a;
  }

  void free_shallow() {
    dealloc(items);
  }

  void zero() {
    memset(items, 0, sizeof(T) * size);
  }

  void operator+=(T val) {
    this->push(val);
  }

  void push(T val) {
    pushn(1);
    items[size-1] = val;
  }

  void insertz(int i) {
    if (i == size)
      push(T());
    else
      insert(i, T());
  }

  void insertz(int i, int n) {
    insertn(i, n);
    memset(items+i, 0, sizeof(T)*n);
  }

  void insert(int i, T value) {
    pushn(1);
    memmove(items+i+1, items+i, (size-i)*sizeof(T));
    items[i] = value;
  }

  void resize(int newsize) {
    if (newsize > size)
      pushn(newsize-size);
    size = newsize;
  }

  void reserve(int l) {
    int oldsize = size;
    if (l > size)
      pushn(l-size);
    size = oldsize;
  }

  void push(T *items, int n) {
    pushn(n);
    memmove(items+size-n, items, n*sizeof(T));
  }

  void remove(T *t) {
    remove((int)(t - items));
  }

  void remove(int i) {
    items[i] = items[size-1];
    --size;
  }

  void remove_slow(int i, int n) {
    if (!n)
      return;
    memmove(items+i, items+i+n, (size-i-n)*sizeof(T));
    size -= n;
  }

  void remove_slow_and_free(int i, int n) {
    if (!n)
      return;
    for (int j = i; j < i+n; ++j)
      util_free(items[j]);
    memmove(items+i, items+i+n, (size-i-n)*sizeof(T));
    size -= n;
  }

  void remove_slow(int i) {
    memmove(items+i, items+i+1, (size-i-1)*sizeof(T));
    --size;
  }

  void insertn(int i, int n) {
    pushn(n);
    memmove(items+i+n, items+i, (size-i-n)*sizeof(T));
  }

  void push() {
    pushn(1);
    items[size-1] = T();
  }

  template<class U>
  T* find(const U& u) {
    for (int i = 0; i < size; ++i)
      if (items[i] == u)
        return items + i;
    return 0;
  }

  void inserta(int i, const T *items, int n) {
    pushn(n);
    memmove(items+i+n, items+i, (size-i-n)*sizeof(T));
    memcpy(items+i, items, n*sizeof(T));
  }

  void copy_to(T *dest) {
    memcpy(dest, items, size*sizeof(T));
  }

  T* pushn(int n) {
    if (size+n >= cap) {
      int newcap = cap ? cap*2 : 1;
      while (newcap < size+n)
        newcap *= 2;
      items = realloc(items, cap, newcap);
      cap = newcap;
    }
    size += n;
    return items + size - n;
  }

  void pusha(T *items, int n) {
    pushn(n);
    memcpy(items+size-n, items, n*sizeof(T));
  }

  void clear() {
    size = 0;
  }
};

template<class T>
View<T> view(Array<T> a) {
  return {a.items, a.size, sizeof(T)};
}


template<class T>
void util_free(Array<T> &a) {
  if (a.items) {
    for (int i = 0; i < a.size; ++i)
      util_free(a.items[i]);
    dealloc(a.items);
  }
  a.items = 0;
  a.size = a.cap = 0;
}


#define ARRAY_FIND(a, ptr, expr) {for ((ptr) = (a).items; (ptr) < (a).items+(a).size; ++(ptr)) {if (expr) break;} if ((ptr) == (a).items+(a).size) {(ptr) = 0;}}
#define ARRAY_MIN_BY(a, field) (VIEW(a, field).min())
#define ARRAY_MAX_BY(a, field) (VIEW(a, field).max())
#define ARRAY_MAXVAL_BY(a, field) VIEW(a, field).maxval()

#endif












/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                           STRING                           **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/







#ifndef UTIL_STRING
#define UTIL_STRING

// A POD string, with overridable global allocator

#define is_utf8_trail(c) (((c)&0xC0) == 0x80)
#define is_utf8_head(c) ((c)&0x80)
#define is_utf8_symbol(c) ((c)&0x80)

struct Utf8char {
  u32 code;

  void operator=(const char *bytes) {
    int l = strlen(bytes);
    switch (l) {
      case 4:
        code |= (bytes[3] & 0xFF000000) >> 24;
      case 3:
        code |= (bytes[2] & 0xFF0000) >> 16;
      case 2:
        code |= (bytes[1] & 0xFF00) >> 8;
      case 1:
        code |= bytes[0] & 0xFF;
      case 0:
        break;
    }
  }

  void operator=(char c) {
    code = c & 0xFF;
  }

  bool is_ansi() const {
    return !is_utf8_head(code & 0xFF);
  }

  // TODO: check calls to this function
  char ansi() const {
    return code & 0xFF;
  }

  bool isspace() const {
    return code == ' ' || code == '\t' || code == '\n' || code == '\r';
  }

  bool isalpha() const {
    return (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
  }

  static Utf8char create(char c) {return Utf8char{(u32)c};}
};

bool operator==(Utf8char uc, char c) {
  return !(uc.code & 0xFF00) && (uc.code & 0xFF) == (u32)c;
}

bool operator==(char c, Utf8char uc) {
  return uc == c;
}

bool operator!=(Utf8char uc, char c) {
  return !(uc == c);
}

bool operator!=(char c, Utf8char uc) {
  return !(uc == c);
}


static const void *memmem(const void *needle, int needle_len, const void *haystack, int haystack_len) {
  char *h, *hend;
  if (!needle_len || haystack_len < needle_len)
    return 0;

  h = (char*)haystack;
  hend = h + haystack_len - needle_len + 1;

  for (; h < hend; ++h)
    if (memcmp(needle, h, needle_len) == 0)
      return h;
  return 0;
}

struct Utf8Iter {
  char *str;
  char *end;

  void operator++() {
    ++str;
    while (str != end && is_utf8_trail(*str))
      ++str;
  }

  bool operator!=(Utf8Iter) {
    return str != end;
  }

  Utf8char operator*() {
    Utf8char r = {};
    char *s = str;

    if (str == end)
      return r;

    r.code |= *s++ & 0xFF;

    if (s == end || !is_utf8_trail(*s))
      return r;
    r.code |= (*s++ & 0xFF00);
    if (s == end || !is_utf8_trail(*s))
      return r;
    r.code |= (*s++ & 0xFF0000);
    if (s == end || !is_utf8_trail(*s))
      return r;
    r.code |= (*s++ & 0xFF000000);
    return r;
  }
};

#define TO_STR(s) (*(Slice*)(&(s)))

#define STRING_METHODS_DECLARATION \
  char& operator[](int i); \
  const char& operator[](int i) const; \
  Slice operator()(int a, int b) const; \
  Utf8Iter begin() const; \
  Utf8Iter end() const; \
  int prev(int i) const; \
  int next(int i) const; \
  int from_visual_offset(int x, int tab_width) const; \
  int visual_offset(int x, int tab_width) const; \
  bool equals(const char *str, int n) const; \
  bool find(int offset, Slice s, int *result) const; \
  bool find(int offset, char c, int *result) const; \
  bool find(char c, int *result) const; \
  bool find(Slice s, int *result) const; \
  bool find_r(String s, int *result) const; \
  bool find_r(Slice s, int *result) const; \
  bool find_r(StringBuffer s, int *result) const; \
  bool find_r(char c, int *result) const; \
  bool find_r(int offset, char c, int *result) const; \
  bool begins_with(int offset, String s) const; \
  bool begins_with(int offset, Slice s) const; \
  bool begins_with(int offset, StringBuffer s) const; \
  bool begins_with(int offset, const char *str, int n) const; \
  bool begins_with(int offset, const char *str) const; \
  bool empty() const {return length;}; \
  String copy() const;

#define STRING_METHODS_IMPL(classname) \
  char& classname::operator[](int i) {return (char&)chars[i];} \
  const char& classname::operator[](int i) const {return chars[i];} \
  Slice classname::operator()(int a, int b) const {return Slice::slice(chars,length,a,b);} \
  Utf8Iter classname::begin() const {return Utf8Iter{(char*)chars, (char*)chars+length};} \
  Utf8Iter classname::end() const {return {};} \
  int classname::prev(int i) const {return Slice::prev(chars, i);} \
  int classname::next(int i) const {return Slice::next(chars, length, i);} \
  int classname::from_visual_offset(int x, int tab_width) const {return Slice::from_visual_offset(chars, length, x, tab_width);} \
  int classname::visual_offset(int x, int tab_width) const {return Slice::visual_offset(chars, length, x, tab_width);} \
  bool classname::equals(const char *str, int n) const {return Slice::equals(chars, length, str, n);} \
  bool classname::find(int offset, Slice s, int *result) const {return Slice::find(chars, length, offset, s, result);} \
  bool classname::find(int offset, char c, int *result) const {return Slice::find(chars, length, offset, c, result);} \
  bool classname::find(char c, int *result) const {return Slice::find(chars, length, c, result);} \
  bool classname::find(Slice s, int *result) const {return Slice::find(chars, length, s, result);} \
  bool classname::find_r(String s, int *result) const {return Slice::find_r(chars, length, TO_STR(s), result);} \
  bool classname::find_r(Slice s, int *result) const {return Slice::find_r(chars, length, TO_STR(s), result);} \
  bool classname::find_r(StringBuffer s, int *result) const {return Slice::find_r(chars, length, TO_STR(s), result);} \
  bool classname::find_r(char c, int *result) const {return Slice::find_r(chars, length, c, result);} \
  bool classname::find_r(int offset, char c, int *result) const {return Slice::find_r(chars, offset, c, result);} \
  bool classname::begins_with(int offset, String s) const {return Slice::begins_with(chars, length, offset, TO_STR(s));} \
  bool classname::begins_with(int offset, Slice s) const {return Slice::begins_with(chars, length, offset, TO_STR(s));} \
  bool classname::begins_with(int offset, StringBuffer s) const {return Slice::begins_with(chars, length, offset, TO_STR(s));} \
  bool classname::begins_with(int offset, const char *str, int n) const {return Slice::begins_with(chars, length, offset, str, n);} \
  bool classname::begins_with(int offset, const char *str) const {return Slice::begins_with(chars, length, offset, str);} \
  String classname::copy() const {return Slice::copy(chars, length);};

// A non-owning string

struct Slice {
  const char *chars;
  int length;

  static Slice slice(const char *str, int len, int a, int b);

  static String copy(const char *str, int len);

  static int prev(const char *chars, int i) {
    for (--i;; --i) {
      if (i <= 0)
        return 0;
      if (is_utf8_trail(chars[i]))
        continue;
      return i;
    };
  }

  static int next(const char *chars, int length, int i) {
    for (++i;; ++i) {
      if (i >= length)
        return length;
      if (is_utf8_trail(chars[i]))
        continue;
      return i;
    }
  }

  /* returns the logical index located visually at x */
  static int from_visual_offset(const char *chars, int length, int x, int tab_width) {
    int visual = 0;
    int i;

    if (!chars) return 0;

    for (i = 0; i < length; ++i) {
      if (is_utf8_trail(chars[i]))
        continue;
      ++visual;
      if (chars[i] == '\t')
        visual += tab_width-1;

      if (visual > x)
        return i;
    }

    return i;
  }

  static int visual_offset(const char *chars, int length, int x, int tab_width) {
    if (!chars)
      return 0;

    if (x > length)
      x = length;

    int result = 0;
    for (int i = 0; i < x; ++i) {
      if (is_utf8_trail(chars[i]))
        continue;

      ++result;
      if (chars[i] == '\t')
        result += tab_width-1;
    }
    return result;
  }

  static bool equals(const char *chars, int length, const char *str, int n) {
    return length == n && !memcmp(chars, str, n);
  }

  static bool find(const char *chars, int length, int offset, Slice s, int *result) {
    const void *p = memmem(s.chars, s.length, chars + offset, length - offset);
    if (!p)
      return false;
    *result = (char*)p - chars;
    return true;
  }

  static bool find(const char *chars, int length, Slice s, int *result) {
    return find(chars, length, 0, s, result);
  }

  static bool find(const char *chars, int length, int offset, char c, int *result) {
    for (int i = offset; i < length; ++i)
      if (chars[i] == c) {
        *result = i;
        return true;
      }
    return false;
  }

  static bool find(const char *chars, int length, char c, int *result) {
    return find(chars, length, 0, c, result);
  }

  static bool find_r(const char *chars, int length, Slice &s, int *result) {
    // TODO: implement properly
    char *p = (char*)memmem(s.chars, s.length, chars, length);
    if (!p)
      return false;

    char *next;
    while (1) {
      if (p+1 >= chars+length)
        break;
      next = (char*)memmem(s.chars, s.length, p+1, chars + length - p - 1);
      if (!next)
        break;
      p = next;
    }
    *result = (char*)p - chars;
    return true;
  }

  static bool find_r(const char *chars, int length, char c, int *result) {
    for (int i = length-1; i >= 0; --i)
      if (chars[i] == c) {
        *result = i;
        return true;
      }
    return false;
  }

  static bool begins_with(const char *chars, int length, int offset, Slice &s) {
    return length - offset >= s.length && !memcmp(s.chars, chars+offset, s.length);
  }

  static bool begins_with(const char *chars, int length, int offset, const char *str, int n) {
    return length - offset >= n && !memcmp(str, chars+offset, n);
  }

  static bool begins_with(const char *chars, int length, int offset, const char *str) {
    int n = strlen(str);
    return length - offset >= n && !memcmp(str, chars+offset, n);
  }

  static Slice create(const char *s, int len) {
    Slice sl;
    sl.chars = s;
    sl.length = len;
    return sl;
  }

  static Slice create(const char *s) {
    return Slice::create(s, strlen(s));
  }

  static void print_hex(Slice s) {
    putchar('\n');
    for (int i = 0; i < s.length; ++i)
      printf("%.2x", s.chars[i]);
    putchar('\n');
  }

  STRING_METHODS_DECLARATION
};

void util_free(Slice &) {}


static bool operator==(Slice a, Slice b) {
  return a.length == b.length && !memcmp(a.chars, b.chars, a.length);
}
static bool operator!=(Slice a, Slice b) {
  return !(a == b);
}
static bool operator==(Slice a, const char *str) {
  int l = strlen(str);
  return a.length == l && !memcmp(a.chars, str, l);
}
static bool operator!=(Slice a, const char *str) {
  return !(a == str);
}

// A string that owns its data
union String {
  struct {
    char *chars;
    int length;
  };
  Slice slice;

  static String create(const char *str, int len) {
    if (!len || !str)
      return {};

    String s;
    s.length = len;
    s.chars = alloc_array<char>(s.length+1);
    memcpy(s.chars, str, s.length);
    s.chars[s.length] = '\0';
    return s;
  }

  static String create(const char *str) {
    return String::create(str, strlen(str));
  }

  static String create(Slice s);

  STRING_METHODS_DECLARATION;
};

static bool operator==(String a, String b) {
  return a.slice == b.slice;
}
static bool operator==(String a, Slice b) {
  return a.slice == b;
}
static bool operator==(String a, const char *b) {
  return a.slice == b;
}

void util_free(String &s) {
  if (s.chars)
    dealloc(s.chars);
  s.chars = 0;
  s.length = 0;
}

// You can assume the StringBuffer is always null-terminated
union StringBuffer {
  struct {
    char *chars;
    int length;
    int cap;
  };
  String string;
  Slice slice;

  void resize(int l) {
    if (l > length)
      extend(l - length);
    length = l;
  }

  void extend(int l) {
    length += l;
    if (cap <= length) {
      const int oldcap = cap;
      while (cap <= length+1)
        cap = cap ? cap*2 : 4;
      chars = realloc(chars, oldcap, cap);
    }
    chars[length] = '\0';
  }

  void insert(int i, const char *str, int n) {
    if (i == length) {
      append(str, n);
      return;
    }

    extend(n);
    memmove(chars+i+n, chars+i, length-i-n);
    memcpy(chars+i, str, n);
  }

  void insert(int i, char c, int n) {
    if (i == length) {
      append(c, n);
      return;
    }

    extend(n);
    memmove(chars+i+n, chars+i, length-i-n);
    for (int j = i; j < i+n; ++j)
      chars[j] = c;
  }

  void insert(int i, char c) {
    if (i == length) {
      append(c);
      return;
    }

    extend(1);
    memmove(chars+i+1, chars+i, length-i-1);
    chars[i] = c;
  }

  void insert(int i, Utf8char c) {
    char buf[4];
    int n = 0;
    buf[n++] = c.code & 0xFF;
    if (!(c.code & 0xFF00)) goto done;
    buf[n++] = (c.code & 0xFF00) >> 8;
    if (!(c.code & 0xFF0000)) goto done;
    buf[n++] = (char)((c.code & 0xFF0000) >> 16);
    if (!(c.code & 0xFF000000)) goto done;
    buf[n++] = (c.code & 0xFF000000) >> 24;

    done:
    insert(i, buf, n);
  }

  void insert(int i, Slice s) {
    insert(i, s.chars, s.length);
  }

  void insert(int i, StringBuffer s) {
    insert(i, s.chars, s.length);
  }

  void remove(int i) {
    return remove(i, 1);
  }

  void remove(int i, int n) {
    memmove(chars+i, chars+i+n, length+1-i-n);
    length -= n;
  }

  void clear() {
    length = 0;
  }

  void append(String s) {
    (*this) += s;
  }

  void append(Utf8char c) {
    (*this) += c;
  }

  void append(Slice s) {
    (*this) += s;
  }

  void append(StringBuffer s) {
    this->append(TO_STR(s));
  }

  void append(char c, int n) {
    extend(n);
    for (int i = length-n; i < length; ++i)
      chars[i] = c;
  }

  void append(const char *str, int n) {
    if (!n)
      return;
    extend(n);
    memcpy(chars + length - n, str, n);
  }

  void append(const Utf8char *str, int n) {
    if (!n)
      return;
    // TODO: @utf8
    for (int i = 0; i < n; ++i) {
      if (str[i].is_ansi()) 
        append(str[i].ansi());
      else
        append('?');
    }
  }

  void append(long i) {
    (*this) += i;
  }

  void append(double d) {
    (*this) += d;
  }

  void append(char c) {
    (*this) += c;
  }

  void append(const char *str) {
    (*this) += str;
  }

  void operator+=(long i) {
    char buf[32];
    char* b = buf + 31;
    int neg = i < 0;
    *b-- = 0;
    if (neg) i *= -1;
    if (i == 0)
      *b-- = '0';
    else {
      while (i) {
        *b-- = '0' + i%10;
        i /= 10;
      }
    }
    if (neg) *b-- = '-';
    append(b+1, buf+31-(b+1));
  }

  void operator+=(double d) {
    int i;
    if (d < 0) {
      append('-');
      d *= -1;
    }
    append((long)d);
    d -= (double)(long)d;
    if (d < 0.0000001)
      return;

    int last_nonzero = length;
    append('.');
    for (i = 0; i < 9 && d > 0.0; ++i) {
      int digit = ((int)(d*10.0+0.5))%10;
      append((char)('0' + digit));
      if (digit)
        last_nonzero = length;
      d = fmod(d*10.0, 1.0);
    }
    length = last_nonzero;
  }

  void operator+=(Utf8char c) {
    char buf[4];
    int n = 0;
    buf[n++] = c.code & 0xFF;
    if (!(c.code & 0xFF00)) goto done;
    buf[n++] = (c.code & 0xFF00) >> 8;
    if (!(c.code & 0xFF0000)) goto done;
    buf[n++] = (char)((c.code & 0xFF0000) >> 16);
    if (!(c.code & 0xFF000000)) goto done;
    buf[n++] = (c.code & 0xFF000000) >> 24;

    done:
    append(buf, n);
  }

  void operator+=(char c) {
    extend(1);
    chars[length-1] = c;
  }

  void operator+=(const char *str) {
    if (!str)
      return;

    #if 0
    for (; *str; ++str)
      s += *str;
    #else
    int l = strlen(str);
    extend(l);
    memcpy(chars + length - l, str, l);
    #endif
  }

  void operator+=(String s) {
    (*this) += *(Slice*)&s;
  }

  void operator+=(StringBuffer b) {
    (*this) += CAST(Slice, b);
  }

  void operator+=(Slice b) {
    if (!b.length)
      return;
    extend(b.length);
    memcpy(chars + length - b.length, b.chars, b.length);
  }
  void appendv(const char* fmt, va_list args) {
    for (; *fmt; ++fmt) {
      if (*fmt == '{' && fmt[1] == '}') {
        append(*va_arg(args, Slice*));
        ++fmt;
      }
      else if (*fmt != '%') {
        append(*fmt);
      } else {
        ++fmt;
        switch (*fmt) {

          case 'i': append((long)va_arg(args, int)); break;

          case 'u': append((long)va_arg(args, unsigned int)); break;

          case 's': append(va_arg(args, char*)); break;

          case '%': append('%'); break;

          case 'f': append(va_arg(args, double)); break;

        }
      }
    }
  }

  void appendf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    appendv(fmt, args);
    va_end(args);
  }

  static StringBuffer create(const char *str) {
    StringBuffer s = {};
    s += str;
    return s;
  }

  static StringBuffer create(Slice sl) {
    StringBuffer s = {};
    s += sl;
    return s;
  }

  STRING_METHODS_DECLARATION;
};

void util_free(StringBuffer &s) {
  if (s.chars)
    dealloc(s.chars);
  s.chars = 0;
  s.length = s.cap = 0;
}

String String::create(Slice s) {
  return String::create(s.chars, s.length);
}

Slice Slice::slice(const char *str, int len, int a, int b) {
  if (b < 0)
    b = len + b + 1;
  if (b < 0)
    b = 0;
  Slice s;
  s.chars = (char*)(str+a);
  s.length = b-a;
  return s;
}

String Slice::copy(const char *chars, int length) {
  String s;
  s.chars = alloc_array<char>(length+1);
  s.length = length;
  memcpy(s.chars, chars, length);
  s.chars[length] = '\0';
  return s;
}

STRING_METHODS_IMPL(Slice)
STRING_METHODS_IMPL(String)
STRING_METHODS_IMPL(StringBuffer)



#endif /* UTIL_STRING */








/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                            FILE                            **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/







#ifndef UTIL_FILE
#define UTIL_FILE

struct Path {
  StringBuffer string;

  #ifdef OS_LINUX
  static const char separator = '/';
  #else
  static const char separator = '\\';
  #endif

  static Path base() {
    Path p = {};
    p.string += '.';
    return p;
  };

  void prepend(Slice dir) {
    string.insert(0, dir);
    string.insert(dir.length, separator);
  }

  void prepend(Path p) {
    prepend(p.string.slice);
  }

  void push(const char *str) {
    push(Slice::create(str));
  }

  void push(Slice file) {
    if (string.length == 0 || string[string.length-1] != separator)
      string += Path::separator;
    string += file;
    for (int i = 1; i < string.length-3; ++i) {
      if (string[i] == separator && string[i+1] == '.' && string[i+2] == '.' && string[i+3] == separator) {
        int prev;
        bool succ = string.find_r(i-1, separator, &prev);
        if (!succ)
          prev = -1;
        printf("%i\n", prev);
        int num_to_remove = i-prev+3;
        string.remove(prev, num_to_remove);
        i -= num_to_remove;
      }
    }
  }

  void pop() {
    int l;
    if (string.find_r(separator, &l)) {
      string.resize(l+1);
      string.length = l;
      string[l] = '\0';
    }
  }

  Path copy() const {
    return {StringBuffer::create(string.slice)};
  }

  Slice name() const {
    int x;
    if (string.find_r(separator, &x))
      return string(x+1, -1);
    return string.slice;
  }

  static Slice name(Slice path) {
    int x;
    if (path.find_r(separator, &x))
      return path(x+1, -1);
    return path;
  }

  static Path create(Slice p) {
    return {StringBuffer::create(p)};
  }
  static Path create(const char *s) {
    return {StringBuffer::create(s)};
  }
};

void util_free(Path &p) {
  util_free(p.string);
}

enum FileType {
  FILETYPE_UNKNOWN,
  FILETYPE_FILE,
  FILETYPE_DIR
};

namespace File {

  #ifdef OS_LINUX

  bool change_dir(Path p) {
    return !chdir(p.string.chars);
  }

  bool cwd(Path *p) {
    StringBuffer s = {};
    s.extend(64);
    while (1) {
      char *ptr = getcwd(s.chars, s.length);

      if (ptr == s.chars)
        break;
      else if (errno == ERANGE)
        s.extend(64);
      else
        goto err;
    }
    s.length = strlen(s.chars);
    p->string = s;
    return true;

    err:
    util_free(s);
    return false;
  }

  FileType filetype(Path path) {
    struct stat buf;
    int err = stat(path.string.chars, &buf);
    if (err)
      return FILETYPE_UNKNOWN;
    if (S_ISREG(buf.st_mode))
      return FILETYPE_FILE;
    if (S_ISDIR(buf.st_mode))
      return FILETYPE_DIR;
    return FILETYPE_UNKNOWN;
  }

  bool list_files(Path p, Array<Path> *result) {
    *result = {};
    DIR *dp = opendir(p.string.chars);
    if (!dp)
      return false;

    for (struct dirent *ep; ep = readdir(dp), ep;) {
      if (ep->d_name[0] == '.')
        continue;

      Path pp = p.copy();
      pp.push(ep->d_name);
      result->push(pp);
    }

    closedir(dp);
    return true;
  }

  #else

  bool change_dir(Path p) {
    return SetCurrentDirectory(p.string.chars);
  }

  bool cwd(Path *p) {
    StringBuffer s = {};
    s.extend(64);
    int n;
    while (1) {
      n = GetCurrentDirectory(s.length, s.chars);
      if (!n)
        goto err;
      if (n <= s.length)
        break;
      else
        s.extend(64);
    }
    s.length = n;
    p->string = s;
    return true;

    err:
    util_free(s);
    return false;
  }

  FileType filetype(Path path) {
    DWORD res = GetFileAttributes(path.string.chars);
    if (res == INVALID_FILE_ATTRIBUTES)
      return FILETYPE_UNKNOWN;
    if (res & FILE_ATTRIBUTE_DIRECTORY)
      return FILETYPE_DIR;
    if (res & FILE_ATTRIBUTE_NORMAL)
      return FILETYPE_FILE;
    return FILETYPE_UNKNOWN;
  }

  bool list_files(Path directory, Array<Path> *result) {
    *result = {};
    Path dir = directory.copy();
    WIN32_FIND_DATA find_data;
    dir.push("*");
    HANDLE handle = FindFirstFile(dir.string.chars, &find_data);
    util_free(dir);

    if (handle == INVALID_HANDLE_VALUE)
      return false;

    do {
      if (find_data.cFileName[0] == '.')
        continue;
      Path p = directory.copy();
      p.push(find_data.cFileName);
      result->push(p);
    } while (FindNextFile(handle, &find_data) != 0);

    FindClose(handle);
    return true;
  }

  #endif /* OS */

  bool get_contents(const char *path, Array<u8> *result) {
    *result = {};
    FILE *f = fopen(path, "rb");
    if (!f)
      goto err;
    while (1) {
      result->reserve(result->size + 1024);
      int n = fread(result->items + result->size, 1, 1024, f);
      result->size += n;
      if (n != 1024)
        break;
    }
    if (ferror(f))
      goto err;

    fclose(f);
    return true;

    err:
    if (f)
      fclose(f);
    return false;
  }

  bool get_contents(const char *path, String *result) {
    Array<u8> r;
    if (!get_contents(path, &r))
      return false;
    *result = {(char*)r.items, r.size};
    return true;
  }

  bool get_contents(Path path, String *result) {return get_contents(path.string.chars, result);}
  bool get_contents(Path path, Array<u8> *result) {return get_contents(path.string.chars, result);}

  bool isdir(Path path) {
    return filetype(path) == FILETYPE_DIR;
  }

  bool isfile(Path path) {
    return filetype(path) == FILETYPE_FILE;
  }
}

#endif /* UTIL_FILE */




#ifndef UTIL_JSON
#define UTIL_JSON

struct Json {
  struct ObjectField;
  enum Type {
    JSON_INVALID,
    JSON_NIL,
    JSON_ARRAY,
    JSON_NUMBER,
    JSON_STRING,
    JSON_OBJECT,
    JSON_BOOLEAN
  };

  Type type;
  union {
    // JSON_ARRAY 
    Array<Json> array;
    // JSON_NUMBER
    double number;
    // JSON_STRING
    String string;
    // JSON_OBJECT
    Array<ObjectField> fields;
    // JSON_BOOLEAN
    bool boolean;
  };

  // methods
  Json& operator[](int i);
  Json& operator[](const char *str);
  String dump() const;

  // constructors
  static bool parse(Slice str, Json *result) {const char *s = str.chars; return _parse(s, s + str.length, result);}
  static bool parse_file(Path path, Json *result) {
    String s;
    if (!File::get_contents(path, &s))
      return false;
    bool success = parse(s.slice, result);
    util_free(s);
    return success;
  }
  static Json create(double d) {Json j = {JSON_NUMBER}; j.number = d; return j;}
  static Json create(const char *str) {return create(Slice::create(str));}
  static Json create(Slice s) {Json j = {JSON_STRING}; j.string = String::create(s); return j;}
  static Json create(bool b) {Json j = {JSON_BOOLEAN}; j.boolean = b; return j;}
  static Json create() {return {JSON_NIL};}

  // private
  void _dump(StringBuffer &sb, int indent, bool indent_first) const;
  static bool _parse(const char *&str, const char *end, Json *result);
  static bool _parse_string(const char *&str, const char *end, String *result);
  static char _gettoken(const char *&str, const char *end);
};

struct Json::ObjectField {
  String name;
  Json value;
};

static Json _invalidJson; // a dump for all invalid values

void Json::_dump(StringBuffer &sb, int indent, bool indent_first) const {
  static const int INDENT_SIZE = 4;
  if (type == JSON_INVALID)
    return;

  if (indent_first)
    sb.append(' ', INDENT_SIZE*indent);
  switch (type) {
    case JSON_INVALID:
      break;

    case JSON_NIL:
      sb += "null";
      break;

    case JSON_ARRAY:
      sb += "[\n";
      for (int i = 0; i < array.size; ++i) {
        array[i]._dump(sb, indent+1, true);
        if (i < array.size-1)
          sb += ",";
        sb += '\n';
      }
      sb.append(' ', INDENT_SIZE*indent);
      sb += "]";
      break;

    case JSON_NUMBER:
      sb += number;
      break;

    case JSON_STRING:
      sb += '"';
      for (Utf8char c : string) {
        if (c == '"' || c == '\\')
          sb += '\\';
        sb += c;
      }
      sb += '"';
      break;

    case JSON_OBJECT:
      sb += '{';
      if (fields.size == 0)
        sb += '}';
      else {
        sb += '\n';
        for (int i = 0; i < fields.size; ++i) {
          sb.append(' ', INDENT_SIZE*(indent+1));
          sb += '"';
          sb += fields[i].name;
          sb += '"';
          sb += ": ";
          fields[i].value._dump(sb, indent+1, false);
          if (i < fields.size-1)
            sb += ',';
          sb += '\n';
        }
        sb.append(' ', INDENT_SIZE*indent);
        sb += "}";
      }
      break;

    case JSON_BOOLEAN:
      if (boolean)
        sb += "true";
      else
        sb += "false";
      break;
  }
}

char Json::_gettoken(const char *&str, const char *end) {
  while (isspace(*str) && str < end)
    ++str;
  if (str >= end)
    return '\0';
  return *str;
}

// properly removes escape sequences, so
// "\"hello\", said the bard. \\\\sincerely yours"
// ->
// "hello" said the bard. \\sincerely yours
bool Json::_parse_string(const char *&str, const char *end, String *result) {
  StringBuffer sb = {};
  if (_gettoken(str, end) != '"')
    goto err;

  ++str;
  bool escaped = false;
  while (1) {
    if (str >= end)
      goto err;
    char c = *str;
    if (!c)
      goto err;
    if (c == '\\') {
      if (escaped) {
        sb += '\\';
        escaped = false;
      }
      else
        escaped = true;
      ++str;
      continue;
    }

    if (c == '"' && !escaped)
      break;
    escaped = false;
    sb += c;
    ++str;
  }
  ++str;
  *result = sb.string;
  return true;

  err:
  util_free(sb);
  return false;
}

bool Json::_parse(const char *&str, const char *end, Json *result) {
  // object
  if (_gettoken(str, end) == '{') {
    ++str;
    Json j = {};
    j.type = JSON_OBJECT;
    IF_DEBUG(puts("object"));
    while (1) {
      if (_gettoken(str, end) == '}') {
        ++str;
        break;
      }

      // get field
      Json::ObjectField field;
      if (!Json::_parse_string(str, end, &field.name))
        return false;

      if (_gettoken(str, end) != ':')
        return false;
      ++str;

      // get value
      if (!Json::_parse(str, end, &field.value))
        return false;

      j.fields += field;

      if (_gettoken(str, end) == '}') {
        ++str;
        break;
      }
      if (_gettoken(str, end) == ',') {
        ++str;
        continue;
      }
      return false;
    }
    *result = j;
    return true;
  }

  // array
  else if (_gettoken(str, end) == '[') {
    ++str;
    Json j = {};
    j.type = JSON_ARRAY;
    IF_DEBUG(puts("array");)
    while (1) {
      if (_gettoken(str, end) == ']') {
        ++str;
        break;
      }

      Json jj;
      if (!Json::_parse(str, end, &jj))
        return false;
      j.array += jj;

      if (_gettoken(str, end) == ']') {
        ++str;
        break;
      }
      if (_gettoken(str, end) == ',') {
        ++str;
        continue;
      }
      IF_DEBUG(puts("unexpected end of array");)
      IF_DEBUG(puts(str);)
      return false;
    }
    *result = j;
    return true;
  }

  // string
  else if (_gettoken(str, end) == '"') {
    Json j = {};
    j.type = JSON_STRING;
    IF_DEBUG(puts("string");)
    if (!Json::_parse_string(str, end, &j.string))
      return false;
    *result = j;
    return true;
  }

  // number
  else if (_gettoken(str, end) == '-' || (_gettoken(str, end) >= '0' && _gettoken(str, end) <= '9')) {
    Json j = {};
    IF_DEBUG(puts("number");)
    j.type = JSON_NUMBER;
    char *digit_end;
    j.number = strtod(str, &digit_end);
    if (digit_end == str) {
      IF_DEBUG(puts("bad number");)
      return false;
    }
    str = digit_end;
    *result = j;
    return true;
  }

  // null
  else if (_gettoken(str, end) == 'n' && end-str >= 4 && Slice::create(str, 4) == "null") {
    str += 4;
    IF_DEBUG(puts("null");)
    Json j = {};
    j.type = JSON_NIL;
    *result = j;
    return true;
  }

  else if (_gettoken(str, end) == 't' && end-str >= 4 && Slice::create(str, 4) == "true") {
    str += 4;
    IF_DEBUG(puts("true");)
    Json j = {};
    j.type = JSON_BOOLEAN;
    j.boolean = true;
    *result = j;
    return true;
  }

  else if (_gettoken(str, end) == 'f' && end-str >= 5 && Slice::create(str, 5) == "false") {
    str += 5;
    IF_DEBUG(puts("false");)
    Json j = {};
    j.type = JSON_BOOLEAN;
    j.boolean = false;
    *result = j;
    return true;
  }

  IF_DEBUG(puts("invalid");)
  return false;
}

Json& Json::operator[](int i) {
  if (type != JSON_ARRAY || i < 0 || i >= array.size) {
    _invalidJson.type = JSON_INVALID;
    return _invalidJson;
  }
  return array[i];
}

Json& Json::operator[](const char *str) {
  if (type != JSON_OBJECT) {
    _invalidJson.type = JSON_INVALID;
    return _invalidJson;
  }

  for (ObjectField &f : fields)
    if (f.name == str)
      return f.value;

  _invalidJson.type = JSON_INVALID;
  return _invalidJson;
}

String Json::dump() const {
  StringBuffer sb = {};
  _dump(sb, 0, false);
  return sb.string;
}

#endif /* UTIL_JSON */


/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                           LOGGING                          **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/

#ifndef UTIL_LOGGING
#define UTIL_LOGGING

#ifdef OS_WINDOWS
#include <io.h>
#endif

// Terminal textstyles
static const char* TERM_RED = "";
static const char* TERM_GREEN = "";
static const char* TERM_YELLOW = "";
static const char* TERM_BLUE = "";
static const char* TERM_BOLD = "";
static const char* TERM_UNBOLD = "";
static const char* TERM_RESET_FORMAT = "";
static const char* TERM_RESET_COLOR = "";
static const char* TERM_RESET = "";

#define LOGGING_LEVEL_IMPLEMENTATION(level, color) \
void log_##level(Slice s) {fprintf(stderr, "%s%.*s%s", color, s.length, s.chars, TERM_RESET_COLOR);} \
void log_##level(String s) {log_##level(s.slice);} \
void log_##level(va_list args, const char *fmt) { \
  StringBuffer sb = {}; \
  sb.appendv(fmt, args); \
  log_##level(sb.string.slice); \
  util_free(sb); \
} \
\
void log_##level(const char *fmt, ...) { \
  va_list args; \
  va_start(args, fmt); \
  log_##level(args, fmt); \
  va_end(args); \
}

LOGGING_LEVEL_IMPLEMENTATION(info, TERM_GREEN)
LOGGING_LEVEL_IMPLEMENTATION(warn, TERM_YELLOW)
LOGGING_LEVEL_IMPLEMENTATION(error, TERM_RED)

#endif /* UTIL_LOGGING */



/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                           PROCESS                          **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/

#ifndef UTIL_PROCESS
#define UTIL_PROCESS

static bool call(Slice command, int *errcode, String *std_out, String *std_err) {
  // TODO: currently, we use blocking IO which causes a problem when reading both from stdout and stderr.
  // We can fix this by using Named pipes instead of Anonymous pipes (CreatePipe), and using concurrent reads

  // For a reference see https://docs.microsoft.com/en-us/windows/desktop/ProcThread/creating-a-child-process-with-redirected-input-and-output
  bool get_output = std_out || std_err;
  bool success = false;
  HANDLE proc_stdout = 0;
  HANDLE proc_stderr = 0;
  String cmd = {};
  Array<u8> stdout_data = {};
  Array<u8> stderr_data = {};

  STARTUPINFO info = {sizeof(info)};
  if (get_output)
    info.dwFlags |= STARTF_USESTDHANDLES;
  PROCESS_INFORMATION process_info = {};

  // create pipe for stdout
  if (std_out) {
    SECURITY_ATTRIBUTES sattr = {sizeof(sattr), NULL, TRUE};
    if (!CreatePipe(&proc_stdout, &info.hStdOutput, &sattr, 0)) {
      log_error("Failed to create pipe for stdout\n");
      goto err;
    }

    // Ensure the read handle to the pipe for STDOUT is not inherited.
    if ( ! SetHandleInformation(proc_stdout, HANDLE_FLAG_INHERIT, 0) ) {
      log_error("Failed SetHandleInformation for stdout\n"); 
      goto err;
    }
  }

  if (std_err) {
    SECURITY_ATTRIBUTES sattr = {sizeof(sattr), NULL, TRUE};
    if (!CreatePipe(&proc_stderr, &info.hStdError, &sattr, 0)) {
      log_error("Failed to create pipe for stderr\n");
      goto err;
    }

    // Ensure the read handle to the pipe for STDOUT is not inherited.
    if ( ! SetHandleInformation(proc_stderr, HANDLE_FLAG_INHERIT, 0) ) {
      log_error("Failed SetHandleInformation for stderr\n"); 
      goto err;
    }
  }

  // create process
  cmd = String::create(command);
  if (!CreateProcessA(NULL, cmd.chars, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &info, &process_info)) {
    log_error("Failed to create process (%i)\n", GetLastError());
    goto err;
  }
  util_free(cmd);

  // we won't be using the write end of the stdout,stderr pipes
  if (std_out) {
    CloseHandle(info.hStdOutput);
    info.hStdOutput = 0;
  }

  if (std_err) {
    CloseHandle(info.hStdError);
    info.hStdError = 0;
  }

  // read stdout from process
  while (1) {
    bool something_was_read = false;
    if (std_out) {
      stdout_data.reserve(stdout_data.size + 512);
      DWORD num_read;
      success = ReadFile(proc_stdout, stdout_data.items + stdout_data.size, 512, &num_read, NULL);
      something_was_read |= (success && num_read);
      stdout_data.size += num_read;
    }
    if (std_err) {
      stderr_data.reserve(stderr_data.size + 512);
      DWORD num_read;
      success = ReadFile(proc_stderr, stderr_data.items + stderr_data.size, 512, &num_read, NULL);
      something_was_read |= (success && num_read);
      stderr_data.size += num_read;
    }
    if (!something_was_read)
      break;
  }
  if (std_out)
    *std_out = {(char*)stdout_data.items, stdout_data.size};
  if (std_err)
    *std_err = {(char*)stderr_data.items, stderr_data.size};

  if (errcode) {
    WaitForSingleObject(process_info.hProcess, INFINITE);
    DWORD exit_code;
    if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
      log_error("Failed to get error code of command {}\n", &command);
      goto err;
    }
    *errcode = exit_code;
  }

  CloseHandle(process_info.hProcess);
  CloseHandle(process_info.hThread);
  if (std_out)
    CloseHandle(proc_stdout);
  if (std_err)
    CloseHandle(proc_stderr);
  return true;

  err:
  if (process_info.hProcess)
    CloseHandle(process_info.hProcess);
  if (process_info.hThread)
    CloseHandle(process_info.hThread);
  if (std_out) {
    CloseHandle(proc_stdout);
    if (info.hStdOutput)
      CloseHandle(info.hStdOutput);
  }
  if (std_err) {
    CloseHandle(proc_stderr);
    if (info.hStdError)
      CloseHandle(info.hStdError);
  }
  util_free(cmd);
  stdout_data.free_shallow();
  stderr_data.free_shallow();
  return false;
}
static bool call(const char *command, int *errcode, String *std_out, String *std_err) {
  return call(Slice::create(command), errcode, std_out, std_err);
}

// TODO: posix version
// https://jineshkj.wordpress.com/2006/12/22/how-to-capture-stdin-stdout-and-stderr-of-child-program/

#endif /* UTIL_PROCESS */



/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                           ALLOCATORS                       **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/


/*****************************************************
*                                                    *
*           Temp (arena/colony) allocator            *
*                                                    *
*****************************************************/

// A linked list of exponentially growing blocks that only gets freed when you free the whole thing
// This is really nice because it allows you to completely ignore freeing anything until the very end of whatever you're doing
// And you are always sure you don't have any leaks
// Just remember that anything you want to keep, you store separately before freeing
//
// Usage
//
//   TempAllocator tmp = TempAllocator::create(1024);
//   tmp.push();
//   ...
//   tmp.pop_and_free();
//
// If you want to persist something, first pop, then copy, then free
//
//   TempAllocator tmp = TempAllocator::create(1024);
//   tmp.push();
//   ...
//   tmp.pop();
//   result = x.copy();
//   tmp.free();
//  

static void* temporary_alloc(void *data, size_t size, size_t align);
static void temporary_dealloc(void *data, void *mem, size_t);
static void* temporary_realloc(void *data, void *prev, size_t prev_size, size_t size, size_t align);

struct TempAllocator {
  struct Block {
    size_t size;
    size_t cap;
    Block *next;
    max_align_t data;
  };
  Block *current;
  Block *first;

  static TempAllocator create(size_t initial_size = 1024) {
    TempAllocator t = {};
    Block *b = (Block*)alloc(offsetof(Block, data) + initial_size, alignof(Block));
    *b = {};
    t.first = t.current = b;
    return t;
  }

  // pushes the allocator
  void push() {
    push_allocator({temporary_alloc, temporary_realloc, temporary_dealloc, (void*)this});
  }

  void free() {
    #ifdef DEBUG
    int size = 0;
    #endif
    Block *b = current;
    while (b) {
      Block *next = b->next;
      #ifdef DEBUG
      size += b->size;
      #endif
      dealloc(b, offsetof(Block, data) + b->cap);

      b = next;
    }

    #ifdef DEBUG
    log_info("Freed %i bytes of temporary storage\n", (int)size);
    #endif
  }

  // pops the allocator and frees memory
  void pop() {
    pop_allocator();
  }

  void pop_and_free() {
    free();
    pop_allocator();
  }
};

static void* temporary_alloc(void *data, size_t size, size_t align) {
  // TODO: try to make the block size a power of two
  TempAllocator &t = *(TempAllocator*)data;
  TempAllocator::Block *b = t.current;
  b->size = ALIGNI(b->size, align); // this works because data is max_align_t, so offset (b->size) should also be aligned
  if (b->size + size >= b->cap) {
    size_t newcap = b->cap;
    while (newcap < size)
      newcap *= 2;
    TempAllocator::Block *new_block = (TempAllocator::Block*)allocators[num_allocators-2].alloc(allocators[num_allocators-2].alloc_data, offsetof(TempAllocator::Block, data) + newcap, alignof(TempAllocator::Block));
    new_block->size = 0;
    new_block->cap = newcap;
    new_block->next = 0;
    b->next = new_block;
    t.current = new_block;
    b = new_block;
  }

  b->size += size;
  return (char*)&b->data + b->size - size;
}

static void* temporary_realloc(void *data, void *, size_t, size_t size, size_t align) {
  return temporary_alloc(data, size, align);
}

static void temporary_dealloc(void*, void*, size_t) {}















/***************************************************************
***************************************************************
*                                                            **
*                                                            **
*                           INIT                             **
*                                                            **
*                                                            **
***************************************************************
***************************************************************/


void util_init() {

  #ifdef UTIL_LOGGING

  #ifdef OS_LINUX
  if (isatty(1))
  #elif OS_WINDOWS
  if (_isatty(_fileno(stderr)))
  #endif
  {
    TERM_RED = "\x1B[31m";
    TERM_GREEN = "\x1B[32m";
    TERM_YELLOW = "\x1B[33m";
    TERM_BLUE = "\x1B[34m";
    TERM_BOLD = "\x1B[1m";
    TERM_UNBOLD = "\x1B[21m";
    TERM_RESET_FORMAT = "\x1B[0m";
    TERM_RESET_COLOR = "\x1B[39m";
    TERM_RESET = "\x1B[0m\x1B[39m";
  }

  #endif /* UTIL_LOGGING */
}
