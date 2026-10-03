# operators

`in` / `not in` SOMPLE

```shl
say 2 in [1, 2, 3]        # true
say 9 not in [1, 2, 3]    # true
say "el" in "hello"       # true
say "a" in {"a": 1}       # true

```

arithmetic: `+ - * / % **`. `/` is always float, `**` is power, `+` glues strings STUPID

```shl
say 2 ** 3        # 8
say 7 % 3         # 1
say 10 / 4        # 2.5
say "a" + "b"    # "ab"
say 1 + 2 * 3    # 7, normal precedence
```

comparisons `== != < > <= >=`, logic `and` `or` `not`. words not symbols bud.

`==` is by value for numbers strings bools.