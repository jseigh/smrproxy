/*
   Copyright 20203 Joseph W. Seigh
   
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef SMRPROXY_H
#define SMRPROXY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef unsigned long epoch_t;

/**
 * @brief opaque handle to smr proxy object
*/
typedef struct smrproxy_t smrproxy_t;   // forward declare

/*
* smrproxy configuration
*/
typedef struct smrproxy_config_t {
    unsigned int queue_size;        // size of retired objects queue
    unsigned int polltime;          // proxy refs poll interval in milliseconds
    bool poll;                      // use backgroupd polling thread -- boolean (default=false)
    long cachesize;                 // default cachesize if not available from system, must be a power of 2.
} smrproxy_config_t;

/*
* hazard pointer reference to epoch
*/
typedef struct smrproxy_ref_t {
    epoch_t epoch;
    epoch_t *proxy_epoch;
    //
    uintptr_t   data;               // for optional use by user application, e.g. recursive counting, ...
} smrproxy_ref_t;

/*
*
*/

/**
 * Get copy of default config
 * 
 */
extern smrproxy_config_t *smrproxy_default_config();

/**
 * Create an smr proxy
 * 
 * @param config smrproxy configureation or if NULL, use default configuration
 * 
 * @return the smrproxy
*/
extern smrproxy_t * smrproxy_create(smrproxy_config_t *config);
/**
 * Destroy an smr proxy
 * 
 * @param proxy the smrproxy to be destroyed
 * 
 * @note any smrproxy refs still existing are destroyed
 * and any pending retires are executed.
*/
extern void smrproxy_destroy(smrproxy_t *proxy);

/**
 * Retire a data object asynchronously.
 * 
 * @param proxy the smr proxy
 * @param data address of data to be retired
 * @param dtor destructor function for data
 * 
 * @return epoch  -- ?
*/
extern long smrproxy_retire_async(smrproxy_t *prooxy, void *data, void (*dtor)(void *));

/**
 * Retire a data object synchronously.
 * 
 * @param proxy the smr proxy
 * @param data address of data to be retired
 * @param dtor destructor function for data
 * 
 * @return
 *- 0 if successful
 *- EDEADLK if current thread has a acquired ref
*/
extern int smrproxy_retire_sync(smrproxy_t *proxy, void *data, void (*dtor)(void *));

/**
 * Create an smrproxy reference
 * 
 * @param proxy the smrproxy
 * @return smrproxy reference
*/
extern smrproxy_ref_t * smrproxy_ref_create(smrproxy_t *proxy);

/**
 * Destroy an smrproxy reference
 * 
 * @param ref smrproxy reference
*/
extern void smrproxy_ref_destroy(smrproxy_ref_t *ref);

/**
 * Acquire an smrproxy protected reference to current epoch
 * 
 * @param ref smrproxy reference
*/
inline static void smrproxy_ref_acquire(smrproxy_ref_t *ref)
{
    long *epoch = ref->proxy_epoch;
    long *ref_epoch = &ref->epoch;

    long local, local2;

#ifndef SMRPROXY_MB
    local = atomic_load_explicit(epoch, memory_order_relaxed);
    do {
        local2 = local;
        atomic_store_explicit(ref_epoch, local, memory_order_relaxed);
        atomic_signal_fence(memory_order_seq_cst);
        local = atomic_load_explicit(epoch, memory_order_relaxed);
    }
    while (local != local2);
    atomic_thread_fence(memory_order_acquire);
#else
    local = atomic_load_explicit(epoch, memory_order_seq_cst);
    do {
        local2 = local;
        //atomic_store_explicit(ref_epoch, local, memory_order_seq_cst);
        atomic_store_explicit(ref_epoch, local, memory_order_seq_cst);
        //atomic_thread_fence(memory_order_seq_cst);
        local = atomic_load_explicit(epoch, memory_order_seq_cst);
    }
    while (local != local2);
    atomic_thread_fence(memory_order_acquire);
#endif
}

inline static void smrproxy_ref_release(smrproxy_ref_t *ref)
{
    atomic_store_explicit(&ref->epoch, 0, memory_order_release);
}

#ifdef __cplusplus
}
#endif

#endif /* SMRPROXY_H */