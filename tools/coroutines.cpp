#include "../src/util.hpp"

int main(int argc, char const *argv[])
{
	util_init();

	if (argc < 3)
		log_info("usage: coroutines INPUTFILE OUTPUTFILE\n"), exit(1);

	String data;
	if (!File::get_contents(argv[1], &data))
		log_err("Failed to read file\n"), exit(1);

	StringBuffer out = {};

	// escape backslashes in path
	out += "#line 1 \"";
	for (const char *c = argv[1]; *c; ++c) {
		if (*c == '\\')
			out += '\\';
		out += *c;
	}
	out += "\"\n";

	int num_coroutines_found = 0;
	int a = 0, b = 0;
	while (1) {
		int start = a;
		if (!data.find(a, Slice::create("COROUTINE_BEGIN"), &a))
			break;
		out += data(start, a);
		a += strlen("COROUTINE_BEGIN");

		if (!data.find(a, Slice::create("COROUTINE_END"), &b))
			log_err("Failed to find matching COROUTINE_END\n"), exit(1);
		++num_coroutines_found;

		Array<Slice> yields = {};
		{
			Slice block = data(a,b);
			int aa = 0, bb = 0;
			while (1) {
				if (!block.find(aa, Slice::create("yield("), &aa))
					break;
				if (!block.find(aa, ')', &bb))
					break;
				yields += block(aa+6, bb);
				aa = bb;
			}

			// check for return
			int pos = 0;
			while (1) {
				if (!block.find(pos, Slice::create("return"), &pos))
					break;
				if (!isalnum(block[pos-1]) && !isalnum(block[pos+6]))
					log_err("'return' is not allowed in coroutines\n"), exit(1);
				++pos;
			}
		}

		out.append("static enum {");
		for (int i = 0; i < yields.size; ++i)
			out.appendf("{} = %i, ", yields[i], i+1);
		out.append("} step; ");

		out.append("switch (step) {");
		for (int i = 0; i < yields.size; ++i)
			out.appendf("case {}: goto {};", yields[i], yields[i]);
		out.append("}");

		out += data(a, b);
		b += strlen("COROUTINE_END");
		a = b;
	}
	out += data(b,-1);
  #ifdef OS_WINDOWS
    FILE *output = 0;
    if (fopen_s(&output, argv[2], "wb"))
      log_err("Failed to open output file\n"), exit(1);
  #else
    FILE *output = fopen(argv[2], "wb");
    if (!output)
      log_err("Failed to open output file\n"), exit(1);
  #endif
	fprintf(output, "%s", out(0, 20).chars);
	return 0;
}