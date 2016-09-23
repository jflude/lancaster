/*
   Copyright (c)2014-2016 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

#include "version.h"

const char *version_get_source(void)
{
     return "1.0";
}

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
