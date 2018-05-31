
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
struct Array {
  T *data;
  int size,cap;

  T* begin() {
    return data;
  }

  T* end() {
    return data + size;
  }

  const T* begin() const {
    return data;
  }

  const T* end() const {
    return data + size;
  }

  T& operator[](int i) {return data[i];}
  const T& operator[](int i) const {return data[i];}
  operator T*() {return data;}
  operator const T*() const {return data;}

  T* last() {
    return data+size-1;
  }

  void push(T val) {
    pushn(1);
    data[size-1] = val;
  }

  void insertz(int i) {
    if (i == size)
      push(T());
    else
      insert(i, T());
  }

  void insert(int i, T value) {
    pushn(1);
    memmove(data+i+1, data+i, (size-i)*sizeof(T));
    data[i] = value;
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

  void push(T *data, int n) {
    pushn(n);
    memmove(data+size-n, data, n*sizeof(T));
  }

  void remove(int i) {
    data[i] = data[size-1];
    --size;
  }

  void remove_slown(int i, int n) {
    memmove(data+i, data+i+n, (size-i-n)*sizeof(T));
    size -= n;
  }

  void remove_slow(int i) {
    memmove(data+i, data+i+1, (size-i-1)*sizeof(T));
    --size;
  }

  void insertn(int i, int n) {
    pushn(n);
    memmove(data+i+n, data+i, (size-i-n)*sizeof(T));
  }

  void push() {
    pushn(1);
    data[size-1] = T();
  }

  void inserta(int i, const T *data, int n) {
    pushn(n);
    memmove(data+i+n, data+i, (size-i-n)*sizeof(T));
    memcpy(data+i, data, n*sizeof(T));
  }

  void copy_to(T *dest) {
    memcpy(dest, data, size*sizeof(T));
  }

  T* pushn(int n) {
    if (size+n >= cap) {
      int newcap = cap ? cap*2 : 1;
      while (newcap < size+n)
        newcap *= 2;
      data = (T*)ARRAY_REALLOC(data, newcap * sizeof(T));
      cap = newcap;
    }
    size += n;
    return data + size - n;
  }

  void pusha(T *data, int n) {
    pushn(n);
    memcpy(data+size-n, data, n*sizeof(T));
  }

  void free() {
    if (data)
      ::ARRAY_FREE(data);
    data = 0;
    size = cap = 0;
  }
};

#define ARRAY_FIND(a, ptr, expr) {for ((ptr) = (a).data; (ptr) < (a).data+(a).size; ++(ptr)) {if (expr) break;} if ((ptr) == (a).data+(a).size) {(ptr) = 0;}}

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

typedef unsigned int u32;

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

static void *memmem(void *needle, int needle_len, void *haystack, int haystack_len) {
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

struct String {
  char *chars;
  int length, cap;

  char& operator[](int i) {return chars[i];}
  char operator[](int i) const {return chars[i];}

  Utf8Iter begin() {
    return {chars, chars+length};
  }

  Utf8Iter end() {
    return {};
  }

  String operator()(int a, int b) const {
    if (b < 0)
      b = length+b+1;
    return {chars+a, b-a};
  }

  int prev(int i) const {
    for (--i;; --i) {
      if (i < 0)
        return 0;
      if (is_utf8_trail(chars[i]))
        continue;
      return i;
    };
  }

  int next(int i) const {
    for (++i;; ++i) {
      if (i >= length)
        return length;
      if (is_utf8_trail(chars[i]))
        continue;
      return i;
    }
  }

  /* returns the logical index located visually at x */
  int from_visual_offset(int x, int tab_width) {
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

  int visual_offset(int x, int tab_width) const {
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

  bool equals(const char *str, int n) const {
    return length == n && !memcmp(chars, str, n);
  }

  int find(int offset, String s) const {
    void *p = memmem(s.chars, s.length, chars + offset, length - offset);
    if (!p)
      return -1;
    return (char*)p - chars;
  }

  bool begins_with(int offset, String s) const {
    return length - offset >= s.length && !memcmp(s.chars, chars+offset, s.length);
  }

  bool begins_with(int offset, const char *str, int n) const {
    return length - offset >= n && !memcmp(str, chars+offset, n);
  }

  bool begins_with(int offset, const char *str) const {
    int n = strlen(str);
    return length - offset >= n && !memcmp(str, chars+offset, n);
  }

  void extend(int l) {
    length += l;
    while (cap <= length)
      cap = cap ? cap*2 : 4;
    chars = (char*)realloc(chars, cap);
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
    memmove(chars+i+1, chars+i, length-i-1);
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
    buf[n++] = (c.code & 0xFF0000) >> 16;
    if (!(c.code & 0xFF000000)) goto done;
    buf[n++] = (c.code & 0xFF000000) >> 24;

    done:
    insert(i, buf, n);
  }

  void insert(int i, String s) {
    insert(i, s.chars, s.length);
  }

  operator bool() {return length;}

  void remove(int i, int n) {
    memmove(chars+i, chars+i+n, length-i-n);
    length -= n;
  }

  void free() {
    if (chars)
      ::free(chars);
    length = cap = 0;
  }

  void append(String s) {
    (*this) += s;
  }

  void append(Utf8char c) {
    (*this) += c;
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
    buf[n++] = (c.code & 0xFF0000) >> 16;
    if (!(c.code & 0xFF000000)) goto done;
    buf[n++] = (c.code & 0xFF000000) >> 24;

    done:
    append(buf, n);
  }

  void clear() {
    length = 0;
  }

  void formatv(const char* fmt, va_list args) {
    for (; *fmt; ++fmt) {
      if (*fmt == '{' && fmt[1] == '}') {
        append(va_arg(args, String));
        ++fmt;
      }
      else if (*fmt != '%') {
        append(*fmt);
      } else {
        ++fmt;
        switch (*fmt) {

          case 'i': append((long)va_arg(args, int)); break;

          case 's': append(va_arg(args, char*)); break;

          case '%': append('%'); break;

          case 'f': append(va_arg(args, double)); break;

        }
      }
    }
  }

  void format(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    formatv(fmt, args);
    va_end(args);
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

  void operator+=(const String b) {
    if (!b.length)
      return;
    extend(b.length);
    memcpy(chars + length - b.length, b.chars, b.length);
  }

  String operator+(int i) {
    return {chars+i, length-i};
  }
};

bool operator==(String a, const char *str) {
  int l = strlen(str);
  return a.length == l && !memcmp(a.chars, str, l);
}

bool operator==(const char *str, String a) {
  return a == str;
}

bool operator==(String a, String b) {
  return a.length == b.length && !memcmp(a.chars, b.chars, a.length);
}

#endif /* UTIL_STRING */
