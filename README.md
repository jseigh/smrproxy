# smrproxy
Proxy collector using memory barrier free hazard pointers for extremely fast lock-free read access to shared data.

Similar to sleepable RCU but without the need for explicit quiesce points.

Written in C (C17) using stanard C libraries except for some platform specific code to execute a global memory barrier and get the shared cache line size.  Currently the only supported platform is linux.   Windows does not fully support C17 and is unlikely to do so in a timely manner.

Based on

2005-04-19 hazard pointers w/o the memory barrier
How to implement hazard pointers without the expensive store/load memory barrier.
https://groups.google.com/g/comp.programming.threads/c/XU6BtGNSkF0/m/AmWXvkGn3DAJ

2005-05-09 hazard pointers w/o the memory barrier
https://groups.google.com/g/linux.kernel/c/gk6AUkXR9As/m/-1Ws1gPsXocJ

2006-01-23 Hazard pointer based proxy collector
Proxy collector using hazard pointers but using version or epoch numbers instead of objects.
https://groups.google.com/g/comp.programming.threads/c/aPx1YrpOzHo/m/plShTt8PCZYJ

## Example
In main thread

```
smrproxy_t *proxy = smrproxy_create(NULL);      // default config
...
smrproxy_destroy(proxy);
```

In reader threads
```
smrproxy_ref_t *ref = smrproxy_ref_create(proxy);      // once
...
smrproxy_ref_acquire(ref); // prior to every read access to data
...
smrproxy_ref_release(ref); // after every read access data
...
smrproxy_ref_destroy(ref);        // once before thread exit
```

In writer thread
```
... // update shared data
smrproxy_retire_sync(proxy, pdata, &free);   // synchronously free data when safe to do so
smrproxy_retire_async(proxy, pdata, &free);  // asynchronously free data when safe to do so (smrproxy must be configured for this)
```

## Build
In main directory
...
cmake .
make install
...
Header file, smrproxy.h, in project include directory.
Library file, libsmrproxy.a, in project lib directory.

In test directlry
...
cmake .
make
...
