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

#include <stddef.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <threads.h>
#include <string.h>
#include <errno.h>

#include <smrproxy_intr.h>


static smrproxy_config_t default_config = {
    200,    // 200 retire queue sloots
    50,     // 50 msec poll interval
    64,     // default cachesize
};

smrproxy_config_t *smrproxy_default_config()
{
    smrproxy_config_t * config = malloc(sizeof(smrproxy_config_t));
    memcpy(config, &default_config, sizeof(smrproxy_config_t));
    return config;
}

static int *smrproxy_poll3(void *arg);

smrproxy_t * smrproxy_create(smrproxy_config_t *config)
{
    if (config == NULL)
        config = &default_config;

    smrproxy_t * proxy = malloc(sizeof(smrproxy_t));
    proxy->config = *config;
    long cachesize = getcachesize();
    if (cachesize > 0)
        proxy->config.cachesize = cachesize;
    else
        cachesize = proxy->config.cachesize;

    mtx_init(&proxy->mutex, mtx_plain);
    cnd_init(&proxy->cvar);
    tss_create(&proxy->key, (tss_dtor_t) &smrproxy_ref_destroy);

    proxy->membar = smrproxy_membar_create();   // TODO test return value

    proxy->epoch = aligned_alloc(cachesize, cachesize);     // cachesize > sizeof epoch_t

    epoch_t epoch = 1;
    *proxy->epoch = epoch;
    proxy->head = epoch;
    proxy->sync_epoch = epoch - 2;  // ?

    proxy->refs = NULL;

    proxy->queue = smrqueue_create(*proxy->epoch, config->queue_size);

    proxy->active = true;
    /*
    * proxy initialized
    */

    thrd_t *tid = &proxy->poll_tid;
    thrd_create(tid, (thrd_start_t) &smrproxy_poll3, proxy);
    proxy->poll_thread = tid;

    return proxy;
}

void smrproxy_destroy(smrproxy_t *proxy)
{
    mtx_lock(&proxy->mutex);

    if (proxy->poll_thread != NULL) {
        thrd_t tid = *(proxy->poll_thread);
        proxy->active = false;
        cnd_broadcast(&proxy->cvar);
        mtx_unlock(&proxy->mutex);
        thrd_join(tid, NULL);
        mtx_lock(&proxy->mutex);
        //free(proxy->poll_thread);
        proxy->poll_thread = NULL;
    }

    mtx_unlock(&proxy->mutex);


    // delete all refs
    while (proxy->refs != NULL) {
        // TODO add tid to ref in ref create and print diagnostics
        smrproxy_ref_destroy((smrproxy_ref_t *)proxy->refs);
    }


    smr_dequeue(proxy->queue, *proxy->epoch);

    smrqueue_destroy(proxy->queue);
    smrproxy_membar_destroy(proxy->membar);

    tss_delete(proxy->key);
    cnd_destroy(&proxy->cvar);
    mtx_destroy(&proxy->mutex);

    free(proxy->epoch);
    memset(proxy, 0, sizeof(smrproxy_t));
    free(proxy);
}

static smrproxy_ref_ex_t * smrproxy_ref_ex_create(smrproxy_t *proxy)
{
    smrproxy_ref_ex_t *ref_ex = tss_get(proxy->key);
    if (ref_ex != NULL)
    {
        return ref_ex;
    }

    size_t cachesize = proxy->config.cachesize;

    size_t size = ((sizeof(smrproxy_ref_t) + cachesize - 1)/cachesize)*cachesize;
    ref_ex = aligned_alloc(cachesize, size);
    if (ref_ex == NULL)
        return  NULL;

    memset(ref_ex, 0, size);
    ref_ex->proxy = proxy;

    mtx_lock(&proxy->mutex);

    ref_ex->ref.proxy_epoch = proxy->epoch;
    ref_ex->ref.epoch = 0;
    ref_ex->ref.current_epoch = *proxy->epoch;
    ref_ex->ref.effective_epoch = *proxy->epoch;

    ref_ex->next = proxy->refs;
    proxy->refs = ref_ex;
    mtx_unlock(&proxy->mutex);

    tss_set(proxy->key, ref_ex);

    return ref_ex;
}

smrproxy_ref_t * smrproxy_ref_create(smrproxy_t *proxy)
{
    smrproxy_ref_ex_t *ref_ex = smrproxy_ref_ex_create(proxy);
    return &ref_ex->ref;
}

static void smrproxy_ref_ex_destroy(smrproxy_ref_ex_t *ref_ex)
{
    if (ref_ex == NULL)
        return;

    smrproxy_t *proxy = ref_ex->proxy;

    smrproxy_ref_ex_t *ref2 = tss_get(proxy->key);
    if (ref2 != NULL && ref2 == ref_ex)
    {
        tss_set(proxy->key, NULL);
    }

/*
    if (ref->epoch != 0)
        ;   // error;  TODO do a release ?
*/

    int rc = mtx_lock(&proxy->mutex);
    if (rc != thrd_success)
        return;

    if (proxy->refs == ref_ex) {
        proxy->refs = ref_ex->next;
    }

    else {
        smrproxy_ref_ex_t *prev  = proxy->refs;
        while (prev != NULL && prev->next != ref_ex)
        {
            prev = prev->next;
        }
        if (prev != NULL)
            prev->next = ref_ex->next;
        else
            ;   // error
    }

    free(ref_ex);

    mtx_unlock(&proxy->mutex);
}

void smrproxy_ref_destroy(smrproxy_ref_t *ref)
{
    smrproxy_ref_ex_destroy((smrproxy_ref_ex_t *) ref);
}

static epoch_t poll_smrref(epoch_t oldest, smrproxy_ref_ex_t *ref)
{
        epoch_t ref_epoch = ref->ref.epoch;

        if (ref_epoch == 0)
            return oldest;
        else if (xcmp(ref_epoch, ref->proxy->head) < 0)
            return oldest;
        else if (xcmp(ref_epoch, oldest) < 0)
            return ref_epoch;
        else
            return oldest;     
}

static inline epoch_t update_effective_epochs(smrproxy_t *proxy, epoch_t effective)
{
    epoch_t current_epoch = *proxy->epoch;
    epoch_t oldest = current_epoch;     // should be same as current epoch / tail

    for (smrproxy_ref_ex_t *ref_ex = proxy->refs; ref_ex != NULL; ref_ex = ref_ex->next) {
        atomic_store_explicit(&ref_ex->ref.current_epoch, current_epoch, memory_order_relaxed);
        epoch_t ref_epoch = atomic_load_explicit(&ref_ex->ref.epoch, memory_order_relaxed);
        if (ref_epoch == 0)
            ref_ex->ref.effective_epoch = effective;    // ? will always be >= previous value
        else if (xcmp(ref_epoch,ref_ex->ref.effective_epoch) > 0)
            ref_ex->ref.effective_epoch = ref_epoch;

        epoch_t effective_epoch = ref_ex->ref.effective_epoch;

        if (xcmp(effective_epoch, proxy->head) < 0)
            continue;
        else if (xcmp(effective_epoch, oldest) < 0)
            oldest = effective_epoch;        

    }

    return oldest;
}


/**
 * Scan registered refs (hazard pointers) for oldest referenced epoch
 * Dequeue and deallocate any entries older than that.
 * 
 * mutext must be held.
 * 
 * @param proxy
 * @returns queue head epoch
 * 
*/
static epoch_t smrproxy_poll(smrproxy_t *proxy) {
    epoch_t epoch = *proxy->epoch;
    if (epoch != proxy->sync_epoch)
    {
        // update_effective_epochs(proxy, proxy->sync_epoch);          // premature optization

        proxy->sync_epoch = epoch;
        smrproxy_membar_sync(proxy->membar);
        /*
        * sync after other thread memory barriers
        * after call to smrproxy_membar_sync.
        */
        atomic_thread_fence(memory_order_seq_cst);
    }

    if (smrqueue_empty(proxy->queue))
        return epoch;


    epoch_t oldest = update_effective_epochs(proxy, proxy->sync_epoch);;     // should be same as current epoch / tail

    proxy->head = smr_dequeue(proxy->queue, oldest); // ?
    return proxy->head;
}

#define NANOS 1000000000
static inline int poll_wait(smrproxy_t *proxy)
{
    unsigned long wait =  proxy->config.polltime * 1000000;  // milliseconds to nanoseconds

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    ts.tv_sec += wait / NANOS;
    ts.tv_nsec += wait % NANOS;
    if (ts.tv_nsec > NANOS)
    {
        ts.tv_sec++;
        ts.tv_nsec -= NANOS;
    }

    return cnd_timedwait(&proxy->cvar, &proxy->mutex, &ts);
}

static epoch_t smrproxy_poll2(smrproxy_t *proxy, epoch_t epoch)
{
    for (;;) {  // TODO test for shutdown
        epoch_t oldest = smrproxy_poll(proxy); // xxxx = oldest or head
        if (epoch != 0 && xcmp(oldest, epoch) >= 0)
            return oldest;

        if (smrqueue_empty(proxy->queue))
            cnd_wait(&proxy->cvar, &proxy->mutex);
        else
            poll_wait(proxy);

        if (proxy->active == false)
            return oldest;
    }

}

static int *smrproxy_poll3(void *arg)
{
    smrproxy_t *proxy = arg;
    mtx_lock(&proxy->mutex);
    smrproxy_poll2(proxy, 0);
    mtx_unlock(&proxy->mutex);
    return 0;
}

epoch_t smrproxy_retire_exp(smrproxy_t *proxy, void *data, void (*dtor)(void *), void (*setexpiry)(epoch_t expiry, void *data, void *ctx), void *ctx)
{
    mtx_lock(&proxy->mutex);

    if (smrqueue_full(proxy->queue))
    {
        mtx_unlock(&proxy->mutex);
        return 0;
    }

    if (setexpiry != NULL)
    {
        epoch_t expiry = *(proxy->epoch);
        (*setexpiry)(expiry, data, ctx);
        // store/store membar below from proxy->epoch update
    }

    epoch_t epoch = smr_enqueue(proxy->queue, data, dtor);
    atomic_store_explicit(proxy->epoch, epoch, memory_order_release);

    cnd_broadcast(&proxy->cvar);

    mtx_unlock(&proxy->mutex);
    return epoch;
}


epoch_t smrproxy_retire(smrproxy_t *proxy, void *data, void (*dtor)(void *))
{
    return smrproxy_retire_exp(proxy, data, dtor, NULL, NULL);
}

/**
 * get current epoch
 * @param proxy
 * @returns the current epoch
*/
epoch_t smrproxy_get_epoch(smrproxy_t *proxy)
{
    return atomic_load_explicit(proxy->epoch, memory_order_acquire);
}

/**
 * update proxy reference epoch if node expiry is newer than it.
 * @param ref the proxy reference
 * @param getexpiry get node expiry function
 * @param node data node
 * @param ctx getexpiry context or NULL
 * 
 * @note
 * To ensure monoticity when muliple nodes are being retired,
 * the nodes should be retires as a batch, meaning the dtor and setexpiry
 * need to take a "batch" parameter, or if done with multiple retire calls,
 * ordered in decending order, meaning a node doesn't get retired until its
 * parent nodes in the batch are retired.
 * 
*/
void smrproxy_ref_next(smrproxy_ref_t *ref, epoch_t (*getexpiry)(void *node, void *ctx), void *node, void *ctx)
{
    if (ref->epoch == 0)
    {
        smrproxy_ref_acquire(ref);
        return;
    }

    epoch_t current_epoch = atomic_load_explicit(ref->proxy_epoch, memory_order_acquire);
    // should be a load/load memory barrier here from the above memory_order_acquire
    epoch_t node_expiry = (*getexpiry)(node, ctx);

    if (node_expiry == 0)
        atomic_store_explicit(&ref->epoch, current_epoch, memory_order_relaxed);
    else if (xcmp(node_expiry, ref->epoch) > 0)
        atomic_store_explicit(&ref->epoch, node_expiry, memory_order_relaxed);
    else
        ;
}

/*-*/

