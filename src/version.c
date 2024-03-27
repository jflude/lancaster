/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/version.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

int version_get_file_major(void)
{
    return 1;
}

int version_get_file_minor(void)
{
    return 0;
}

int version_get_wire_major(void)
{
    return 2;
}

int version_get_wire_minor(void)
{
    return 0;
}

void show_version(const char *canon_name)
{
    printf("%s (%s)\n", PACKAGE_STRING, canon_name);
    exit(0);
}
