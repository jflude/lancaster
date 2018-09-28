/*
  Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/dump.h>
#include <lancaster/error.h>
#include <ctype.h>
#include <stddef.h>

#define OCTET_PER_LINE 16

status io_error(const char *func, FILE *f)
{
    return (feof(f) ? error_eof : error_errno)(func);
}

status fdump(const void *p, const void *base, size_t sz, FILE *f)
{
    const char *q = p;
    size_t n = 0;
    int i;

    if (!p || sz == 0 || !f)
	return error_invalid_arg("fdump");

    while (n < sz) {
	ptrdiff_t offset = (const char *)q - (const char *)base + n;
	if (fprintf(f, "%012lX|", (long)offset) < 0)
	    return io_error("fprintf", f);

	for (i = 0; i < OCTET_PER_LINE; ++i) {
	    if ((n + i) < sz) {
		unsigned val = (unsigned char)q[n + i];
		if (fprintf(f, "%02X", val) < 0)
		    return io_error("fprintf", f);
	    } else if (putc(' ', f) == EOF || putc(' ', f) == EOF)
		return io_error("putc", f);

	    if (i < (OCTET_PER_LINE - 1) && putc(' ', f) == EOF)
		return io_error("putc", f);
	}

	if (putc('|', f) == EOF)
	    return io_error("putc", f);

	for (i = 0; i < OCTET_PER_LINE && (n + i) < sz; ++i) {
	    char c = isprint((int)q[n + i]) ? q[n + i] : '.';
	    if (putc(c, f) == EOF)
		return io_error("putc", f);
	}

	if (putc('\n', f) == EOF)
	    return io_error("putc", f);

	n += OCTET_PER_LINE;
    }

    return OK;
}

status dump(const void *p, const void *base, size_t sz)
{
    return fdump(p, base, sz, stdout);
}
