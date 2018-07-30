static bool git_parse_blame(String output, Array<String> *result) {
	Slice hash = {};
	Slice author = {};
	Slice summary = {};
	Slice orig_file_line_number = {};
	Slice final_file_line_number = {};
	int offset = 0;
	int items_left_in_group = 0;

	while (1) {
		Slice row = output.token(&offset, '\n');
		if (!row.length)
			break;
		int i = 0;
		Slice key = row.token(&i, ' ');
		if (key.length == 40) {
			if (hash.length)
				*result += String::createf("{} - {}", &author, &summary);
			hash = key;
			orig_file_line_number = row.token(&i, ' ');
			final_file_line_number = row.token(&i, ' ');
			if (items_left_in_group == 0)
				if (!row(i, -1).toint(&items_left_in_group))
					return false;
			--items_left_in_group;
		}
		else if (key == "author")
			author = row(i, -1);
		else if (key == "summary")
			summary = row(i, -1);
	}
	if (hash.length)
		*result += String::createf("{} - {}", &author, &summary);

	return true;
}