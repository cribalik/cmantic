
#ifdef OS_LINUX
  #include <errno.h>
  #include <sys/types.h>
  #include <dirent.h>
  #include <sys/stat.h>
#else
  #include <direct.h>
  #include <sys/types.h>
  #include <sys/stat.h>
#endif /* OS */

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

#define CONTAINEROF(ptr, type, member) (((type)*)((char*)ptr - offsetof(type, member)))
#define CAST(type, val) (*(type*)(&(val)))
#define STATIC_ASSERT(expr, name) typedef char static_assert_##name[expr?1:-1]
#define ARRAY_LEN(a) (sizeof(a)/sizeof(*a))
#define foreach(a) for (auto it = a; it < (a)+ARRAY_LEN(a); ++it)
typedef unsigned int u32;
STATIC_ASSERT(sizeof(u32) == 4, u32_is_4_bytes);

#define at_least(a,b) max((a),(b))
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

#ifndef ARRAY_REALLOC
  #include <stdlib.h>
  #define ARRAY_REALLOC realloc
#endif

#ifndef ARRAY_FREE
  #include <stdlib.h>
  #define ARRAY_FREE free
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
};

template<class T>
View<T> view(T *items, int size, int stride) {
  return {items, size, stride};
}

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

  void reserve(int size) {
    int oldsize = size;
    if (size > size)
      pushn(size-size);
    size = oldsize;
  }

  void push(T *items, int n) {
    pushn(n);
    memmove(items+size-n, items, n*sizeof(T));
  }

  void remove(int i) {
    items[i] = items[size-1];
    --size;
  }

  void remove_slown(int i, int n) {
    for (int j = 0; i < n; ++j)
      util_free(items[i+j]);
    memmove(items+i, items+i+n, (size-i-n)*sizeof(T));
    size -= n;
  }

  void remove_slow(int i) {
    util_free(items[i]);
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
      items = (T*)ARRAY_REALLOC(items, newcap * sizeof(T));
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
    free(a.items);
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
  bool find_r(String s, int *result) const; \
  bool find_r(Slice s, int *result) const; \
  bool find_r(StringBuffer s, int *result) const; \
  bool find_r(char c, int *result) const; \
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
  bool classname::find_r(String s, int *result) const {return Slice::find_r(chars, length, TO_STR(s), result);} \
  bool classname::find_r(Slice s, int *result) const {return Slice::find_r(chars, length, TO_STR(s), result);} \
  bool classname::find_r(StringBuffer s, int *result) const {return Slice::find_r(chars, length, TO_STR(s), result);} \
  bool classname::find_r(char c, int *result) const {return Slice::find_r(chars, length, c, result);} \
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
      if (i < 0)
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
    String s;
    s.length = len;
    s.chars = (char*)malloc(s.length+1);
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

void util_free(String &s) {
  if (s.chars)
    free(s.chars);
  s.chars = 0;
  s.length = 0;
}

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
      while (cap <= length+1)
        cap = cap ? cap*2 : 4;
      chars = (char*)realloc(chars, cap);
    }
    chars[length] = '\0';
  }

  void reserve(int n) {
    if (n > cap)
      extend(n - cap);
  }

  void insert(int i, const char *str, int n) {
    extend(n);
    memmove(chars+i+n, chars+i, length-i-n);
    memcpy(chars+i, str, n);
  }

  void insert(int i, char c, int n) {
    extend(n);
    memmove(chars+i+n, chars+i, length-i-n);
    for (int j = i; j < i+n; ++j)
      chars[j] = c;
  }

  void insert(int i, char c) {
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

  void remove(int i, int n) {
    memmove(chars+i, chars+i+n, length-i-n);
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
    while (i) {
    *b-- = '0' + i%10;
    i /= 10;
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
    append('.');
    for (i = 0; i < 2; ++i) {
    d *= 10.0;
    append((char)('0' + ((int)(d+0.5))%10));
    }
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
    free(s.chars);
  s.chars = 0;
  s.length = s.cap = 0;
}

String String::create(Slice s) {
  return String::create(s.chars, s.length);
}

Slice Slice::slice(const char *str, int len, int a, int b) {
  if (b < 0)
    b = len + b + 1;
  Slice s;
  s.chars = (char*)(str+a);
  s.length = b-a;
  return s;
}

String Slice::copy(const char *chars, int length) {
  String s;
  s.chars = (char*)malloc(length+1);
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

  void push(const char *str) {
    string += Path::separator;
    string += str;
  }

  void push(StringBuffer file) {
    string += Path::separator;
    string.append(file);
  }

  void pop() {
    int l;
    if (string.find_r(separator, &l))
      string.resize(l);
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

  bool list_files(Path p, Array<StringBuffer> *result) {
    *result = {};
    DIR *dp = opendir(p.string.chars);
    if (!dp)
      return false;

    for (struct dirent *ep; ep = readdir(dp), ep;) {
      if (ep->d_name[0] == '.')
        continue;

      Path p = {};
      p.string += ep->d_name;
      result->push(p.string);
    }

    closedir(dp);
    return true;
  }

  #else

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

  bool list_files(Path &directory, Array<StringBuffer> *result) {
    WIN32_FIND_DATA find_data;
    directory.push("*");
    HANDLE handle = FindFirstFile(directory.string.chars, &find_data);
    directory.pop();

    if (handle == INVALID_HANDLE_VALUE)
      return false;

    do {
      if (find_data.cFileName[0] == '.')
        continue;
      result->push(StringBuffer::create(find_data.cFileName));
    } while (FindNextFile(handle, &find_data) != 0);

    FindClose(handle);
    return true;
  }

  #endif /* OS */
}

#endif /* UTIL_FILE */
