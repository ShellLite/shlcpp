# shlcpp

### shlcpp (ShellLite (i know it's a weird name don't bother me) but in cpp) is a programming language which has pseudocode like syntax (i know it's a really weird concept but this should help people get a good grasp of the logic of programming before stepping into a more powerful programming language) (sort of like COBOL and AppleScript)

### Developer's Note:- Hi I am Shrey :), I started ShellLite (shlcpp) after looking at kids struggle (in grade 11th cs, we just started with python at that time) with learning syntax in school, and from there my journey began (BOOM), learning about how programming languages work, etc. that lead to me writing the initial prototype of ShellLite written in python (yes i know don't judge me)... Since then it always bugged me that the performance for the language was pretty crap (like how she treats u), so almost immediately after i was happy with the prototype and wanted to do some proper testing and benchmarks i began the work on shlcpp... and here we are today :) (or i guess a couple of days/weeks/months ahead?)

## Some Simple Examples

### Functions & I/O
```shl
to calculate_area(width, height)
    give width * height

can greet(name)
    show "Hello, " + name

say calculate_area(10, 5)
greet("Alice")
```

### Conditionals & Loops
```shl
is_ready = false
unless is_ready
    say "Not ready"

repeat 3 times
    say "Looping..."

count = 0
until count == 3
    count += 1
```

### Structures / Classes
```shl
thing Counter
    has value = 0

    can increment(amount)
        self.value = self.value + amount

c = make Counter(10)
c.increment(5)
say c.value
```

## Building

Requires CMake 3.15+ and a C++17 compiler.

**Windows**:
```powershell
.\compile_shl.bat
```

**Linux / macOS**:
```bash
./install.sh
```

**Manual (CMake)**:
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

## Usage

```bash
# REPL
shlcpp

# Run script
shlcpp script.shl

# Run inline code
shlcpp -e "say 'Hello!'"

# Check syntax
shlcpp check script.shl

# Compile to bytecode (.shbc)
shlcpp -c script.shl script.shbc
shlcpp script.shbc
```
^ I also added the formator but will add it here in a bit

## Tests

```bash
pytest tests/ -v
```

## Documentation

- [Language Specification](LANGUAGE_SPEC.md)
- [Other Documentation don't read it unless u want horrors](docs/) 

## License

GNU GPL v3 with Classpath Exception - see [LICENSE](LICENSE).
