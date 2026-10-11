# concurrency

## spawn n await

```shl
to work(n)
    return n * 2

t = spawn work(21)   # runs in the background t is a task btw
say await t          # 42 blocks till it is done
```

spawn ANYTHING, uh u can await it as well... I guess that's it

## channels

tasks talk through channels, very civilized UNLIKE YOU MONSTER

```shl
ch = chan_open()
chan_send(ch, "hello")
say chan_recv(ch)   # "hello", blocks till something arrives
chan_close(ch)      # hangs up like she hung up on your face :)
```

## locks

btw spawned tasks are isolated, they get a copy of ur globals not the real thing so there is no shared mutable state to protect. use channels to talk between tasks...

locks exist (`create_lock`, `lock_acquire`, `lock_release`) but since tasks dont share memory u probably dont need them

also FORGET TO RELEASE AND UR DEADLOCKED LMFAO HAVE FUN

```shl
l = create_lock()
lock_acquire(l)
say "critical section, very serious (unlike u)"
lock_release(l)
```
