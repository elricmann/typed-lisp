(program
  ;; (def add : bool ()
  ;;   ;; (let x : int 1)
  ;;   true) ;; returning this literal works

  ;; issue with the unbound variables is the def scope
  ;; also, infer_binary_op is not actually used anywhere yet
  ;; whereas infer_literal works on every atom node

  ;; visit_call is messed up
  (let k : int 2)
  0)