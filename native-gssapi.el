;;; -*- lexical-binding: t -*-

(require 'cl)
(require 'eieio)
(require 'emacs-gssapi)

(defclass gss-name ()
  ((ptr  :initarg :ptr
         :reader gss-name/ptr)))

(defclass gss-context ()
  ((ptr :initarg :ptr
        :reader gss-context/ptr)))

(defun gss--make-string-from-result (content)
  (if content
      (with-temp-buffer
        (loop for ch across content
              do (insert ch))
        (buffer-string))
    nil))

(cl-defun gss-make-name (name &key (type :hostbased-service))
  (check-type name string)
  (check-type type (member :user-name :machine-uid-name :string-uid-name :hostbased-service))
  (make-instance 'gss-name :ptr (gss--internal-import-name name type)))

(defun gss--token-to-vector (string)
  (let ((vector (make-vector (length string) 0)))
    (loop for ch across string
          for i from 0
          do (setf (aref vector i) ch))
    vector))

(defun gss-name-to-string (name)
  (check-type name gss-name)
  (gss--internal-name-to-string (gss-name/ptr name)))

(cl-defun gss-init-sec (name &key flags (time-req 0) context input-token)
  (check-type name (or string gss-name))
  (check-type flags list)
  (check-type time-req integer)
  (check-type context (or null gss-context))
  (check-type input-token (or null string))
  (let ((name-native (etypecase name
                       (string (gss-make-name name))
                       (gss-name name))))
    (destructuring-bind (continue-needed context content flags)
        (gss--internal-init-sec-context (gss-name/ptr name-native)
                                        flags
                                        (if context (gss-context/ptr context) nil)
                                        time-req
                                        (gss--token-to-vector input-token))
      (list continue-needed
            (make-instance 'gss-context :ptr context)
            (gss--make-string-from-result content)
            flags))))

(cl-defun gss-accept-sec (content &key context)
  (check-type content string)
  (check-type context (or null gss-context))
  (destructuring-bind (continue-needed context name output-token flags time-rec delegated-cred-handle)
      (gss--internal-accept-sec-context (gss--token-to-vector content) (if context (gss-context/ptr context) nil))
    (list continue-needed
          (make-instance 'gss-context :ptr context)
          (make-instance 'gss-name :ptr name)
          (gss--make-string-from-result output-token)
          flags
          time-rec
          delegated-cred-handle)))

(defun gss-krb5-register-acceptor-identity (file)
  (check-type file string)
  (unless (file-exists-p file)
    (error "Could not find keytab file: %s" file))
  (gss--internal-krb5-register-acceptor-identity (expand-file-name file)))

(cl-defun gss-wrap (context data &key conf)
  (check-type context gss-context)
  (check-type data string)
  (destructuring-bind (data conf)
      (gss--internal-wrap (gss-context/ptr context) (gss--token-to-vector data) conf)
    (list (gss--make-string-from-result data) conf)))

(cl-defun gss-unwrap (context data)
  (check-type context gss-context)
  (check-type data string)
  (destructuring-bind (data conf)
      (gss--internal-unwrap (gss-context/ptr context) (gss--token-to-vector data))
    (list (gss--make-string-from-result data) conf)))

(provide 'native-gssapi)
