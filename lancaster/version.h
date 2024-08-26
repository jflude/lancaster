/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Use of this source code is governed by the COPYING file.
*/

/* storage layout, network protocol and package versions */

#ifndef VERSION_H
#define VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

int version_get_file_major(void);
int version_get_file_minor(void);

int version_get_wire_major(void);
int version_get_wire_minor(void);

void show_version(const char *canon_name);

#ifdef __cplusplus
}
#endif

#endif
