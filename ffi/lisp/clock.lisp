;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(in-package #:lancaster)

(defctype microsec :int64)

(defcfun "clock_sleep" status
  (usec microsec))

(defcfun "clock_time" status
  (pusec :pointer microsec))

(defconstant +unix-epoch-as-universal+ 2208988800)

(defun get-universal-from-microsec (usec)
  (+ (floor usec 1000000) +unix-epoch-as-universal+))

(defun get-microsec-from-universal (univ)
  (* 1000000 (- univ +unix-epoch-as-universal+)))
