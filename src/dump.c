/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the COPYING file.
*/

#include <lancaster/dump.h>
#include <lancaster/error.h>
#include <ctype.h>

#define OCTET_PER_LINE 16

status fdump(const void *p, const void *base, size_t sz, FILE *f)
{
    const char *q = p;
    size_t n = 0;
    int i;

    if (!p || sz == 0 || !f)
	return error_invalid_arg("fdump");

    while (n < sz) {
	if (fprintf(f, "%012lX|", (const char *)q - (const char *)base + n) <
	    0)
	    return (feof(f) ? error_eof : error_errno)("fprintf");

	for (i = 0; i < OCTET_PER_LINE; ++i) {
	    if ((n + i) < sz) {
		unsigned val = (unsigned char)q[n + i];
		if (fprintf(f, "%02X", val) < 0)
		    return (feof(f) ? error_eof : error_errno)("fprintf");
	    } else if (putc(' ', f) == EOF || putc(' ', f) == EOF)
		return (feof(f) ? error_eof : error_errno)("putc");

	    if (i < (OCTET_PER_LINE - 1) && putc(' ', f) == EOF)
		return (feof(f) ? error_eof : error_errno)("putc");
	}

	if (putc('|', f) == EOF)
	    return (feof(f) ? error_eof : error_errno)("putc");

	for (i = 0; i < OCTET_PER_LINE && (n + i) < sz; ++i) {
	    char c = isprint((int)q[n + i]) ? q[n + i] : '.';
	    if (putc(c, f) == EOF)
		return (feof(f) ? error_eof : error_errno)("putc");
	}

	if (putc('\n', f) == EOF)
	    return (feof(f) ? error_eof : error_errno)("putc");

	n += OCTET_PER_LINE;
    }

    return OK;
}

status dump(const void *p, const void *base, size_t sz)
{
    return fdump(p, base, sz, stdout);
}
