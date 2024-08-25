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
  (timeout microsec)
  (head :pointer q-index))
