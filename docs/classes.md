# classes

yea i have oop dont cry dont cry child i know its tuff man

```shl
class Dog # dog like u hah stopid
    to init(name)
        self.name = name
    to speak()
        say self.name + " says woof"

d = Dog("rex")   # init runs automatically
d.speak()        # rex says woof
```

## inheritance

```shl
class Puppy extends Dog
    to init(name)
        super(name)   # parent init and args pass through it
    to speak()
        say self.name + " says yip"

p = Puppy("max")
p.speak()   # max says yip, override works like u expect
```

## fields

fields live on `self` make em read em `del` em tbf I DON'T CARE OKAY? DO WHAT U WANT JUST DONT BOTHER ME

```shl
d.tricks = ["sit"]   # just assign it dumbo
del d.name           # gone poof (like she left ur life)
```

THAT IS IT GET OUT

run before i eat u.
