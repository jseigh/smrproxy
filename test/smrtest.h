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

#ifndef SMRTEST_H
#define SMRTEST_H

#include <threads.h>
#include <pthread.h>
#include <smrproxy.h>

#define STATE_LIVE    0xfeefeffe01234567
#define STATE_STALE   0xfeefeffe89abcdef
#define STATE_INVALID 0xfeefeffe76543210


typedef struct test_data_t {
    long state;
    long *deletes;   // &delete_data  --  atomic count of frees
    struct test_data_t *next;
} test_data_t;

typedef struct {
    char *name;
    thrd_start_t rtest; 
    thrd_start_t wtest;
    char *desc;
} testcase_t;

typedef struct {
    bool async;                 // use asynchronous retirement
    unsigned int nreaders;      // number of reader threads
    unsigned long count;        // reader loop count
    unsigned int mod;           // reader sleep every mod interations
    unsigned int rsleep_ms;     // reader sleep in milliseconds, 0 = thrd_yield

    unsigned int wsleep_ms;     // writer update interval

    testcase_t *test;           // testcase

    bool verbose;
} test_config_t;
static const test_config_t test_config_init = {false, 10, 10000, 0, 0, 50, NULL, false};

typedef struct {
    long long arccount;
    tss_t key;
    smrproxy_t *proxy;
    test_data_t *pdata;             // shared data

    atomic_bool active;

    pthread_rwlock_t rwlock;
    pthread_mutex_t mutex;
    pthread_cond_t cvar;
} test_context_t;
static const test_context_t test_context_init = {
        0, 0, NULL, NULL,
        true,
        PTHREAD_RWLOCK_INITIALIZER,
        PTHREAD_MUTEX_INITIALIZER,
        PTHREAD_COND_INITIALIZER,
    };

typedef struct {
        long live;
        long stale;
        long invalid;
        long other;

        long alloc_data;
        long delete_data;

        long retire_count;       // sum of smrproxy_retire_sync invocations
        long long retire_time;     // sum of smrproxy_retire_sync times

        long read_count;         // sum of all reader counts
        long long read_time;       // sum of all reader runtimes

        // rusage stats
        long ru_nvcsw;          // voluntary context switches
        long ru_nivcsw;         // involuntary context switches
        long long ru_utime;     // user cpu time
        long long ru_stime;     // system cpu time
} test_stats_t;
static const test_stats_t test_stats_init = {0,0,0,0,0,0,0,0, 0, 0, 0, 0, 0, 0};

typedef struct {
    test_config_t config;
    test_context_t context;
    test_stats_t stats;
} test_env_t;
static const test_env_t test_env_init = {test_config_init, test_context_init, test_stats_init};

extern bool getconfig(testcase_t *tests, test_config_t *config, int argc, char **argv);

#endif // SMRTEST_H