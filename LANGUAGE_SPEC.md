# shlcpp Language Specification

shlcpp is an indentation sensitive, dynamically typed programming language with a pseudocode like syntax. It supports both **Standard Syntax** as well as **Natural Language Syntax**.

---

# Standard Syntax

## 1. Syntax & Indentation

- 4 space indentation defines code blocks. Tabs expand to 4 spaces.
- Trailing colons (`:`) on block headers are optional.
- `#` for single-line comments, `/* ... */` for multi-line comments.

```shl
# Single-line comment
/* Multi-line
   comment */

x = 10
if x > 5
    print("x is greater than 5")
```

## 2. Types & Literals

```shl
# Numbers (64-bit float)
count = 42
pi = 3.14159

# Strings
title = "ShellLite"
msg = 'Hello, ' + title

# Booleans & Null
is_valid = true
is_done = false
empty_val = null

# Lists (dynamic arrays)
items = [10, 20, 30]
items.append(40)
print(items[0])       # 10
print(items.len())    # 4 (method dispatch delegating to len())

# Dictionaries (hash maps)
user = {
    "name": "Alice",
    "age": 30
}
print(user["name"])
user["role"] = "Admin"
```

## 3. Operators

```shl
# Arithmetic
a = 10 + 5         # 15
b = 10 - 3         # 7
c = 4 * 2          # 8
d = 10 / 2         # 5
e = 10 % 3         # 1
f = 2 ** 3         # 8

# Comparison
eq = (a == b)
neq = (a != b)
gt = (a > b)
lt = (a < b)
gte = (a >= b)
lte = (a <= b)

# Logical
ok = (true and false)    # false
any_ok = (true or false) # true
inv = not true           # false

# In-place Assignment
x = 10
x += 5             # 15
x -= 2             # 13
x *= 2             # 26
x /= 2             # 13
```

## 4. Control Flow

### Conditionals (`if`, `elif`, `else`)
```shl
score = 85

if score >= 90
    grade = "A"
elif score >= 80
    grade = "B"
else
    grade = "C"

# Optional trailing colon
if score > 50:
    print("Passed")
```

### Loops (`while`, `for`)
```shl
# While loop
i = 0
while i < 5
    i += 1

# For-in loop
for num in [1, 2, 3]
    print(num)

for ch in "Shell"
    print(ch)

# Loop control: stop (break) & skip (continue)
for n in [1, 2, 3, 4, 5]
    if n == 2
        skip
    if n == 4
        stop
```

## 5. Functions & Lambdas

```shl
# Function definition (def)
def add(x, y)
    return x + y

# Recursion
def factorial(n)
    if n <= 1
        return 1
    return n * factorial(n - 1)

# Higher-order functions & Lambdas (fn / lambda)
# Arguments are comma-separated bare identifiers: fn <args> => <expr>
def transform(arr, mapper)
    res = []
    for x in arr
        res.append(mapper(x))
    return res

nums = [1, 2, 3]
doubled = transform(nums, fn x => x * 2)
sum_fn = fn x, y => x + y
```

## 6. Classes & OOP

```shl
class BankAccount
    # Declares properties (compiler synthesizes default init constructor)
    # Positional constructor arguments override default field values in declaration order
    has owner
    has balance = 0

    def deposit(amount)
        self.balance = self.balance + amount

    def withdraw(amount)
        if amount <= self.balance
            self.balance = self.balance - amount
            return true
        return false

# Instantiation & Method Calls (new)
acc = new BankAccount("Alice", 1000)
acc.deposit(500)
print(acc.balance)    # 1500
acc.withdraw(200)
print(acc.balance)    # 1300
```

## 7. Error Handling

```shl
try
    throw "Database error"
catch err
    print("Caught: " + err)
finally
    print("Done")
```

## 8. Concurrency & Channels

```shl
# Lock-free channels (Copy-on-Write)
ch = chan_open()

# Shared send (zero-copy until mutation)
data = [1, 2, 3]
chan_send(ch, data)
data[0] = 999      # Sender triggers local copy; receiver unaffected

received = chan_recv(ch)
print(received[0]) # 1

# Ownership transfer (tombstones sender variable)
obj = {"id": 101}
chan_transfer(ch, obj)
# obj is now null in sender thread

# Mutual exclusion locks
l = create_lock()
lock_acquire(l)
print("In critical section")
lock_release(l)
```

## 9. Modules & Imports

```shl
import "net" as network
use "math"
```

---

# The NLP syntax

ShellLite provides natural English language aliases for expressive scripting.

## 1. Natural Functions & I/O (`to`, `can`, `give`, `say`, `show`)

```shl
# to / can defines a function
to calculate_area(width, height)
    give width * height

can greet(user_name)
    show "Hello, " + user_name

say calculate_area(5, 4)   # 20
greet("Alice")
```

## 2. Natural Conditionals & Assertions (`unless`, `check`)

```shl
# unless (inverted conditional)
is_done = false
unless is_done
    say "Processing in progress..."

# check assertion
count = 0
check count == 0
```

## 3. Natural Loops (`repeat ... times`, `until`, `forever`)

```shl
# Repeat fixed times
total = 0
repeat 5 times
    total += 10

# Until loop (runs until condition is true)
count = 0
until count == 5
    count += 1

# Forever loop
forever
    if count >= 5
        stop
```

## 4. Natural OOP (`thing`, `has`, `make`)

```shl
# Thing defines a structure/class
# Positional constructor arguments override default field values in declaration order
thing Counter
    has value = 0

    can increment()
        self.value = self.value + 1

    can get_value()
        give self.value

# Make instantiates a thing (10 overrides default 0)
c = make Counter(10)
c.increment()
say c.get_value()  # 11
```

## 5. Natural Operators & English Words

```shl
# Arithmetic words
sum = 10 plus 5             # 15
diff = 20 minus 8           # 12

# Comparison phrases
is_above = 100 is more than 50
is_below = 10 is less than 20
is_at_least = 25 is at least 20

# Boolean words & noise keywords
let is_active be yes        # true
let is_closed be no         # false
```

## 6. Natural Parallel Execution (`parallel`)

```shl
# Structured parallel block execution
parallel
    say "Executing task A"
    say "Executing task B"
```

---

## Standard Built in References

```shl
print("Inline output")
say "Line output"
len([1, 2, 3])             # 3 (global function; also items.len() method dispatch)
str(100)                   # "100"
int("42")                  # 42
float("3.14")              # 3.14
bool(1)                    # true
type("hello")              # "string"
assert(1 + 1 == 2, "Test failed")
```
