# syntax

## blocks

it's the same indentation like anywhere stupid

also uhm block headers do NOT need colons unlike python... so `if x > 5` and `if x > 5:` both parse idk why u would want colons tho -_- it's only accepted so Python people feel fine ig...

```shl
if x > 5
    say "big"
elif x > 2
    say "medium"
else
    say "small"
```

Blocks nest by indenting further and a block ends when the indentation drops back also for some stopids i need to tell this... blank lines DO NOT MEAN THE BLOCK HAS ENDED OKAY?

also do NOT mix tabs and spaces in the same file idiot.

tbf it might still run (barely) BUT u are on thin ice bud.

## comments

```shl
# this is a comment stupid

/* so is this bud
   it just spans lines */
```

## values

```shl
x = 42            # numbers: everything is a 64-bit float, ints and decimals are the same kind like pythona
pi = 3.14
name = "shrey"    # strings: double or single quotes, both fine
ok = true         # booleans
nada = null       # null like ur brain
items = [1, 2, 3]                    # lists
user = {"name": "a", "age": 30}      # dicts
```

we have one number type as it keeps things simple for idiots like YOU yes YOU SPECIFICALLY (that means no int vs float bugs so `10 / 4` is just 2.5) The tradeoff is you dont get real integers :(
(prob should change this)

uh do `\n` for newlines, Lists and dicts nest like how there are pigeon nests in your home kiddo

## variables

`x = 10`  BOOM! (fyi if u had to read this part, then uhm ggwp u are cooked)

Indexing works just how it works in python (maybe i should add a warning for people to study python before they start learning my language (wasn't i trying to make learning languages easier than python? damn what a mess i created well idts i can do anything else now so good luck 💀))

```shl
items = [10, 20, 30, 40]
say items[0]     # 10
say items[-1]    # 40 negative indices count from the end
say items[1:3]   # [20, 30], slices are [start:end)
say "hello"[1:4] # "ell" string slicin
say user["name"] # dict lookup by key
```

we got a few methods for list `.len()`, `.append()...` I will make it better dw "trust"
