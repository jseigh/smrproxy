/*
* SMRProxoy usage example
*/
#include <stdlib.h>
#include <stdio.h>
#include <smrproxy.h>
#include <stdatomic.h>
#include <threads.h>
#include <string.h>


#define LIVE    "live"
#define STALE   "stale"
#define INVALID "invalid"

typedef struct {
    smrproxy_t *proxy;
    char *pdata;
} env_t;


static int reader(env_t *env)
{
    smrproxy_ref_t *ref = smrproxy_ref_create(env->proxy);      // once per thread

    for (int ndx = 0; ndx < 4; ndx++)
    {
        smrproxy_ref_acquire(ref); // prior to every read of data

        char *pdata = atomic_load_explicit(&env->pdata, memory_order_acquire);

        fprintf(stdout, "%d) data=%s (before yield)\n", ndx, pdata);
        thrd_yield();
        fprintf(stdout, "%d) data=%s (after yield)\n", ndx, pdata);

        smrproxy_ref_release(ref); // after every read of data
    }

    smrproxy_ref_destroy(ref);  // once per thread
    return 0;
}

static void freedata(void *data) {
    int *pdata = data;
    strcpy(data, INVALID);
    thrd_yield();
    free(data);
}

static int writer(env_t *env)
{
    char *pdata = malloc(16);
    strcpy(pdata, LIVE);
    pdata = atomic_exchange(&env->pdata, pdata);
    strcpy(pdata, STALE);   // indicate old data is now stale

    // smrproxy_retire_sync(env->proxy, pdata, &freedata);
    smrproxy_retire(env->proxy, pdata, &freedata);
    return 0;
}

int main(int argc, char **argv)
{
    env_t env;

    env.proxy = smrproxy_create(NULL);      // default config
    env.pdata = malloc(16);
    strcpy(env.pdata, LIVE);

    thrd_t reader_tid;
    thrd_create(&reader_tid, (thrd_start_t) &reader, &env);
    thrd_yield();

    thrd_t writer_tid;
    thrd_create(&writer_tid, (thrd_start_t) &writer, &env);

    thrd_join(reader_tid, NULL);
    thrd_join(writer_tid, NULL);


    smrproxy_destroy(env.proxy);
    return 0;
}