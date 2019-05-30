#ifndef PARSE_CPP
#define PARSE_CPP

enum Token {
  TOKEN_NULL                =  0,
  TOKEN_IDENTIFIER          = -2,
  TOKEN_NUMBER              = -3,
  TOKEN_STRING              = -4,
  TOKEN_STRING_BEGIN        = -6,
  TOKEN_BLOCK_COMMENT       = -7,
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

struct Keyword {
  const char *name;
  KeywordType type;
};

static Keyword cpp_keywords[] = {

  // constants

  {"true", KEYWORD_CONSTANT},
  {"false", KEYWORD_CONSTANT},
  {"NULL", KEYWORD_CONSTANT},
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
  {"uint", KEYWORD_TYPE},
  {"u64", KEYWORD_TYPE},
  {"u32", KEYWORD_TYPE},
  {"u16", KEYWORD_TYPE},
  {"u8", KEYWORD_TYPE},
  {"i64", KEYWORD_TYPE},
  {"i32", KEYWORD_TYPE},
  {"i16", KEYWORD_TYPE},
  {"i8", KEYWORD_TYPE},
  {"va_list", KEYWORD_TYPE},
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
  {"protected", KEYWORD_SPECIFIER},
  {"override", KEYWORD_SPECIFIER},
  {"virtual", KEYWORD_SPECIFIER},
  {"abstract", KEYWORD_SPECIFIER},
  {"delete", KEYWORD_SPECIFIER},
  {"new", KEYWORD_SPECIFIER},
  {"auto", KEYWORD_SPECIFIER},
  {"sizeof", KEYWORD_SPECIFIER},
  {"alignof", KEYWORD_SPECIFIER},
  {"constexpr", KEYWORD_SPECIFIER},

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
  {"try", KEYWORD_CONTROL},
  {"catch", KEYWORD_CONTROL},
  {"throw", KEYWORD_CONTROL},
  {"case", KEYWORD_CONTROL},
  {"if", KEYWORD_CONTROL},
  {"else", KEYWORD_CONTROL},
  {"for", KEYWORD_CONTROL},
  {"while", KEYWORD_CONTROL},
  {"do", KEYWORD_CONTROL},
  {"return", KEYWORD_CONTROL},
  {"continue", KEYWORD_CONTROL},
  {"break", KEYWORD_CONTROL},
  {"goto", KEYWORD_CONTROL},
  {"yield", KEYWORD_CONTROL},
  {"default", KEYWORD_CONTROL},
};

static Keyword csharp_keywords[] = {

  // constants

  {"true", KEYWORD_CONSTANT},
  {"false", KEYWORD_CONSTANT},
  {"NULL", KEYWORD_CONSTANT},
  {"null", KEYWORD_CONSTANT},
  {"this", KEYWORD_CONSTANT},
  {"value", KEYWORD_CONSTANT},

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
  {"string", KEYWORD_TYPE},

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
  {"var", KEYWORD_SPECIFIER},
  {"extern", KEYWORD_SPECIFIER},
  {"nothrow", KEYWORD_SPECIFIER},
  {"noexcept", KEYWORD_SPECIFIER},
  {"public", KEYWORD_SPECIFIER},
  {"unsafe", KEYWORD_SPECIFIER},
  {"private", KEYWORD_SPECIFIER},
  {"in", KEYWORD_SPECIFIER},
  {"delegate", KEYWORD_SPECIFIER},
  {"protected", KEYWORD_SPECIFIER},
  {"override", KEYWORD_SPECIFIER},
  {"virtual", KEYWORD_SPECIFIER},
  {"abstract", KEYWORD_SPECIFIER},
  {"delete", KEYWORD_SPECIFIER},
  {"new", KEYWORD_SPECIFIER},
  {"using", KEYWORD_SPECIFIER},
  {"get", KEYWORD_SPECIFIER},
  {"set", KEYWORD_SPECIFIER},
  {"partial", KEYWORD_SPECIFIER},
  {"out", KEYWORD_SPECIFIER},
  {"ref", KEYWORD_SPECIFIER},
  {"sealed", KEYWORD_SPECIFIER},
  {"internal", KEYWORD_SPECIFIER},
  {"readonly", KEYWORD_SPECIFIER},

  // declarations

  {"struct", KEYWORD_DEFINITION},
  {"interface", KEYWORD_DEFINITION},
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
  {"do", KEYWORD_CONTROL},
  {"return", KEYWORD_CONTROL},
  {"continue", KEYWORD_CONTROL},
  {"break", KEYWORD_CONTROL},
  {"goto", KEYWORD_CONTROL},
  {"yield", KEYWORD_CONTROL},
  {"foreach", KEYWORD_CONTROL},
  {"throw", KEYWORD_CONTROL},
  {"default", KEYWORD_CONTROL},
  {"try", KEYWORD_CONTROL},
  {"catch", KEYWORD_CONTROL},
  {"finally", KEYWORD_CONTROL},
  {"with", KEYWORD_CONTROL},
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

  {"in", KEYWORD_CONTROL},
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
  {"raise", KEYWORD_CONSTANT},
  {"and", KEYWORD_CONTROL},
  {"not", KEYWORD_CONTROL},
  {"or", KEYWORD_CONTROL},
  {"with", KEYWORD_CONTROL},
  {"try", KEYWORD_CONTROL},
  {"except", KEYWORD_CONTROL},
  {"del", KEYWORD_CONTROL},
};

static Keyword julia_keywords[] = {

  // constants

  {"true", KEYWORD_CONSTANT},
  {"false", KEYWORD_CONSTANT},

  // types

  {"Int", KEYWORD_TYPE},
  {"Int32", KEYWORD_TYPE},
  {"Int64", KEYWORD_TYPE},
  {"Float", KEYWORD_TYPE},
  {"Float64", KEYWORD_TYPE},
  {"Float32", KEYWORD_TYPE},
  {"Double", KEYWORD_TYPE},
  {"unsigned", KEYWORD_TYPE},
  {"Vector", KEYWORD_TYPE},
  {"Array", KEYWORD_TYPE},

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
  {"mutable", KEYWORD_SPECIFIER},

  // declarations

  {"function", KEYWORD_DEFINITION},
  {"struct", KEYWORD_DEFINITION},
  {"immutable", KEYWORD_DEFINITION},
  {"using", KEYWORD_DEFINITION},
  {"export", KEYWORD_DEFINITION},
  {"as", KEYWORD_DEFINITION},
  {"module", KEYWORD_DEFINITION},

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
  {"do", KEYWORD_CONTROL},
  {"default", KEYWORD_CONTROL},
  {"and", KEYWORD_CONTROL},
  {"or", KEYWORD_CONTROL},
  {"with", KEYWORD_CONTROL},
  {"try", KEYWORD_CONTROL},
  {"except", KEYWORD_CONTROL},
  {"end", KEYWORD_CONTROL},
  {"elseif", KEYWORD_CONTROL},
};

static Keyword go_keywords[] = {

  // constants

  {"true", KEYWORD_CONSTANT},
  {"false", KEYWORD_CONSTANT},
  {"nil", KEYWORD_CONSTANT},

  // types

  {"int",        KEYWORD_TYPE},
  {"int8",       KEYWORD_TYPE},
  {"int16",      KEYWORD_TYPE},
  {"int32",      KEYWORD_TYPE},
  {"int64",      KEYWORD_TYPE},
  {"uint",       KEYWORD_TYPE},
  {"uint8",      KEYWORD_TYPE},
  {"uint16",     KEYWORD_TYPE},
  {"uint32",     KEYWORD_TYPE},
  {"uint64",     KEYWORD_TYPE},
  {"byte",       KEYWORD_TYPE},
  {"rune",       KEYWORD_TYPE},
  {"float32",    KEYWORD_TYPE},
  {"float64",    KEYWORD_TYPE},
  {"complex64",  KEYWORD_TYPE},
  {"complex128", KEYWORD_TYPE},
  {"error",      KEYWORD_TYPE},
  {"string",     KEYWORD_TYPE},
  {"bool",       KEYWORD_TYPE},

  // specifiers

  {"extern",    KEYWORD_SPECIFIER},
  {"nothrow",   KEYWORD_SPECIFIER},
  {"noexcept",  KEYWORD_SPECIFIER},
  {"public",    KEYWORD_SPECIFIER},
  {"private",   KEYWORD_SPECIFIER},
  {"delegate",  KEYWORD_SPECIFIER},
  {"protected", KEYWORD_SPECIFIER},
  {"override",  KEYWORD_SPECIFIER},
  {"virtual",   KEYWORD_SPECIFIER},
  {"abstract",  KEYWORD_SPECIFIER},
  {"global",    KEYWORD_SPECIFIER},
  {"mutable",   KEYWORD_SPECIFIER},

  // declarations

  {"const",     KEYWORD_DEFINITION},
  {"type",      KEYWORD_DEFINITION},
  {"func",      KEYWORD_DEFINITION},
  {"struct",    KEYWORD_DEFINITION},
  {"immutable", KEYWORD_DEFINITION},
  {"using",     KEYWORD_DEFINITION},
  {"export",    KEYWORD_DEFINITION},
  {"import",    KEYWORD_DEFINITION},
  {"as",        KEYWORD_DEFINITION},
  {"package",   KEYWORD_DEFINITION},
  {"var",       KEYWORD_DEFINITION},

  // macro

  // flow control

  {"in",       KEYWORD_CONTROL},
  {"defer",    KEYWORD_CONTROL},
  {"switch",   KEYWORD_CONTROL},
  {"case",     KEYWORD_CONTROL},
  {"if",       KEYWORD_CONTROL},
  {"else",     KEYWORD_CONTROL},
  {"elif",     KEYWORD_CONTROL},
  {"for",      KEYWORD_CONTROL},
  {"while",    KEYWORD_CONTROL},
  {"return",   KEYWORD_CONTROL},
  {"continue", KEYWORD_CONTROL},
  {"break",    KEYWORD_CONTROL},
  {"goto",     KEYWORD_CONTROL},
  {"yield",    KEYWORD_CONTROL},
  {"do",       KEYWORD_CONTROL},
  {"default",  KEYWORD_CONTROL},
  {"and",      KEYWORD_CONTROL},
  {"or",       KEYWORD_CONTROL},
  {"with",     KEYWORD_CONTROL},
  {"try",      KEYWORD_CONTROL},
  {"except",   KEYWORD_CONTROL},
  {"end",      KEYWORD_CONTROL},
  {"elseif",   KEYWORD_CONTROL},
  {"fallthrough", KEYWORD_CONTROL},
  {"range", KEYWORD_CONTROL},
};

static Keyword bash_keywords[] = {

  // constants

  {"true", KEYWORD_CONSTANT},
  {"false", KEYWORD_CONSTANT},
  {"unset", KEYWORD_CONSTANT},

  // types

  // function

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

  {"export", KEYWORD_DEFINITION},
  {"set", KEYWORD_DEFINITION},
  {"function", KEYWORD_DEFINITION},

  // macro

  // flow control

  {"switch", KEYWORD_CONTROL},
  {"case", KEYWORD_CONTROL},
  {"in", KEYWORD_CONTROL},
  {"esac", KEYWORD_CONTROL},
  {"if", KEYWORD_CONTROL},
  {"elif", KEYWORD_CONTROL},
  {"else", KEYWORD_CONTROL},
  {"fi", KEYWORD_CONTROL},
  {"for", KEYWORD_CONTROL},
  {"then", KEYWORD_CONTROL},
  {"while", KEYWORD_CONTROL},
  {"do", KEYWORD_CONTROL},
  {"done", KEYWORD_CONTROL},
  {"return", KEYWORD_CONTROL},
  {"continue", KEYWORD_CONTROL},
  {"break", KEYWORD_CONTROL},
};

static Keyword makefile_keywords[] = {

  // constants

  {"true", KEYWORD_CONSTANT},
  {"false", KEYWORD_CONSTANT},
  {"unset", KEYWORD_CONSTANT},

  // types

  // function

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

  {"export", KEYWORD_DEFINITION},
  {"function", KEYWORD_DEFINITION},

  // macro

  // flow control

  {"switch", KEYWORD_CONTROL},
  {"ifeq", KEYWORD_CONTROL},
  {"ifdef", KEYWORD_CONTROL},
  {"ifndef", KEYWORD_CONTROL},
  {"endif", KEYWORD_CONTROL},
  {"shell", KEYWORD_CONTROL},
  {"case", KEYWORD_CONTROL},
  {"in", KEYWORD_CONTROL},
  {"esac", KEYWORD_CONTROL},
  {"if", KEYWORD_CONTROL},
  {"elif", KEYWORD_CONTROL},
  {"else", KEYWORD_CONTROL},
  {"fi", KEYWORD_CONTROL},
  {"for", KEYWORD_CONTROL},
  {"then", KEYWORD_CONTROL},
  {"while", KEYWORD_CONTROL},
  {"do", KEYWORD_CONTROL},
  {"done", KEYWORD_CONTROL},
  {"return", KEYWORD_CONTROL},
  {"continue", KEYWORD_CONTROL},
  {"break", KEYWORD_CONTROL},
};

static Keyword terraform_keywords[] = {

  // constants

  {"true", KEYWORD_CONSTANT},
  {"false", KEYWORD_CONSTANT},
  {"null", KEYWORD_CONSTANT},

  // types

  // function

  // specifiers

  // declarations

  {"data", KEYWORD_DEFINITION},
  {"resource", KEYWORD_DEFINITION},
  {"variable", KEYWORD_DEFINITION},
  {"output", KEYWORD_DEFINITION},

  // macro

  // flow control

  {"switch", KEYWORD_CONTROL},
  {"case", KEYWORD_CONTROL},
  {"in", KEYWORD_CONTROL},
  {"esac", KEYWORD_CONTROL},
  {"if", KEYWORD_CONTROL},
  {"elif", KEYWORD_CONTROL},
  {"else", KEYWORD_CONTROL},
  {"fi", KEYWORD_CONTROL},
  {"for", KEYWORD_CONTROL},
  {"then", KEYWORD_CONTROL},
  {"while", KEYWORD_CONTROL},
  {"do", KEYWORD_CONTROL},
  {"done", KEYWORD_CONTROL},
  {"return", KEYWORD_CONTROL},
  {"continue", KEYWORD_CONTROL},
  {"break", KEYWORD_CONTROL},
};

template<int N>
static bool is_keyword(Slice str, Keyword (&keywords)[N]) {
  for (int i = 0; i < N; ++i)
    if (str == keywords[i].name && keywords[i].type != KEYWORD_TYPE)
      return true;
  return false;
}

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

static const Slice text_operators[] = {
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

static const Slice julia_operators[] = {
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

static const Slice terraform_operators[] = {
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

static const Slice go_operators[] = {
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

static const Slice bash_operators[] = {
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

static const Slice makefile_operators[] = {
  {(char*)"||", 2},
  {(char*)"&&", 2},
  {(char*)"==", 2},
  {(char*)"!=", 2},
  {(char*)"<<", 2},
  {(char*)">>", 2},
  {(char*)"++", 2},
  {(char*)"::", 2},
  {(char*)":=", 2},
  {(char*)"?=", 2},
  {(char*)"--", 2},
  {(char*)"+", 1},
  {(char*)"-", 1},
  {(char*)"*", 1},
  {(char*)"/", 1},
  {(char*)"@", 1},
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
  return isalpha(c) || c == '_' || c == '#' || c == '$';
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

struct Range {
  Pos a;
  Pos b;
  bool contains(Pos p) {return p >= a && p < b;}
};
static void util_free(Range) {}

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

static void util_free(ParseResult &p) {
  util_free(p.tokens);
  util_free(p.definitions);
  util_free(p.identifiers);
}

#define NEXT_CHAR(n) (x += n, c = line[x])

static bool parse_identifier(Slice line, int &x, TokenInfo &t, const char *additional_identifier_heads, const char *additional_identifier_tails) {
  char c = line[x];
  if (!is_identifier_head(c) && !Slice::contains(additional_identifier_heads, c))
    return false;
  NEXT_CHAR(1);
  while (is_identifier_tail(c) || Slice::contains(additional_identifier_tails, c))
    NEXT_CHAR(1);
  t.token = TOKEN_IDENTIFIER;
  return true;
}

static bool parse_identifier(Slice line, int &x, TokenInfo &t) {
  char c = line[x];
  if (!is_identifier_head(c))
    return false;
  NEXT_CHAR(1);
  while (is_identifier_tail(c))
    NEXT_CHAR(1);
  t.token = TOKEN_IDENTIFIER;
  return true;
}

static bool parse_number(Slice line, int &x, TokenInfo &t) {
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
  t.token = TOKEN_NUMBER;
  return true;
}

static bool parse_string(Slice line, int &x, TokenInfo &t) {
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
  t.token = TOKEN_STRING;
  return true;
}

static bool parse_multiline_string(Slice line, Array<StringBuffer> lines, int &x, int &y, TokenInfo &t, const char *quote) {
  Slice q = Slice::create(quote);
  char c = line[x];
  if (!line.begins_with(x, q))
    return false;

  NEXT_CHAR(q.length);
  // goto matching end block
  for (;;) {
    // EOF
    if (y >= lines.size)
      goto done;
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
    if (line.begins_with(x, q)) {
      NEXT_CHAR(q.length);
      break;
    }
    NEXT_CHAR(1);
  }
  done:
  t.token = TOKEN_STRING;
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
      continue;
    }

    // line comment
    if (c == '#') {
      t.token = TOKEN_LINE_COMMENT;
      x = line.length;
      goto token_done;
    }

    // identifier
    if (parse_identifier(line, x, t, "@", ""))
      goto token_done;

    // number
    if (parse_number(line, x, t))
      goto token_done;

    // triple quoted string
    if (parse_multiline_string(line, lines, x, y, t, "\"\"\""))
      goto token_done;

    // string
    if (parse_string(line, x, t))
      goto token_done;

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

static ParseResult colorscheme_parse(const Array<StringBuffer> lines) {
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
      continue;
    }

    // identifier
    if (parse_identifier(line, x, t, "#", ""))
      goto token_done;

    // number
    if (parse_number(line, x, t))
      goto token_done;

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
  }

  tokens += {TOKEN_EOF, 0, lines.size, 0, lines.size};

  return {tokens, definitions, identifiers};
}

static ParseResult julia_parse(const Array<StringBuffer> lines) {
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
      continue;
    }

    // line comment
    if (c == '#') {
      t.token = TOKEN_LINE_COMMENT;
      x = line.length;
      goto token_done;
    }

    // identifier
    if (parse_identifier(line, x, t, "@", ""))
      goto token_done;

    // triple quoted string
    if (parse_multiline_string(line, lines, x, y, t, "\"\"\""))
      goto token_done;

    // backtick string
    if (parse_multiline_string(line, lines, x, y, t, "`"))
      goto token_done;

    // number
    if (parse_number(line, x, t))
      goto token_done;

    // string
    if (parse_string(line, x, t))
      goto token_done;

    // operators
    for (int i = 0; i < (int)ARRAY_LEN(julia_operators); ++i) {
      if (line.begins_with(x, julia_operators[i])) {
        t.token = TOKEN_OPERATOR;
        NEXT_CHAR(julia_operators[i].length);
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
  }

  tokens += {TOKEN_EOF, 0, lines.size, 0, lines.size};

  // find definitions
  for (int i = 0; i < tokens.size; ++i) {
    TokenInfo ti = tokens[i];
    switch (ti.token) {
      case TOKEN_IDENTIFIER:
        if (i+1 < tokens.size && (ti.str == "function" || ti.str == "struct" || ti.str == "const" || ti.str == "immutable")) {
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

static ParseResult terraform_parse(const Array<StringBuffer> lines) {
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
      continue;
    }

    // line comment
    if (c == '#') {
      t.token = TOKEN_LINE_COMMENT;
      x = line.length;
      goto token_done;
    }

    // identifier
    if (parse_identifier(line, x, t))
      goto token_done;

    // number
    if (parse_number(line, x, t))
      goto token_done;

    // string
    if (parse_string(line, x, t))
      goto token_done;

    // operators
    for (int i = 0; i < (int)ARRAY_LEN(terraform_operators); ++i) {
      if (line.begins_with(x, terraform_operators[i])) {
        t.token = TOKEN_OPERATOR;
        NEXT_CHAR(terraform_operators[i].length);
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
  }

  tokens += {TOKEN_EOF, 0, lines.size, 0, lines.size};

  // find definitions
  for (int i = 0; i < tokens.size; ++i) {
    TokenInfo ti = tokens[i];
    switch (ti.token) {
      case TOKEN_IDENTIFIER:
        if (i+1 < tokens.size && (ti.str == "resource" || ti.str == "data" || ti.str == "variable" || ti.str == "output")) {
          if (i+2 < tokens.size && tokens[i+1].token == TOKEN_STRING && tokens[i+2].token == TOKEN_STRING)
            definitions += tokens[i+2].r;
          else
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

static ParseResult go_parse(const Array<StringBuffer> lines) {
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
      continue;
    }

    // line comment
    if (line.begins_with(x, "//")) {
      t.token = TOKEN_LINE_COMMENT;
      x = line.length;
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

    // identifier
    if (parse_identifier(line, x, t, "@", ""))
      goto token_done;

    // number
    if (parse_number(line, x, t))
      goto token_done;

    // tick string
    if (parse_multiline_string(line, lines, x, y, t, "`"))
      goto token_done;

    // string
    if (parse_string(line, x, t))
      goto token_done;

    // operators
    for (int i = 0; i < (int)ARRAY_LEN(go_operators); ++i) {
      if (line.begins_with(x, go_operators[i])) {
        t.token = TOKEN_OPERATOR;
        NEXT_CHAR(go_operators[i].length);
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
  }

  tokens += {TOKEN_EOF, 0, lines.size, 0, lines.size};

  // find definitions
  for (int i = 0; i < tokens.size; ++i) {
    TokenInfo ti = tokens[i];
    switch (ti.token) {
      case TOKEN_IDENTIFIER:
        if (i+1 < tokens.size && ti.str == "func") {
          if (tokens[i+1].token == TOKEN_IDENTIFIER) {
            definitions += tokens[i+1].r;
            break;
          }

          // parse methods
          else if (tokens[i+1].str == "(") {
            ++i;
            for (int depth = 0; i < tokens.size; ++i) {
              if (tokens[i].str == "(") ++depth;
              if (tokens[i].str == ")") --depth;
              if (depth == 0) break;
            }
            if (i+2 < tokens.size && tokens[i+1].token == TOKEN_IDENTIFIER && tokens[i+2].str == "(") {
              definitions += tokens[i+1].r;
              break;
            }
          }
        }

        else if (i+1 < tokens.size && ti.str == "type" && tokens[i+1].token == TOKEN_IDENTIFIER) {
          definitions += tokens[i+1].r;
          break;
        }
        // const
        else if (i+1 < tokens.size && tokens[i+1].token == TOKEN_IDENTIFIER && ti.str == "const") {
          definitions += tokens[i+1].r;
          break;
        }
        // const ( <const list> )
        else if (i+1 < tokens.size && ti.str == "const" && tokens[i+1].str == "(") {
          for (i += 2; i < tokens.size; ++i) {
            if (tokens[i].str == ")")
              break;
            // identifier =
            if (i+1 < tokens.size && tokens[i].token == TOKEN_IDENTIFIER && tokens[i+1].str == "=")
              definitions += tokens[i].r;
          }
        }
        break;

      default:
        break;
    }
  }
  return {tokens, definitions, identifiers};
}

static ParseResult bash_parse(const Array<StringBuffer> lines) {
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
      continue;
    }

    // line comment
    if (c == '#') {
      t.token = TOKEN_LINE_COMMENT;
      x = line.length;
      goto token_done;
    }

    // identifier
    if (parse_identifier(line, x, t, "0123456789", "-+"))
      goto token_done;

    // number
    if (parse_number(line, x, t))
      goto token_done;

    // multiline string
    if (parse_multiline_string(line, lines, x, y, t, "'"))
      goto token_done;

    // string
    if (parse_string(line, x, t))
      goto token_done;

    // operators
    for (int i = 0; i < (int)ARRAY_LEN(bash_operators); ++i) {
      if (line.begins_with(x, bash_operators[i])) {
        t.token = TOKEN_OPERATOR;
        NEXT_CHAR(bash_operators[i].length);
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
  }

  tokens += {TOKEN_EOF, 0, lines.size, 0, lines.size};

  // find definitions
  for (int i = 0; i < tokens.size; ++i) {
    TokenInfo *ti = tokens+i;
    switch (ti->token) {
      case TOKEN_IDENTIFIER:
        if      (i+1 < tokens.size && (ti->str == "function" || ti->str == "export"))
          definitions += ti[1].r, ++i;
        else if (i+1 < tokens.size && (ti[1].str == "=" || ti[1].str == "(") && (i == 0 || ti[-1].a.y < ti[0].a.y))
          definitions += ti[0].r, ++i;
        break;

      default:
        break;
    }
  }
  return {tokens, definitions, identifiers};
}

static ParseResult makefile_parse(const Array<StringBuffer> lines) {
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
      continue;
    }

    // line comment
    if (c == '#') {
      t.token = TOKEN_LINE_COMMENT;
      x = line.length;
      goto token_done;
    }

    // identifier
    if (parse_identifier(line, x, t, "0123456789", "-+"))
      goto token_done;

    // number
    if (parse_number(line, x, t))
      goto token_done;

    // multiline string
    if (parse_multiline_string(line, lines, x, y, t, "'"))
      goto token_done;

    // string
    if (parse_string(line, x, t))
      goto token_done;

    // operators
    for (int i = 0; i < (int)ARRAY_LEN(makefile_operators); ++i) {
      if (line.begins_with(x, makefile_operators[i])) {
        t.token = TOKEN_OPERATOR;
        NEXT_CHAR(makefile_operators[i].length);
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
  }

  tokens += {TOKEN_EOF, 0, lines.size, 0, lines.size};

  // find definitions
  for (int i = 0; i < tokens.size; ++i) {
    TokenInfo *ti = tokens+i;
    switch (ti->token) {
      case TOKEN_IDENTIFIER:
        if      (i+1 < tokens.size && (ti->str == "function" || ti->str == "export"))
          definitions += ti[1].r, ++i;
        // variable definitions
        // x := 1
        // x = 1
        else if (i+1 < tokens.size && (ti[1].str == ":" || ti[1].str == ":=" || ti[1].str == "=" || ti[1].str == "?=") && (i == 0 || ti[-1].a.y < ti[0].a.y))
          definitions += ti[0].r, ++i;
        // function of style
        // x() {}
        else if (i+1 < tokens.size && ti[1].str == "(" && (i == 0 || ti[-1].a.y < ti[0].a.y)) {
          // walk through param list, and check that it is followed by a '{'
          int depth = 1;
          int j = i+2;
          for (; j < tokens.size && depth; ++j) {
            if (tokens[j].token == '(') ++depth;
            if (tokens[j].token == ')') --depth;
          }
          if (depth == 0 && j < tokens.size && tokens[j].token == '{')
            definitions += {tokens[i].a, tokens[i].b};
        }
        break;

      default:
        break;
    }
  }
  return {tokens, definitions, identifiers};
}

static ParseResult textfile_parse(const Array<StringBuffer> lines) {
  Array<TokenInfo> tokens = {};
  Array<String> identifiers = {};

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
      continue;
    }

    // identifier
    if (parse_identifier(line, x, t, "0123456789", "-+"))
      goto token_done;

    // number
    if (parse_number(line, x, t))
      goto token_done;

    // operators
    for (int i = 0; i < (int)ARRAY_LEN(text_operators); ++i) {
      if (line.begins_with(x, text_operators[i])) {
        t.token = TOKEN_OPERATOR;
        NEXT_CHAR(text_operators[i].length);
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
  }

  tokens += {TOKEN_EOF, 0, lines.size, 0, lines.size};

  return {tokens, {}, identifiers};
}

// returns the index after a generic arglist, or the same index if there was none, or -1 if there was an error
static int skip_c_style_generics_args(int i, Array<TokenInfo> tokens) {
  int j = i;
  // since we assume the current identifier is the type, we must check for templates here
  if (j < tokens.size && tokens[j].str == "<") {
    ++j;
    int depth = 1;
    for (; j < tokens.size && depth; ++j) {
      // this isn't bulletproof, but we want something to keep us from parsing the rest of the file
      // every time we encounter an expression like `x < 3`
      if (tokens[j].token != TOKEN_IDENTIFIER && tokens[j].str != "," && tokens[j].str != "<" && tokens[j].str != ">")
        return -1;
      if (tokens[j].str == "<") ++depth;
      if (tokens[j].str == ">") --depth;
    }
    if (depth)
      return -1;
  }
  return j;
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
      continue;
    }

    // C++11 raw string
    if (line.begins_with(x, "R\"")) {
      t.token = TOKEN_STRING;
      NEXT_CHAR(2);
      TokenInfo iden = {};
      int x0 = x;
      if (!parse_identifier(line, x, iden)) {
        log_warn("Failed to parse identifier on {}\n", (Slice)line(x,-1));
        continue;
      }
      Slice identifier = line(x0, x);
      String end_of_string = String::createf("){}\"", identifier);
      while (!line.find(x, end_of_string.slice, &x)) {
        ++y;
        if (y == lines.size)
          goto raw_string_done;
        line = lines[y].slice;
        x = 0;
      }
      NEXT_CHAR(end_of_string.length);
      raw_string_done:;
      util_free(end_of_string);
      goto token_done;
    }

    // identifier
    if (parse_identifier(line, x, t))
      goto token_done;

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
    if (parse_number(line, x, t))
      goto token_done;

    // string
    if (parse_string(line, x, t))
      goto token_done;

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
  }

  tokens += {TOKEN_EOF, 0, lines.size, 0, lines.size};

  // find definitions
  for (int i = 0; i < tokens.size; ++i) {
    TokenInfo ti = tokens[i];
    switch (ti.token) {
      case TOKEN_IDENTIFIER: {
        if (i+1 < tokens.size && ti.str == "#define") {
          definitions += tokens[i+1].r;
          goto token_def_done;
        }

        if (i+2 < tokens.size &&
            (ti.str == "struct" || ti.str == "enum" || ti.str == "class" || ti.str == "union" || ti.str == "namespace") &&
            tokens[i+1].token == TOKEN_IDENTIFIER &&
            (tokens[i+2].str == "{" || tokens[i+2].str == ":")) {
          definitions += tokens[i+1].r;
          goto token_def_done;
        }

        // check for function definition
        // TODO: only do this when not inside function scope
        {
          // is it a keyword, then ignore (things like else if (..) is not a definition)
          if (is_keyword(ti.str, cpp_keywords))
            goto token_def_done;

          {
            // since we assume the current identifier is the type, we must check for templates here
            int j = skip_c_style_generics_args(i+1, tokens);
            if (j == -1)
              goto token_def_done;

            // skip pointer and references
            for (; j < tokens.size && tokens[j].token == TOKEN_OPERATOR; ++j) {
              if (tokens[j].str == "*" || tokens[j].str == "&")
                continue;
              goto token_def_done;
            }

            if (j+1 < tokens.size &&
                tokens[j].token == TOKEN_IDENTIFIER &&
                tokens[j+1].token == '(') {
              // walk through param list, and check that it is followed by a '{'
              int depth = 1;
              int k = j+2;
              for (; k < tokens.size && depth; ++k) {
                if (tokens[k].token == '(') ++depth;
                if (tokens[k].token == ')') --depth;
              }
              // check for keywords after param list like "override"
              if (depth == 0) {
                while (k < tokens.size && tokens[k].token == TOKEN_IDENTIFIER && is_keyword(tokens[k].str, cpp_keywords))
                  ++k;
                if (depth == 0 && k < tokens.size && tokens[k].token == '{')
                  definitions += {tokens[j].a, tokens[j].b};
              }
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
        }

        // handle preprocessor command
        token_def_done:
        if (ti.token == TOKEN_IDENTIFIER && ti.str[0] == '#') {
          // parse #if 0-style comment
          if (i+1 < tokens.size && tokens[i].str == "#if" && (tokens[i+1].str == "0" || tokens[i+1].str == "false")) {
            i += 2;
            int i0 = i;
            int depth = 1;
            for (; i < tokens.size && depth; ++i) {
              if (tokens[i].str.begins_with("#if"))
                ++depth;
              else if (tokens[i].str == "#endif")
                --depth;
            }
            int i1 = i-1;
            if (i1 > i0) {
              TokenInfo t = {TOKEN_BLOCK_COMMENT, tokens[i0].a, tokens[i1].b};
              tokens.replace(i0, i1-i0, &t, 1);
            }
          }
          // skip parsing preprocessor commands for now
          // TODO: check for \ at end of line
          // Maybe we should do this in the tokenizer instead?
          else {
            int prev_y = ti.a.y;
            while (i+1 < tokens.size && tokens[i+1].a.y == prev_y)
              ++i;
          }
        }
        break;}
      default:
        break;
    }
  }
  return {tokens, definitions, identifiers};
}

static ParseResult csharp_parse(const Array<StringBuffer> lines) {
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
      continue;
    }

    // C++11 raw string
    if (line.begins_with(x, "R\"")) {
      t.token = TOKEN_STRING;
      NEXT_CHAR(2);
      TokenInfo iden = {};
      if (!parse_identifier(line, x, iden))
        continue;
      log_warn("Identifier is {}\n", (Slice)iden.str);
      String end_of_string = String::createf("){}\"", (Slice)iden.str);
      log_warn("Looking for {}\n", end_of_string.slice);
      while (!line.find(x, end_of_string.slice, &x)) {
        ++y;
        if (y == lines.size)
          goto raw_string_done;
        line = lines[y].slice;
        x = 0;
      }
      raw_string_done:;
      util_free(end_of_string);
      goto token_done;
    }

    // identifier
    if (parse_identifier(line, x, t))
      goto token_done;

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
    if (parse_number(line, x, t))
      goto token_done;

    // string
    if (parse_string(line, x, t))
      goto token_done;

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
  }

  tokens += {TOKEN_EOF, 0, lines.size, 0, lines.size};

  // find definitions
  for (int i = 0; i < tokens.size; ++i) {
    TokenInfo ti = tokens[i];
    switch (ti.token) {
      case TOKEN_IDENTIFIER: {
        if (i+1 < tokens.size && ti.str == "#define") {
          definitions += tokens[i+1].r;
          goto token_def_done;
        }

        if (i+1 < tokens.size && (ti.str == "struct" || ti.str == "enum" || ti.str == "class" || ti.str == "union" || ti.str == "interface") &&
            tokens[i+1].token == TOKEN_IDENTIFIER) {
          definitions += tokens[i+1].r;
          goto token_def_done;
        }

        // check for function definition
        // TODO: only do this when not inside function scope
        {
          if (is_keyword(ti.str, csharp_keywords))
            goto token_def_done;

          {
            // since we assume the current identifier is the type, we must check for templates here
            int j = skip_c_style_generics_args(i+1, tokens);
            if (j == -1)
              goto token_def_done;

            // skip pointer and references
            for (; j < tokens.size && tokens[j].token == TOKEN_OPERATOR; ++j) {
              if (tokens[j].str == "*" || tokens[j].str == "&" || tokens[j].str == "[" || tokens[j].str == "]")
                continue;
              goto token_def_done;
            }

            if (j+1 < tokens.size && tokens[j].token == TOKEN_IDENTIFIER) {
              int identifier_idx = j;
              j = skip_c_style_generics_args(j+1, tokens);
              if (j == -1)
                goto token_def_done;

              // if parameter list, walk past it
              if (j < tokens.size && tokens[j].str == "(") {
                int depth = 1;
                int k = j+1;
                for (; k < tokens.size && depth; ++k) {
                  if (tokens[k].token == '(') ++depth;
                  if (tokens[k].token == ')') --depth;
                }
                if (depth)
                  goto token_def_done;

                j = k;
              }
              if (j < tokens.size && tokens[j].str == "{")
                definitions += {tokens[identifier_idx].a, tokens[identifier_idx].b};
            }
          }
        }

        // if preprocessor command, jump to next line
        token_def_done:
        if (ti.token == TOKEN_IDENTIFIER && ti.str[0] == '#') {
          // parse #if 0-style comment
          if (i+1 < tokens.size && tokens[i].str == "#if" && (tokens[i+1].str == "0" || tokens[i+1].str == "false")) {
            i += 2;
            int i0 = i;
            int depth = 1;
            for (; i < tokens.size && depth; ++i) {
              if (tokens[i].str.begins_with("#if"))
                ++depth;
              else if (tokens[i].str == "#endif")
                --depth;
            }
            int i1 = i-1;
            if (i1 > i0) {
              TokenInfo t = {TOKEN_BLOCK_COMMENT, tokens[i0].a, tokens[i1].b};
              tokens.replace(i0, i1-i0, &t, 1);
            }
          }
          // skip parsing preprocessor commands for now
          // TODO: check for \ at end of line
          // Maybe we should do this in the tokenizer instead?
          else {
            int prev_y = ti.a.y;
            while (i+1 < tokens.size && tokens[i+1].a.y == prev_y)
              ++i;
          }
        }
        break;}
      default:
        break;
    }
  }
  return {tokens, definitions, identifiers};
}

enum Language {
  LANGUAGE_NULL,
  LANGUAGE_C,
  LANGUAGE_CSHARP,
  LANGUAGE_PYTHON,
  LANGUAGE_JULIA,
  LANGUAGE_BASH,
  LANGUAGE_CMANTIC_COLORSCHEME,
  LANGUAGE_GOLANG,
  LANGUAGE_TERRAFORM,
  LANGUAGE_MAKEFILE,
  NUM_LANGUAGES
};

typedef ParseResult (*ParseFun)(const Array<StringBuffer> lines);
struct LanguageSettings {
  StaticArray<Keyword> keywords;
  Slice line_comment;
  ParseFun parse_fun;
  Slice name;
};
LanguageSettings language_settings[] = {
  {StaticArray<Keyword>{},           {},                  textfile_parse,    Slice::create("")},  // LANGUAGE_NULL
  {static_array(cpp_keywords),       Slice::create("//"), cpp_parse,         Slice::create("C/C++")}, // LANGUAGE_C
  {static_array(csharp_keywords),    Slice::create("//"), csharp_parse,      Slice::create("C#")}, // LANGUAGE_CSHARP
  {static_array(python_keywords),    Slice::create("#"),  python_parse,      Slice::create("Python")},  // LANGUAGE_PYTHON
  {static_array(julia_keywords),     Slice::create("#"),  julia_parse,       Slice::create("Julia")},  // LANGUAGE_JULIA
  {static_array(bash_keywords),      Slice::create("#"),  bash_parse,        Slice::create("Shell")},  // LANGUAGE_BASH
  {{},                               {},                  colorscheme_parse, Slice::create("Cmantic-colorscheme")},  // LANGUAGE_CMANTIC_COLORSCHEME
  {static_array(go_keywords),        Slice::create("//"), go_parse,          Slice::create("Go")},  // LANGUAGE_GOLANG
  {static_array(terraform_keywords), Slice::create("#"), terraform_parse,          Slice::create("Terraform")},  // LANGUAGE_TERRAFORM
  {static_array(makefile_keywords),  Slice::create("#"), makefile_parse,          Slice::create("Makefile")},  // LANGUAGE_MAKEFILE
};
STATIC_ASSERT(ARRAY_LEN(language_settings) == NUM_LANGUAGES, all_language_settings_defined);

static ParseResult parse(const Array<StringBuffer> lines, Language language) {
  if ((int)language < LANGUAGE_NULL || (int)language >= NUM_LANGUAGES) {
    log_err("Unknown language %i\n", (int)language);
    return {};
  }

  return language_settings[language].parse_fun(lines);
}

#endif /* PARSE_CPP */
