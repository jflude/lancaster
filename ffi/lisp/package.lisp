;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(cl:defpackage #:lancaster
  (:use #:cl)
  (:export #:status #:failed #:error-last-msg #:try #:microsec
           #:clock-sleep #:clock-time #:+unix-epoch-as-universal+
           #:get-universal-from-microsec #:get-microsec-from-universal
           #:o-rdonly #:o-wronly #:o-rdwr #:o-creat #:o-exl #:o-nofollow
           #:storage-handle #:record-handle #:identifier #:q-index #:revision
           #:storage-create #:storage-open #:storage-destroy
           #:storage-get-data-version #:storage-set-data-version
           #:storage-get-base-id #:storage-get-max-id #:storage-get-record-size
           #:storage-get-value-size #:storage-get-property-size
           #:storage-get-file #:storage-get-description
           #:storage-set-description #:storage-get-array #:storage-delete
           #:storage-get-property-ref #:record-get-value-ref
           #:with-create-storage #:with-open-storage #:with-record
           #:toucher-create #:toucher-destroy #:toucher-handle
           #:toucher-add-storage #:with-toucher #:batch-read-records
           #:batch-write-records #:batch-read-changed-records))

(cl:in-package #:lancaster)

(cffi:define-foreign-library liblancaster
  (t (:default "liblancaster")))

(cffi:use-foreign-library liblancaster)
