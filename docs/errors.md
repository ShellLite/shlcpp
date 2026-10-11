# errors

`try` / `catch` / `always`:-

```shl
try
    risky_thing()
catch e
    say "broke: " + str(e)
always
    say "cleaning up"
```

```shl
to f()
    try
        give 1
    always
        say "cleanup ran"

say f()   # "cleanup ran", then 1
```

```shl
to f()
    try
        give 1
    always
        give 2

say f()   # 2
```

`try`/`always` with no `catch` runs cleanup then rethrows the error btw. it does NOT swallow it, dont rely on that okay stupid?