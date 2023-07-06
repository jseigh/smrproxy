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
    false,  // no background thread
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
    //memcpy(&proxy->config, config, sizeof(smrproxy_config_t));
    //proxy->config.queue_size = config->queue_size;
    //proxy->config.polltime = config->polltime;
    //proxy->config.poll = config->poll;
    long cachesize = getcachesize();
    if (cachesize > 0)
        proxy->config.cachesize = cachesize;

    mtx_init(&proxy->mutex, mtx_plain);
    cnd_init(&proxy->cvar);
    tss_create(&proxy->key, (tss_dtor_t) &smrproxy_ref_destroy);

    proxy->membar = smrproxy_membar_create();   // TODO test return value

    proxy->epoch = 1;
    proxy->head = 1;
    proxy->sync_epoch = 1;

    proxy->refs = NULL;

    proxy->queue = smrqueue_create(proxy->epoch, config->queue_size);

    proxy->active = true;
    /*
    * proxy initialized
    */

    if (proxy->config.poll)
    {
        thrd_t *tid = &proxy->poll_tid;
        thrd_create(tid, (thrd_start_t) &smrproxy_poll3, proxy);
        proxy->poll_thread = tid;
    }
    else
    {
        proxy->poll_thread = NULL;
    }

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


    //smr_dequeue(proxy->queue, proxy->epoch);
    smr_dequeue(proxy->queue, proxy->epoch);

    smrqueue_destroy(proxy->queue);
    smrproxy_membar_destroy(proxy->membar);

    tss_delete(proxy->key);
    cnd_destroy(&proxy->cvar);
    mtx_destroy(&proxy->mutex);

    memset(proxy, 0, sizeof(smrproxy_t));
    free(proxy);
}

smrproxy_ref_t * smrproxy_ref_create(smrproxy_t *proxy)
{
    smrproxy_ref_ex_t *ref_ex = tss_get(proxy->key);
    if (ref_ex != NULL)
        return &ref_ex->ref;

    ref_ex = smrproxy_ref_alloc(proxy->config.cachesize);
    if (ref_ex == NULL)
        return  NULL;

    ref_ex->ref.proxy_epoch = &proxy->epoch;
    ref_ex->ref.epoch = 0;
    ref_ex->proxy = proxy;

    mtx_lock(&proxy->mutex);
    ref_ex->next = proxy->refs;
    proxy->refs = ref_ex;
    mtx_unlock(&proxy->mutex);

    tss_set(proxy->key, ref_ex);

    return &ref_ex->ref;
}

void smrproxy_ref_destroy(smrproxy_ref_t *ref)
{
    if (ref == NULL)
        return;

    smrproxy_ref_ex_t *ref_ex = (smrproxy_ref_ex_t *) ref;
    smrproxy_t *proxy = ref_ex->proxy;

    smrproxy_ref_t *ref2 = tss_get(proxy->key);
    if (ref2 != NULL && ref2 == ref)
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

    smrproxy_ref_dealloc((smrproxy_ref_ex_t *) ref);

    mtx_unlock(&proxy->mutex);
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
    epoch_t epoch = proxy->epoch;
    if (epoch != proxy->sync_epoch)
    {
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


    epoch_t oldest = epoch;     // should be same as current epoch / tail
    for (smrproxy_ref_ex_t *ref_ex = proxy->refs; ref_ex != NULL; ref_ex = ref_ex->next) {
        epoch_t ref_epoch = ref_ex->ref.epoch;
        if (ref_epoch == 0)
            continue;
        else if (xcmp(ref_epoch, proxy->head) < 0)
            continue;
        else if (xcmp(ref_epoch, oldest) < 0)
            oldest = ref_epoch;        
    }

    proxy->head = smr_dequeue(proxy->queue, oldest); // ?
    return proxy->head;
}

static inline int poll_wait(smrproxy_t *proxy)
{
    long wait_nanos = proxy->config.polltime * 1000000;
    struct timespec waittime = {0, wait_nanos};
    return cnd_timedwait(&proxy->cvar, &proxy->mutex, &waittime);
}

static epoch_t smrproxy_poll2(smrproxy_t *proxy, epoch_t epoch)
{
    for (;;) {  // TODO test for shutdown
        epoch_t oldest = smrproxy_poll(proxy); // xxxx = oldest or head
        if (epoch != 0 && xcmp(oldest, epoch) >= 0)
            return oldest;

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

long smrproxy_retire_async(smrproxy_t *proxy, void *data, void (*dtor)(void *))
{
    mtx_lock(&proxy->mutex);

    epoch_t epoch = smr_enqueue(proxy->queue, data, dtor);
    atomic_store(&proxy->epoch, epoch);

    cnd_broadcast(&proxy->cvar);

    mtx_unlock(&proxy->mutex);
    return epoch;
}

int smrproxy_retire_sync(smrproxy_t *proxy, void *data, void (*dtor)(void *))
{
    smrproxy_ref_t *ref = tss_get(proxy->key);
    if (ref != NULL && ref->epoch != 0)
        return EDEADLK;

    int rc = mtx_lock(&proxy->mutex);
    if (rc != 0)
        return rc;


    epoch_t epoch = 0;
    while ((epoch = smr_enqueue(proxy->queue, data, dtor)) == 0) {
        smrproxy_poll(proxy);
        if (!smrqueue_full(proxy->queue))
            break;
        else
            poll_wait(proxy);
    }
    atomic_store(&proxy->epoch, epoch);

    smrproxy_poll2(proxy, epoch);

    mtx_unlock(&proxy->mutex);
    return 0;
}

/*-*/

