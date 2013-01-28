;;; esh-mode.el --- esh script editing mode.
;;;
;;; This is a crude hack of scheme.el from the GNU emacs distribution.
;;;
;;; The new bits were added by Ivan Tkatchev.
;;;
;;; Code:

(require 'lisp-mode)

(defvar esh-mode-syntax-table nil "")

(if (not esh-mode-syntax-table)
    (let ((i 0))
      (setq esh-mode-syntax-table (make-syntax-table))
      (set-syntax-table esh-mode-syntax-table)

      ;; Default is atom-constituent.
      (while (< i 256)
	(modify-syntax-entry i "_   ")
	(setq i (1+ i)))

      ;; Word components.
      (setq i ?0)
      (while (<= i ?9)
	(modify-syntax-entry i "w   ")
	(setq i (1+ i)))
      (setq i ?A)
      (while (<= i ?Z)
	(modify-syntax-entry i "w   ")
	(setq i (1+ i)))
      (setq i ?a)
      (while (<= i ?z)
	(modify-syntax-entry i "w   ")
	(setq i (1+ i)))

      ;; Whitespace
      (modify-syntax-entry ?\t "    ")
      (modify-syntax-entry ?\n ">   ")
      (modify-syntax-entry ?\f "    ")
      (modify-syntax-entry ?\r "    ")
      (modify-syntax-entry ?  "    ")

      ;; These characters are delimiters but otherwise undefined.
      ;; Brackets and braces balance for editing convenience.
      (modify-syntax-entry ?\[ "(]  ")
      (modify-syntax-entry ?\] ")[  ")
      (modify-syntax-entry ?{ "(}  ")
      (modify-syntax-entry ?} "){  ")

      ;; Other atom delimiters
      (modify-syntax-entry ?\( "()  ")
      (modify-syntax-entry ?\) ")(  ")
      (modify-syntax-entry ?\# "<   ")
      (modify-syntax-entry ?\" "\"    ")
      (modify-syntax-entry ?\' "\"    ")
      (modify-syntax-entry ?~ "  p")
      (modify-syntax-entry ?$ "  p")))


(defvar esh-mode-abbrev-table nil "")
(define-abbrev-table 'esh-mode-abbrev-table ())

(defvar esh-imenu-generic-expression
  '((nil 
     "^(define[ \n\t][ \n\t]*\\(.*\\)[ \n\t]" 1)))

(defvar esh-font-lock-keywords
  '(("([ \t\n]*\\(define\\|eval\\|begin\\|if\\|eval!\\|and\\|or\\|not\\)[ \n\t)]"
     1 font-lock-variable-name-face)
    ("([ \t\n]*\\(top\\|pop\\|push\\|stack\\|rot\\|l-stack\\)[ \t\n)]"
     1 font-lock-keyword-face)
    ("(define[ \n\y][ \n\t]*\\(.*\\)[ \n\t]" 
     1 font-lock-warning-face)
    ("~\\|\\$"
     0 font-lock-warning-face))
  "Default expressions to highlight in esh mode.")


(defun esh-mode-variables ()
  (set-syntax-table esh-mode-syntax-table)
  (setq local-abbrev-table esh-mode-abbrev-table)
  (make-local-variable 'paragraph-start)
  (setq paragraph-start (concat "$\\|" page-delimiter))
  (make-local-variable 'paragraph-separate)
  (setq paragraph-separate paragraph-start)
  (make-local-variable 'paragraph-ignore-fill-prefix)
  (setq paragraph-ignore-fill-prefix t)
  (make-local-variable 'fill-paragraph-function)
  (setq fill-paragraph-function 'lisp-fill-paragraph)
  ;; Adaptive fill mode gets in the way of auto-fill,
  ;; and should make no difference for explicit fill
  ;; because lisp-fill-paragraph should do the job.
  (make-local-variable 'adaptive-fill-mode)
  (setq adaptive-fill-mode nil)
  (make-local-variable 'indent-line-function)
  (setq indent-line-function 'lisp-indent-line)
  (make-local-variable 'parse-sexp-ignore-comments)
  (setq parse-sexp-ignore-comments t)
  (make-local-variable 'outline-regexp)
  (setq outline-regexp "### \\|(....")
  (make-local-variable 'comment-start)
  (setq comment-start "#")
  (make-local-variable 'comment-start-skip)
  ;; Look within the line for a # following an even number of backslashes
  ;; after either a non-backslash or the line beginning.
  (setq comment-start-skip "\\(\\(^\\|[^\\\\\n]\\)\\(\\\\\\\\\\)*\\)#+[ \t]*")
  (make-local-variable 'comment-column)
  (setq comment-column 40)
  (make-local-variable 'comment-indent-function)
  (setq comment-indent-function 'lisp-comment-indent)
  (make-local-variable 'parse-sexp-ignore-comments)
  (setq parse-sexp-ignore-comments t)
  (make-local-variable 'lisp-indent-function)
  (set lisp-indent-function 'esh-indent-function)
  (setq mode-line-process '("" esh-mode-line-process))
  (make-local-variable 'imenu-generic-expression)
  (setq imenu-generic-expression esh-imenu-generic-expression)
  (make-local-variable 'font-lock-defaults)
  (setq font-lock-defaults 
	'(esh-font-lock-keywords
	  nil nil
	  ((?/ . "w")))))


(defvar esh-mode-line-process "")

(defvar esh-mode-map nil
  "Keymap for esh mode.
All commands in `shared-lisp-mode-map' are inherited by this map.")

(if esh-mode-map
    ()
  (let ((map (make-sparse-keymap "esh")))
    (setq esh-mode-map
	  (nconc (make-sparse-keymap) shared-lisp-mode-map))
    (define-key esh-mode-map "\e\t" 'lisp-complete-symbol)
    (define-key esh-mode-map [menu-bar] (make-sparse-keymap))
    (define-key esh-mode-map [menu-bar esh]
      (cons "esh" map))
    (define-key map [comment-region] '("Comment Out Region" . comment-region))
    (define-key map [indent-region] '("Indent Region" . indent-region))
    (define-key map [indent-line] '("Indent Line" . lisp-indent-line))
    (put 'comment-region 'menu-enable 'mark-active)
    (put 'indent-region 'menu-enable 'mark-active)))


;;;###autoload
(defun esh-mode ()
  "Major mode for editing esh code.
Editing commands are similar to those of lisp-mode.

Commands:
Delete converts tabs to spaces as it moves back.
Blank lines separate paragraphs.  Semicolons start comments.
\\{esh-mode-map}
Entry to this mode calls the value of esh-mode-hook
if that value is non-nil."
  (interactive)
  (kill-all-local-variables)
  (esh-mode-initialize)
  (esh-mode-variables)
  (run-hooks 'esh-mode-hook))

(defun esh-mode-initialize ()
  (use-local-map esh-mode-map)
  (setq major-mode 'esh-mode)
  (setq mode-name "esh"))

(defgroup esh nil
  "Editing esh code"
  :group 'lisp)

(defvar calculate-lisp-indent-last-sexp)

;; Copied from lisp-indent-function, but with gets of
;; esh-indent-{function,hook}.
(defun esh-indent-function (indent-point state)
  (let ((normal-indent (current-column)))
    (goto-char (1+ (elt state 1)))
    (parse-partial-sexp (point) calculate-lisp-indent-last-sexp 0 t)
    (if (and (elt state 2)
             (not (looking-at "\\sw\\|\\s_")))
        ;; car of form doesn't seem to be a a symbol
        (progn
          (if (not (> (save-excursion (forward-line 1) (point))
                      calculate-lisp-indent-last-sexp))
              (progn (goto-char calculate-lisp-indent-last-sexp)
                     (beginning-of-line)
                     (parse-partial-sexp (point)
					 calculate-lisp-indent-last-sexp 0 t)))
          ;; Indent under the list or under the first sexp on the same
          ;; line as calculate-lisp-indent-last-sexp.  Note that first
          ;; thing on that line has to be complete sexp since we are
          ;; inside the innermost containing sexp.
          (backward-prefix-chars)
          (current-column))
      (let ((function (buffer-substring (point)
					(progn (forward-sexp 1) (point))))
	    method)
	(setq method (or (get (intern-soft function) 'esh-indent-function)
			 (get (intern-soft function) 'esh-indent-hook)))
	(cond ((or (eq method 'defun)
		   (and (null method)
			(> (length function) 3)
			(string-match "\\`def" function)))
	       (lisp-indent-defform state indent-point))
	      ((integerp method)
	       (lisp-indent-specform method state
				     indent-point normal-indent))
	      (method
	       (funcall method state indent-point)))))))

;; (put 'begin 'esh-indent-function 0), say, causes begin to be indented
;; like defun if the first form is placed on the next line, otherwise
;; it is indented like any other form (i.e. forms line up under first).

(put 'begin 'esh-indent-function 0)
(put 'eval 'esh-indent-function 0)
(put 'eval! 'esh-indent-function 0)
(put 'if 'esh-indent-function 0)


(provide 'esh)

;;; esh-mode.el ends here
