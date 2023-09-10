
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <threads.h>
#include <string.h>

#include <pthread.h>

#include <smrproxy.h>

/**
 * Example of using smrproxy_ref_next to implements a multi-headed queue as an event listener.
 * The listener threads use an smrproxy_ref_t as a private queue head.
*/

typedef struct node_t {
    epoch_t expiry;
    struct node_t *next;

    int event_id;
    // ....
} node_t, *pnode_t;

typedef struct {
    smrproxy_t *proxy;

    pnode_t events;

    mtx_t mutex;
    cnd_t cvar;

    pthread_barrier_t barrier;

} queue_t;

static epoch_t getexpiry(void *x, void *ctx)
{
    pnode_t node = (pnode_t) x;
    return node->expiry;
}

static void setexpiry(epoch_t expiry, void *data, void *ctx)
{
    pnode_t node = (pnode_t) data;
    atomic_store_explicit(&node->expiry, expiry, memory_order_relaxed);
}

static void free_node(void *x)
{
    pnode_t node = (pnode_t) x;
    fprintf(stdout, "free event_id=%d expiry=%lu\n", node->event_id, node->expiry);
    free(x);
}

static void sleep(unsigned int millis)
{
    unsigned int secs = millis / 1000;
    millis = millis % 1000;
    struct timespec time = { secs, millis * 1000000};
    thrd_sleep(&time, NULL);
}

static void push(queue_t *queue, const int event_id)
{
    pnode_t node = malloc(sizeof(node_t));
    node->expiry = 0;
    node->next = NULL;
    node->event_id = 0;

    mtx_lock(&queue->mutex);

    pnode_t current = queue->events;
    current->event_id = event_id;

    atomic_store_explicit(&current->next, node, memory_order_release);
    atomic_store_explicit(&queue->events, node, memory_order_release); 

    epoch_t expiry = smrproxy_get_epoch(queue->proxy);
    fprintf(stdout, "retiring event_id=%d expiry=%lu\n", event_id, expiry);

    smrproxy_retire_async_exp(queue->proxy, current, &free_node, &setexpiry, NULL);

    cnd_broadcast(&queue->cvar);
    mtx_unlock(&queue->mutex);

}

/*
 * listener reader thread
*/
static int listen(void *arg)
{

    time_t t = time(NULL);
    srandom((int) t);

    queue_t *queue = (queue_t *) arg;

    thrd_t tid = thrd_current();

    smrproxy_t *proxy = queue->proxy;
    smrproxy_ref_t *ref = smrproxy_ref_create(proxy);

    smrproxy_ref_acquire(ref);

    pnode_t node = atomic_load_explicit(&queue->events, memory_order_acquire);

    pthread_barrier_wait(&queue->barrier);

    for (;;)
    {

        if (atomic_load_explicit(&node->next, memory_order_acquire) == NULL)
        //if (node->next == NULL)
        {
            mtx_lock(&queue->mutex);
            while (node->next == NULL)
            {
                cnd_wait(&queue->cvar, &queue->mutex);
            }
            mtx_unlock(&queue->mutex);
        }

        epoch_t prev_epoch = ref->epoch;
        smrproxy_ref_next(ref, &getexpiry, node, NULL);
        epoch_t next_epoch = ref->epoch;
        fprintf(stdout, "%lu) event_id=%d epoch: prev=%u next=%u\n", tid, node->event_id, prev_epoch, next_epoch);

        int adj = random() % 250;
        sleep(300 + adj);

        if (node->event_id == -1)
            break;

        node = atomic_load_explicit(&node->next, memory_order_acquire);
    }

    smrproxy_ref_release(ref);

    smrproxy_ref_destroy(ref);

    return 0;
}


int main(int argc, char **argv)
{

    smrproxy_config_t *config = smrproxy_default_config();
    config->poll = true;

    queue_t *queue = malloc(sizeof(queue_t));
    memset(queue, 0, sizeof(queue_t));
    queue->proxy = smrproxy_create(config);
    mtx_init(&queue->mutex, mtx_plain);
    cnd_init(&queue->cvar);
    pthread_barrier_init(&queue->barrier, NULL, 3);
    queue->events = malloc(sizeof(node_t));
    memset(queue->events, 0, sizeof(node_t));

    thrd_t reader;
    thrd_t reader2;

    thrd_create(&reader, &listen, queue);
    thrd_create(&reader2, &listen, queue);

    pthread_barrier_wait(&queue->barrier);

    for (int ndx = 1; ndx <= 20; ndx++)
    {
        push(queue, 1000 + ndx);
        sleep(200);
    }
    push(queue, -1);

    thrd_join(reader, NULL);
    thrd_join(reader2, NULL);

    smrproxy_destroy(queue->proxy);
    free_node(queue->events);
    pthread_barrier_destroy(&queue->barrier);
    cnd_destroy(&queue->cvar);
    mtx_destroy(&queue->mutex);
    free(queue);

    return 0;
}