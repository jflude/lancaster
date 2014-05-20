#include "dump.h"
#include "error.h"
#include <ctype.h>

status dump(const void* p, size_t sz, FILE* f)
{
	const char* q = p;
	size_t n = 0;

	while (n < sz) {
		int i;
		fprintf(f, "%08lX | ", (unsigned long) (q + n));

		for (i = 0; i < 16; ++i) {
			if ((n + i) < sz) {
				if (fprintf(f, "%02X ", (unsigned) (unsigned char) q[n + i]) < 0) {
					if (feof(f))
						return CLOSED;

					error_errno("fprintf");
					return FAIL;
				}
			} else {
				if (fprintf(f, "   ") < 0) {
					if (feof(f))
						return CLOSED;

					error_errno("fprintf");
					return FAIL;
				}
			}
		}

		if (fprintf(f, "| ") < 0) {
			if (feof(f))
				return CLOSED;

			error_errno("fprintf");
			return FAIL;
		}

		for (i = 0; i < 16 && (n + i) < sz; ++i) {
			char c = isprint(q[n + i]) ? q[n + i] : '.';
			if (putc(c, f) == EOF) {
				if (feof(f))
					return CLOSED;

				error_errno("putc");
				return FAIL;
			}
		}

		if (putc('\n', f) == EOF) {
			if (feof(f))
				return CLOSED;

			error_errno("putc");
			return FAIL;
		}

		n += 16;
	}

	return OK;
}
