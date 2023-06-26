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

#include <unistd.h>

static int names[] = {
    _SC_LEVEL3_CACHE_LINESIZE,
    _SC_LEVEL2_CACHE_LINESIZE,
    _SC_LEVEL1_DCACHE_LINESIZE
};
#define NAMES_SZ 3

long getcachesize() {
    for (int ndx = 0; ndx < NAMES_SZ; ndx++) {
        long val = sysconf(names[ndx]);
        if (val != -1)
            return val;
    }
    return 64;
}