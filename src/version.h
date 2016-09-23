/*
   Copyright (c)2014-2016 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* library build version */

#ifndef VERSION_H
#define VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

const char *version_get_source(void);

int version_get_file_major(void);
int version_get_file_minor(void);

int version_get_wire_major(void);
int version_get_wire_minor(void);

#ifdef __cplusplus
}
#endif

#endif
