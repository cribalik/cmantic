template<int N>
struct StringCache {
	String cache[N];
	int index;

	IF_DEBUG(
		int num_misses;
		int num_hits;
		int num_bytes_alloced;
	)
	String get(Slice s) {
		for (int i = 0; i < N; ++i) {
			if (cache[i].slice == s) {
				IF_DEBUG(++num_hits);
				return cache[i];
			}
		}
		IF_DEBUG(++num_misses);
		IF_DEBUG(num_bytes_alloced += s.length);
		return cache[(index++)%N] = String::create(s);
	}
};

struct BlameData {int line; char *hash, *author, *summary;};

static bool git_parse_blame(String output, Array<BlameData> *result) {
	StringCache<64> hash_cache = {};
	StringCache<64> author_cache = {};
	StringCache<64> summary_cache = {};

	*result = {};

	// first count number of unique lines so we can preallocate
	int num_lines = 0;
	{
		int off = 0;
		Slice last = {};
		while (1) {
			Slice row = output.token(&off, '\n');
			if (!row.length)
				break;
			int i = 0;
			Slice key = row.token(&i, ' ');
			if (key.length == 40 && key != last)
				++num_lines, last = key;
		}
	}
	// printf("%i %i\n", num_lines, num_lines * sizeof(BlameData));
	result->reserve(num_lines+1);

	Slice hash = {};
	Slice author = {};
	Slice summary = {};
	Slice orig_file_line_number = {};
	Slice final_file_line_number = {};
	int offset = 0;
	int items_left_in_group = 0;
	Slice last_hash = {};

	int line = 0;
	int current_line = 0;
	while (1) {
		Slice row = output.token(&offset, '\n');
		if (!row.length)
			break;
		int col = 0;
		Slice key = row.token(&col, ' ');
		if (key.length == 40) {
			if (key != hash) {
				*result += BlameData{current_line, hash_cache.get(hash(0, 8)).chars, author_cache.get(author).chars, summary_cache.get(summary).chars};
				current_line = line;
				last_hash = hash;
				hash = key;
			}
			++line;
			orig_file_line_number = row.token(&col, ' ');
			final_file_line_number = row.token(&col, ' ');
			if (items_left_in_group == 0)
				if (!row(col, -1).toint(&items_left_in_group))
					return false;
			--items_left_in_group;
		}
		else if (key == "author")
			author = row(col, -1);
		else if (key == "summary")
			summary = row(col, -1);
	}
	if (hash != last_hash)
		*result += BlameData{current_line, hash_cache.get(hash(0, 8)).chars, author_cache.get(author).chars, summary_cache.get(summary).chars};
	#if 0
	printf("%i %i %i\n", hash_cache.num_misses, hash_cache.num_hits, hash_cache.num_bytes_alloced);
	printf("%i %i %i\n", author_cache.num_misses, author_cache.num_hits, author_cache.num_bytes_alloced);
	printf("%i %i %i\n", summary_cache.num_misses, summary_cache.num_hits, summary_cache.num_bytes_alloced);
	printf("%i\n", result->size);
	#endif
	return true;
}