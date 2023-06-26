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
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/membarrier.h>

#define MB_REGISTER MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED
#define MB_CMD MEMBARRIER_CMD_PRIVATE_EXPEDITED

typedef struct smrproxy_membar_t {
	int x;
} smrproxy_membar_t;

static int membarrier(int cmd, unsigned int flags, int cpu_id)
{
	return syscall(__NR_membarrier, cmd, flags, cpu_id);
}

smrproxy_membar_t *smrproxy_membar_create()
{
	membarrier(MB_REGISTER, 0, 0);
	smrproxy_membar_t *mb = malloc(sizeof(smrproxy_membar_t));
	return mb;
}

void smrproxy_membar_destroy(smrproxy_membar_t * membar)
{
	free(membar);
}

void smrproxy_membar_sync(smrproxy_membar_t * membar)
{
	if (membar == NULL)
		return;
	membarrier(MB_CMD, 0, 0);
}



