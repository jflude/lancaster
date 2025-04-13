;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(cl:in-package #:lancaster)

(defconstant datum-max-id 1000)

(cffi:defcstruct datum
  (xyz :long))
