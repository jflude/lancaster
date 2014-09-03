#include "dump.h"
#include "error.h"
#include <ctype.h>

status fdump(const void* p, size_t sz, FILE* f)
{
	const char* q = p;
	size_t n = 0;

	if (!p || sz == 0 || !f)
		return error_invalid_arg("fdump");

	while (n < sz) {
		int i;
		fprintf(f, "%08lX | ", (unsigned long) (q + n));

		for (i = 0; i < 16; ++i) {
			if ((n + i) < sz) {
				unsigned val = (unsigned char) q[n + i];
				if (fprintf(f, "%02X ", val) < 0)
					return feof(f) ? EOF : error_errno("fprintf");
			} else {
				if (fprintf(f, "   ") < 0)
					return feof(f) ? EOF : error_errno("fprintf");
			}
		}

		if (fprintf(f, "| ") < 0)
			return feof(f) ? EOF : error_errno("fprintf");

		for (i = 0; i < 16 && (n + i) < sz; ++i) {
			char c = isprint((int) q[n + i]) ? q[n + i] : '.';
			if (putc(c, f) == EOF)
				return feof(f) ? EOF : error_errno("putc");
		}

		if (putc('\n', f) == EOF)
			return feof(f) ? EOF : error_errno("putc");

		n += 16;
	}

	return OK;
}

status dump(const void* p, size_t sz)
{
	return fdump(p, sz, stdout);
}
