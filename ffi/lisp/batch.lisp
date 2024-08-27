;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(in-package #:lancaster)

(defcfun "batch_read_records" status
  (store storage-handle)
  (copy-size :size)
  (ids :pointer identifier)
  (values :pointer)
  (revs :pointer revision)
  (times :pointer microsec)
  (count :size))

(defcfun "batch_write_records" status
  (store storage-handle)
  (copy-size :size)
  (ids :pointer identifier)
  (values :pointer)
  (count :size))

(defcfun "batch_read_changed_records" status
  (store storage-handle)
  (copy-size :size)
  (ids :pointer identifier)
  (values :pointer)
  (revs :pointer revision)
  (times :pointer microsec)
  (count :size)
  (read-timeout microsec)
  (head :pointer q-index))

(defctype batch-context-handle :pointer)

(defcfun "batch_read_changed_records2" status
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

(defcfun "batch_context_destroy" status
  (pctx :pointer batch-context-handle))
