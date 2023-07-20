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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <smrproxy.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <threads.h>
#include <time.h>
#include <sys/resource.h>
#include <string.h>

#include <smrtest.h>
#include <pthread.h>


#define unlikely(x)     __builtin_expect((x),0)

static int testreader_default(void *x);
static int testreader_smr(void *x);
static int testreader_smr_tss(void *x);
static int testreader_smr_noacc(void *x);
static int testreader_smr_nest(void  *x);
static int testreader_smr_nest2(void  *x);
static int testreader_unsafe(void *x);
static int testreader_rcu(void *x);
static int testreader_arc(void *x);
static int testreader_empty(void *x);
static int testwriter(void *x);
static int testreader_rwlock(void *x);
static int testwriter_rwlock(void *x);
static int testwriter_rwlock_wp(void *x);

static testcase_t test_suite[] = {
    {"smr_stats",    &testreader_default,  &testwriter, "smr dependent load w/ stats"},     // default
    {"unsafe_stats", &testreader_unsafe,   &testwriter, "unsafe access w/ stats"},
    
    {"smr",        &testreader_smr,      &testwriter, "smr w/ simple dependent load"},
    {"smr_tss",    &testreader_smr_tss,  &testwriter, "simple dependent loads w/ tss ref acquire"},
    {"smr_noacc",  &testreader_smr_noacc,  &testwriter, "smr w/ no access"},
    {"smr_nest",   &testreader_smr_nest, &testwriter, "nestable smr"},
    {"smr_nest2",  &testreader_smr_nest2, &testwriter, "nestable smr, ver 2"},
    {"rcu",        &testreader_rcu,      &testwriter, "rcu (simulated)"},
    {"arc",        &testreader_arc,      &testwriter, "refcounted (simulated)"},
    {"empty",      &testreader_empty,    &testwriter, "empty loop"},
    {"rwlock",     &testreader_rwlock,   &testwriter_rwlock, "rwlock - reader preference"},
    {"rwlock_wp",  &testreader_rwlock,   &testwriter_rwlock_wp, "rwlock - writer preference"},
    {NULL, NULL, NULL, NULL},
};

static inline void delay()
{
    #pragma GCC unroll 10
    for (int ndx = 0; ndx < 10; ndx++) {
        asm volatile ("nop");
    }
}

static inline long long timespec_nsecs(struct timespec *t)
{
    long long value = (t->tv_sec * 1000000000) + t->tv_nsec;
    return value;
}

static inline long long timeval_nsecs(struct timeval *t)
{
    long long value = (t->tv_sec * 1000000000) + (t->tv_usec * 1000);
    return value;
}

static inline double ll2d(long long value)
{
    return (double) value / 1e9;
}

static long long gettimex(clock_t id)
{
    struct timespec t;
    clock_gettime(id, &t);
    long long value = (t.tv_sec * 1000000000) + t.tv_nsec;
    return value;
}

static long long gettime()
{
    return gettimex(CLOCK_THREAD_CPUTIME_ID);
}

static inline void sleep(int wait_msec)
{
    if (wait_msec <= 0)
    {
        thrd_yield();
    }
    else
    {
        struct timespec wait;
        wait.tv_sec = wait_msec / 1000;
        wait.tv_nsec = (wait_msec % 1000) * 1000000;
        thrd_sleep(&wait, NULL);
    }
}

static inline void try_sleep(int wait_msec, long count, long mod)
{
    if (mod != 0 && !(count%mod == (mod - 1)))
        return;

    //sleep(wait_msec);
    if (wait_msec <= 0)
    {
        thrd_yield();
    }
    else
    {
        struct timespec wait;
        wait.tv_sec = wait_msec / 1000;
        wait.tv_nsec = (wait_msec % 1000) * 1000000;
        thrd_sleep(&wait, NULL);
    }
}

/**
 * writer sleep using cvar
*/
static void sleep2(test_env_t *env, int wait_msec)
{
    struct timespec wait;
    clock_gettime(CLOCK_REALTIME, &wait);
    wait.tv_sec += wait_msec / 1000;
    wait.tv_nsec += (wait_msec % 1000) * 1000000;
    time_t xsec = wait.tv_nsec / 1000000000;
    if (xsec > 0)
    {
        wait.tv_sec += xsec;
        wait.tv_nsec = wait.tv_nsec % 1000000000;
    }
    int rc;
    rc = pthread_mutex_lock(&env->context.mutex);
    if (rc != 0)
        abort();
    rc = pthread_cond_timedwait(&env->context.cvar, &env->context.mutex, &wait);
    rc = pthread_mutex_unlock(&env->context.mutex);
    sched_yield();
}

static void freedata(void *data)
{
    test_data_t *pdata = data;
    pdata->state = STATE_INVALID;
    atomic_fetch_add(pdata->deletes, 1);
    free(pdata);
}

static test_data_t *newtestdata(test_env_t *env)
{
    test_data_t *pdata = malloc(sizeof(test_data_t));
    pdata->state = 0;
    atomic_fetch_add(&env->stats.alloc_data, 1);
    pdata->deletes = &env->stats.delete_data;
    pdata->next = pdata;    // allow linked list simulation
    return pdata;
}

#define test_prolog \
    test_env_t *env = x;\
    pthread_rwlock_t *rwlock = &env->context.rwlock;\
    const unsigned int mod = env->config.mod;\
    test_stats_t stats = test_stats_init;\
    smrproxy_ref_t *ref = smrproxy_ref_create(env->context.proxy);\
    if (env->config.verbose)\
        fprintf(stderr, "ref@ %p\n", ref);\
    ref->data = 0;\
    tss_t key = env->context.key;\
    tss_set(key, ref);\
    long long t0 = gettime();\
    test_data_t **ppdata = &env->context.pdata;\
    long long *arccount = &env->context.arccount;\
    unsigned long count = env->config.count / 10;\
    for (unsigned long ndx = 0; ndx < count; ndx++)\
    {\
        _Pragma("GCC unroll 10")\
        for (int ndx2 = 0; ndx2 < 10; ndx2++)\
        {
// end test_prolog
#define test_epilog \
        }\
    }\
    long long t1 = gettime();\
    smrproxy_ref_destroy(ref);\
    merge_stats(env, &stats, count*10, (t1 - t0));\
    return 0;
// end test_epilog



static inline void dependent_load(test_data_t **ppdata)
{
    test_data_t *pdata = atomic_load_explicit(ppdata, memory_order_consume);
    long state = pdata->state;
    __asm__ __volatile__ ("" : : "r" (state) : "memory");
}

static inline void access_data(test_env_t *env, test_data_t **ppdata, test_stats_t *stats, unsigned long ndx, unsigned long mod)
{
    test_data_t *pdata = atomic_load_explicit(ppdata, memory_order_consume);
    if (unlikely(ndx%mod == (mod - 1))) {
        sleep(env->config.rsleep_ms);
    }

    long state = pdata->state;

    switch (state) {
        case STATE_LIVE: stats->live++; break;
        case STATE_STALE: stats->stale++; break;
        case STATE_INVALID: stats->invalid++; break;
        default: stats->other++; break;
    }
}

static void merge_stats(test_env_t *env, test_stats_t *stats, long count, long long time)
{
    struct rusage usage;

    getrusage(RUSAGE_THREAD, &usage);

    atomic_fetch_add(&env->stats.ru_utime, timeval_nsecs(&(usage.ru_utime)));
    atomic_fetch_add(&env->stats.ru_stime, timeval_nsecs(&(usage.ru_stime)));
    atomic_fetch_add(&env->stats.ru_nvcsw, usage.ru_nvcsw);
    atomic_fetch_add(&env->stats.ru_nivcsw, usage.ru_nivcsw);

    atomic_fetch_add(&env->stats.live, stats->live);
    atomic_fetch_add(&env->stats.stale, stats->stale);
    atomic_fetch_add(&env->stats.invalid, stats->invalid);
    atomic_fetch_add(&env->stats.other, stats->other);

    atomic_fetch_add(&env->stats.read_count, count);
    atomic_fetch_add(&env->stats.read_time, time);
}

static int testwriter(void *x)
{
    test_env_t *env = x;

    smrproxy_t *proxy = env->context.proxy;

    test_data_t *pdata = newtestdata(env);


    long long retire_time = 0;
    int retire_count = 0;

    while(env->context.active)
    {
        sleep2(env, env->config.wsleep_ms);

        if (!env->context.active)
            break;

        pdata->state = STATE_LIVE;
        pdata = atomic_exchange(&env->context.pdata, pdata);
        pdata->state = STATE_STALE;
        test_data_t *pdata2 = newtestdata(env);

        long long t0 = gettimex(CLOCK_MONOTONIC);
        if (env->config.async)
            smrproxy_retire_async(proxy, pdata, &freedata);
        else
            smrproxy_retire_sync(proxy, pdata, &freedata);
        long long t1 = gettimex(CLOCK_MONOTONIC);
        pdata = pdata2;

        retire_time += t1 - t0;
        retire_count++;

    }

    freedata(pdata);

    atomic_fetch_add(&env->stats.retire_time, retire_time);
    atomic_fetch_add(&env->stats.retire_count, retire_count);

    return 0;
}

static int testwriter_rwlock_ex(void *x, bool write_pref)
{
    test_env_t *env = x;
    pthread_rwlock_t *rwlock = &env->context.rwlock;

    pthread_rwlockattr_t lock_attr;
    pthread_rwlockattr_init(&lock_attr);
    if (write_pref)
        pthread_rwlockattr_setkind_np(&lock_attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
    else
        pthread_rwlockattr_setkind_np(&lock_attr, PTHREAD_RWLOCK_PREFER_READER_NP);
    pthread_rwlock_init(rwlock, &lock_attr);

    test_data_t *pdata = newtestdata(env);


    long long retire_time = 0;
    int retire_count = 0;

    while(env->context.active)
    {
        sleep(env->config.wsleep_ms);

        long long t0 = gettimex(CLOCK_MONOTONIC);
        pthread_rwlock_wrlock(rwlock);

        freedata(env->context.pdata);
        env->context.pdata = newtestdata(env);
        env->context.pdata->state = STATE_LIVE;

        pthread_rwlock_unlock(rwlock);
        long long t1 = gettimex(CLOCK_MONOTONIC);

        retire_time += t1 - t0;
        retire_count++;
    }

    freedata(pdata);

    atomic_fetch_add(&env->stats.retire_time, retire_time);
    atomic_fetch_add(&env->stats.retire_count, retire_count);

    return 0;

}

static int testwriter_rwlock(void *x)
{
    return testwriter_rwlock_ex(x, false);
}

static int testwriter_rwlock_wp(void *x)
{
    return testwriter_rwlock_ex(x, true);
}

static int testreader_rwlock(void *x)
{

    test_prolog
    pthread_rwlock_rdlock(rwlock);
    dependent_load(ppdata);
    pthread_rwlock_unlock(rwlock);
    test_epilog
}

static int testreader_smr_noacc(void *x) {
    test_prolog
    smrproxy_ref_acquire(ref);
    smrproxy_ref_release(ref);
    test_epilog
}

static int testreader_default(void * x) {
    test_prolog
    smrproxy_ref_acquire(ref);
    access_data(env, ppdata, &stats, ndx*10 + ndx2, mod);
    smrproxy_ref_release(ref);
    test_epilog
}

static int testreader_smr(void * x) {
    test_prolog
    smrproxy_ref_acquire(ref);
    dependent_load(ppdata);
    smrproxy_ref_release(ref);
    test_epilog
}

static int testreader_smr_nest(void * x) {
    test_prolog
    bool zz = (ref->epoch == 0);
    if (zz)    
        smrproxy_ref_acquire(ref);
    dependent_load(ppdata);
    if (zz)
        smrproxy_ref_release(ref);
    test_epilog
}

static int testreader_smr_nest2(void * x) {
    test_prolog
    bool zz = (ref->epoch == 0);
    if (ref->data++ == 0)
        smrproxy_ref_acquire(ref);
    dependent_load(ppdata);
    if (--ref->data == 0)
        smrproxy_ref_release(ref);
    test_epilog
}

static int testreader_smr_tss(void * x) {
    test_prolog
    ref = tss_get(key);
    if (ref == NULL)
        ref = smrproxy_ref_create(env->context.proxy);
    smrproxy_ref_acquire(ref);
    dependent_load(ppdata);
    smrproxy_ref_release(ref);
    test_epilog
}

static int testreader_unsafe(void * x) {
    test_prolog
    access_data(env, ppdata, &stats, ndx*10 + ndx2, mod);
    test_epilog
}

static int testreader_rcu(void * x) {
    test_prolog
    dependent_load(ppdata);
    test_epilog
}

static int testreader_arc(void *x) {
    test_prolog
    atomic_fetch_add_explicit(arccount, 1, memory_order_acquire);
    dependent_load(ppdata);
    long long val = atomic_fetch_add_explicit(arccount, 1, memory_order_release);
    __asm__ __volatile__ ("" : : "r" (val) : "memory");
    test_epilog
}

static int testreader_empty(void *x) {
    test_prolog
    __asm__ __volatile__ ("" : : "r" (ppdata) : "memory");
    test_epilog
}



static double avg(double nanoseconds, double count, double scale)
{
    return count == 0.0 ? 0.0 : (nanoseconds/count)/scale;
}

static void print_stats(test_env_t  *env)
{
    test_stats_t stats = env->stats;


    fprintf(stderr, "Statistics:\n");
    fprintf(stderr, "  reader thread count = %d\n", env->config.nreaders);
    fprintf(stderr, "  read_count = %ld\n", stats.read_count);
    fprintf(stderr, "  elapsed read_time = %lld nsecs\n", stats.read_time);
    fprintf(stderr, "  avg read_time = %8.4f nsecs\n", avg(stats.read_time, stats.read_count, 1));

    if ((stats.live + stats.stale + stats.invalid + stats.other) != 0)
    {
        fprintf(stderr, "  data state counts:\n", stats.live);
        fprintf(stderr, "    live = %ld\n", stats.live);
        fprintf(stderr, "    stale = %ld\n", stats.stale);
        fprintf(stderr, "    invalid = %ld\n", stats.invalid);
        fprintf(stderr, "    other = %ld\n", stats.other);
    }

    fprintf(stderr, "  retire_count = %ld\n", stats.retire_count);
    fprintf(stderr, "  elapsed retire_time = %lld nsecs\n", stats.retire_time);
    fprintf(stderr, "  avg retire_time = %8.4f usecs\n", avg(stats.retire_time, stats.retire_count, 1e3));

    fprintf(stderr, "  allocs = %ld\n", stats.alloc_data);
    fprintf(stderr, "  deletes = %ld\n", stats.delete_data);

    fprintf(stderr, "  voluntary context switches = %ld\n", stats.ru_nvcsw);
    fprintf(stderr, "  involuntary context switches = %ld\n", stats.ru_nivcsw);
    fprintf(stderr, "  user cpu time = %lld nsecs\n", stats.ru_utime);
    fprintf(stderr, "  system cpu time = %lld nsecs\n", stats.ru_stime);

}


int main(int argc, char **argv) {

    test_env_t env = {};

    if (!getconfig(test_suite, &env.config, argc, argv))
        return -1;

    fprintf(stderr, "test=%s\n", env.config.test->name);

    smrproxy_config_t *config = smrproxy_default_config();

    config->poll = env.config.async;
    config->polltime = env.config.wsleep_ms;
    env.context.proxy = smrproxy_create(config);

    env.context.pdata = newtestdata(&env);
    env.context.pdata->state = STATE_LIVE;
    env.context.active = true;
    tss_create(&(env.context.key), NULL);

    thrd_t writer;
    thrd_create(&writer, env.config.test->wtest, &env);

    thrd_t *readers = malloc(env.config.nreaders * sizeof(thrd_t));
    for (int ndx = 0; ndx < env.config.nreaders; ndx++) {
        int rc = thrd_create(&readers[ndx], env.config.test->rtest, &env);
        if (rc != 0) {
            fprintf(stderr, "error %d creating reader threads.  setting to %d\n", rc, ndx);
            env.config.nreaders = ndx;
            break;
        }
    }

    fprintf(stderr, "readers created\n");

    for (int ndx = 0; ndx < env.config.nreaders; ndx++) {
        thrd_join(readers[ndx], NULL);
    }

    fprintf(stderr, "readers finished\n");

    atomic_store(&env.context.active, false);
    pthread_mutex_lock(&env.context.mutex);
    pthread_cond_broadcast(&env.context.cvar);
    pthread_mutex_unlock(&env.context.mutex);
    thrd_join(writer, NULL);

    smrproxy_destroy(env.context.proxy);
    pthread_rwlock_destroy(&env.context.rwlock);
    pthread_cond_destroy(&env.context.cvar);
    pthread_mutex_destroy(&env.context.mutex);

    freedata(env.context.pdata);
    env.context.pdata = NULL;

    print_stats(&env);

    fprintf(stdout, "done\n");

    return 0;
}

