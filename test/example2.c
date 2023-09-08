
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <threads.h>
#include <string.h>

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

} queue_t;

static epoch_t getexpiry(void *x)
{
    pnode_t node = (pnode_t) x;
    return node->expiry;
}

static void free_node(void *x)
{
    pnode_t node = (pnode_t) x;
    fprintf(stdout, "free event_id=%d\n", node->event_id);
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

    epoch_t ref_epoch = smrproxy_get_epoch(queue->proxy);
    atomic_store_explicit(&current->expiry, ref_epoch, memory_order_release);

    current->event_id = event_id;
    atomic_store_explicit(&current->next, node, memory_order_release);
    // make node unreachable
    atomic_store_explicit(&queue->events, node, memory_order_release); 

    fprintf(stdout, "retiring event_id=%d expiry=%lu\n", event_id, ref_epoch);
    smrproxy_retire_async(queue->proxy, current, &free_node);

    cnd_broadcast(&queue->cvar);
    mtx_unlock(&queue->mutex);

}

/*
 * listener reader thread
*/
static int listen(void *arg)
{
    queue_t *queue = (queue_t *) arg;

    thrd_t tid = thrd_current();

    smrproxy_t *proxy = queue->proxy;
    smrproxy_ref_t *ref = smrproxy_ref_create(proxy);

    smrproxy_ref_acquire(ref);

    pnode_t node = atomic_load_explicit(&queue->events, memory_order_acquire);

    for (;;)
    {

        if (node->next == NULL)
        {
            mtx_lock(&queue->mutex);
            while (node->next == NULL)
            {
                cnd_wait(&queue->cvar, &queue->mutex);
            }
            mtx_unlock(&queue->mutex);
        }

        epoch_t prev_epoch = ref->epoch;
        smrproxy_ref_next(ref, &getexpiry, node);
        epoch_t next_epoch = ref->epoch;
        fprintf(stdout, "%lu) event_id=%d epoch: prev=%u next=%u\n", tid, node->event_id, prev_epoch, next_epoch);

        sleep(500);

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
    queue->events = malloc(sizeof(node_t));
    memset(queue->events, 0, sizeof(node_t));

    thrd_t reader;
    thrd_t reader2;

    thrd_create(&reader, &listen, queue);
    thrd_create(&reader2, &listen, queue);

    sleep(500);  // give reader threads chance to start


    for (int ndx = 1; ndx <= 20; ndx++)
    {
        push(queue, 1000 + ndx);
        sleep(300);
    }
    push(queue, -1);

    thrd_join(reader, NULL);
    thrd_join(reader2, NULL);

    smrproxy_destroy(queue->proxy);
    free_node(queue->events);
    cnd_destroy(&queue->cvar);
    mtx_destroy(&queue->mutex);
    free(queue);

    return 0;
}