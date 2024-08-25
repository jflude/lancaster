;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(in-package #:lancaster)

(defparameter *mmap-file* "shm:/test")
(defparameter *persist* t)
(defparameter *timeout* 1000000)
(defparameter *touch-period* 1000000)
(defparameter *max-id* 1000)
(defparameter *at-random* nil)
(defparameter *batch-size* 10)

(defvar *stop-now* nil)
(defvar *pstore* nil)
(defvar *head* nil)
(defvar *xyz* 0)

(defun test-create ()
  (setf *pstore* (foreign-alloc 'storage-handle))
  (try #'storage-create *pstore* *mmap-file*
       (logior o-rdwr o-creat o-excl) #o600
       *persist* 0 2000 8 0 512 "TEST"))

(defun test-describe ()
  (with-open-storage (store *mmap-file*)
    (format t "Storage: ~S~%Description: ~S~%Data Version: ~D.~D~%~
               Base Id: ~D~%Max Id:  ~D~%Record Size: ~S~%Value Size: ~S~%"
            (storage-get-file store)
            (storage-get-description store)
            (ash (storage-get-data-version store) -8)
            (logand (storage-get-data-version store) #xFF)
            (storage-get-base-id store)
            (storage-get-max-id store)
            (storage-get-record-size store)
            (storage-get-value-size store))))

(defun test-dump ()
  (with-open-storage (store *mmap-file*)
    (do ((id (storage-get-base-id store) (incf id)))
        ((>= id (storage-get-max-id store)))
      (with-record (rec store id)
        (let ((val (mem-ref (record-get-value-ref rec) '(:struct datum)))
              (prop (storage-get-property-ref store rec)))
          (format t "~5,'0D Rec: ~S~@[ (Prop: ~S)~]~%"
                  id val (if (null-pointer-p prop) nil prop)))))))

(defun test-destroy ()
  (prog1 (try #'storage-destroy *pstore*)
    (foreign-free *pstore*)
    (setf *pstore* nil)))

(defun test-delete ()
  (try #'storage-delete *mmap-file* nil))

(defun test-examine (id val rev timestamp)
  (multiple-value-bind (sec min hour day mon year)
      (decode-universal-time (get-universal-from-microsec timestamp))
    (format t "#~8,'0D rev ~8,'0D ~2,'0D-~2,'0D-~D ~
               ~2,'0D:~2,'0D:~2,'0D.~8,'0D ~S~%"
            id rev day mon year hour min sec (mod timestamp 1000000) val)))

(defun test-reset ()
  (when *head*
    (foreign-free *head*)
    (setf *head* nil)))

(defun test-read (&optional (count 1))
  (unless *head*
    (setf *head* (foreign-alloc 'q-index)
          (mem-ref *head* 'q-index) -1))
  (with-open-storage (store *mmap-file*)
    (with-foreign-objects ((ids 'identifier *batch-size*)
                           (values '(:struct datum) *batch-size*)
                           (revs 'revision *batch-size*)
                           (times 'microsec *batch-size*))
      (dotimes (i count)
        (when *stop-now*
          (setf *stop-now* nil)
          (test-reset)
          (return-from test-read))
        (let ((n (try #'batch-read-changed-records
                      store (storage-get-value-size store)
                      ids values revs times *batch-size*
                      *timeout* *head*)))
          (when (> n 0)
            (dotimes (j n)
              (test-examine (mem-aref ids 'identifier j)
                            (mem-aref values '(:struct datum) j)
                            (mem-aref revs 'revision j)
                            (mem-aref times 'microsec j)))))))))

(defun test-populate (ids values count)
  (dotimes (i count)
    (setf (getf (mem-aref values '(:struct datum) i) 'xyz) (incf *xyz*)
          (mem-aref ids 'identifier i) (if *at-random*
                                           (random *max-id*)
                                           (mod *xyz* *max-id*)))))

(defun test-write (&optional (count 1))
  (with-create-storage (store *mmap-file* :persist t :max-id *max-id*
                                          :value-size 8 :q-capacity 256
                                          :desc "TEST")
    (with-toucher (touch *touch-period*)
      (try #'toucher-add-storage touch store)
      (with-foreign-objects ((ids 'identifier *batch-size*)
                             (values '(:struct datum) *batch-size*))
        (dotimes (i count)
          (when *stop-now*
            (setf *stop-now* nil *xyz* 0)
            (return-from test-write))
          (test-populate ids values *batch-size*)
          (try #'batch-write-records store (storage-get-value-size store)
               ids values *batch-size*)
          #+darwin
          (sleep (/ *timeout* 1000000)) ; TODO Darwin CFFI is slightly broken
          #-darwin
          (try #'clock-sleep *timeout*))))))
