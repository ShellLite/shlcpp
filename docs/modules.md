# modules

split ur code like a responsible adult (sorry i asked this from YOU)

```shl
# greet.shl
to hello(name)
    return "hi " + name
VERSION = "SIGMA"
```

```shl
# main.shl
import greet
say greet.hello("bud")   # hi bud
say greet.VERSION        # 1.0
```

## the variants

```shl
import greet as g          # alias for lazy typists
from greet import hello    # yoink just the function
say hello("stupid")        # hi stupid (yes i am referring to you here yes you)
```

modules are just `.shl` files, import finds em next to ur script (like python obviously)

functions and globals come along (like how ur mom drags u to go grocery shoping) and local shit stay where they are.
