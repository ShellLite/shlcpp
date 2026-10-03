# stdlib

## the greatest hits

```shl
say "hi"              # print (CRAZY, ISNT THAT OBVIOUS DUMBO, i am so ashamed i need to tell u dis man) anyways say is cooler... do NOT argue with me
len([1, 2, 3])        # prints 3, fyi dis works on strings and dicts too
push(items, 4)        # stick it on the end (hah like she sticks at last of her priority list lmfao)
x = pop(items)        # yoink it off the end (like how removed u from her life :O )
range(5)              # prints [0, 1, 2, 3, 4] just like python stupid
list_sort(items)      # sorts (yes i need to explain this for some idiots) also idiot list_reverse exists too (i don't expect u to be smart enough to try it out so dw)
```

## types

`str()`, `int()`, `float()`, `bool()` u can convert stuff from one type to another using it, `typeof(x)` snitches (like ur sister snitched on u and then u got whopped at home) and tells u what type it is

## strings

```shl
str_upper("hi")       # "HI"
str_lower("HI")       # "hi"
str_trim("  hi  ")    # "hi" i eat the whitespace (yes i EAT it)
```

## dicts

```shl
dict_keys({"a": 1})       # ["a"]
dict_values({"a": 1})     # [1]
dict_has({"a": 1}, "a")   # true
```

## json

```shl
json_stringify({"a": 1})  # '{"a": 1}'
json_parse('{"a": 1}')     # back to a dict
json_is_valid("[1, 2")     # false obviously (it's fine i don't epxect u to get this)
```

## files

```shl
f = file_open("notes.txt", "r")
say file_read(f)    # print the whole file obviously
file_close(f)       # please and i mean please don't forget to close the file u animal
```

there is also `randint(a, b)`, `hex()`, `bin()`, `zip()`, `enumerate()` and like 70 more, full list lives in the source if ur curious (i aint typing all that leave me alone) plus modules for db, web, csv and other fancy stuff (which again i aint typin i mean i technically SHOULD and would love some help on it :D)
