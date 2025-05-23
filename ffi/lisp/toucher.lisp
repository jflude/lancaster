;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(cl:in-package #:lancaster)

(cffi:defctype toucher-handle :pointer)

(cffi:defcfun "toucher_create" status
  (ptouch :pointer toucher-handle)
  (touch-period-usec microsec))

(cffi:defcfun "toucher_destroy" status
  (ptouch :pointer toucher-handle))

(cffi:defcfun "toucher_add_storage" status
  (touch toucher-handle)
  (store storage-handle))

(defmacro with-toucher ((toucher-var touch-period-usec) &body body)
  (let ((ptouch (gensym)))
    `(let ((,ptouch (cffi:foreign-alloc 'toucher-handle)))
       (unwind-protect
            (progn
              (try #'toucher-create ,ptouch ,touch-period-usec)
              (unwind-protect
                   (let ((,toucher-var (mem-ref ,ptouch 'toucher-handle)))
                     ,@body)
                (try #'toucher-destroy ,ptouch)))
         (cffi:foreign-free ,ptouch)))))
