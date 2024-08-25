;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(in-package #:lancaster)

(defcfun "error_last_msg" :string)

(defun try (fn &rest args)
  (let ((st (apply fn args)))
    (if (failed st)
        (error "~A (~D)" (error-last-msg) st)
        st)))
