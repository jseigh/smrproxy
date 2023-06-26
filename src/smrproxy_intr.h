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

#ifndef SMRPROXY_INTR_H
#define SMRPROXY_INTR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <threads.h>

#include "smrproxy.h"

#define MB_REGISTER MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED
#define MB_CMD MEMBARRIER_CMD_PRIVATE_EXPEDITED

inline static long xcmp(epoch_t a, epoch_t b) { return a - b; }
inline static unsigned int e2ndx(epoch_t epoch, unsigned int size) { return (epoch >> 1)%size; }

typedef struct smrqueue_t smrqueue_t;

typedef struct smrproxy_membar_t smrproxy_membar_t;

typedef struct smrproxy_ref_ex_t {
    smrproxy_ref_t ref;
    smrproxy_t *proxy;
    struct smrproxy_ref_ex_t *next;
} smrproxy_ref_ex_t;

/*
* smrproxy
*
* epoch values are odd, monotonic, and may wrap.
*   A hazard pointer epoch value of zero, is a
*   null reference.
*
*/
typedef struct smrproxy_t {
    epoch_t epoch;          // current epoch, a.k.a tail
    epoch_t head;           // oldest

    epoch_t sync_epoch;     // last memorybarrier synced epoch

    mtx_t mutex;
    cnd_t cvar;
    tss_t key;

    thrd_t poll_tid;

    thrd_t *poll_thread;

    smrproxy_membar_t  *membar;
    /*
    * registered hazard pointers
    */
    smrproxy_ref_ex_t *refs;

    smrqueue_t *queue;

    smrproxy_config_t config;

    atomic_bool active;
} smrproxy_t;


/*
* internal
*/

extern smrqueue_t *smrqueue_create(epoch_t epoch, unsigned int size);
extern void smrqueue_destroy(smrqueue_t *queue);
extern bool smrqueue_empty(smrqueue_t *queue);
extern bool smrqueue_full(smrqueue_t *queue);
extern epoch_t smr_enqueue(smrqueue_t *queue, void *obj, void (*dtor)(void *));
extern epoch_t smr_dequeue(smrqueue_t *queue, const epoch_t oldest);

/*
* get cache line size
*/
extern long getcachesize();

/*
 * memorybarrier
*/

extern smrproxy_membar_t *smrproxy_membar_create();
extern void smrproxy_membar_destroy(smrproxy_membar_t * membar);
extern void smrproxy_membar_sync(smrproxy_membar_t * membar);


#ifdef __cplusplus
}
#endif

#endif /* SMRPROXY_INTR_H */