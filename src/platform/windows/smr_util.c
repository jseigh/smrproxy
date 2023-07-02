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

#include <Windows.h>
#include <stdlib.h>

long getcachesize()
{
	char buf[512] = { 0 };
	DWORD bufSize = 512;
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pInfo = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) buf;
	char* pBuf = NULL;		// malloc'd buffer if not null

	if (!GetLogicalProcessorInformationEx(RelationCache, pInfo, &bufSize))
	{
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			return -1;
		}

		pBuf = malloc(bufSize);
		if (pBuf == NULL)
		{
			return -1;
		}
		pInfo = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) pBuf;

		if (!GetLogicalProcessorInformationEx(RelationCache, pInfo, &bufSize))
		{
			return -1;
		}
	}

	char* pCur = (char *) pInfo;
	char* pEnd = pCur + bufSize;

	WORD size[] = { 0, 0, 0, 0};	// per level (1 - 3) cache line sizes

	for (; pCur < pEnd; pCur += pInfo->Size)
	{
		pInfo = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) pCur;

		if (pInfo->Relationship == RelationCache)
		{
			PCACHE_RELATIONSHIP pCacheInfo = &pInfo->Cache;
			BYTE level = pCacheInfo->Level;
			PROCESSOR_CACHE_TYPE type = pCacheInfo->Type;
			WORD linesize = pCacheInfo->LineSize;

			if (level > 3)
				continue;

			switch (type)
			{
			case CacheUnified:
			case CacheData:
				size[level] = linesize;
				break;
			default:
				break;
			}
		}
	}

	if (pBuf != NULL)
		free(pBuf);

	for (int level = 3; level > 0; level--)
	{
		if (size[level] > 0)
			return size[level];
	}

	return -1;
}
