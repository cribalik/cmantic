
// fuzzy matching stuff
struct FuzzyMatch {
  Slice str;
  float points;
  int idx;
};

static int fuzzy_cmp(const void *aa, const void *bb) {
  const FuzzyMatch *a = (FuzzyMatch*)aa, *b = (FuzzyMatch*)bb;

  if (a->points != b->points)
    return (int)(b->points - a->points + 0.5f);
  return a->str.length - b->str.length;
}

// returns number of found matches
static int fuzzy_match(Slice string, View<Slice> strings, View<FuzzyMatch> result, bool ignore_identical_strings) {
  int num_results = 0;

  if (string.length == 0) {
    int l = min(result.size, strings.size);
    for (int i = 0; i < l; ++i)
      result[i] = {strings[i], 0.0f, i};
    return l;
  }

  for (int i = 0; i < strings.size; ++i) {
    Slice identifier = strings[i];
    if (ignore_identical_strings && string == identifier)
      continue;

    const int test_len = identifier.length;
    if (string.length > test_len)
      continue;

    const char *in = string.chars;
    const char *in_end = in + string.length;
    const char *test = identifier.chars;
    const char *test_end = test + test_len;

    float points = 0;
    float gain = 10;

    for (; in < in_end && test < test_end; ++test) {
      if (*in == *test) {
        points += gain;
        gain = 10;
        ++in;
      }
      else if (tolower(*in) == tolower(*test)) {
        points += gain*0.8f;
        gain = 10;
        ++in;
      }
      /* Don't penalize special characters */
      else if (isalnum(*test))
        gain *= 0.7;
    }

    if (in != in_end || points <= 0)
      continue;

    /* push match */

    if (num_results < result.size) {
      result[num_results].str = identifier;
      result[num_results].points = points;
      result[num_results].idx = i;
      ++num_results;
    } else {
      /* find worst match and replace */
      FuzzyMatch &worst = result[ARRAY_MIN_BY(result, points)];
      if (points > worst.points) {
        worst.str = identifier;
        worst.points = points;
        worst.idx = i;
      }
    }
  }

  qsort(result.items, num_results, sizeof(result[0]), fuzzy_cmp);
  return num_results;
}

static void easy_fuzzy_match(Slice input, View<Slice> options, bool ignore_identical_strings, Array<int> *result) {
  StackArray<FuzzyMatch, 15> matches = {};
  *result = {};

  int n = fuzzy_match(input, options, view(matches), ignore_identical_strings);

  for (int i = 0; i < n; ++i)
    *result += matches[i].idx;
}

static void easy_fuzzy_match(Slice input, View<Slice> options, bool ignore_identical_strings, Array<String> *result) {
  StackArray<FuzzyMatch, 15> matches = {};
  *result = {};

  int n = fuzzy_match(input, options, view(matches), ignore_identical_strings);

  for (int i = 0; i < n; ++i)
    *result += String::create(matches[i].str);
}

