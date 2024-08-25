;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(in-package #:lancaster)

(defctype storage-handle :pointer)
(defctype record-handle :pointer)
(defctype identifier :int64)
(defctype q-index :long)
(defctype revision :int64)

(defcfun "storage_create" status
  (pstore :pointer storage-handle)
  (mmap-file :string)
  (open-flags :int)
  (mode-flags :mode)
  (persist :boolean)
  (base-id identifier)
  (max-id identifier)
  (value-size :size)
  (property-size :size)
  (q-capacity :size)
  (desc :string))

(defcfun "storage_open" status
  (pstore :pointer storage-handle)
  (mmap-file :string)
  (open-flags :int))

(defcfun "storage_destroy" status
  (pstore :pointer storage-handle))

(defcfun "storage_get_data_version" :unsigned-short
  (store storage-handle))

(defcfun "storage_set_data_version" status
  (store storage-handle)
  (data-ver :unsigned-short))

(defcfun "storage_get_array" record-handle
  (store storage-handle))

(defcfun "storage_get_base_id" identifier
  (store storage-handle))

(defcfun "storage_get_max_id" identifier
  (store storage-handle))

(defcfun "storage_get_record_size" :size
  (store storage-handle))

(defcfun "storage_get_value_size" :size
  (store storage-handle))

(defcfun "storage_get_property_size" :size
  (store storage-handle))

(defcfun "storage_get_file" :string
  (store storage-handle))

(defcfun "storage_get_description" :string
  (store storage-handle))

(defcfun "storage_set_description" status
  (store storage-handle)
  (desc :string))

(defcfun "storage_get_queue_capacity" :size
  (store storage-handle))

(defcfun "storage_get_record" status
  (store storage-handle)
  (id identifier)
  (prec :pointer record-handle))

(defcfun "storage_delete" status
  (mmap_file :string)
  (force :boolean))

(defcfun "storage_clear_record" status
  (store storage-handle)
  (rec record-handle))

(defcfun "storage_copy_record" status
  (from-store storage-handle)
  (from-rec record-handle)
  (to-store storage-handle)
  (to-rec record-handle)
  (to-ts microsec)
  (with-prop :boolean))

(defcfun "storage_get_property_ref" :pointer
  (store storage-handle)
  (rec record-handle))

(defcfun "record_get_value_ref" :pointer
  (rec record-handle))

(defmacro with-create-storage ((store-var mmap-file
                                &key (open-flags (logior o-rdwr o-creat))
                                  (mode-flags 0)
                                  (persist nil)
                                  (base-id 0)
                                  max-id
                                  value-size
                                  (property-size 0)
                                  (q-capacity 0)
                                  (desc (null-pointer)))
                               &body body)
  (let ((pstore (gensym)))
    `(let ((,pstore (foreign-alloc 'storage-handle)))
       (unwind-protect
            (progn
              (try #'storage-create ,pstore ,mmap-file
                   ,open-flags ,mode-flags ,persist ,base-id ,max-id
                   ,value-size ,property-size ,q-capacity ,desc)
              (unwind-protect
                   (let ((,store-var (mem-ref ,pstore 'storage-handle)))
                     ,@body)
                (try #'storage-destroy ,pstore)))
         (foreign-free ,pstore)))))

(defmacro with-open-storage ((store-var mmap-file &key (open-flags o-rdonly))
                             &body body)
  (let ((pstore (gensym)))
    `(let ((,pstore (foreign-alloc 'storage-handle)))
       (unwind-protect
            (progn
              (try #'storage-open ,pstore ,mmap-file ,open-flags)
              (unwind-protect
                   (let ((,store-var (mem-ref ,pstore 'storage-handle)))
                     ,@body)
                (try #'storage-destroy ,pstore)))
         (foreign-free ,pstore)))))

(defmacro with-record ((record-var store id)
                               &body body)
  (let ((prec (gensym)))
    `(let ((,prec (foreign-alloc 'record-handle)))
       (unwind-protect
            (progn
              (try #'storage-get-record ,store ,id ,prec)
              (let ((,record-var (mem-ref ,prec 'record-handle)))
                ,@body))
         (foreign-free ,prec)))))
