;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(cl:in-package #:lancaster)

(cffi:defctype storage-handle :pointer)
(cffi:defctype record-handle :pointer)
(cffi:defctype identifier :int64)
(cffi:defctype q-index :long)
(cffi:defctype revision :int64)

(cffi:defcfun "storage_create" status
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

(cffi:defcfun "storage_open" status
  (pstore :pointer storage-handle)
  (mmap-file :string)
  (open-flags :int))

(cffi:defcfun "storage_destroy" status
  (pstore :pointer storage-handle))

(cffi:defcfun "storage_get_data_version" :unsigned-short
  (store storage-handle))

(cffi:defcfun "storage_set_data_version" status
  (store storage-handle)
  (data-ver :unsigned-short))

(cffi:defcfun "storage_get_array" record-handle
  (store storage-handle))

(cffi:defcfun "storage_get_base_id" identifier
  (store storage-handle))

(cffi:defcfun "storage_get_max_id" identifier
  (store storage-handle))

(cffi:defcfun "storage_get_record_size" :size
  (store storage-handle))

(cffi:defcfun "storage_get_value_size" :size
  (store storage-handle))

(cffi:defcfun "storage_get_property_size" :size
  (store storage-handle))

(cffi:defcfun "storage_get_file" :string
  (store storage-handle))

(cffi:defcfun "storage_get_description" :string
  (store storage-handle))

(cffi:defcfun "storage_set_description" status
  (store storage-handle)
  (desc :string))

(cffi:defcfun "storage_get_queue_capacity" :size
  (store storage-handle))

(cffi:defcfun "storage_get_record" status
  (store storage-handle)
  (id identifier)
  (prec :pointer record-handle))

(cffi:defcfun "storage_delete" status
  (mmap_file :string)
  (force :boolean))

(cffi:defcfun "storage_clear_record" status
  (store storage-handle)
  (rec record-handle))

(cffi:defcfun "storage_copy_record" status
  (from-store storage-handle)
  (from-rec record-handle)
  (to-store storage-handle)
  (to-rec record-handle)
  (to-ts microsec)
  (with-prop :boolean))

(cffi:defcfun "storage_get_property_ref" :pointer
  (store storage-handle)
  (rec record-handle))

(cffi:defcfun "record_get_value_ref" :pointer
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
    `(let ((,pstore (cffi:foreign-alloc 'storage-handle)))
       (unwind-protect
            (progn
              (try #'storage-create ,pstore ,mmap-file
                   ,open-flags ,mode-flags ,persist ,base-id ,max-id
                   ,value-size ,property-size ,q-capacity ,desc)
              (unwind-protect
                   (let ((,store-var (mem-ref ,pstore 'storage-handle)))
                     ,@body)
                (try #'storage-destroy ,pstore)))
         (cffi:foreign-free ,pstore)))))

(defmacro with-open-storage ((store-var mmap-file &key (open-flags o-rdonly))
                             &body body)
  (let ((pstore (gensym)))
    `(let ((,pstore (cffi:foreign-alloc 'storage-handle)))
       (unwind-protect
            (progn
              (try #'storage-open ,pstore ,mmap-file ,open-flags)
              (unwind-protect
                   (let ((,store-var (mem-ref ,pstore 'storage-handle)))
                     ,@body)
                (try #'storage-destroy ,pstore)))
         (cffi:foreign-free ,pstore)))))

(defmacro with-record ((record-var store id)
                               &body body)
  (let ((prec (gensym)))
    `(let ((,prec (cffi:foreign-alloc 'record-handle)))
       (unwind-protect
            (progn
              (try #'storage-get-record ,store ,id ,prec)
              (let ((,record-var (mem-ref ,prec 'record-handle)))
                ,@body))
         (cffi:foreign-free ,prec)))))
