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

#include <stdatomic.h>
#include <smrproxy_intr.h>

/**
 * get current epoch
 * @param proxy
 * @returns the current epoch
*/
epoch_t smrproxy_get_epoch(smrproxy_t *proxy)
{
    return atomic_load_explicit(proxy->epoch, memory_order_acquire);
}

/**
 * update proxy reference epoch if node expiry is newer than it.
 * @param ref the proxy reference
 * @param getexpiry get node expiry function
 * @param node data node
 * 
*/
void smrproxy_ref_next(smrproxy_ref_t *ref, epoch_t (*getexpiry)(void *), void *node)
{
    if (ref->epoch == 0)
    {
        smrproxy_ref_acquire(ref);
        return;
    }

    epoch_t current_epoch = atomic_load_explicit(ref->proxy_epoch, memory_order_acquire);
    epoch_t node_expiry = (*getexpiry)(node);

    if (node_expiry == 0)
        atomic_store_explicit(&ref->epoch, current_epoch, memory_order_release);
    else if (xcmp(node_expiry, ref->epoch) > 0)
        atomic_store_explicit(&ref->epoch, node_expiry, memory_order_release);
    else
        ;
}
