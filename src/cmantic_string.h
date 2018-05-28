#ifndef CMANTIC_STRING
#define CMANTIC_STRING

// A POD string, with overridable global allocator

#define is_utf8_trail(c) (((c)&0xC0) == 0x80)
#define is_utf8_head(c) ((c)&0x80)

struct Utf8char {
  char code[4];

  void operator=(char c) {
	code[1] = code[2] = code[3] = 0;
	code[0] = c;
  }

  void write_to_string(char *&str) {
	for (int i = 0; i < 4 && code[i]; ++i)
	  *str++ = code[i];
  }

  static Utf8char from_string(const char *&str, const char *end) {
	Utf8char r = {};
	char *res = r.code;

	if (str == end)
	  return r;

	*res++ = *str++;

	if (str == end || !is_utf8_trail(*str))
	  return r;
	*res++ = *str++;
	if (str == end || !is_utf8_trail(*str))
	  return r;
	*res++ = *str++;
	if (str == end || !is_utf8_trail(*str))
	  return r;
	*res++ = *str++;
	return r;
  }

  static void to_string(Utf8char *in, int n, Array<char> &out) {
	array_resize(out, n*4);
	char * const begin = out.data;
	char *end = begin;
	for (int i = 0; i < n; ++i)
	  in[i].write_to_string(end);
	array_resize(out, end - begin);
  }

  bool is_utf8() {
	return is_utf8_head(code[0]);
  }
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

struct String {
	char *chars;
	int length, cap;

	char& operator[](int i) {return chars[i];}
	char operator[](int i) const {return chars[i];}

	String operator()(int a, int b) const {
		if (b < 0)
			b = length+b+1;
		return {chars+a, b-a};
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
		while (cap < length)
			cap = cap ? cap*2 : 4;
		chars = (char*)realloc(chars, cap);
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

	void append(const char *str, int n) {
		if (!n)
			return;
		extend(n);
		memcpy(chars + length - n, str, n);
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

	void format(const char *fmt, va_list args) {

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

	void text_append(const char* fmt, ...) {
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

#endif