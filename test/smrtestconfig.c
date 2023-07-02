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
#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>

#include "smrtest.h"

static testcase_t *find_test(testcase_t *tests, char *opt)
{
    for (int ndx = 0; tests[ndx].name != NULL; ndx++) {
        if (strcmp(opt, tests[ndx].name) == 0)
            return &tests[ndx];
    }
    fprintf(stderr, "Unknown reader type %s\n", opt);
    return tests;
}

bool getconfig(testcase_t *tests, test_config_t *config, int argc, char **argv) {

    static struct option long_options[] = {
        {"async", no_argument, 0, 'a'},
        {"count", required_argument, 0, 'c'},
        {"nreaders", required_argument, 0, 'n'},
        {"mod", required_argument, 0, 'm'},
        {"rsleep", required_argument, 0, 'r'},
        {"wsleep", required_argument, 0, 'w'},
        {"type", required_argument, 0, 't'},
        {"verbose", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

    *config = test_config_init;

    bool verbose = false;
    bool help = false;

    while (1)
    {
        int this_option_optind = optind ? optind : 1;
        int option_index = 0;

        int c = getopt_long(argc, argv, "ac:n:m:r:w:t:vh", long_options, &option_index);
        if (c == -1)
            break;

        switch (c)
        {
            case 'a':
                config->async = true;
                break;
            case 'c':
                config->count = atol(optarg);
                break;
            case 'n':
                config->nreaders = atoi(optarg);
                break;
            case 'm':
                config->mod = atoi(optarg);
                break;
            case 'r':
                config->rsleep_ms = atoi(optarg);
                break;
            case 'w':
                config->wsleep_ms = atoi(optarg);
                break;
            case 't':
                config->test = find_test(tests, optarg);
                break;
            case 'v':
                config->verbose = true;
                verbose = true;
                break;
            case 'h':
                help = true;
                break;
            case '?':
                help = true;
                break;
            default:
                break;
        }

    }


    if (config->mod <= 0)
    {
        config->mod = config->count;
    }

    if (config->test == NULL)
        config->test = tests;


    if (help)
    {
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  -a --async use asynchdronous retirement (default false)\n");
        fprintf(stderr, "  -c --count <arg>  reader access count (default %u)\n", test_config_init.count);
        fprintf(stderr, "  -n --nreaders <arg>  number of reader threads (default %u)\n", test_config_init.nreaders);
        fprintf(stderr, "  -m --mod <arg>  reader sleep on every n'th access (default %u)\n", test_config_init.mod);
        fprintf(stderr, "  -r --rsleep_ms <arg>  reader sleep duration in milliseconds (default %u)\n", test_config_init.rsleep_ms);
        fprintf(stderr, "  -w --wsleep_ms <arg>  writer sleep duration in milliseconds (default %u)\n", test_config_init.wsleep_ms);
        fprintf(stderr, "  -t --type testcase:\n");
        for (int ndx = 0; tests[ndx].name != NULL; ndx++)
        {
            fprintf(stderr, "    %s -- %s\n", tests[ndx].name, tests[ndx].desc);
        }

        fprintf(stderr, "  -v --verbose show config values (default false)\n");
        return false;
    }

    if (verbose)
    {
        fprintf(stderr, "Test configuration:\n");
        fprintf(stderr, "  aync=%s\n", config->async ? "true" : "false");
        fprintf(stderr, "  count=%u\n", config->count);
        fprintf(stderr, "  nreaders=%u\n", config->nreaders);
        fprintf(stderr, "  mod=%u\n", config->mod);
        fprintf(stderr, "  rsleep_ms=%u\n", config->rsleep_ms);
        fprintf(stderr, "  wsleep_ms=%u\n", config->wsleep_ms);
        fprintf(stderr, "  type=%s\n", config->test->name);
#ifndef SMRPROXY_MB
        fprintf(stderr, "  SMRPROXY_MB not defined\n");
#else
        fprintf(stderr, "  SMRPROXY_MB defined\n");
#endif
    }

    return true;
}