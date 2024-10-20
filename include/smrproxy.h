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

typedef uint32_t epoch_t;

/**
 * local quiescent state count
*/
typedef uint64_t qslocal_t;

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
    long cachesize;                 // default cachesize if not available from system, must be a power of 2.
} smrproxy_config_t;

/*
* reader reference to epoch
*/
typedef struct smrproxy_ref_t {
    epoch_t epoch;                  // epoch as observed by reader thread, or 0
    epoch_t *proxy_epoch;


    /*-*/

    epoch_t current_epoch;          // current epoch set by reclaim thread


    epoch_t effective_epoch;         // effective epoch as observed by reclaim thread, never 0


    //
    uintptr_t   data;               // for optional use by user application, e.g. recursive counting, ...

    // TODO stats...
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
 * Retire a data object asynchronously and set expiry epoch.
 * @param proxy the smr proxy
 * @param data address of data to be retired
 * @param dtor destructor function for data
 * @param setexpiry set expiry value function
 * @param ctx optional context for setexpiry or NULL
 * @returns expiry epoch of retired object or 0 if no space to queue retirement
*/
extern epoch_t smrproxy_retire_exp(smrproxy_t *proxy, void *data, void (*dtor)(void *), void (*setexpiry)(epoch_t expiry, void *data, void *ctx), void *ctx);

/**
 * Retire a data object asynchronously.
 * @param proxy the smr proxy
 * @param data address of data to be retired
 * @param dtor destructor function for data
 * @returns expiry epoch of retired object or 0 if no space to queue retirement
*/
extern epoch_t smrproxy_retire(smrproxy_t *proxy, void *data, void (*dtor)(void *));

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
 * long
 * @param ref smrproxy reference
*/
inline static void smrproxy_ref_acquire(smrproxy_ref_t *ref)
{
    // __builtin_prefetch(ref, 1, 0);

    epoch_t *epoch = &ref->current_epoch;
    epoch_t *ref_epoch = &ref->epoch;

    epoch_t local;

#ifndef SMRPROXY_MB
    local = atomic_load_explicit(epoch, memory_order_relaxed);
    atomic_store_explicit(ref_epoch, local, memory_order_relaxed);

    atomic_thread_fence(memory_order_acquire);
#else
    // TODO Does this still work?
    local = atomic_load_explicit(epoch, memory_order_seq_cst);
    atomic_store_explicit(ref_epoch, local, memory_order_seq_cst);
    atomic_thread_fence(memory_order_acquire);
#endif
}

inline static void smrproxy_ref_release(smrproxy_ref_t *ref)
{
    atomic_store_explicit(&ref->epoch, 0, memory_order_release);
}

/*
 * experimental api
 * may be removed or changed
*/

/**
 * Get current epoch.
 * The current epoch will be the expiry epoch used for the next retire.
 * Synchronization is reqired to ensure this.
 * @param proxy the proxy
 * @returns current epoch
*/
extern epoch_t smrproxy_get_epoch(smrproxy_t *proxy);

/**
 * Set the reference epoch to node's expiry epoch if the object has been retired
 * or a recent current epoch value if the node is still live.  Any traveral of a
 * data structure requires the expiry values (if set) be monotonically increasing.
 * @param ref current threads epoch reference
 * @param getexpiry function to get expiry epoch value or 0 if node is still live.
 * @param node the current data structure node.
 * 
*/
extern void smrproxy_ref_next(smrproxy_ref_t *ref, epoch_t (*getexpiry)(void *data, void *ctx), void *node, void *ctx);



#ifdef __cplusplus
}
#endif

#endif /* SMRPROXY_H */