#include "globals.h"

#ifdef WITH_LB
#include "module-cccam.h"

#define UNDEF_AVG_TIME 80000
#define MAX_ECM_SEND_CACHE 16

#define LB_REOPEN_MODE_STANDARD 0
#define LB_REOPEN_MODE_FAST 1

#define LB_NONE 0
#define LB_FASTEST_READER_FIRST 1
#define LB_OLDEST_READER_FIRST 2
#define LB_LOWEST_USAGELEVEL 3
#define LB_LOG_ONLY 10

#define DEFAULT_LOCK_TIMEOUT 1000

static int32_t stat_load_save;
static struct timeb nulltime;
static time_t last_housekeeping = 0;

void init_stat(void)
{
	cs_ftime(&nulltime);
	stat_load_save = -100;

	//checking config
	if (cfg.lb_nbest_readers < 2)
		cfg.lb_nbest_readers = DEFAULT_NBEST;
	if (cfg.lb_nfb_readers < 2)
		cfg.lb_nfb_readers = DEFAULT_NFB;
	if (cfg.lb_min_ecmcount < 2)
		cfg.lb_min_ecmcount = DEFAULT_MIN_ECM_COUNT;
	if (cfg.lb_max_ecmcount < 3)
		cfg.lb_max_ecmcount = DEFAULT_MAX_ECM_COUNT;
	if (cfg.lb_reopen_seconds < 10)
		cfg.lb_reopen_seconds = DEFAULT_REOPEN_SECONDS;
	if (cfg.lb_retrylimit <= 0)
		cfg.lb_retrylimit = DEFAULT_RETRYLIMIT;
	if (cfg.lb_stat_cleanup <= 0)
		cfg.lb_stat_cleanup = DEFAULT_LB_STAT_CLEANUP;
}

#define LINESIZE 1024
#endif

#ifdef WITH_LB


static uint32_t get_prid(uint16_t caid, uint32_t prid)
{
	int32_t i;
	for (i=0;i<CS_MAXCAIDTAB;i++) {
		uint16_t tcaid = cfg.lb_noproviderforcaid.caid[i];
		if (!tcaid) break;
		if ((tcaid == caid) || (tcaid < 0x0100 && (caid >> 8) == tcaid)) {
			prid = 0;
			break;
		}

	}
	return prid;
}

static uint32_t get_subid(ECM_REQUEST *er)
{
	if (!er->l)
		return 0;

	uint32_t id = 0;
	switch (er->caid>>8)
	{
		case 0x01: id = b2i(2, er->ecm+7); break;
		case 0x06: id = b2i(2, er->ecm+6); break;
		case 0x09: id = b2i(2, er->ecm+11); break;
		case 0x4A: // DRE-Crypt, Bulcrypt, others?
			if (er->caid != 0x4AEE) // Bulcrypt
				id = er->ecm[7];
			break;
	}
	return id;
}


void get_stat_query(ECM_REQUEST *er, STAT_QUERY *q)
{
	memset(q, 0, sizeof(STAT_QUERY));

	q->caid = er->caid;
	q->prid = get_prid(er->caid, er->prid);
	q->srvid = er->srvid;
	q->chid = get_subid(er);
	q->ecmlen = er->l;
}

void load_stat_from_file(void)
{
	stat_load_save = 0;
	char buf[256];
	char *line;
	char *fname;
	FILE *file;
	if (!cfg.lb_savepath || !cfg.lb_savepath[0]) {
		snprintf(buf, sizeof(buf), "%s/stat", get_tmp_dir());
		fname = buf;
	}
	else
		fname = cfg.lb_savepath;
		
	file = fopen(fname, "r");
		
	if (!file) {
		cs_log("loadbalancer: can't read from file %s", fname);
		return;
	}
	setvbuf(file, NULL, _IOFBF, 128*1024);

	cs_debug_mask(D_LB, "loadbalancer: load statistics from %s", fname);

	struct timeb ts, te;
    cs_ftime(&ts);
         	
	struct s_reader *rdr = NULL;
	READER_STAT *stat;
	line = cs_malloc(&line, LINESIZE, 0);
		
	int32_t i=1;
	int32_t valid=0;
	int32_t count=0;
	int32_t type=0;
	char *ptr, *saveptr1 = NULL;
	char *split[12];
	
	while (fgets(line, LINESIZE, file))
	{
		if (!line[0] || line[0] == '#' || line[0] == ';')
			continue;
		
		if(!cs_malloc(&stat,sizeof(READER_STAT), -1)) continue;

		//get type by evaluating first line:
		if (type==0) {
			if (strstr(line, " rc ")) type = 2;
			else type = 1;
		}	
		
		if (type==1) { //New format - faster parsing:
			for (i = 0, ptr = strtok_r(line, ",", &saveptr1); ptr && i<12 ; ptr = strtok_r(NULL, ",", &saveptr1), i++)
				split[i] = ptr;
			valid = (i==11);
			if (valid) {
				strncpy(buf, split[0], sizeof(buf)-1);
				stat->rc = atoi(split[1]);
				stat->caid = a2i(split[2], 4);
				stat->prid = a2i(split[3], 6);
				stat->srvid = a2i(split[4], 4);
				stat->chid = a2i(split[5], 4);
				stat->time_avg = atoi(split[6]);
				stat->ecm_count = atoi(split[7]);
				stat->last_received = atol(split[8]);
				stat->fail_factor = atoi(split[9]);
				stat->ecmlen = a2i(split[10], 2);
			}
		} else { //Old format - keep for compatibility:
			i = sscanf(line, "%255s rc %04d caid %04hX prid %06X srvid %04hX time avg %dms ecms %d last %ld fail %d len %02hX\n",
				buf, &stat->rc, &stat->caid, &stat->prid, &stat->srvid, 
				&stat->time_avg, &stat->ecm_count, &stat->last_received, &stat->fail_factor, &stat->ecmlen);
			valid = i>5;
		}
		
		if (valid && stat->ecmlen > 0) {
			if (rdr == NULL || strcmp(buf, rdr->label) != 0) {
				LL_ITER itr = ll_iter_create(configured_readers);
				while ((rdr=ll_iter_next(&itr))) {
					if (strcmp(rdr->label, buf) == 0) {
						break;
					}
				}
			}
			
			if (rdr != NULL && strcmp(buf, rdr->label) == 0) {
				if (!rdr->lb_stat) {
					rdr->lb_stat = ll_create("lb_stat");
					cs_lock_create(&rdr->lb_stat_lock, DEFAULT_LOCK_TIMEOUT, rdr->label);
				}
					
				ll_append(rdr->lb_stat, stat);
				count++;
			}
			else 
			{
				cs_log("loadbalancer: statistics could not be loaded for %s", buf);
				free(stat);
			}
		}
		else 
		{
			cs_debug_mask(D_LB, "loadbalancer: statistics ERROR: %s rc=%d i=%d", buf, stat->rc, i);
			free(stat);
		}
	} 
	fclose(file);
	free(line);

    cs_ftime(&te);
#ifdef WITH_DEBUG
	int32_t time = 1000*(te.time-ts.time)+te.millitm-ts.millitm;

	cs_debug_mask(D_LB, "loadbalancer: statistics loaded %d records in %dms", count, time);
#endif
}

/**
 * get statistic values for reader ridx and caid/prid/srvid/ecmlen
 **/
READER_STAT *get_stat_lock(struct s_reader *rdr, STAT_QUERY *q, int8_t lock)
{
	if (!rdr->lb_stat) {
		rdr->lb_stat = ll_create("lb_stat");
		cs_lock_create(&rdr->lb_stat_lock, DEFAULT_LOCK_TIMEOUT, rdr->label);
	}

	if (lock) cs_readlock(&rdr->lb_stat_lock);

	LL_ITER it = ll_iter_create(rdr->lb_stat);
	READER_STAT *stat;
	int32_t i = 0;
	while ((stat = ll_iter_next(&it))) {
		i++;
		if (stat->caid==q->caid && stat->prid==q->prid && stat->srvid==q->srvid && stat->chid==q->chid) {
			if (stat->ecmlen == q->ecmlen)
				break;
			if (!stat->ecmlen) {
				stat->ecmlen = q->ecmlen;
				break;
			}
			if (!q->ecmlen) //Query without ecmlen from dvbapi
				break;
		}
	}
	if (lock) cs_readunlock(&rdr->lb_stat_lock);
	
	//Move stat to list start for faster access:
	if (i > 10 && stat) {
		if (lock) cs_writelock(&rdr->lb_stat_lock);
		ll_iter_move_first(&it);
		if (lock) cs_writeunlock(&rdr->lb_stat_lock);
	}
	
	return stat;
}

/**
 * get statistic values for reader ridx and caid/prid/srvid/ecmlen
 **/
READER_STAT *get_stat(struct s_reader *rdr, STAT_QUERY *q)
{
	return get_stat_lock(rdr, q, 1);
}

/**
 * removes caid/prid/srvid/ecmlen from stat-list of reader ridx
 */
int32_t remove_stat(struct s_reader *rdr, uint16_t caid, uint32_t prid, uint16_t srvid, uint16_t chid, int16_t ecmlen)
{
	if (!rdr->lb_stat)
		return 0;

	prid = get_prid(caid, prid);

	cs_writelock(&rdr->lb_stat_lock);
	int32_t c = 0;
	LL_ITER it = ll_iter_create(rdr->lb_stat);
	READER_STAT *stat;
	while ((stat = ll_iter_next(&it))) {
		if (stat->caid==caid && stat->prid==prid && stat->srvid==srvid && stat->chid==chid) {
			if (!stat->ecmlen || stat->ecmlen == ecmlen) {
				ll_iter_remove_data(&it);
				c++;
			}
		}
	}
	cs_writeunlock(&rdr->lb_stat_lock);
	return c;
}

/**
 * Calculates average time
 */
void calc_stat(READER_STAT *stat)
{
	int32_t i, c=0, t = 0;
	for (i = 0; i < LB_MAX_STAT_TIME; i++) {
		if (stat->time_stat[i] > 0) {
			t += (int32_t)stat->time_stat[i];
			c++;
		}
	}
	if (!c)
		stat->time_avg = UNDEF_AVG_TIME;
	else
		stat->time_avg = t / c;
}

/**
 * Saves statistik to /tmp/.oscam/stat.n where n is reader-index
 */
void save_stat_to_file_thread(void)
{
	stat_load_save = 0;
	char buf[256];

	char *fname;
	if (!cfg.lb_savepath || !cfg.lb_savepath[0]) {
		snprintf(buf, sizeof(buf), "%s/stat", get_tmp_dir());
		fname = buf;
	}
	else
		fname = cfg.lb_savepath;
		
	FILE *file = fopen(fname, "w");
	
	if (!file) {
		cs_log("can't write to file %s", fname);
		return;
	}
	
	setvbuf(file, NULL, _IOFBF, 128*1024);

	struct timeb ts, te;
    cs_ftime(&ts);
         
	time_t cleanup_time = time(NULL) - (cfg.lb_stat_cleanup*60*60);
         	
	int32_t count=0;
	struct s_reader *rdr;
	LL_ITER itr = ll_iter_create(configured_readers);
	while ((rdr=ll_iter_next(&itr))) {
		
		if (rdr->lb_stat) {
			cs_readlock(&rdr->lb_stat_lock);
			LL_ITER it = ll_iter_create(rdr->lb_stat);
			READER_STAT *stat;
			while ((stat = ll_iter_next(&it))) {
			
				if (stat->last_received < cleanup_time || !stat->ecmlen) { //cleanup old stats
					ll_iter_remove_data(&it);
					continue;
				}
				
				//Old version, too slow to parse:
				//fprintf(file, "%s rc %d caid %04hX prid %06X srvid %04hX time avg %dms ecms %d last %ld fail %d len %02hX\n",
				//	rdr->label, stat->rc, stat->caid, stat->prid, 
				//	stat->srvid, stat->time_avg, stat->ecm_count, stat->last_received, stat->fail_factor, stat->ecmlen);
				
				//New version:
				fprintf(file, "%s,%d,%04hX,%06X,%04hX,%04hX,%d,%d,%ld,%d,%02hX\n",
					rdr->label, stat->rc, stat->caid, stat->prid,
					stat->srvid, stat->chid, stat->time_avg, stat->ecm_count, stat->last_received, stat->fail_factor, stat->ecmlen);

				count++;
//				if (count % 500 == 0) { //Saving stats is using too much cpu and causes high file load. so we need a break
//					cs_readunlock(&rdr->lb_stat_lock);
//					cs_sleepms(100);
//					cs_readlock(&rdr->lb_stat_lock);
//				}
			}
			cs_readunlock(&rdr->lb_stat_lock);
		}
	}
	
	fclose(file);

    cs_ftime(&te);
	int32_t time = 1000*(te.time-ts.time)+te.millitm-ts.millitm;


	cs_log("loadbalancer: statistic saved %d records to %s in %dms", count, fname, time);
}

void save_stat_to_file(int32_t thread)
{
	stat_load_save = 0;
	if (thread)
		start_thread((void*)&save_stat_to_file_thread, "save lb stats");
	else
		save_stat_to_file_thread();
}

/**
 * fail_factor is multiplied to the reopen_time. This function increases the fail_factor
 **/
void inc_fail(READER_STAT *stat)
{
	if (stat->fail_factor <= 0)
		stat->fail_factor = 1;
	else
		stat->fail_factor *= 2;
}

READER_STAT *get_add_stat(struct s_reader *rdr, STAT_QUERY *q)
{
	if (!rdr->lb_stat) {
		rdr->lb_stat = ll_create("lb_stat");
		cs_lock_create(&rdr->lb_stat_lock, DEFAULT_LOCK_TIMEOUT, rdr->label);
	}

	cs_writelock(&rdr->lb_stat_lock);

	READER_STAT *stat = get_stat_lock(rdr, q, 0);
	if (!stat) {
		if(cs_malloc(&stat,sizeof(READER_STAT), -1)){
			stat->caid = q->caid;
			stat->prid = q->prid;
			stat->srvid = q->srvid;
			stat->chid = q->chid;
			stat->ecmlen = q->ecmlen;
			stat->time_avg = UNDEF_AVG_TIME; //dummy placeholder
			stat->rc = E_NOTFOUND;
			ll_append(rdr->lb_stat, stat);
		}
	}

	if (stat->ecm_count < 0)
		stat->ecm_count=0;

	cs_writeunlock(&rdr->lb_stat_lock);

	return stat;
}

/**
 * Adds caid/prid/srvid/ecmlen to stat-list for reader ridx with time/rc
 */
void add_stat(struct s_reader *rdr, ECM_REQUEST *er, int32_t ecm_time, int32_t rc)
{
	if (!rdr || !er || !cfg.lb_mode ||!er->l || !er->client)
		return;

	struct s_client *cl = rdr->client;
	if (!cl)
		return;
		
	READER_STAT *stat;
	
	//inc ecm_count if found, drop to 0 if not found:
	// rc codes:
	// 0 = found       +
	// 1 = cache1      #
	// 2 = cache2      #
	// 3 = cacheex     #
	// 4 = not found   -
	// 5 = timeout     -2
	// 6 = sleeping    #
	// 7 = fake        #
	// 8 = invalid     #
	// 9 = corrupt     #
	// 10= no card     #
	// 11= expdate     #
	// 12= disabled    #
	// 13= stopped     #
	// 100= unhandled  #
	//        + = adds statistic values
	//        # = ignored because of duplicate values, temporary failures or softblocks
	//        - = causes loadbalancer to block this reader for this caid/prov/sid
	//        -2 = causes loadbalancer to block if happens too often
	
	
	if (rc == E_NOTFOUND && (uint32_t)ecm_time >= cfg.ctimeout) //Map "not found" to "timeout" if ecm_time>client time out
		rc = E_TIMEOUT;

	if ((uint32_t)ecm_time >= 3*cfg.ctimeout) //ignore too old ecms
		return;

	STAT_QUERY q;
	get_stat_query(er, &q);

	time_t ctime = time(NULL);
	
	if (rc == E_FOUND) { //found
		stat = get_add_stat(rdr, &q);
		stat->rc = E_FOUND;
		stat->ecm_count++;
		stat->last_received = ctime;
		stat->fail_factor = 0;
		
		//FASTEST READER:
		stat->time_idx++;
		if (stat->time_idx >= LB_MAX_STAT_TIME)
			stat->time_idx = 0;
		stat->time_stat[stat->time_idx] = ecm_time;
		calc_stat(stat);

		//OLDEST READER now set by get best reader!
		
		
		//USAGELEVEL:
		int32_t ule = rdr->lb_usagelevel_ecmcount;
		if (ule > 0 && ((ule / cfg.lb_min_ecmcount) > 0)) //update every MIN_ECM_COUNT usagelevel:
		{
			time_t t = (ctime-rdr->lb_usagelevel_time);
			rdr->lb_usagelevel = 1000/(t<1?1:t);
			ule = 0;
		}
		if (ule == 0)
			rdr->lb_usagelevel_time = ctime;
		rdr->lb_usagelevel_ecmcount = ule+1;
	}
	else if (rc < E_NOTFOUND ) { //cache1+2+3
		//no increase of statistics here, cachetime is not real time
		stat = get_stat(rdr, &q);
		if (stat != NULL)
			stat->last_received = ctime;
		return;
	}
	else if (rc == E_NOTFOUND||rc == E_INVALID) { //not found / invalid
		//special rcEx codes means temporary problems, so do not block!
		//CCcam card can't decode, 0x28=NOK1, 0x29=NOK2
		//CCcam loop detection = E2_CCCAM_LOOP
		if (er->rcEx >= LB_NONBLOCK_E2_FIRST) {
			stat = get_stat(rdr, &q);
			if (stat != NULL)
				stat->last_received = ctime; //to avoid timeouts
			return;
		}
			
		stat = get_add_stat(rdr, &q);
		if (stat->rc == E_NOTFOUND) { //we have already "not found", so we change the time. In some cases (with services/ident set) the failing reader is selected again:
			if (ecm_time < 100)
				ecm_time = 100;
			stat->time_avg += ecm_time;
		}

		if (stat->ecm_count > cfg.lb_min_ecmcount) //there were many founds? Do not close, give them another chance
			stat->ecm_count = 0;
		else
			stat->rc = rc;

		inc_fail(stat);
		stat->last_received = ctime;
		
		//reduce ecm_count step by step
		if (!cfg.lb_reopen_mode)
			stat->ecm_count /= 10;
	}
	else if (rc == E_TIMEOUT) { //timeout
		stat = get_add_stat(rdr, &q);
		
		//catch suddenly occuring timeouts and block reader:
//		if ((int)(ctime-stat->last_received) < (int)(5*cfg.ctimeout) && 
//				stat->rc == E_FOUND && stat->ecm_count == 0) {
//			stat->rc = E_TIMEOUT;
//				//inc_fail(stat); //do not inc fail factor in this case
//		}
		//reader is longer than 5s connected && not more then 5 pending ecms:
//		else if ((cl->login+(int)(2*cfg.ctimeout/1000)) < ctime && cl->pending < 5 &&  
//				stat->rc == E_FOUND && stat->ecm_count == 0) {
//			stat->rc = E_TIMEOUT;
//			inc_fail(stat);
//		}
//		else 
		if (!stat->ecm_count)
			stat->rc = E_TIMEOUT;
		else if (stat->rc == E_FOUND && ctime > stat->last_received+1) {
			//search for alternate readers. If we have one, block this reader:
			int8_t n = 0;
			struct s_ecm_answer *ea;
			for (ea = er->matching_rdr; ea; ea = ea->next) {
				if (ea->reader != rdr && ea->rc < E_NOTFOUND){
					n = 1;
					break;
				}
			}
			if (n > 0) //We have alternative readers, so we can block this one:
				stat->rc = E_TIMEOUT;
			else { //No other reader found. Inc fail factor and retry lb_min_ecmount times:
				inc_fail(stat);
				if (stat->fail_factor > cfg.lb_min_ecmcount) {
					stat->fail_factor = 0;
					stat->rc = E_TIMEOUT;
				}
			}
		}
				
		stat->last_received = ctime;

		//add timeout to stat:
		if (ecm_time<=0 || ecm_time > (int)cfg.ctimeout)
			ecm_time = cfg.ctimeout;
		stat->time_idx++;
		if (stat->time_idx >= LB_MAX_STAT_TIME)
			stat->time_idx = 0;
		stat->time_stat[stat->time_idx] = ecm_time;
		calc_stat(stat);
	}
	else
	{
#ifdef WITH_DEBUG
		if (rc >= E_FOUND) {
			char buf[ECM_FMT_LEN];
			format_ecm(er, buf, ECM_FMT_LEN);
			cs_debug_mask(D_LB, "loadbalancer: not handled stat for reader %s: rc %d %s time %dms",
				rdr->label, rc, buf, ecm_time);
		}
#endif
		return;
	}
	
	housekeeping_stat(0);
		
#ifdef WITH_DEBUG
	char buf[ECM_FMT_LEN];
	format_ecm(er, buf, ECM_FMT_LEN);
	cs_debug_mask(D_LB, "loadbalancer: adding stat for reader %s: rc %d %s time %dms fail %d",
				rdr->label, rc, buf, ecm_time, stat->fail_factor);
#endif
	
	if (cfg.lb_save) {
		stat_load_save++;
		if (stat_load_save > cfg.lb_save)
			save_stat_to_file(1);	
	}
}

void reset_stat(STAT_QUERY *q)
{
	//cs_debug_mask(D_LB, "loadbalance: resetting ecm count");
	struct s_reader *rdr;
	for (rdr=first_active_reader; rdr ; rdr=rdr->next) {
		if (rdr->lb_stat && rdr->client) {
			READER_STAT *stat = get_stat(rdr, q);
			if (stat) {
				if (stat->ecm_count > 0)
					stat->ecm_count = 1; //not zero, so we know it's decodeable
				stat->rc = E_FOUND;
				stat->fail_factor = 0;
			}
		}
	}
}

int32_t clean_stat_by_rc(struct s_reader *rdr, int8_t rc, int8_t inverse)
{
	int32_t count = 0;
	if (rdr && rdr->lb_stat) {
		cs_writelock(&rdr->lb_stat_lock);
		READER_STAT *stat;
		LL_ITER itr = ll_iter_create(rdr->lb_stat);
		while ((stat = ll_iter_next(&itr))) {
			if ((!inverse && stat->rc == rc) || (inverse && stat->rc != rc)) {
				ll_iter_remove_data(&itr);
				count++;
			}
		}
		cs_writeunlock(&rdr->lb_stat_lock);
	}
	return count;
}

int32_t clean_all_stats_by_rc(int8_t rc, int8_t inverse)
{
	int32_t count = 0;
	LL_ITER itr = ll_iter_create(configured_readers);
	struct s_reader *rdr;
	while ((rdr = ll_iter_next(&itr))) {
		count += clean_stat_by_rc(rdr, rc, inverse);
	}
	save_stat_to_file(0);
	return count;
}

int32_t clean_stat_by_id(struct s_reader *rdr, uint16_t caid, uint32_t prid, uint16_t srvid, uint16_t chid, uint16_t ecmlen)
{
	int32_t count = 0;
	if (rdr && rdr->lb_stat) {

		cs_writelock(&rdr->lb_stat_lock);
		READER_STAT *stat;
		LL_ITER itr = ll_iter_create(rdr->lb_stat);
		while ((stat = ll_iter_next(&itr))) {
			if (stat->caid == caid &&
					stat->prid == prid &&
					stat->srvid == srvid &&
					stat->chid == chid &&
					stat->ecmlen == ecmlen) {
				ll_iter_remove_data(&itr);
				count++;
				break; // because the entry should unique we can left here
			}
		}
		cs_writeunlock(&rdr->lb_stat_lock);
	}
	return count;
}


int32_t has_ident(FTAB *ftab, ECM_REQUEST *er) {

	if (!ftab || !ftab->filts)
		return 0;
		
	int32_t j, k;

    for (j = 0; j < ftab->nfilts; j++) {
		if (ftab->filts[j].caid) {
			if (ftab->filts[j].caid==er->caid) { //caid matches!
				int32_t nprids = ftab->filts[j].nprids;
				if (!nprids) // No Provider ->Ok
               		return 1;

				for (k = 0; k < nprids; k++) {
					uint32_t prid = ftab->filts[j].prids[k];
					if (prid == er->prid) { //Provider matches
						return 1;
					}
				}
			}
		}
	}
    return 0; //No match!
}

#ifdef WITH_DEBUG 
static char *strend(char *c) {
	while (c && *c) c++;
	return c;
}
#endif

static int32_t get_retrylimit(ECM_REQUEST *er) {
		int32_t i;
		for (i = 0; i < cfg.lb_retrylimittab.n; i++) {
				if (cfg.lb_retrylimittab.caid[i] == er->caid || cfg.lb_retrylimittab.caid[i] == er->caid>>8) 
						return cfg.lb_retrylimittab.value[i];
		}
		return cfg.lb_retrylimit;
}

static int32_t get_nbest_readers(ECM_REQUEST *er) {
		int32_t i;
		for (i = 0; i < cfg.lb_nbest_readers_tab.n; i++) {
				if (cfg.lb_nbest_readers_tab.caid[i] == er->caid || cfg.lb_nbest_readers_tab.caid[i] == er->caid>>8)
						return cfg.lb_nbest_readers_tab.value[i];
		}
		return cfg.lb_nbest_readers;
}

static time_t get_reopen_seconds(READER_STAT *stat)
{
		int32_t max = (INT_MAX / cfg.lb_reopen_seconds);
		if (max > 9999) max = 9999;
		if (stat->fail_factor > max)
				stat->fail_factor = max;
		if (!stat->fail_factor)
			return cfg.lb_reopen_seconds;
		return (time_t)stat->fail_factor * (time_t)cfg.lb_reopen_seconds;
}

void convert_to_beta_int(ECM_REQUEST *er, uint16_t caid_to)
{
	unsigned char md5tmp[MD5_DIGEST_LENGTH];
	convert_to_beta(er->client, er, caid_to);
	// update ecmd5 for store ECM in cache
	memcpy(er->ecmd5, MD5(er->ecm+13, er->l-13, md5tmp), CS_ECMSTORESIZE);
#ifdef CS_CACHEEX
	er->csp_hash = csp_ecm_hash(er);
#endif
	er->btun = 2; //marked as auto-betatunnel converted. Also for fixing recursive lock in get_cw
}

/**	
 * Gets best reader for caid/prid/srvid/ecmlen.
 * Best reader is evaluated by lowest avg time but only if ecm_count > cfg.lb_min_ecmcount (5)
 * Also the reader is asked if he is "available"
 * returns ridx when found or -1 when not found
 */
int32_t get_best_reader(ECM_REQUEST *er)
{
	if (!cfg.lb_mode || cfg.lb_mode==LB_LOG_ONLY)
		return 0;

	struct s_reader *rdr;
	struct s_ecm_answer *ea;

#ifdef MODULE_CCCAM
	//preferred card forwarding (CCcam client):
	if (cfg.cc_forward_origin_card && er->origin_card) {
			struct cc_card *card = er->origin_card;
			struct s_ecm_answer *eab = NULL;
			for(ea = er->matching_rdr; ea; ea = ea->next) {
				ea->status &= !(READER_ACTIVE|READER_FALLBACK);
				if (card->origin_reader == ea->reader)
					eab = ea;		
			}
			if (eab) {
				cs_debug_mask(D_LB, "loadbalancer: forward card: forced by card %d to reader %s", card->id, eab->reader->label);
				eab->status |= READER_ACTIVE;
				return 1;
			}
	}
#endif

	STAT_QUERY q;
	get_stat_query(er, &q);

	//auto-betatunnel: The trick is: "let the loadbalancer decide"!
	if (cfg.lb_auto_betatunnel && er->caid >> 8 == 0x18 && er->l) { //nagra
		uint16_t caid_to = get_betatunnel_caid_to(er->caid);
		if (caid_to) {
			int8_t needs_stats_nagra = 1, needs_stats_beta = 1;
			
			//Clone query parameters for beta:
			STAT_QUERY qbeta = q;
			qbeta.caid = caid_to;
			qbeta.prid = 0;
			qbeta.ecmlen = er->ecm[2] + 3 + 10;

			int32_t time_nagra = 0;
			int32_t time_beta = 0;
			int32_t weight;
			int32_t time;
			
			READER_STAT *stat_nagra;
			READER_STAT *stat_beta;
			
			//What is faster? nagra or beta?
			for(ea = er->matching_rdr; ea; ea = ea->next) {
				rdr = ea->reader;
				weight = rdr->lb_weight;
				if (weight <= 0) weight = 1;
				
				stat_nagra = get_stat(rdr, &q);
				
				//Check if betatunnel is allowed on this reader:
				int8_t valid = chk_ctab(caid_to, &rdr->ctab) //Check caid
					&& chk_rfilter2(caid_to, 0, rdr) //Ident
					&& chk_srvid_by_caid_prov_rdr(rdr, caid_to, 0) //Services
					&& (!rdr->caid || rdr->caid==caid_to); //rdr-caid
				if (valid) {
					stat_beta = get_stat(rdr, &qbeta);
				}
				else
					stat_beta = NULL;

				//calculate nagra data:				
				if (stat_nagra && stat_nagra->rc == E_FOUND) {
					time = stat_nagra->time_avg*100/weight;
					if (!time_nagra || time < time_nagra)
						time_nagra = time;
				}
				
				//calculate beta data:
				if (stat_beta && stat_beta->rc == E_FOUND) {
					time = stat_beta->time_avg*100/weight;
					if (!time_beta || time < time_beta)
						time_beta = time;
				}
				
				//Uncomplete reader evaluation, we need more stats!
				if (stat_nagra)
					needs_stats_nagra = 0;
				if (stat_beta)
					needs_stats_beta = 0;
			}
			
			if (cfg.lb_auto_betatunnel_prefer_beta)
				time_beta = time_beta * cfg.lb_auto_betatunnel_prefer_beta/100;
			
			//if we needs stats, we send 2 ecm requests: 18xx and 17xx:
			if (needs_stats_nagra || needs_stats_beta) {
				cs_debug_mask(D_LB, "loadbalancer-betatunnel %04X:%04X (%d/%d) needs more statistics...", er->caid, caid_to, 
				needs_stats_nagra, needs_stats_beta);
				if (needs_stats_beta) {
					//Duplicate Ecms for gettings stats:
//					ECM_REQUEST *converted_er = get_ecmtask();
//					memcpy(converted_er->ecm, er->ecm, er->l);
//					converted_er->l = er->l;
//					converted_er->caid = er->caid;
//					converted_er->srvid = er->srvid;
//					converted_er->chid = er->chid;
//					converted_er->pid = er->pid;
//					converted_er->prid = er->prid;
//					if (er->src_data) { //camd35:
//						int size = 0x34 + 20 + er->l;
//						cs_malloc(&converted_er->src_data, size, 0);
//						memcpy(converted_er->src_data, er->src_data, size);
//					}
//					convert_to_beta_int(converted_er, caid_to);
//					get_cw(converted_er->client, converted_er);

					convert_to_beta_int(er, caid_to);
					get_stat_query(er, &q);
				}
			}
			else if (time_beta && (!time_nagra || time_beta <= time_nagra)) {
				cs_debug_mask(D_LB, "loadbalancer-betatunnel %04X:%04X selected beta: n%dms > b%dms", er->caid, caid_to, time_nagra, time_beta);
				convert_to_beta_int(er, caid_to);
				get_stat_query(er, &q);
			}
			else {
				cs_debug_mask(D_LB, "loadbalancer-betatunnel %04X:%04X selected nagra: n%dms < b%dms", er->caid, caid_to, time_nagra, time_beta);
			}
			// else nagra is faster or no beta, so continue unmodified
		}
	}
 		
	
	struct timeb new_nulltime;
	memset(&new_nulltime, 0, sizeof(new_nulltime));
	time_t current_time = time(NULL);
	int32_t current = -1;
	READER_STAT *stat = NULL;
	int32_t retrylimit = get_retrylimit(er);
	int32_t reader_count = 0;
	int32_t new_stats = 0;	
	int32_t nlocal_readers = 0;
	int32_t nbest_readers = get_nbest_readers(er);
	int32_t nfb_readers = cfg.lb_nfb_readers;
	int32_t nreaders = cfg.lb_max_readers;
	if (!nreaders)
		nreaders = -1;
	else if (nreaders <= cfg.lb_nbest_readers)
		nreaders = cfg.lb_nbest_readers+1;

#ifdef WITH_DEBUG 
	if (cs_dblevel & 0x01) {
		//loadbalancer debug output:
		int32_t nr = 0;
		char buf[512];
		char *rptr = buf;
		*rptr = 0;

		for(ea = er->matching_rdr; ea; ea = ea->next) {
			nr++;

			if (nr>5) continue;

			if (!(ea->status & READER_FALLBACK))
				snprintf(rptr, 32, "%s%s%s ", ea->reader->label, (ea->status&READER_CACHEEX)?"*":"", (ea->status&READER_LOCAL)?"L":"");
			else
				snprintf(rptr, 32, "[%s%s%s] ", ea->reader->label, (ea->status&READER_CACHEEX)?"*":"", (ea->status&READER_LOCAL)?"L":"");
			rptr = strend(rptr);
		}

		if (nr>5)
			snprintf(rptr, 20, "...(%d more)", nr - 5);

		cs_debug_mask(D_LB, "loadbalancer: client %s for %04X&%06X/%04X:%04X/%02hX: n=%d valid readers: %s",
			username(er->client), q.caid, q.prid, q.srvid, q.chid, q.ecmlen, nr, buf);
	}
#endif	

	for(ea = er->matching_rdr; ea; ea = ea->next) {
		ea->status &= ~(READER_ACTIVE|READER_FALLBACK);
		ea->value = 0;
	}

	for(ea = er->matching_rdr; ea && nreaders; ea = ea->next) {
			rdr = ea->reader;
#ifdef CS_CACHEEX
			int8_t cacheex = rdr->cacheex;
			if (cacheex == 1) {
				ea->status |= READER_ACTIVE; //no statistics, this reader is a cacheex reader and so always active
				continue;
			}
#endif
			struct s_client *cl = rdr->client;
			reader_count++;
	
			int32_t weight = rdr->lb_weight <= 0?100:rdr->lb_weight;
				
			stat = get_stat(rdr, &q);
			if (!stat) {
				cs_debug_mask(D_LB, "loadbalancer: starting statistics for reader %s", rdr->label);
				ea->status |= READER_ACTIVE; //no statistics, this reader is active (now) but we need statistics first!
				new_stats = 1;
				nreaders--;
				continue;
			}
			
			if (stat->ecm_count < 0||(stat->ecm_count > cfg.lb_max_ecmcount && stat->time_avg > retrylimit)) {
				cs_debug_mask(D_LB, "loadbalancer: max ecms (%d) reached by reader %s, resetting statistics", cfg.lb_max_ecmcount, rdr->label);
				reset_stat(&q);
				ea->status |= READER_ACTIVE; //max ecm reached, get new statistics
				nreaders--;
				continue;
			}

//			if (nreopen_readers && stat->rc != E_FOUND && stat->last_received+get_reopen_seconds(stat) < current_time) {
//				cs_debug_mask(D_LB, "loadbalancer: reopen reader %s", rdr->label);
//				reset_stat(er->caid, prid, er->srvid, er->chid, er->l);
//				ea->status |= READER_ACTIVE; //max ecm reached, get new statistics
//				nreopen_readers--;
//				continue;
//			}
				
			int32_t hassrvid;
			if(cl)
				hassrvid = has_srvid(cl, er) || has_ident(&rdr->ftab, er);
			else
				hassrvid = 0;
			
			if (stat->rc == E_FOUND && stat->ecm_count < cfg.lb_min_ecmcount) {
				cs_debug_mask(D_LB, "loadbalancer: reader %s needs more statistics", rdr->label);
				ea->status |= READER_ACTIVE; //need more statistics!
				new_stats = 1;
				nreaders--;
				continue;
			}

			//Reader can decode this service (rc==0) and has lb_min_ecmcount ecms:
			if (stat->rc == E_FOUND || hassrvid) {
				if (cfg.preferlocalcards && (ea->status & READER_LOCAL))
					nlocal_readers++; //Prefer local readers!

				switch (cfg.lb_mode) {
					default:
					case LB_NONE:
					case LB_LOG_ONLY:
						//cs_debug_mask(D_LB, "loadbalance disabled");
						ea->status |= READER_ACTIVE;
						if (rdr->fallback)
							ea->status |= READER_FALLBACK;
						continue;
						
					case LB_FASTEST_READER_FIRST:
						current = stat->time_avg * 100 / weight;
						break;
						
					case LB_OLDEST_READER_FIRST:
						if (!rdr->lb_last.time)
							rdr->lb_last = nulltime;
						current = (1000*(rdr->lb_last.time-nulltime.time)+
							rdr->lb_last.millitm-nulltime.millitm);
						if (!new_nulltime.time || (1000*(rdr->lb_last.time-new_nulltime.time)+
							rdr->lb_last.millitm-new_nulltime.millitm) < 0)
							new_nulltime = rdr->lb_last;
						break;
						
					case LB_LOWEST_USAGELEVEL:
						current = rdr->lb_usagelevel * 100 / weight;
						break;
				}
#if defined(WEBIF) || defined(LCDSUPPORT)
				rdr->lbvalue = current;
#endif
				if (rdr->ph.c_available && !rdr->ph.c_available(rdr, AVAIL_CHECK_LOADBALANCE, er)) {
					current=current*2;
				}
				
				if (cl && cl->pending)
					current=current*cl->pending;

				if (stat->rc >= E_NOTFOUND) { //when reader has service this is possible
					current=current*(stat->fail_factor+2); //Mark als slow
				}
					
				if (current < 1)
					current=1;

				ea->value = current;
				ea->time = stat->time_avg;
			}
	}

	if (nlocal_readers > nbest_readers) { //if we have local readers, we prefer them!
		nlocal_readers = nbest_readers;
		nbest_readers = 0;	
	}
	else
		nbest_readers = nbest_readers-nlocal_readers;

	struct s_reader *best_rdr = NULL;
	struct s_reader *best_rdri = NULL;
	int32_t best_time = 0;
	int32_t result_count = 0;

	int32_t n=0;
	while (nreaders) {
		struct s_ecm_answer *best = NULL;

		for(ea = er->matching_rdr; ea; ea = ea->next) {
			if (nlocal_readers && !(ea->status & READER_LOCAL))
				continue;

			if (ea->value && (!best || ea->value < best->value))
				best=ea;
		}
		if (!best)
			break;
	
		n++;
		best_rdri = best->reader;
		if (!best_rdr) {
			best_rdr = best_rdri;
			best_time = best->time;
		}
		best->value = 0;
			
		if (nlocal_readers) {//primary readers, local
			nlocal_readers--;
			nreaders--;
			best->status |= READER_ACTIVE;
			//OLDEST_READER:
			cs_ftime(&best_rdri->lb_last);
		}
		else if (nbest_readers) {//primary readers, other
			nbest_readers--;
			nreaders--;
			best->status |= READER_ACTIVE;
			//OLDEST_READER:
			cs_ftime(&best_rdri->lb_last);
		}
		else if (nfb_readers) { //fallbacks:
			nfb_readers--;
			best->status |= (READER_ACTIVE|READER_FALLBACK);
		}
		else
			break;
		result_count++;
	}
	
	if (!new_stats && result_count < reader_count) {
		if (!n) //no best reader found? reopen if we have ecm_count>0
		{
			cs_debug_mask(D_LB, "loadbalancer: NO MATCHING READER FOUND, reopen last valid:");
			for(ea = er->matching_rdr; ea; ea = ea->next) {
				if (!(ea->status&READER_ACTIVE)) {
					rdr = ea->reader;
   	     			stat = get_stat(rdr, &q);
   		     		if (stat && stat->rc != E_FOUND && stat->last_received+get_reopen_seconds(stat) < current_time) {
	   	     			if (!ea->status && nreaders) {
   	     					ea->status |= READER_ACTIVE;
   	     					nreaders--;
   	     					cs_debug_mask(D_LB, "loadbalancer: reopened reader %s", rdr->label);
	   	     			}
	   	     			n++;
   		     		}
				}
			}
			cs_debug_mask(D_LB, "loadbalancer: reopened %d readers", n);
		}

		//algo for reopen other reader only if responsetime>retrylimit:
		int32_t reopen = !best_rdr || (best_time && (best_time > retrylimit));
		if (reopen) {
#ifdef WITH_DEBUG 
			if (best_rdr)
				cs_debug_mask(D_LB, "loadbalancer: reader %s reached retrylimit (%dms), reopening other readers", best_rdr->label, best_time);
			else
				cs_debug_mask(D_LB, "loadbalancer: no best reader found, reopening other readers");
#endif	
			for(ea = er->matching_rdr; ea && nreaders; ea = ea->next) {
				if (!(ea->status&READER_ACTIVE)) {
					rdr = ea->reader;
					stat = get_stat(rdr, &q);

					if (stat && stat->rc != E_FOUND) { //retrylimit reached:
						if (cfg.lb_reopen_mode || stat->last_received+get_reopen_seconds(stat) < current_time) { //Retrying reader every (900/conf) seconds
							stat->last_received = current_time;
							if (!ea->status) {
								ea->status |= READER_ACTIVE;
								nreaders--;
								cs_debug_mask(D_LB, "loadbalancer: retrying reader %s (fail %d)", rdr->label, stat->fail_factor);
							}
						}
					}
				}
			}
		}
	}

	if (new_nulltime.time)
		nulltime = new_nulltime;

#ifdef WITH_DEBUG 
	if (cs_dblevel & 0x01) {
		//loadbalancer debug output:
		int32_t nr = 0;
		char buf[512];
		char *rptr = buf;
		*rptr = 0;

		for(ea = er->matching_rdr; ea; ea = ea->next) {
			if (!(ea->status & READER_ACTIVE))
				continue;

			nr++;

			if (nr>5) continue;

			if (!(ea->status & READER_FALLBACK))
				snprintf(rptr, 32, "%s%s%s ", ea->reader->label, (ea->status&READER_CACHEEX)?"*":"", (ea->status&READER_LOCAL)?"L":"");
			else
				snprintf(rptr, 32, "[%s%s%s] ", ea->reader->label, (ea->status&READER_CACHEEX)?"*":"", (ea->status&READER_LOCAL)?"L":"");
			rptr = strend(rptr);
		}

		if (nr>5)
			snprintf(rptr, 20, "...(%d more)", nr - 5);

		cs_debug_mask(D_LB, "loadbalancer: client %s for %04X&%06X/%04X:%04X/%02hX: n=%d selected readers: %s",
			username(er->client), q.caid, q.prid, q.srvid, q.chid, q.ecmlen, nr, buf);
	}
#endif
	return 1;
}

/**
 * clears statistic of reader ridx.
 **/
void clear_reader_stat(struct s_reader *rdr)
{
	if (!rdr->lb_stat) 
		return;

	ll_clear_data(rdr->lb_stat);
}

void clear_all_stat(void)
{
	struct s_reader *rdr;
	LL_ITER itr = ll_iter_create(configured_readers);
	while ((rdr = ll_iter_next(&itr))) { 
		clear_reader_stat(rdr);
	}
}

void housekeeping_stat_thread(void)
{	
	time_t cleanup_time = time(NULL) - (cfg.lb_stat_cleanup*60*60);
	int32_t cleaned = 0;
	struct s_reader *rdr;
    LL_ITER itr = ll_iter_create(configured_readers);
    while ((rdr = ll_iter_next(&itr))) {
		if (rdr->lb_stat) {
			cs_writelock(&rdr->lb_stat_lock);
			LL_ITER it = ll_iter_create(rdr->lb_stat);
			READER_STAT *stat;
			while ((stat=ll_iter_next(&it))) {
				
				if (stat->last_received < cleanup_time) {
					ll_iter_remove_data(&it);
					cleaned++;
				}
			}
			cs_writeunlock(&rdr->lb_stat_lock);
		}
	}
	cs_debug_mask(D_LB, "loadbalancer cleanup: removed %d entries", cleaned);
}

void housekeeping_stat(int32_t force)
{
	time_t now = time(NULL);
	if (!force && last_housekeeping + 60*60 > now) //only clean once in an hour
		return;
	
	last_housekeeping = now;
	start_thread((void*)&housekeeping_stat_thread, "housekeeping lb stats");
}

static int compare_stat(READER_STAT **ps1, READER_STAT **ps2) {
	READER_STAT *s1 = (*ps1), *s2 = (*ps2);
	int res = s1->rc - s2->rc;
	if (res) return res;
	res = s1->caid - s2->caid;
	if (res) return res;
	res = s1->prid - s2->prid;
	if (res) return res;
	res = s1->srvid - s2->srvid;
	if (res) return res;
	res = s1->chid - s2->chid;
	if (res) return res;
	res = s1->ecmlen - s2->ecmlen;
	if (res) return res;
	res = s1->last_received - s2->last_received;
	return res;	
}

static int compare_stat_r(READER_STAT **ps1, READER_STAT **ps2) {
	return -compare_stat(ps1, ps2);
}

READER_STAT **get_sorted_stat_copy(struct s_reader *rdr, int32_t reverse, int32_t *size)
{
	if (reverse)
		return (READER_STAT **)ll_sort(rdr->lb_stat, compare_stat_r, size);
	else
		return (READER_STAT **)ll_sort(rdr->lb_stat, compare_stat, size);
}

int8_t stat_in_ecmlen(struct s_reader *rdr, READER_STAT *stat)
{
	struct s_ecmWhitelist *tmp;
	struct s_ecmWhitelistIdent *tmpIdent;
	struct s_ecmWhitelistLen *tmpLen;
	for (tmp = rdr->ecmWhitelist; tmp; tmp = tmp->next) {
		if (tmp->caid == 0 || (tmp->caid == stat->caid)) {
			for (tmpIdent = tmp->idents; tmpIdent; tmpIdent = tmpIdent->next) {
				if (tmpIdent->ident == 0 || tmpIdent->ident == stat->prid) {
					for (tmpLen = tmpIdent->lengths; tmpLen; tmpLen = tmpLen->next) {
						if (tmpLen->len == stat->ecmlen) {
							return 1;
						}
					}
				}
			}
		}
	}
	return 0;
}

int8_t add_to_ecmlen(struct s_reader *rdr, READER_STAT *stat)
{
	struct s_ecmWhitelist *tmp;
	struct s_ecmWhitelistIdent *tmpIdent;
	struct s_ecmWhitelistLen *tmpLen;

	for (tmp = rdr->ecmWhitelist; tmp; tmp = tmp->next) {
		if (tmp->caid == stat->caid) {
			for (tmpIdent = tmp->idents; tmpIdent; tmpIdent = tmpIdent->next) {
				if (tmpIdent->ident == stat->prid) {
					for (tmpLen = tmpIdent->lengths; tmpLen; tmpLen = tmpLen->next) {
						if (tmpLen->len == stat->ecmlen) {
							return 1;
						}
					}
					break;
				}
			}
			break;
		}
	}

	if (!tmp) {
		tmp = cs_malloc(&tmp, sizeof(struct s_ecmWhitelist), 0);
		tmp->caid = stat->caid;
		tmp->next = rdr->ecmWhitelist;
		rdr->ecmWhitelist = tmp;
	}

	if (!tmpIdent) {
		tmpIdent = cs_malloc(&tmpIdent, sizeof(struct s_ecmWhitelistIdent), 0);
		tmpIdent->ident = stat->prid;
		tmpIdent->next = tmp->idents;
		tmp->idents = tmpIdent;
	}

	if (!tmpLen) {
		tmpLen = cs_malloc(&tmpLen, sizeof(struct s_ecmWhitelistLen), 0);
		tmpLen->len = stat->ecmlen;
		tmpLen->next = tmpIdent->lengths;
		tmpIdent->lengths =  tmpLen;
	}

	return 0;
}

void update_ecmlen_from_stat(struct s_reader *rdr)
{
	if (!rdr || &rdr->lb_stat)
		return;

	cs_readlock(&rdr->lb_stat_lock);
	LL_ITER it = ll_iter_create(rdr->lb_stat);
	READER_STAT *stat;
	while ((stat = ll_iter_next(&it))) {
		if (stat->rc ==E_FOUND) {
			if (!stat_in_ecmlen(rdr, stat))
				add_to_ecmlen(rdr, stat);
		}
	}
	cs_readunlock(&rdr->lb_stat_lock);
}

int32_t lb_valid_btun(ECM_REQUEST *er, uint16_t caidto)
{
	STAT_QUERY q;
	READER_STAT *stat;
	struct s_reader *rdr;

	get_stat_query(er, &q);
	q.caid = caidto;

	for (rdr=first_active_reader; rdr ; rdr=rdr->next) {
		if (rdr->lb_stat && rdr->client) {
			stat = get_stat(rdr, &q);
			if (stat && stat->rc == E_FOUND)
				return 1;
		}
	}
	return 0;
}

#endif
