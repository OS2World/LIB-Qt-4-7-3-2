/*------------------------------------------------------------------------------
* Copyright (C) 2003-2006 Ben van Klinken and the CLucene Team
*
* Distributable under the terms of either the Apache License (Version 2.0) or
* the GNU Lesser General Public License, as specified in the COPYING file.
------------------------------------------------------------------------------*/
//NOTE: do not include this file directly, it is included from lucene internally.

#ifndef lucene_config_threadOS2_h
#define lucene_config_threadOS2_h

#if defined(_LUCENE_PRAGMA_ONCE)
# pragma once
#endif

#define	INCL_DOSPROCESS
#define	INCL_DOSSEMAPHORES
#include <os2.h>


CL_NS_DEF(util)
	class mutex_os2
	{
	private:
		HMTX	mtx;
	public:
		mutex_os2(const mutex_os2& clone);
		mutex_os2();
		~mutex_os2();
		void lock();
		void unlock();

		static ULONG getCurrentTid(void);
	};

	class CLuceneThreadIdCompare
	{
	public:

		enum
		{	// parameters for hash table
			bucket_size = 4,	// 0 < bucket_size
			min_buckets = 8
		};	// min_buckets = 2 ^^ N, 0 < N

		bool operator()( ULONG t1, ULONG t2 ) const{
			return t1 < t2;
		}
	};

	#define _LUCENE_SLEEP(x) DosSleep(x)
	#define _LUCENE_THREADMUTEX CL_NS(util)::mutex_os2
	#define _LUCENE_CURRTHREADID CL_NS(util)::mutex_os2::getCurrentTid()
	#define _LUCENE_THREADID_TYPE ULONG
CL_NS_END

#endif //lucene_config_threadCSection_h
