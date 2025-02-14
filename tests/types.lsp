(def id : 'a (x : 'a) x)

(def add : int (x : int) (y : int)
  (+ x y))

(def increment : int (x : int)
  (+ x 1))

(def is_unsigned : bool (x : int)
  (> x 0))

(def test_if : int (k : int)
  (if (is_unsigned 7)
    (increment 10)
    0))

(def bad_add : int (k : int)
  (+ true 7))

(def bad_if : int (k : int)
  (if 7
    1
    0))
