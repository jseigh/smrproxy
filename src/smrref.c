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

#include <smrproxy.h>
#include <stdlib.h>
#include <string.h>
#include "smrproxy_intr.h"

#include <stdio.h>


smrproxy_ref_ex_t *smrproxy_ref_alloc(const size_t cachesize)
{
    uintptr_t xx = cachesize - 1;
    uintptr_t yy = ~xx;

    if ((xx & yy) != 0)
        return NULL;        // not a power of 2

    size_t sz =  sizeof(smrproxy_ref_ex_t);
    sz += xx;
    //sz &= yy;
    //smrproxy_ref_ex_t *ref_ex = aligned_alloc(cachesize, sz);
    void *base = malloc(sz);
    if (base != NULL) {
        memset(base, 0, sz);

        void *base2 = (void *)(((uintptr_t) base & yy));    // truncate down to cache boundary
        if (base2 < base)
        {
            base2 += cachesize;                 
        }
        smrproxy_ref_ex_t *ref_ex = base2;
        ref_ex->base = base;
        ref_ex->size = sz;      // informational
        return ref_ex;
    }
    else
        return NULL;

}

void smrproxy_ref_dealloc(smrproxy_ref_ex_t *ref_ex)
{
    void *base = ref_ex->base;
    memset(ref_ex, 0, sizeof(smrproxy_ref_t));
    free(base);
}
