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

shared state? protect it like you try to protect your chips u fatty

```shl
l = create_lock()
lock_acquire(l)
say "critical section, very serious (unlike u)"
lock_release(l)   # FORGET THIS AND UR DEADLOCKED LMFAO HAVE FUN
```
