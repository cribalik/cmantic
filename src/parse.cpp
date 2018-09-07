#ifndef PARSE_CPP
#define PARSE_CPP

enum Token {
  TOKEN_NULL                =  0,
  TOKEN_IDENTIFIER          = -2,
  TOKEN_NUMBER              = -3,
  TOKEN_STRING              = -4,
  TOKEN_STRING_BEGIN        = -6,
  TOKEN_BLOCK_COMMENT       = -7,
  TOKEN_BLOCK_COMMENT_BEGIN = -8,
  TOKEN_BLOCK_COMMENT_END   = -9,
  TOKEN_LINE_COMMENT        = -10,
  TOKEN_OPERATOR            = -11,
  TOKEN_EOF                 = -12,
};

enum KeywordType {
  KEYWORD_NONE,
  KEYWORD_CONTROL, // control flow
  KEYWORD_TYPE,
  KEYWORD_SPECIFIER,
  KEYWORD_DEFINITION,
  KEYWORD_FUNCTION,
  KEYWORD_MACRO,
  KEYWORD_CONSTANT,
  KEYWORD_COUNT
};

Color keyword_colors[KEYWORD_COUNT];

struct Keyword {
  const char *name;
  KeywordType type;
};

static Keyword cpp_keywords[] = {

  // constants

  {"true", KEYWORD_CONSTANT},
  {"false", KEYWORD_CONSTANT},
  {"NULL", KEYWORD_CONSTANT},
  {"delete", KEYWORD_CONSTANT},
  {"new", KEYWORD_CONSTANT},
  {"null", KEYWORD_CONSTANT},
  {"this", KEYWORD_CONSTANT},

  // types

  {"char", KEYWORD_TYPE},
  {"short", KEYWORD_TYPE},
  {"int", KEYWORD_TYPE},
  {"long", KEYWORD_TYPE},
  {"float", KEYWORD_TYPE},
  {"double", KEYWORD_TYPE},
  {"unsigned", KEYWORD_TYPE},
  {"void", KEYWORD_TYPE},
  {"bool", KEYWORD_TYPE},
  {"uint64_t", KEYWORD_TYPE},
  {"uint32_t", KEYWORD_TYPE},
  {"uint16_t", KEYWORD_TYPE},
  {"uint8_t", KEYWORD_TYPE},
  {"int64_t", KEYWORD_TYPE},
  {"int32_t", KEYWORD_TYPE},
  {"int16_t", KEYWORD_TYPE},
  {"int8_t", KEYWORD_TYPE},
  {"u64", KEYWORD_TYPE},
  {"u32", KEYWORD_TYPE},
  {"u16", KEYWORD_TYPE},
  {"u8", KEYWORD_TYPE},
  {"i64", KEYWORD_TYPE},
  {"i32", KEYWORD_TYPE},
  {"i16", KEYWORD_TYPE},
  {"i8", KEYWORD_TYPE},
  {"va_list", KEYWORD_TYPE},
  {"IEnumerator", KEYWORD_TYPE},
  {"byte", KEYWORD_TYPE},

  // function

  #if 0
  {"typeof", KEYWORD_FUNCTION},
  {"sizeof", KEYWORD_FUNCTION},
  {"printf", KEYWORD_FUNCTION},
  {"puts", KEYWORD_FUNCTION},
  {"strcmp", KEYWORD_FUNCTION},
  {"strlen", KEYWORD_FUNCTION},
  {"fprintf", KEYWORD_FUNCTION},
  {"malloc", KEYWORD_FUNCTION},
  {"free", KEYWORD_FUNCTION},
  {"new", KEYWORD_FUNCTION},
  {"delete", KEYWORD_FUNCTION},
  {"fflush", KEYWORD_FUNCTION},
  {"va_start", KEYWORD_FUNCTION},
  {"vfprintf", KEYWORD_FUNCTION},
  {"va_end", KEYWORD_FUNCTION},
  {"abort", KEYWORD_FUNCTION},
  {"exit", KEYWORD_FUNCTION},
  {"min", KEYWORD_FUNCTION},
  {"max", KEYWORD_FUNCTION},
  {"memcmp", KEYWORD_FUNCTION},
  {"putchar", KEYWORD_FUNCTION},
  {"putc", KEYWORD_FUNCTION},
  {"fputc", KEYWORD_FUNCTION},
  {"getchar", KEYWORD_FUNCTION},
  {"swap", KEYWORD_FUNCTION},
  #endif

  // specifiers

  {"static", KEYWORD_SPECIFIER},
  {"const", KEYWORD_SPECIFIER},
  {"extern", KEYWORD_SPECIFIER},
  {"nothrow", KEYWORD_SPECIFIER},
  {"noexcept", KEYWORD_SPECIFIER},
  {"public", KEYWORD_SPECIFIER},
  {"private", KEYWORD_SPECIFIER},
  {"in", KEYWORD_SPECIFIER},
  {"delegate", KEYWORD_SPECIFIER},
  {"protected", KEYWORD_SPECIFIER},
  {"override", KEYWORD_SPECIFIER},
  {"virtual", KEYWORD_SPECIFIER},
  {"abstract", KEYWORD_SPECIFIER},

  // declarations

  {"struct", KEYWORD_DEFINITION},
  {"class", KEYWORD_DEFINITION},
  {"union", KEYWORD_DEFINITION},
  {"enum", KEYWORD_DEFINITION},
  {"typedef", KEYWORD_DEFINITION},
  {"template", KEYWORD_DEFINITION},
  {"operator", KEYWORD_DEFINITION},
  {"namespace", KEYWORD_DEFINITION},

  // macro

  {"#include", KEYWORD_MACRO},
  {"#define", KEYWORD_MACRO},
  {"#undef", KEYWORD_MACRO},
  {"#ifdef", KEYWORD_MACRO},
  {"#ifndef", KEYWORD_MACRO},
  {"#endif", KEYWORD_MACRO},
  {"#elif", KEYWORD_MACRO},
  {"#else", KEYWORD_MACRO},
  {"#if", KEYWORD_MACRO},
  {"#error", KEYWORD_MACRO},

  // flow control

  {"switch", KEYWORD_CONTROL},
  {"case", KEYWORD_CONTROL},
  {"if", KEYWORD_CONTROL},
  {"else", KEYWORD_CONTROL},
  {"for", KEYWORD_CONTROL},
  {"while", KEYWORD_CONTROL},
  {"return", KEYWORD_CONTROL},
  {"continue", KEYWORD_CONTROL},
  {"break", KEYWORD_CONTROL},
  {"goto", KEYWORD_CONTROL},
  {"yield", KEYWORD_CONTROL},
  {"foreach", KEYWORD_CONTROL},
  {"default", KEYWORD_CONTROL},
};

static Keyword python_keywords[] = {

  // constants

  {"True", KEYWORD_CONSTANT},
  {"False", KEYWORD_CONSTANT},
  {"None", KEYWORD_CONSTANT},
  {"delete", KEYWORD_CONSTANT},
  {"self", KEYWORD_CONSTANT},

  // types

  {"char", KEYWORD_TYPE},
  {"short", KEYWORD_TYPE},
  {"int", KEYWORD_TYPE},
  {"long", KEYWORD_TYPE},
  {"float", KEYWORD_TYPE},
  {"double", KEYWORD_TYPE},
  {"unsigned", KEYWORD_TYPE},
  {"void", KEYWORD_TYPE},
  {"bool", KEYWORD_TYPE},
  {"byte", KEYWORD_TYPE},

  // function

  #if 0
  {"typeof", KEYWORD_FUNCTION},
  {"sizeof", KEYWORD_FUNCTION},
  {"printf", KEYWORD_FUNCTION},
  {"puts", KEYWORD_FUNCTION},
  {"strcmp", KEYWORD_FUNCTION},
  {"strlen", KEYWORD_FUNCTION},
  {"fprintf", KEYWORD_FUNCTION},
  {"malloc", KEYWORD_FUNCTION},
  {"free", KEYWORD_FUNCTION},
  {"new", KEYWORD_FUNCTION},
  {"delete", KEYWORD_FUNCTION},
  {"fflush", KEYWORD_FUNCTION},
  {"va_start", KEYWORD_FUNCTION},
  {"vfprintf", KEYWORD_FUNCTION},
  {"va_end", KEYWORD_FUNCTION},
  {"abort", KEYWORD_FUNCTION},
  {"exit", KEYWORD_FUNCTION},
  {"min", KEYWORD_FUNCTION},
  {"max", KEYWORD_FUNCTION},
  {"memcmp", KEYWORD_FUNCTION},
  {"putchar", KEYWORD_FUNCTION},
  {"putc", KEYWORD_FUNCTION},
  {"fputc", KEYWORD_FUNCTION},
  {"getchar", KEYWORD_FUNCTION},
  {"swap", KEYWORD_FUNCTION},
  #endif

  // specifiers

  {"def", KEYWORD_SPECIFIER},
  {"const", KEYWORD_SPECIFIER},
  {"extern", KEYWORD_SPECIFIER},
  {"nothrow", KEYWORD_SPECIFIER},
  {"noexcept", KEYWORD_SPECIFIER},
  {"public", KEYWORD_SPECIFIER},
  {"private", KEYWORD_SPECIFIER},
  {"delegate", KEYWORD_SPECIFIER},
  {"protected", KEYWORD_SPECIFIER},
  {"override", KEYWORD_SPECIFIER},
  {"virtual", KEYWORD_SPECIFIER},
  {"abstract", KEYWORD_SPECIFIER},
  {"global", KEYWORD_SPECIFIER},

  // declarations

  {"def", KEYWORD_DEFINITION},
  {"class", KEYWORD_DEFINITION},
  {"import", KEYWORD_DEFINITION},
  {"from", KEYWORD_DEFINITION},
  {"as", KEYWORD_DEFINITION},

  // macro

  // flow control

  {"in", KEYWORD_SPECIFIER},
  {"switch", KEYWORD_CONTROL},
  {"case", KEYWORD_CONTROL},
  {"if", KEYWORD_CONTROL},
  {"else", KEYWORD_CONTROL},
  {"elif", KEYWORD_CONTROL},
  {"for", KEYWORD_CONTROL},
  {"while", KEYWORD_CONTROL},
  {"return", KEYWORD_CONTROL},
  {"continue", KEYWORD_CONTROL},
  {"break", KEYWORD_CONTROL},
  {"goto", KEYWORD_CONTROL},
  {"yield", KEYWORD_CONTROL},
  {"default", KEYWORD_CONTROL},
  {"and", KEYWORD_CONTROL},
  {"or", KEYWORD_CONTROL},
  {"with", KEYWORD_CONTROL},
  {"try", KEYWORD_CONTROL},
  {"except", KEYWORD_CONTROL},
};

enum Language {
  LANGUAGE_NULL,
  LANGUAGE_C,
  LANGUAGE_PYTHON,
  NUM_LANGUAGES
};

StaticArray<Keyword> keywords[] = {
  // LANGUAGE_NULL
  {},
  // LANGUAGE_C
  {cpp_keywords, ARRAY_LEN(cpp_keywords)},
  // LANGUAGE_PYTHON
  {python_keywords, ARRAY_LEN(python_keywords)},
};
STATIC_ASSERT(ARRAY_LEN(keywords) == NUM_LANGUAGES, all_keywords_defined);

Slice line_comments[] = {
  // LANGUAGE_NULL
  {},
  // LANGUAGE_C
  Slice::create("//"),
  // LANGUAGE_PYTHON
  Slice::create("#")
};
STATIC_ASSERT(ARRAY_LEN(line_comments) == NUM_LANGUAGES, all_line_comments_defined);

// MUST BE REVERSE SIZE ORDER
static const Slice cpp_operators[] = {
  {(char*)"===", 3},
  {(char*)"!==", 3},
  {(char*)"<<=", 3},
  {(char*)">>=", 3},
  {(char*)"||", 2},
  {(char*)"&&", 2},
  {(char*)"==", 2},
  {(char*)"!=", 2},
  {(char*)"<<", 2},
  {(char*)">>", 2},
  {(char*)"++", 2},
  {(char*)"::", 2},
  {(char*)"--", 2},
  {(char*)"+", 1},
  {(char*)"-", 1},
  {(char*)"*", 1},
  {(char*)"/", 1},
  {(char*)"&", 1},
  {(char*)"%", 1},
  {(char*)"=", 1},
  {(char*)":", 1},
  {(char*)"<", 1},
  {(char*)">", 1},
};

static const Slice python_operators[] = {
  {(char*)"===", 3},
  {(char*)"!==", 3},
  {(char*)"<<=", 3},
  {(char*)">>=", 3},
  {(char*)"||", 2},
  {(char*)"&&", 2},
  {(char*)"==", 2},
  {(char*)"!=", 2},
  {(char*)"<<", 2},
  {(char*)">>", 2},
  {(char*)"++", 2},
  {(char*)"::", 2},
  {(char*)"--", 2},
  {(char*)"+", 1},
  {(char*)"-", 1},
  {(char*)"*", 1},
  {(char*)"/", 1},
  {(char*)"&", 1},
  {(char*)"%", 1},
  {(char*)"=", 1},
  {(char*)":", 1},
  {(char*)"<", 1},
  {(char*)">", 1},
};

static bool is_number_head(char c) {
  return isdigit(c);
}

static bool is_number_tail(char c) {
  return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') || c == 'x';
}

static bool is_number_modifier(char c) {
  return c == 'u' || c == 'l' || c == 'L' || c == 'f';
}

static bool is_identifier_head(char c) {
  return isalpha(c) || c == '_' || c == '#';
}

static bool is_identifier_head(Utf8char c) {
  return c.is_ansi() && is_identifier_head(c.ansi());
}

static bool is_identifier_tail(char c) {
  return isalnum(c) || c == '_';
}

static bool is_identifier_tail(Utf8char c) {
  return c.is_ansi() && is_identifier_tail(c.ansi());
}

struct TokenInfo {
  Token token;
  union {
    struct {
      Pos a;
      Pos b;
    };
    Range r;
  };
  Slice str;
};

void util_free(TokenInfo) {}

struct ParseResult {
  Array<TokenInfo> tokens;
  Array<Range> definitions;
  Array<String> identifiers;
};

#define NEXT_CHAR(n) (x += n, c = line[x])

static bool parse_identifier(Slice line, int &x) {
  char c = line[x];
  if (!is_identifier_head(c))
    return false;
  NEXT_CHAR(1);
  while (is_identifier_tail(c))
    NEXT_CHAR(1);
  return true;
}

static bool parse_number(Slice line, int &x) {
  char c = line[x];
  if (!is_number_head(c))
    return false;
  NEXT_CHAR(1);
  while (is_number_tail(c))
    NEXT_CHAR(1);
  if (line[x] == '.' && x+1 < line.length && isdigit(line[x+1])) {
    NEXT_CHAR(2);
    while (isdigit(c))
      NEXT_CHAR(1);
  }
  while (is_number_modifier(c))
    NEXT_CHAR(1);
  return true;
}

static bool parse_string(Slice line, int &x) {
  char c = line[x];
  if (c != '"' && c != '\'')
    return false;

  const char str_char = c;
  NEXT_CHAR(1);
  while (1) {
    if (x >= line.length)
      break;
    if (c == str_char && (line[x-1] != '\\' || (x >= 2 && line[x-2] == '\\')))
      break;
    NEXT_CHAR(1);
  }
  if (x < line.length)
    NEXT_CHAR(1);
  return true;
}

static ParseResult python_parse(const Array<StringBuffer> lines) {
  Array<TokenInfo> tokens = {};
  Array<String> identifiers = {};
  Array<Range> definitions = {};

  int x = 0;
  int y = 0;

  // parse
  for (;;) {
    TokenInfo t = {TOKEN_NULL, x, y};
    if (y >= lines.size)
      break;
    Slice line = lines[y].slice;

    // endline
    char c;
    if (x >= lines[y].length) {
      ++y, x = 0;
      continue;
    }
    c = line[x];

    // whitespace
    if (isspace(c)) {
      NEXT_CHAR(1);
      goto token_done;
    }

    // line comment
    if (c == '#') {
      t.token = TOKEN_LINE_COMMENT;
      x = line.length;
      goto token_done;
    }

    // identifier
    if (parse_identifier(line, x)) {
      t.token = TOKEN_IDENTIFIER;
      goto token_done;
    }

    // number
    if (parse_number(line, x)) {
      t.token = TOKEN_NUMBER;
      goto token_done;
    }

    // string
    if (parse_string(line, x)) {
      t.token = TOKEN_STRING;
      goto token_done;
    }

    // operators
    for (int i = 0; i < (int)ARRAY_LEN(python_operators); ++i) {
      if (line.begins_with(x, python_operators[i])) {
        t.token = TOKEN_OPERATOR;
        NEXT_CHAR(python_operators[i].length);
        goto token_done;
      }
    }

    // single char token
    t.token = (Token)c;
    NEXT_CHAR(1);

    token_done:;
    if (t.token != TOKEN_NULL) {
      t.b = {x,y};
      if (t.a.y == t.b.y)
        t.str = lines[t.a.y](t.a.x, t.b.x);
      tokens += t;

      // add to identifier list
      if (t.token == TOKEN_IDENTIFIER) {
        Slice identifier = line(t.a.x, t.b.x);
        if (!identifiers.find(identifier))
          identifiers += String::create(identifier);
      }
    }
    #undef NEXT_CHAR
  }

  tokens += {TOKEN_EOF, 0, lines.size, 0, lines.size};

  // find definitions
  for (int i = 0; i < tokens.size; ++i) {
    TokenInfo ti = tokens[i];
    switch (ti.token) {
      case TOKEN_IDENTIFIER:
        if (i+1 < tokens.size && (ti.str == "def" || ti.str == "class")) {
          definitions += tokens[i+1].r;
          break;
        }
        break;

      default:
        break;
    }
  }
  return {tokens, definitions, identifiers};
}

static ParseResult cpp_parse(const Array<StringBuffer> lines) {
  Array<TokenInfo> tokens = {};
  Array<String> identifiers = {};
  Array<Range> definitions = {};

  int x = 0;
  int y = 0;

  // parse
  for (;;) {
    TokenInfo t = {TOKEN_NULL, x, y};
    #define NEXT_CHAR(n) (x += n, c = line[x])
    if (y >= lines.size)
      break;
    Slice line = lines[y].slice;

    // endline
    char c;
    if (x >= lines[y].length) {
      ++y, x = 0;
      continue;
    }
    c = line[x];

    // whitespace
    if (isspace(c)) {
      NEXT_CHAR(1);
      goto token_done;
    }

    // identifier
    if (parse_identifier(line, x)) {
      t.token = TOKEN_IDENTIFIER;
      goto token_done;
    }

    // block comment
    if (line.begins_with(x, "/*")) {
      t.token = TOKEN_BLOCK_COMMENT;
      NEXT_CHAR(2);
      // goto matching end block
      for (;;) {
        // EOF
        if (y >= lines.size)
          goto token_done;
        // EOL
        if (x >= line.length) {
          ++y;
          if (y == lines.size)
            break;
          line = lines[y].slice;
          x = 0;
          continue;
        }
        // End block
        if (line.begins_with(x, "*/")) {
          NEXT_CHAR(2);
          break;
        }
        NEXT_CHAR(1);
      }
      goto token_done;
    }

    // line comment
    if (line.begins_with(x, "//")) {
      t.token = TOKEN_LINE_COMMENT;
      x = line.length;
      goto token_done;
    }

    // number
    if (parse_number(line, x)) {
      t.token = TOKEN_NUMBER;
      goto token_done;
    }

    // string
    if (parse_string(line, x)) {
      t.token = TOKEN_STRING;
      goto token_done;
    }

    // operators
    for (int i = 0; i < (int)ARRAY_LEN(cpp_operators); ++i) {
      if (line.begins_with(x, cpp_operators[i])) {
        t.token = TOKEN_OPERATOR;
        NEXT_CHAR(cpp_operators[i].length);
        goto token_done;
      }
    }

    // single char token
    t.token = (Token)c;
    NEXT_CHAR(1);

    token_done:;
    if (t.token != TOKEN_NULL) {
      t.b = {x,y};
      if (t.a.y == t.b.y)
        t.str = lines[t.a.y](t.a.x, t.b.x);
      tokens += t;
      
      // add to identifier list
      if (t.token == TOKEN_IDENTIFIER) {
        Slice identifier = line(t.a.x, t.b.x);
        if (!identifiers.find(identifier))
          identifiers += String::create(identifier);
      }
    }
    #undef NEXT_CHAR
  }

  tokens += {TOKEN_EOF, 0, lines.size, 0, lines.size};

  // find definitions
  for (int i = 0; i < tokens.size; ++i) {
    TokenInfo ti = tokens[i];
    switch (ti.token) {
      case TOKEN_IDENTIFIER: {
        if (i+1 < tokens.size && ti.str == "#define") {
          definitions += tokens[i+1].r;
          break;
        }

        if (i+2 < tokens.size && (ti.str == "struct" || ti.str == "enum" || ti.str == "class") &&
            tokens[i+1].token == TOKEN_IDENTIFIER &&
            tokens[i+2].token == '{') {
          definitions += tokens[i+1].r;
          break;
        }

        // check for function definition
        {
          // is it a keyword, then ignore (things like else if (..) is not a definition)
          for (Keyword keyword : cpp_keywords)
            if (ti.str == keyword.name && keyword.type != KEYWORD_TYPE)
              goto no_definition;

          {
            int j = i;
            // skip pointer and references
            for (++j; j < tokens.size && tokens[j].token == TOKEN_OPERATOR; ++j) {
              if (tokens[j].str == "*" || tokens[j].str == "&")
                continue;
              goto no_definition;
            }

            if (j+1 < tokens.size &&
                tokens[j].token == TOKEN_IDENTIFIER &&
                tokens[j+1].token == '(') {
              definitions += {tokens[j].a, tokens[j].b};
            }
            else if (j+3 < tokens.size &&
                     tokens[j].token == TOKEN_IDENTIFIER &&
                     tokens[j+1].token == TOKEN_OPERATOR &&
                     tokens[j+1].str == "::" &&
                     tokens[j+2].token == TOKEN_IDENTIFIER &&
                     tokens[j+3].token == '(') {
              definitions += {tokens[j].a, tokens[j+2].b};
            }
          }
          no_definition:;
        }
        break;}
      default:
        break;
    }
  }
  return {tokens, definitions, identifiers};
}

static ParseResult parse(const Array<StringBuffer> lines, Language language) {
  switch (language) {
    case LANGUAGE_NULL: return {};
    case LANGUAGE_C: return cpp_parse(lines);
    case LANGUAGE_PYTHON: return python_parse(lines);
    default:
      log_err("Unknown language %i\n", (int)language);
      return {};
  }
}

#endif /* PARSE_CPP */