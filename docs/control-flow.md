# control flow

`if` / `elif` / `else` bs 

```shl
guess = 5
secret = 7
if guess == secret
    say "nailed it"
elif guess < secret
    say "too low"
else
    say "too high"
```

## loops

3 types.

```shl
repeat 5 times
    say "again"

i = 0
while i < 10
    i = i + 1

for x in [1, 2, 3]
    say x
```

`stop` breaks, `skip` skips crazy ikr.

```shl
i = 0
while i < 5
    i = i + 1
    if i == 3
        skip
    say i      # 1 2 4 5
```