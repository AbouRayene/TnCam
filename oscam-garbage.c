#include "globals.h"
#include "oscam-garbage.h"
#include "oscam-lock.h"
#include "oscam-string.h"
#include "oscam-time.h"
#ifdef MODULE_GBOX
#include "oscam-files.h"
#include "module-gbox.h"
#endif

#define HASH_BUCKETS 16

struct cs_garbage
{
	time_t time;
	void *data;
#ifdef WITH_DEBUG
	char *file;
	uint16_t line;
#endif
	struct cs_garbage *next;
};

static struct cs_garbage *garbage_first[HASH_BUCKETS];
static struct cs_garbage *garbage_last[HASH_BUCKETS];
static CS_MUTEX_LOCK garbage_lock[HASH_BUCKETS];
static pthread_t garbage_thread;
static int32_t garbage_collector_active;
static int32_t garbage_debug;

#ifdef WITH_DEBUG
void add_garbage_debug(void *data, char *file, uint16_t line)
{
#else
void add_garbage(void *data)
{
#endif
	if(!data)
		{ return; }

	if(!garbage_collector_active || garbage_debug == 1)
	{
		NULLFREE(data);
		return;
	}

	int32_t bucket = (uintptr_t)data / 16 % HASH_BUCKETS;
	struct cs_garbage *garbage;
	if(!cs_malloc(&garbage, sizeof(struct cs_garbage)))
	{
		cs_log("*** MEMORY FULL -> FREEING DIRECT MAY LEAD TO INSTABILITY!!!! ***");
		NULLFREE(data);
		return;
	}
	garbage->time = time(NULL);
	garbage->data = data;
#ifdef WITH_DEBUG
	garbage->file = file;
	garbage->line = line;
#endif
	cs_writelock(&garbage_lock[bucket]);

#ifdef WITH_DEBUG
	if(garbage_debug == 2)
	{
		struct cs_garbage *garbagecheck = garbage_first[bucket];
		while(garbagecheck)
		{
			if(garbagecheck->data == data)
			{
				cs_log("Found a try to add garbage twice. Not adding the element to garbage list...");
				cs_log("Current garbage addition: %s, line %d.", file, line);
				cs_log("Original garbage addition: %s, line %d.", garbagecheck->file, garbagecheck->line);
				cs_writeunlock(&garbage_lock[bucket]);
				NULLFREE(garbage);
				return;
			}
			garbagecheck = garbagecheck->next;
		}
	}
#endif

	if(garbage_last[bucket]) { garbage_last[bucket]->next = garbage; }
	else { garbage_first[bucket] = garbage; }
	garbage_last[bucket] = garbage;
	cs_writeunlock(&garbage_lock[bucket]);
}

static pthread_cond_t sleep_cond;
static pthread_mutex_t sleep_cond_mutex;

static void garbage_collector(void)
{
	int8_t i;
	struct cs_garbage *garbage, *next, *prev, *first;
	set_thread_name(__func__);
	while(garbage_collector_active)
	{

		for(i = 0; i < HASH_BUCKETS; ++i)
		{
			cs_writelock(&garbage_lock[i]);
			first = garbage_first[i];
			time_t deltime = time(NULL) - (2*cfg.ctimeout/1000 + 1);
			for(garbage = first, prev = NULL; garbage; prev = garbage, garbage = garbage->next)
			{
				if(deltime < garbage->time)     // all following elements are too new
				{
					if(prev)
					{
						garbage_first[i] = garbage;
						prev->next = NULL;
					}
					break;
				}
			}
			if(!garbage && garbage_first[i])        // end of list reached and everything is to be cleaned
			{
				garbage = first;
				garbage_first[i] = NULL;
				garbage_last[i] = NULL;
			}
			else if(prev) { garbage = first; }          // set back to beginning to cleanup all
			else { garbage = NULL; }        // garbage not old enough yet => nothing to clean
			cs_writeunlock(&garbage_lock[i]);

			// list has been taken out before so we don't need a lock here anymore!
			while(garbage)
			{
				next = garbage->next;
				NULLFREE(garbage->data);
				NULLFREE(garbage);
				garbage = next;
			}
		}
#ifdef MODULE_GBOX 
		if (file_exists(FILE_GSMS_TXT))
		{
		gbox_init_send_gsms();
		}
#endif		
		sleepms_on_cond(&sleep_cond_mutex, &sleep_cond, 1000);
	}
	pthread_exit(NULL);
}

void start_garbage_collector(int32_t debug)
{

	garbage_debug = debug;
	int8_t i;
	for(i = 0; i < HASH_BUCKETS; ++i)
	{
		cs_lock_create(&garbage_lock[i], "garbage_lock", 5000);

		garbage_first[i] = NULL;
	}
	cs_pthread_cond_init(&sleep_cond_mutex, &sleep_cond);

	pthread_attr_t attr;
	pthread_attr_init(&attr);

	garbage_collector_active = 1;

	pthread_attr_setstacksize(&attr, PTHREAD_STACK_SIZE);
	int32_t ret = pthread_create(&garbage_thread, &attr, (void *)&garbage_collector, NULL);
	if(ret)
	{
		cs_log("ERROR: can't create garbagecollector thread (errno=%d %s)", ret, strerror(ret));
		pthread_attr_destroy(&attr);
		cs_exit(1);
	}
	pthread_attr_destroy(&attr);
}

void stop_garbage_collector(void)
{
	if(garbage_collector_active)
	{
		int8_t i;

		garbage_collector_active = 0;
		pthread_cond_signal(&sleep_cond);
		pthread_join(garbage_thread, NULL);
		for(i = 0; i < HASH_BUCKETS; ++i)
			{ cs_writelock(&garbage_lock[i]); }

		for(i = 0; i < HASH_BUCKETS; ++i)
		{
			while(garbage_first[i])
			{
				struct cs_garbage *next = garbage_first[i]->next;
				NULLFREE(garbage_first[i]->data);
				NULLFREE(garbage_first[i]);
				garbage_first[i] = next;
			}
		}
	}
}
