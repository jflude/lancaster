;;;; Copyright (c)2018-2024 Justin Flude.
;;;; Use of this source code is governed by the COPYING file.

(defsystem "lancaster"
  :description "Fast, reliable multicasting of ephemeral data."
  :author "Justin Flude <justin_flude@hotmail.com>"
  :version "0.1"
  :defsystem-depends-on ("cffi-grovel")
  :depends-on ("cffi")
  :components
  ((:file "package")
   (:cffi-grovel-file "grovel" :depends-on ("package"))
   (:file "status" :depends-on ("package"))
   (:file "clock" :depends-on ("status"))
   (:file "error" :depends-on ("status"))
   (:file "storage" :depends-on ("clock" "error"))
   (:file "toucher" :depends-on ("storage"))
   (:file "batch" :depends-on ("storage"))
   (:file "datum")
   (:file "test" :depends-on ("datum" "toucher"))))
