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

typedef struct smrproxy_membar_t {
	int x;
} smrproxy_membar_t;

smrproxy_membar_t *smrproxy_membar_create()
{
	smrproxy_membar_t *mb = malloc(sizeof(smrproxy_membar_t));
	return mb;
}

void smrproxy_membar_destroy(smrproxy_membar_t * membar)
{
	free(membar);
}

/**
 * Nop for platforms which do not support a global memory barrier function.
 * Applications using these platforms should define SMRPROXY_MB so that
 * the hazard pointer loads have a required memory barrier.
*/
void smrproxy_membar_sync(smrproxy_membar_t * membar)
{
}



