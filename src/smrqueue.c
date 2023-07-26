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

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
//#include <stdatomic.h>
//#include <threads.h>
#include <smrproxy_intr.h>

#include <stdio.h>

/*
*
* retired data object holder
*/
typedef struct {
    void *obj;                  // data object being retired
    void (*dtor)(void *);       // retirement function, e.g. free, dtor, ...
} node_t;


typedef struct smrqueue_t {
    unsigned int size;
    epoch_t max_epoch;

    unsigned int head_ndx;
    unsigned int tail_ndx;

    epoch_t head;
    epoch_t tail;
    node_t node[];
} smrqueue_t;

smrqueue_t *smrqueue_create(epoch_t epoch, unsigned int size)
{
    if ((epoch & 1) != 1)
        return NULL;            // starting epoch must be odd number

    // TODO  size < EPOCH_MAX / 4

    int sz = sizeof(smrqueue_t)  + (size * sizeof(node_t));
    smrqueue_t *queue = malloc(sz);
    memset(queue, 0, sz);
    queue->size = size;

    queue->head_ndx = 0;
    queue->tail_ndx = 0;

    queue->head = epoch;
    queue->tail = epoch;

    return queue;
}

void smrqueue_destroy(smrqueue_t *queue)
{
    free(queue);
}

bool smrqueue_empty(smrqueue_t *queue)
{
    return queue->tail == queue->head;
}

bool smrqueue_full(smrqueue_t *queue)
{
    return (queue->tail - queue->head) == queue->size;
}


/**
 * enqueue a deferred delete
 * @note queue is not lock-free, proxy mutex must be held
 * 
 * @param queue
 * @param obj
 * @param dtor
 * 
 * @returns new/update epoch or 0 if queue full
*/
epoch_t smr_enqueue(smrqueue_t *queue, void *obj, void (*dtor)(void *))
{
    if (smrqueue_full(queue))
        return 0;

    node_t *node = &queue->node[queue->tail_ndx];

    node->obj = obj;
    node->dtor = dtor;

    queue->tail_ndx = (queue->tail_ndx + 1) % queue->size;
    queue->tail += 2;

    return queue->tail;
}

/**
 * Dequeue and deallocate unreferenced retired entries.
 * 
 * Entries in includsive/exclusive range [head, oldest)
 * are dequeued and deallocated
 * 
 * @note queue is not lock-free, proxy mutex must be held
 * 
 * @param queue
 * @param oldest referenced epoch
 * 
 * @returns new value of queue head
*
*/
epoch_t smr_dequeue(smrqueue_t *queue, const epoch_t oldest)
{
    if (xcmp(oldest, queue->head) <= 0)
        return queue->head;

    for (epoch_t epoch_ndx = queue->head; epoch_ndx != oldest; epoch_ndx += 2)
    {
        node_t  *node = &queue->node[queue->head_ndx];
        (node->dtor)(node->obj);
        node->obj = NULL;
        node->dtor = NULL;
        queue->head_ndx = (queue->head_ndx + 1) % queue->size;
    }
    queue->head = oldest;
    return oldest;
}