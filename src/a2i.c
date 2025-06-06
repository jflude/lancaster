/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/a2i.h>
#include <lancaster/error.h>
#include <errno.h>
#include <stdio.h>

status a2i(const char *text, const char *format, void *pnum)
{
    if (!text || !format || !pnum)
	return error_invalid_arg("a2i");

    errno = 0;
    if (sscanf(text, format, pnum) != EOF)
	return OK;

    return errno != 0
        ? error_errno("a2i: sscanf")
        : error_msg(INVALID_NUMBER, "a2i: invalid number: \"%s\"", text);
}
