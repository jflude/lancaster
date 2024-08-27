;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(in-package #:lancaster)

(include "fcntl.h")
(constant (o-rdonly "O_RDONLY"))
(constant (o-wronly "O_WRONLY"))
(constant (o-rdwr "O_RDWR"))
(constant (o-creat "O_CREAT"))
(constant (o-excl "O_EXCL"))
(constant (o-nofollow "O_NOFOLLOW"))

(include "sys/types.h")
(ctype :mode "mode_t")
