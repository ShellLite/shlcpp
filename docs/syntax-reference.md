# syntax reference

I have tried to list
every keyword, construct and alias...
if it aint here it probably does not exist

## functions

| does what | syntax |
|-----------|--------|
| define a function | `to name(args)` / `can name(args)` |
| define (alt keywords) | `def name(args)` / `function name(args)` |
| lambda | `fn x => x * 2` / `take x => x * 2` / `lambda x => x * 2` |
| return a value | `return x` / `give x` |

## printing n input

| does what | syntax |
|-----------|--------|
| print | `say x` / `show x` |
| print (alt) | `print x` |
| print to stderr | `esay x` |
| read input | `input("prompt")` / `ask("prompt")` |

## variables

| does what | syntax |
|-----------|--------|
| assign | `x = 5` / `x is 5` / `x be 5` |
| constant | `const x = 5` |
| ignored words | `the`, `let` (e.g. `the x = 5` is just `x = 5`) |
| delete | `del obj.field` / `del obj[key]` (attributes only not plain vars) |
| assert | `check x == 5` (blows up if false) |

## classes

| does what | syntax |
|-----------|--------|
| define | `class Name` / `thing Name` / `structure Name` |
| constructor | `to init(args)` (runs on `Name()`) |
| make one | `Name()` / `make Name()` / `new Name()` |
| inherit | `class Child extends Parent` |
| parent init | `super(args)` |
| field default | `has x = 5` inside the class body |

## conditionals

| does what | syntax |
|-----------|--------|
| if / else if / else | `if cond` / `elif cond` / `else` |
| guard (no else!) | `unless cond` (= `if not cond`, guard only) |
| match | `when x` ... `is value` ... `otherwise` |

## loops

| does what | syntax |
|-----------|--------|
| for loop | `for x in list` / `for each x in list` |
| while loop | `while cond` |
| until loop | `until cond` (loops while cond is false) |
| counted loop | `repeat 5 times` |
| infinite loop | `forever` (pair with `stop` or rip) |
| break | `stop` |
| continue | `skip` |
| do nothing | `pass` |

## booleans n logic

| does what | syntax |
|-----------|--------|
| true | `true` / `yes` |
| false | `false` / `no` |
| logic | `and`, `or`, `not` (words not symbols man) |
| nothing | `null` |

## errors

| does what | syntax |
|-----------|--------|
| try / catch | `try` ... `catch e` |
| cleanup | `always` / `finally` |
| raise | `throw x` / `error x` |

## modules

| does what | syntax |
|-----------|--------|
| import | `import mod` / `import mod as m` |
| selective | `from mod import name` |

## concurrency

| does what | syntax |
|-----------|--------|
| run in background | `t = spawn f()` |
| wait for it | `await t` |
| channel | `chan_open()` / `chan_send(ch, x)` / `chan_recv(ch)` / `chan_close(ch)` |
| lock | `create_lock()` / `lock_acquire(l)` / `lock_release(l)` |

## types

| what | syntax |
|------|--------|
| number | `42`, `3.14` (everything is 64 bit float) |
| string | `"hi"` or `'hi'` |
| list | `[1, 2, 3]` |
| dict | `{"a": 1}` |

## operators

| does what | syntax |
|-----------|--------|
| arithmetic | `+ - * / % **` (`/` is always float, `**` is power) |
| comparison | `== != < > <= >=` |
| compound assign | `+= -= *= /=` |
| membership | `in` / `not in` |

that should be everything i hope...
