;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(cl:in-package #:lancaster)

(cffi:defcfun "batch_read_records" status
  (store storage-handle)
  (copy-size :size)
  (ids :pointer identifier)
  (values :pointer)
  (revs :pointer revision)
  (times :pointer microsec)
  (count :size))

(cffi:defcfun "batch_write_records" status
  (store storage-handle)
  (copy-size :size)
  (ids :pointer identifier)
  (values :pointer)
  (count :size))

(cffi:defcfun "batch_read_changed_records" status
  (store storage-handle)
  (copy-size :size)
  (ids :pointer identifier)
  (values :pointer)
  (revs :pointer revision)
  (times :pointer microsec)
  (count :size)
  (read-timeout microsec)
  (head :pointer q-index))

(cffi:defctype batch-context-handle :pointer)

(cffi:defcfun "batch_read_changed_records2" status
  (store storage-handle)
  (copy-size :size)
  (ids :pointer identifier)
  (values :pointer)
  (revs :pointer revision)
  (times :pointer microsec)
  (count :size)
  (read-timeout microsec)
  (orphan-timeout microsec)
  (pctx :pointer batch-context-handle))

(cffi:defcfun "batch_context_destroy" status
  (pctx :pointer batch-context-handle))
