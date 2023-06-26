/*
* inline expansions
*/

#include "../include/smrproxy.h"


void smracquire(smrproxy_ref_t *ref)
{
    smrproxy_ref_acquire(ref);
}

void smrrelease(smrproxy_ref_t *ref)
{
    smrproxy_ref_release(ref);
}