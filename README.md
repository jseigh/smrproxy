# smrproxy
Proxy collector using memory barrier free hazard pointers for extremely fast lock-free read access to shared data.

Similar to sleepable RCU but without the need for explicit quiesce points.

Written in C (C17) using stanard C libraries except for some platform specific code to execute a global memory barrier and get the shared cache line size.  Currently the initial POC version only supports linux


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

