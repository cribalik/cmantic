template<int N>
struct StringCache {
	String cache[N];
	int num;

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
		return cache[(num++)%N] = String::create(s);
	}
};

template<int N>
static void util_free(StringCache<N> &s) {
	for (int i = 0; i < s.num; ++i)
		util_free(s.cache[i]);
	s.num = 0;
}

struct BlameData {int line; char *hash, *author, *summary;};

// TODO: we might be able to optimize this by storing only the unique commits, and having references to them
static bool git_parse_blame(String output, Array<BlameData> *result) {
	StringCache<64> hash_cache = {};
	StringCache<64> author_cache = {};
	StringCache<64> summary_cache = {};
	struct Data {String hash, author, summary;};
	Array<Data> mem = {};

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
				// if we've seen the key before, it should be in mem
				Data *d;
				ARRAY_FIND(mem, &d, d->hash == key);
				if (!d) {
					mem += Data{
						hash_cache.get(hash(0, 8)),
						author_cache.get(author),
						summary_cache.get(summary),
					};
					d = &mem.last();
				}
				last_hash = hash;
				*result += BlameData{current_line, d->hash.chars, d->author.chars, d->summary.chars};
				current_line = line;
				hash = key;
			}
			++line;
			orig_file_line_number = row.token(&col, ' ');
			final_file_line_number = row.token(&col, ' ');
			if (items_left_in_group == 0)
				if (!row(col, -1).toint(&items_left_in_group))
					goto err;
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
	mem.free_shallow();
	return true;

	err:
	mem.free_shallow();
	util_free(hash_cache);
	util_free(author_cache);
	util_free(summary_cache);
	return false;
}