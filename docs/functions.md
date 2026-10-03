# functions

`to` makes em, `give` gives back. BOOM!

```shl
to add(a, b)
    give a + b

say add(2, 3)   # 5
```

no `give`? u get null stupid.

functions are vals so u can pass em around like candy, nest em, and they grab locals from outside. 

```shl
to counter()
    n = 0
    to bump()
        n = n + 1
        give n
    give bump

c = counter()
say c()   # 1
say c()   # 2
```

```shl
to apply(f, x)
    give f(x)

to double(n)
    give n * 2

say apply(double, 5)   # 10
```

recursion works idk why u wouldnt think so STUPID

```shl
to fact(n)
    if n <= 1
        give 1
    give n * fact(n - 1)

say fact(5)   # 120
```

assigning inside a function makes a LOCAL. STUPID U THINK GLOBAL NO WORK? IT WORKY.

```shl
x = 10
to f()
    x = 99      # local, global untouched
    give x

say f()   # 99
say x     # 10
```