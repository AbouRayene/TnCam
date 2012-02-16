  //FIXME Not checked on threadsafety yet; after checking please remove this line
#define CS_CORE
#include "globals.h"
#include "csctapi/icc_async.h"
#ifdef MODULE_CCCAM
#include "module-cccam.h"
#endif
#if defined(AZBOX) && defined(HAVE_DVBAPI)
#include "openxcas/openxcas_api.h"
#endif
#define CS_VERSION_X  CS_VERSION
#ifdef COOL
void coolapi_close_all();
void coolapi_open_all();
#endif

static void cs_fake_client(struct s_client *client, char *usr, int32_t uniq, in_addr_t ip);
static void chk_dcw(struct s_client *cl, struct s_ecm_answer *ea);

/*****************************************************************************
        Globals
*****************************************************************************/
int32_t exit_oscam=0;
struct s_module 	ph[CS_MAX_MOD]; // Protocols
struct s_cardsystem	cardsystem[CS_MAX_MOD];
struct s_cardreader	cardreader[CS_MAX_MOD];

struct s_client * first_client = NULL; //Pointer to clients list, first client is master
struct s_reader * first_active_reader = NULL; //list of active readers (enable=1 deleted = 0)
LLIST * configured_readers = NULL; //list of all (configured) readers

uint16_t  len4caid[256];    // table for guessing caid (by len)
char  cs_confdir[128]=CS_CONFDIR;
int32_t cs_dblevel=0;   // Debug Level
int32_t thread_pipe[2] = {0, 0};
#ifdef WEBIF
int8_t cs_restart_mode=1; //Restartmode: 0=off, no restart fork, 1=(default)restart fork, restart by webif, 2=like=1, but also restart on segfaults
uint8_t cs_http_use_utf8 = 0;
#endif
int8_t cs_capture_SEGV=0;
int8_t cs_dump_stack=0;
uint16_t cs_waittime = 60;
char  cs_tmpdir[200]={0x00};
pid_t server_pid=0;
#if defined(LIBUSB)
CS_MUTEX_LOCK sr_lock;
#endif
#ifdef ARM
pthread_t	arm_led_thread = 0;
LLIST		*arm_led_actions = NULL;
#endif
CS_MUTEX_LOCK system_lock;
CS_MUTEX_LOCK gethostbyname_lock;
CS_MUTEX_LOCK clientlist_lock;
CS_MUTEX_LOCK readerlist_lock;
CS_MUTEX_LOCK fakeuser_lock;
CS_MUTEX_LOCK ecmcache_lock;
CS_MUTEX_LOCK readdir_lock;
pthread_key_t getclient;

struct s_client *timecheck_client;

//Cache for  ecms, cws and rcs:
struct ecm_request_t	*ecmcwcache = NULL;
uint32_t ecmcwcache_size = 0;

struct  s_config  cfg;

char    *prog_name = NULL;
char    *processUsername = NULL;
#if defined(WEBIF) || defined(MODULE_MONITOR) 
char    *loghist = NULL;     // ptr of log-history
char    *loghistptr = NULL;
#endif

#ifdef CS_CACHEEX
int32_t cs_add_cacheex_stats(struct s_client *cl, uint16_t caid, uint16_t srvid, uint32_t prid, uint8_t direction) {

	if (!cfg.cacheex_enable_stats)
		return -1;

	// create list if doesn't exist
	if (!cl->ll_cacheex_stats)
		cl->ll_cacheex_stats = ll_create("ll_cacheex_stats");

	time_t now = time((time_t*)0);
	LL_ITER itr = ll_iter_create(cl->ll_cacheex_stats);
	S_CACHEEX_STAT_ENTRY *cacheex_stats_entry;

	// check for existing entry
	while ((cacheex_stats_entry = ll_iter_next(&itr))) {
		if (cacheex_stats_entry->cache_srvid == srvid &&
				cacheex_stats_entry->cache_caid == caid &&
				cacheex_stats_entry->cache_prid == prid &&
				cacheex_stats_entry->cache_direction == direction) {
			// we already have this entry - just add count and time
			cacheex_stats_entry->cache_count++;
			cacheex_stats_entry->cache_last = now;
			return cacheex_stats_entry->cache_count;
		}
	}

	// if we land here we have to add a new entry
	if(cs_malloc(&cacheex_stats_entry, sizeof(S_CACHEEX_STAT_ENTRY), -1)){
		cacheex_stats_entry->cache_caid = caid;
		cacheex_stats_entry->cache_srvid = srvid;
		cacheex_stats_entry->cache_prid = prid;
		cacheex_stats_entry->cache_count = 1;
		cacheex_stats_entry->cache_last = now;
		cacheex_stats_entry->cache_direction = direction;
		ll_iter_insert(&itr, cacheex_stats_entry);
		return 1;
	}
	return 0;
}
#endif

int32_t cs_check_v(uint32_t ip, int32_t port, int32_t add, char *info) {
	int32_t result = 0;
	if (cfg.failbantime) {

		if (!cfg.v_list)
			cfg.v_list = ll_create("v_list");

		time_t now = time((time_t*)0);
		LL_ITER itr = ll_iter_create(cfg.v_list);
		V_BAN *v_ban_entry;
		int32_t ftime = cfg.failbantime*60;

		//run over all banned entries to do housekeeping:
		while ((v_ban_entry=ll_iter_next(&itr))) {

			// housekeeping:
			if ((now - v_ban_entry->v_time) >= ftime) { // entry out of time->remove
				free(v_ban_entry->info);
				ll_iter_remove_data(&itr);
				continue;
			}

			if (ip == v_ban_entry->v_ip && port == v_ban_entry->v_port ) {
				result=1;
				if (!info) info = v_ban_entry->info;
				else if (!v_ban_entry->info) {
					int32_t size = strlen(info)+1;
					v_ban_entry->info = cs_malloc(&v_ban_entry->info, size, -1);
					strncpy(v_ban_entry->info, info, size);
				}
				
				if (!add) {
					if (v_ban_entry->v_count >= cfg.failbancount) {
						cs_debug_mask(D_TRACE, "failban: banned ip %s:%d - %ld seconds left%s%s",
								cs_inet_ntoa(v_ban_entry->v_ip), v_ban_entry->v_port,
								ftime - (now - v_ban_entry->v_time), info?", info: ":"", info?info:"");
					} else {
						cs_debug_mask(D_TRACE, "failban: ip %s:%d chance %d of %d%s%s",
								cs_inet_ntoa(v_ban_entry->v_ip), v_ban_entry->v_port,
								v_ban_entry->v_count, cfg.failbancount, info?", info: ":"", info?info:"");

						v_ban_entry->v_count++;
					}
				}
				else {
					cs_debug_mask(D_TRACE, "failban: banned ip %s:%d - already exist in list%s%s",
							cs_inet_ntoa(v_ban_entry->v_ip), v_ban_entry->v_port, info?", info: ":"", info?info:"");
				}
			}
		}
		if (add && !result) {
			if(cs_malloc(&v_ban_entry, sizeof(V_BAN), -1)){
				v_ban_entry->v_time = time((time_t *)0);
				v_ban_entry->v_ip = ip;
				v_ban_entry->v_port = port;
				v_ban_entry->v_count = 1;
				if (info) {
					int32_t size = strlen(info)+1;
					v_ban_entry->info = cs_malloc(&v_ban_entry->info, size, -1);
					strncpy(v_ban_entry->info, info, size);
				}

				ll_iter_insert(&itr, v_ban_entry);

				cs_debug_mask(D_TRACE, "failban: ban ip %s:%d with timestamp %ld%s%s",
						cs_inet_ntoa(v_ban_entry->v_ip), v_ban_entry->v_port, v_ban_entry->v_time, 
						info?", info: ":"", info?info:"");
			}
		}
	}
	return result;
}

int32_t cs_check_violation(uint32_t ip, int32_t port) {
        return cs_check_v(ip, port, 0, NULL);
}
void cs_add_violation_by_ip(uint32_t ip, int32_t port, char *info) {
        cs_check_v(ip, port, 1, info);
}

void cs_add_violation(struct s_client *cl, char *info) {
	 	cs_add_violation_by_ip((uint32_t)cl->ip, ph[cl->ctyp].ptab ? ph[cl->ctyp].ptab->ports[cl->port_idx].s_port : 0, info);
}

#ifdef WEBIF
void cs_add_lastresponsetime(struct s_client *cl, int32_t ltime, time_t timestamp, int32_t rc){

	if(cl->cwlastresptimes_last == CS_ECM_RINGBUFFER_MAX - 1){
		cl->cwlastresptimes_last = 0;
	} else {
		cl->cwlastresptimes_last++;
	}
	cl->cwlastresptimes[cl->cwlastresptimes_last].duration = ltime > 9999 ? 9999 : ltime;
	cl->cwlastresptimes[cl->cwlastresptimes_last].timestamp = timestamp;
	cl->cwlastresptimes[cl->cwlastresptimes_last].rc = rc;
}
#endif

/*****************************************************************************
        Statics
*****************************************************************************/
static const char *logo = "  ___  ____   ___                \n / _ \\/ ___| / __|__ _ _ __ ___  \n| | | \\___ \\| |  / _` | '_ ` _ \\ \n| |_| |___) | |_| (_| | | | | | |\n \\___/|____/ \\___\\__,_|_| |_| |_|\n";

/* Prints usage information and information about the built-in modules. */
static void usage()
{
  fprintf(stderr, "%s\n\n", logo);
  fprintf(stderr, "OSCam cardserver v%s, build #%s (%s) - (w) 2009-2012 Streamboard SVN\n", CS_VERSION_X, CS_SVN_VERSION, CS_OSTYPE);
  fprintf(stderr, "\tsee http://streamboard.gmc.to/oscam/ for more details\n");
  fprintf(stderr, "\tbased on Streamboard mp-cardserver v0.9d - (w) 2004-2007 by dukat\n");
  fprintf(stderr, "\tThis program is distributed under GPL.\n");
  fprintf(stderr, "\tinbuilt add-ons: ");
#ifdef WEBIF
  fprintf(stderr, "webif ");
#endif
#ifdef MODULE_MONITOR
  fprintf(stderr, "monitor ");
#endif
#ifdef WITH_SSL
  fprintf(stderr, "ssl ");
#endif
#ifdef HAVE_DVBAPI
#ifdef WITH_STAPI
  fprintf(stderr, "dvbapi_stapi ");
#else
  fprintf(stderr, "dvbapi ");
#endif
#endif
#ifdef IRDETO_GUESSING
  fprintf(stderr, "irdeto-guessing ");
#endif
#ifdef CS_ANTICASC
  fprintf(stderr, "anticascading ");
#endif
#ifdef WITH_DEBUG
  fprintf(stderr, "debug ");
#endif
#ifdef LIBUSB
  fprintf(stderr, "smartreader ");
#endif
#ifdef HAVE_PCSC
  fprintf(stderr, "pcsc ");
#endif
#ifdef WITH_LB
  fprintf(stderr, "loadbalancing ");
#endif
#ifdef LCDSUPPORT
  fprintf(stderr, "lcd ");
#endif
  fprintf(stderr, "\n\tinbuilt protocols: ");
#ifdef MODULE_CAMD33
  fprintf(stderr, "camd33 ");
#endif
#ifdef MODULE_CAMD35
  fprintf(stderr, "camd35_udp ");
#endif
#ifdef MODULE_CAMD35_TCP
  fprintf(stderr, "camd35_tcp ");
#endif
#ifdef MODULE_NEWCAMD
  fprintf(stderr, "newcamd ");
#endif
#ifdef MODULE_CCCAM
  fprintf(stderr, "cccam ");
#endif
#ifdef MODULE_CCCSHARE
  fprintf(stderr, "cccam share ");
#endif
#ifdef MODULE_PANDORA
  fprintf(stderr, "pandora ");
#endif
#ifdef CS_CACHEEX
  fprintf(stderr, "cache-exchange ");
#endif
#ifdef MODULE_GBOX
  fprintf(stderr, "gbox ");
#endif
#ifdef MODULE_RADEGAST
  fprintf(stderr, "radegast ");
#endif
#ifdef MODULE_SERIAL
  fprintf(stderr, "serial ");
#endif
#ifdef MODULE_CONSTCW
  fprintf(stderr, "constcw ");
#endif
  fprintf(stderr, "\n\tinbuilt cardreaders: ");
#ifdef READER_NAGRA
  fprintf(stderr, "nagra ");
#endif
#ifdef READER_IRDETO
  fprintf(stderr, "irdeto ");
#endif
#ifdef READER_CONAX
  fprintf(stderr, "conax ");
#endif
#ifdef READER_CRYPTOWORKS
  fprintf(stderr, "cryptoworks ");
#endif
#ifdef READER_SECA
  fprintf(stderr, "seca ");
#endif
#ifdef READER_VIACCESS
  fprintf(stderr, "viaccess ");
#endif
#ifdef READER_VIDEOGUARD
  fprintf(stderr, "videoguard ");
#endif
#ifdef READER_DRE
  fprintf(stderr, "dre ");
#endif
#ifdef READER_TONGFANG
  fprintf(stderr, "tongfang ");
#endif
  fprintf(stderr, "\n\n");
#ifdef WEBIF
  fprintf(stderr, "oscam [-a] [-b] [-s] [-c <config dir>] [-t <tmp dir>] [-d <level>] [-r <level>] [-w <secs>] [-g <mode>] [-u] [-h]");
#else
	fprintf(stderr, "oscam [-a] [-b] [-s] [-c <config dir>] [-t <tmp dir>] [-d <level>] [-w <secs>] [-g <mode>] [-h]");
#endif
  fprintf(stderr, "\n\n\t-a         : write oscam.crash on segfault (needs installed GDB and OSCam compiled with debug infos -ggdb)\n");
  fprintf(stderr, "\t-b         : start in background\n");
  fprintf(stderr, "\t-s         : capture segmentation faults\n");
  fprintf(stderr, "\t-c <dir>   : read configuration from <dir>\n");
  fprintf(stderr, "\t             default = %s\n", CS_CONFDIR);
  fprintf(stderr, "\t-t <dir>   : tmp dir <dir>\n");
#ifdef CS_CYGWIN32
  fprintf(stderr, "\t             default = (OS-TMP)\n");
#else
  fprintf(stderr, "\t             default = /tmp/.oscam\n");
#endif
  fprintf(stderr, "\t-d <level> : debug level mask\n");
  fprintf(stderr, "\t               0 = no debugging (default)\n");
  fprintf(stderr, "\t               1 = detailed error messages\n");
  fprintf(stderr, "\t               2 = ATR parsing info, ECM, EMM and CW dumps\n");
  fprintf(stderr, "\t               4 = traffic from/to the reader\n");
  fprintf(stderr, "\t               8 = traffic from/to the clients\n");
  fprintf(stderr, "\t              16 = traffic to the reader-device on IFD layer\n");
  fprintf(stderr, "\t              32 = traffic to the reader-device on I/O layer\n");
  fprintf(stderr, "\t              64 = EMM logging\n");
  fprintf(stderr, "\t             128 = DVBAPI logging\n");
  fprintf(stderr, "\t             255 = debug all\n");
#ifdef WEBIF
  fprintf(stderr, "\t-r <level> : restart level\n");
  fprintf(stderr, "\t               0 = disabled, restart request sets exit status 99\n");
  fprintf(stderr, "\t               1 = restart activated, web interface can restart oscam (default)\n");
  fprintf(stderr, "\t               2 = like 1, but also restart on segmentation faults\n");
#endif
	fprintf(stderr, "\t-g <mode>  : garbage collector debug mode (1=immediate free, 2=check for double frees); these options are only intended for debug!\n");
  fprintf(stderr, "\t-w <secs>  : wait up to <secs> seconds for the system time to be set correctly (default 60)\n");
#ifdef WEBIF 
  fprintf(stderr, "\t-u         : enable output of web interface in UTF-8 charset\n");
#endif
  fprintf(stderr, "\t-h         : show this help\n");
  fprintf(stderr, "\n");
  exit(1);
}

#ifdef NEED_DAEMON
#ifdef OS_MACOSX
// this is done because daemon is being deprecated starting with 10.5 and -Werror will always trigger an error
static int32_t daemon_compat(int32_t nochdir, int32_t noclose)
#else
static int32_t daemon(int32_t nochdir, int32_t noclose)
#endif
{
  int32_t fd;

  switch (fork())
  {
    case -1: return (-1);
    case 0:  break;
    default: _exit(0);
  }

  if (setsid()==(-1))
    return(-1);

  if (!nochdir)
    (void)chdir("/");

  if (!noclose && (fd=open("/dev/null", O_RDWR, 0)) != -1)
  {
    (void)dup2(fd, STDIN_FILENO);
    (void)dup2(fd, STDOUT_FILENO);
    (void)dup2(fd, STDERR_FILENO);
    if (fd>2)
      (void)close(fd);
  }
  return(0);
}
#endif

int32_t recv_from_udpipe(uchar *buf)
{
  uint16_t n;
  if (buf[0]!='U')
  {
    cs_log("INTERNAL PIPE-ERROR");
    cs_exit(1);
  }
  memcpy(&n, buf+1, 2);

  memmove(buf, buf+3, n);

  return n;
}

/* Returns the username from the client. You will always get a char reference back (no NULLs but it may be string containting "NULL")
   which you should never modify and not free()! */
char *username(struct s_client * client)
{
	if (!client)
		return "NULL";

	if (client->typ == 's' || client->typ == 'h' || client->typ == 'a')
	{
		return processUsername?processUsername:"NULL";
	}

	if (client->typ == 'c' || client->typ == 'm') {
		struct s_auth *acc = client->account;
		if(acc)
		{
			if (acc->usr[0])
				return acc->usr;
			else
				return "anonymous";
		}
		else
		{
			return "NULL";
		}
	} else if (client->typ == 'r' || client->typ == 'p'){
		struct s_reader *rdr = client->reader;
		if(rdr)
			return rdr->label;
	}
	return "NULL";
}

static struct s_client * idx_from_ip(in_addr_t ip, in_port_t port)
{
  struct s_client *cl;
  for (cl=first_client; cl ; cl=cl->next)
    if ((cl->ip==ip) && (cl->port==port) && ((cl->typ=='c') || (cl->typ=='m')))
      return cl;
  return NULL;
}

static int32_t chk_caid(uint16_t caid, CAIDTAB *ctab)
{
  int32_t n;
  int32_t rc;
  for (rc=(-1), n=0; (n<CS_MAXCAIDTAB) && (rc<0); n++)
    if ((caid & ctab->mask[n]) == ctab->caid[n])
      rc=ctab->cmap[n] ? ctab->cmap[n] : caid;
  return(rc);
}

int32_t chk_bcaid(ECM_REQUEST *er, CAIDTAB *ctab)
{
  int32_t caid;
  if ((caid=chk_caid(er->caid, ctab))<0)
    return(0);
  er->caid=caid;
  return(1);
}

#ifdef WEBIF
void clear_account_stats(struct s_auth *account)
{
  account->cwfound = 0;
  account->cwcache = 0;
  account->cwnot = 0;
  account->cwtun = 0;
  account->cwignored  = 0;
  account->cwtout = 0;
  account->emmok = 0;
  account->emmnok = 0;
#ifdef CS_CACHEEX
  account->cwcacheexgot = 0;
  account->cwcacheexpush = 0;
  account->cwcacheexhit = 0;
#endif
}

void clear_all_account_stats()
{
  struct s_auth *account = cfg.account;
  while (account) {
    clear_account_stats(account);
    account = account->next;
  }
}

void clear_system_stats()
{
  first_client->cwfound = 0;
  first_client->cwcache = 0;
  first_client->cwnot = 0;
  first_client->cwtun = 0;
  first_client->cwignored  = 0;
  first_client->cwtout = 0;
  first_client->emmok = 0;
  first_client->emmnok = 0;
#ifdef CS_CACHEEX
  first_client->cwcacheexgot = 0;
  first_client->cwcacheexpush = 0;
  first_client->cwcacheexhit = 0;
#endif
}
#endif

void cs_accounts_chk()
{
  struct s_auth *old_accounts = cfg.account;
  struct s_auth *new_accounts = init_userdb();
  struct s_auth *account1,*account2;
  for (account1=cfg.account; account1; account1=account1->next) {
    for (account2=new_accounts; account2; account2=account2->next) {
      if (!strcmp(account1->usr, account2->usr)) {
        account2->cwfound = account1->cwfound;
        account2->cwcache = account1->cwcache;
        account2->cwnot = account1->cwnot;
        account2->cwtun = account1->cwtun;
        account2->cwignored  = account1->cwignored;
        account2->cwtout = account1->cwtout;
        account2->emmok = account1->emmok;
        account2->emmnok = account1->emmnok;
        account2->firstlogin = account1->firstlogin;
#ifdef CS_ANTICASC
		account2->ac_users = account1->ac_users;
		account2->ac_penalty = account1->ac_penalty;
		account2->ac_stat = account1->ac_stat;
#endif
      }
    }
  }
  cs_reinit_clients(new_accounts);
  cfg.account = new_accounts;
  init_free_userdb(old_accounts);

#ifdef CS_ANTICASC
  ac_clear();
#endif
}

static void free_ecm(ECM_REQUEST *ecm) {
	struct s_ecm_answer *ea, *nxt;
	
	ea = ecm->matching_rdr;
	ecm->matching_rdr = NULL;
	while (ea) {
		nxt = ea->next;
		add_garbage(ea);
		ea = nxt;
	}
	add_garbage(ecm);
}


static void cleanup_ecmtasks(struct s_client *cl)
{
	ECM_REQUEST *ecm;
	struct s_ecm_answer *ea_list, *ea_prev;

	if (cl->ecmtask) {
		int32_t i;
		for (i=0; i<CS_MAXPENDING; i++) {
			ecm = &cl->ecmtask[i];
			ecm->matching_rdr=NULL;
			ecm->client = NULL;
		}
		add_garbage(cl->ecmtask);
	}
	
	if (cl->cascadeusers) {
		ll_clear_data(cl->cascadeusers);
		cl->cascadeusers = NULL;
	}
	
	//remove this clients ecm from queue. because of cache, just null the client: 
	cs_readlock(&ecmcache_lock);
	for (ecm = ecmcwcache; ecm; ecm = ecm->next) { 
		if (ecm->client == cl)
			ecm->client = NULL; 
#ifdef CS_CACHEEX
		if (ecm->cacheex_src == cl)
			ecm->cacheex_src = NULL;
#endif
		//cl is a reader, remove from matching_rdr:
		for(ea_list = ecm->matching_rdr, ea_prev=NULL; ea_list; ea_prev = ea_list, ea_list = ea_list->next) {
			if (ea_list->reader->client == cl) {
				if (ea_prev)
					ea_prev->next = ea_list->next;
				else
					ecm->matching_rdr = ea_list->next;
				add_garbage(ea_list);
			}
		}

	} 
	cs_readunlock(&ecmcache_lock);

	//remove client from rdr ecm-queue:
	cs_readlock(&readerlist_lock);
	struct s_reader *rdr = first_active_reader;
	while (rdr) {
		if (rdr->client && rdr->client->ecmtask) {
			ECM_REQUEST *ecm;
			int i;
			for (i=0; i<CS_MAXPENDING; i++) {
				ecm = &rdr->client->ecmtask[i];
				if (ecm->client == cl)
					ecm->client = NULL;
			}
		}
		rdr=rdr->next;
	}
	cs_readunlock(&readerlist_lock);
}

void cleanup_thread(void *var)
{
	struct s_client *cl = var;
	if(!cl) return;
	struct s_reader *rdr = cl->reader;

	// Remove client from client list. kill_thread also removes this client, so here just if client exits itself...
	struct s_client *prev, *cl2;
	cs_writelock(&clientlist_lock);
	cl->kill = 1;
	for (prev=first_client, cl2=first_client->next; prev->next != NULL; prev=prev->next, cl2=cl2->next)
		if (cl == cl2)
			break;
	if (cl == cl2)
		prev->next = cl2->next; //remove client from list
	cs_writeunlock(&clientlist_lock);
		
	// Clean reader. The cleaned structures should be only used by the reader thread, so we should be save without waiting
	if (rdr){
		remove_reader_from_active(rdr);
		if(rdr->ph.cleanup)
			rdr->ph.cleanup(cl);
#ifdef WITH_CARDREADER
		if (cl->typ == 'r')
			ICC_Async_Close(rdr);
#endif
		if (cl->typ == 'p')
			network_tcp_connection_close(rdr, "close");
		cl->reader = NULL;
	}

	// Clean client specific data
	if(cl->typ == 'c'){
		cs_statistics(cl);
		cl->last_caid = 0xFFFF;
		cl->last_srvid = 0xFFFF;
		cs_statistics(cl);
	    
		cs_sleepms(500); //just wait a bit that really really nobody is accessing client data

		if(ph[cl->ctyp].cleanup)
			ph[cl->ctyp].cleanup(cl);
	}
		
	// Close network socket if not already cleaned by previous cleanup functions
	if(cl->pfd)
		close(cl->pfd); 
			
	// Clean all remaining structures

	pthread_mutex_trylock(&cl->thread_lock);
	ll_destroy_data(cl->joblist);
	cl->joblist = NULL;
	pthread_mutex_unlock(&cl->thread_lock);
	pthread_mutex_destroy(&cl->thread_lock);

	cleanup_ecmtasks(cl);
	add_garbage(cl->emmcache);
#ifdef MODULE_CCCAM
	add_garbage(cl->cc);
#endif
#ifdef MODULE_SERIAL
	add_garbage(cl->serialdata);
#endif
	add_garbage(cl);
}

static void cs_cleanup()
{
#ifdef WITH_LB
	if (cfg.lb_mode && cfg.lb_save) {
		save_stat_to_file(0);
		cfg.lb_save = 0; //this is for avoiding duplicate saves
	}
#endif

#ifdef MODULE_CCCAM
#ifdef MODULE_CCCSHARE
	done_share();
#endif
#endif

	//cleanup clients:
	struct s_client *cl;
	for (cl=first_client->next; cl; cl=cl->next) {
		if (cl->typ=='c'){
			if(cl->account && cl->account->usr)
				cs_log("killing client %s", cl->account->usr);
			kill_thread(cl);
		}
	}

	//cleanup readers:
	struct s_reader *rdr;
	for (rdr=first_active_reader; rdr ; rdr=rdr->next) {
		cl = rdr->client;
		if(cl){
			cs_log("killing reader %s", rdr->label);
			kill_thread(cl);
			// Stop MCR reader display thread
			if (cl->typ == 'r' && cl->reader && cl->reader->typ == R_SC8in1
					&& cl->reader->sc8in1_config && cl->reader->sc8in1_config->display_running) {
				cl->reader->sc8in1_config->display_running = FALSE;
			}
		}
	}
	first_active_reader = NULL;

	init_free_userdb(cfg.account);
	cfg.account = NULL;
	init_free_sidtab();
}

/*
 * flags: 1 = restart, 2 = don't modify if SIG_IGN, may be combined
 */
void set_signal_handler(int32_t sig, int32_t flags, void (*sighandler))
{
#ifdef CS_SIGBSD
  if ((signal(sig, sighandler)==SIG_IGN) && (flags & 2))
  {
    signal(sig, SIG_IGN);
    siginterrupt(sig, 0);
  }
  else
    siginterrupt(sig, (flags & 1) ? 0 : 1);
#else
  struct sigaction sa;
  sigaction(sig, (struct sigaction *) 0, &sa);
  if (!((flags & 2) && (sa.sa_handler==SIG_IGN)))
  {
    sigemptyset(&sa.sa_mask);
    sa.sa_flags=(flags & 1) ? SA_RESTART : 0;
    sa.sa_handler=sighandler;
    sigaction(sig, &sa, (struct sigaction *) 0);
  }
#endif
}

static void cs_master_alarm()
{
  cs_log("PANIC: master deadlock!");
  fprintf(stderr, "PANIC: master deadlock!");
  fflush(stderr);
}

static void cs_sigpipe()
{
	if (cs_dblevel & D_ALL_DUMP)
		cs_log("Got sigpipe signal -> captured");
}

static void cs_dummy() {
	return;
}

/* Switch debuglevel forward one step (called when receiving SIGUSR1). */
void cs_debug_level(){	
	switch (cs_dblevel) {
		case 0:
			cs_dblevel = 1;
			break;
		case 128:
			cs_dblevel = 255;
			break;
		case 255:
			cs_dblevel = 0;
			break;
		default:
			cs_dblevel <<= 1;
	}

	cs_log("debug_level=%d", cs_dblevel);
}

void cs_card_info()
{
	struct s_client *cl;
	for (cl=first_client->next; cl ; cl=cl->next)
		if( cl->typ=='r' && cl->reader )
			add_job(cl, ACTION_READER_CARDINFO, NULL, 0);
}

/**
 * write stacktrace to oscam.crash. file is always appended
 * Usage:
 * 1. compile oscam with debug parameters (Makefile: DS_OPTS="-ggdb")
 * 2. you need gdb installed and working on the local machine
 * 3. start oscam with parameter: -a
 */
void cs_dumpstack(int32_t sig)
{
	FILE *fp = fopen("oscam.crash", "a+");

	time_t timep;
	char buf[200];

	time(&timep);
	cs_ctime_r(&timep, buf);

	fprintf(stderr, "oscam crashed with signal %d on %swriting oscam.crash\n", sig, buf);

	fprintf(fp, "%sFATAL: Signal %d: %s Fault. Logged StackTrace:\n\n", buf, sig, (sig == SIGSEGV) ? "Segmentation" : ((sig == SIGBUS) ? "Bus" : "Unknown"));
	fclose(fp);

	FILE *cmd = fopen("/tmp/gdbcmd", "w");
	fputs("bt\n", cmd);
	fputs("thread apply all bt\n", cmd);
	fclose(cmd);

	snprintf(buf, sizeof(buf)-1, "gdb %s %d -batch -x /tmp/gdbcmd >> oscam.crash", prog_name, getpid());
	system(buf);

	exit(-1);
}


/**
 * called by signal SIGHUP
 *
 * reloads configs:
 *  - useraccounts (oscam.user)
 *  - services ids (oscam.srvid)
 *  - tier ids     (oscam.tiers)
 *  Also clears anticascading stats.
 **/
void cs_reload_config()
{
		cs_accounts_chk();
		init_srvid();
		init_tierid();
		#ifdef CS_ANTICASC
		ac_init_stat();
		#endif
}

/* Sets signal handlers to ignore for early startup of OSCam because for example log 
   could cause SIGPIPE errors and the normal signal handlers can't be used at this point. */
static void init_signal_pre()
{
		set_signal_handler(SIGPIPE , 1, SIG_IGN);
		set_signal_handler(SIGWINCH, 1, SIG_IGN);
		set_signal_handler(SIGALRM , 1, SIG_IGN);
		set_signal_handler(SIGHUP  , 1, SIG_IGN);
}

/* Sets the signal handlers.*/
static void init_signal()
{
		set_signal_handler(SIGINT, 3, cs_exit);
		//set_signal_handler(SIGKILL, 3, cs_exit);
#ifdef OS_MACOSX
		set_signal_handler(SIGEMT, 3, cs_exit);
#else
		//set_signal_handler(SIGPOLL, 3, cs_exit);
#endif
		//set_signal_handler(SIGPROF, 3, cs_exit);
		set_signal_handler(SIGTERM, 3, cs_exit);
		//set_signal_handler(SIGVTALRM, 3, cs_exit);

		set_signal_handler(SIGWINCH, 1, SIG_IGN);
		//  set_signal_handler(SIGPIPE , 0, SIG_IGN);
		set_signal_handler(SIGPIPE , 0, cs_sigpipe);
		//  set_signal_handler(SIGALRM , 0, cs_alarm);
		set_signal_handler(SIGALRM , 0, cs_master_alarm);
		// set_signal_handler(SIGCHLD , 1, cs_child_chk);
		set_signal_handler(SIGHUP  , 1, cs_reload_config);
		//set_signal_handler(SIGHUP , 1, cs_sighup);
		set_signal_handler(SIGUSR1, 1, cs_debug_level);
		set_signal_handler(SIGUSR2, 1, cs_card_info);
		set_signal_handler(OSCAM_SIGNAL_WAKEUP, 0, cs_dummy);

		if (cs_capture_SEGV) {
			set_signal_handler(SIGSEGV, 1, cs_exit);
			set_signal_handler(SIGBUS, 1, cs_exit);
		}
		else if (cs_dump_stack) {
			set_signal_handler(SIGSEGV, 1, cs_dumpstack);
			set_signal_handler(SIGBUS, 1, cs_dumpstack);
		}


		cs_log("signal handling initialized (type=%s)",
#ifdef CS_SIGBSD
		"bsd"
#else
		"sysv"
#endif
		);
	return;
}

void cs_exit(int32_t sig)
{
	if (cs_dump_stack && (sig == SIGSEGV || sig == SIGBUS))
		cs_dumpstack(sig);

	set_signal_handler(SIGCHLD, 1, SIG_IGN);
	set_signal_handler(SIGHUP , 1, SIG_IGN);
	set_signal_handler(SIGPIPE, 1, SIG_IGN);

	if (sig==SIGALRM) {
		cs_debug_mask(D_TRACE, "thread %8lX: SIGALRM, skipping", (unsigned long)pthread_self());
		return;
	}

  if (sig && (sig!=SIGQUIT))
    cs_log("thread %8lX exit with signal %d", (unsigned long)pthread_self(), sig);

  struct s_client *cl = cur_client();
  if (!cl)
  	return;

  if(cl->typ == 'h' || cl->typ == 's'){
#ifdef ARM
		if(cfg.enableled == 1){
			cs_switch_led(LED1B, LED_OFF);
			cs_switch_led(LED2, LED_OFF);
			cs_switch_led(LED3, LED_OFF);
			cs_switch_led(LED1A, LED_ON);
		}
		arm_led_stop_thread();
#endif
#ifdef QBOXHD
		if(cfg.enableled == 2){
	    qboxhd_led_blink(QBOXHD_LED_COLOR_YELLOW,QBOXHD_LED_BLINK_FAST);
	    qboxhd_led_blink(QBOXHD_LED_COLOR_RED,QBOXHD_LED_BLINK_FAST);
	    qboxhd_led_blink(QBOXHD_LED_COLOR_GREEN,QBOXHD_LED_BLINK_FAST);
	    qboxhd_led_blink(QBOXHD_LED_COLOR_BLUE,QBOXHD_LED_BLINK_FAST);
	    qboxhd_led_blink(QBOXHD_LED_COLOR_MAGENTA,QBOXHD_LED_BLINK_FAST);
	  }
#endif
#ifdef LCDSUPPORT
    end_lcd_thread();
#endif

#ifndef OS_CYGWIN32
	char targetfile[256];
		snprintf(targetfile, 255, "%s%s", get_tmp_dir(), "/oscam.version");
		if (unlink(targetfile) < 0)
			cs_log("cannot remove oscam version file %s (errno=%d %s)", targetfile, errno, strerror(errno));
#endif
#ifdef COOL
		coolapi_close_all();
#endif
  }

	// this is very important - do not remove
	if (cl->typ != 's') {
		cs_log("thread %8lX ended!", (unsigned long)pthread_self());

		cleanup_thread(cl);

		//Restore signals before exiting thread
		set_signal_handler(SIGPIPE , 0, cs_sigpipe);
		set_signal_handler(SIGHUP  , 1, cs_reload_config);

		pthread_exit(NULL);
		return;
	}

	cs_log("cardserver down");
	cs_close_log();

	if (sig == SIGINT)
		exit(sig);

	cs_cleanup();

	if (!exit_oscam)
	  exit_oscam = sig?sig:1;
}

void cs_reinit_clients(struct s_auth *new_accounts)
{
	struct s_auth *account;
	unsigned char md5tmp[MD5_DIGEST_LENGTH];

	struct s_client *cl;
	for (cl=first_client->next; cl ; cl=cl->next)
		if( (cl->typ == 'c' || cl->typ == 'm') && cl->account ) {
			for (account = new_accounts; (account) ; account = account->next)
				if (!strcmp(cl->account->usr, account->usr))
					break;

			if (account && !account->disabled && cl->pcrc == crc32(0L, MD5((uchar *)account->pwd, strlen(account->pwd), md5tmp), MD5_DIGEST_LENGTH)) {
				cl->account = account;
				if(cl->typ == 'c'){
					cl->grp	= account->grp;
					cl->aureader_list	= account->aureader_list;
					cl->autoau = account->autoau;
					cl->expirationdate = account->expirationdate;
					cl->allowedtimeframe[0] = account->allowedtimeframe[0];
					cl->allowedtimeframe[1] = account->allowedtimeframe[1];
					cl->ncd_keepalive = account->ncd_keepalive;
					cl->c35_suppresscmd08 = account->c35_suppresscmd08;
					cl->tosleep	= (60*account->tosleep);
					cl->c35_sleepsend = account->c35_sleepsend;
					cl->monlvl = account->monlvl;
					cl->disabled	= account->disabled;
					cl->fchid	= account->fchid;  // CHID filters
					cl->cltab	= account->cltab;  // Class
					// newcamd module doesn't like ident reloading
					if(!cl->ncd_server)
						cl->ftab = account->ftab;   // Ident

					cl->sidtabok = account->sidtabok;   // services
					cl->sidtabno = account->sidtabno;   // services
					cl->failban = account->failban;

					memcpy(&cl->ctab, &account->ctab, sizeof(cl->ctab));
					memcpy(&cl->ttab, &account->ttab, sizeof(cl->ttab));
#ifdef WEBIF
					int32_t i;
					for(i = 0; i < CS_ECM_RINGBUFFER_MAX; i++) {
						cl->cwlastresptimes[i].duration = 0;
						cl->cwlastresptimes[i].timestamp = time((time_t*)0);
						cl->cwlastresptimes[i].rc = 0;
					}
					cl->cwlastresptimes_last = 0;
#endif
					if (account->uniq)
						cs_fake_client(cl, account->usr, (account->uniq == 1 || account->uniq == 2)?account->uniq+2:account->uniq, cl->ip);
#ifdef CS_ANTICASC
					int32_t numusers = account->ac_users;
					if ( numusers == -1)
						numusers = cfg.ac_users;
					cl->ac_limit	= (numusers * 100 + 80) * cfg.ac_stime;
					cl->ac_penalty = account->ac_penalty == -1 ? cfg.ac_penalty : account->ac_penalty;
					cs_debug_mask(D_CLIENT, "acasc: client '%s', users=%d, stime=%d min, dwlimit=%d per min, penalty=%d",
								  account->usr, numusers, cfg.ac_stime,
								  numusers*100+80, cl->ac_penalty);
#endif
				}
			} else {
				if (ph[cl->ctyp].type & MOD_CONN_NET) {
					cs_debug_mask(D_TRACE, "client '%s', thread=%8lX not found in db (or password changed)", cl->account->usr, (unsigned long)cl->thread);
					kill_thread(cl);
				} else {
					cl->account = first_client->account;
				}
			}
		} else {
			cl->account = NULL;
		}
}

struct s_client * create_client(in_addr_t ip) {
	struct s_client *cl;

	if(cs_malloc(&cl, sizeof(struct s_client), -1)){
		//client part
		cl->ip=ip;
		cl->account = first_client->account;

		//master part
		pthread_mutex_init(&cl->thread_lock, NULL);

		cl->login=cl->last=time((time_t *)0);
		
		cl->tid = (uint32_t)(uintptr_t)cl;	// Use pointer adress of client as threadid (for monitor and log)

		//Now add new client to the list:
		struct s_client *last;
		cs_writelock(&clientlist_lock);
		if(sizeof(uintptr_t) > 4){		// 64bit systems can have collisions because of the cast so lets check if there are some
			int8_t found;
			do{
				found = 0;
				for (last=first_client; last; last=last->next){
					if(last->tid == cl->tid){
						found = 1;
						break;
					}
				}
				if(found || cl->tid == 0){
					cl->tid = (uint32_t)rand();
				}
			} while (found || cl->tid == 0);
		}
		for (last=first_client; last->next != NULL; last=last->next); //ends with cl on last client
		last->next = cl;
		cs_writeunlock(&clientlist_lock);
	} else {
		cs_log("max connections reached (out of memory) -> reject client %s", cs_inet_ntoa(ip));
		return NULL;
	}
	return(cl);
}


/* Creates the master client of OSCam and inits some global variables/mutexes. */
static void init_first_client()
{
	// get username OScam is running under
	struct passwd pwd;
	char buf[256];
	struct passwd *pwdbuf;
	if ((getpwuid_r(getuid(), &pwd, buf, sizeof(buf), &pwdbuf)) == 0){
		if(cs_malloc(&processUsername, strlen(pwd.pw_name) + 1, -1))
			cs_strncpy(processUsername, pwd.pw_name, strlen(pwd.pw_name) + 1);
		else
			processUsername = "root";
	} else
		processUsername = "root";

  if(!cs_malloc(&first_client, sizeof(struct s_client), -1)){
    fprintf(stderr, "Could not allocate memory for master client, exiting...");
    exit(1);
  }
  first_client->next = NULL; //terminate clients list with NULL
  first_client->login=time((time_t *)0);
  first_client->ip=cs_inet_addr("127.0.0.1");
  first_client->typ='s';
  first_client->thread=pthread_self();
  struct s_auth *null_account;
  if(!cs_malloc(&null_account, sizeof(struct s_auth), -1)){
  	fprintf(stderr, "Could not allocate memory for master account, exiting...");
    exit(1);
  }
  first_client->account = null_account;
  if (pthread_setspecific(getclient, first_client)) {
    fprintf(stderr, "Could not setspecific getclient in master process, exiting...");
    exit(1);
  }

#if defined(LIBUSB)
  cs_lock_create(&sr_lock, 10, "sr_lock");
#endif
  cs_lock_create(&system_lock, 5, "system_lock");
  cs_lock_create(&gethostbyname_lock, 10, "gethostbyname_lock");
  cs_lock_create(&clientlist_lock, 5, "clientlist_lock");
  cs_lock_create(&readerlist_lock, 5, "readerlist_lock");
  cs_lock_create(&fakeuser_lock, 5, "fakeuser_lock");
  cs_lock_create(&ecmcache_lock, 5, "ecmcache_lock");
  cs_lock_create(&readdir_lock, 5, "readdir_lock");

#ifdef COOL
  coolapi_open_all();
#endif
}

/* Checks if the date of the system is correct and waits if necessary. */
static void init_check(){
	char *ptr = __DATE__;
	int32_t month, year = atoi(ptr + strlen(ptr) - 4), day = atoi(ptr + 4);
	if(day > 0 && day < 32 && year > 2010 && year < 9999){
		struct tm timeinfo;
		char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
		for(month = 0; month < 12; ++month){
			if(!strncmp(ptr, months[month], 3)) break;
		}
		if(month > 11) month = 0;
		memset(&timeinfo, 0, sizeof(timeinfo));
		timeinfo.tm_mday = day;
		timeinfo.tm_mon = month;
		timeinfo.tm_year = year - 1900;
		time_t builddate = mktime(&timeinfo) - 86400;
	  int32_t i = 0;
	  while(time((time_t*)0) < builddate){
	  	if(i == 0) cs_log("The current system time is smaller than the build date (%s). Waiting up to %d seconds for time to correct", ptr, cs_waittime);
	  	cs_sleepms(1000);
	  	++i;
	  	if(i > cs_waittime){
	  		cs_log("Waiting was not successful. OSCam will be started but is UNSUPPORTED this way. Do not report any errors with this version.");
				break;
	  	}
	  }
	  // adjust login time of first client
	  if(i > 0) first_client->login=time((time_t *)0);
	}
}

static int32_t start_listener(struct s_module *ph, int32_t port_idx)
{
  int32_t ov=1, timeout, is_udp, i;
  char ptxt[2][32];
  struct   sockaddr_in sad;     /* structure to hold server's address */
  cs_log("Starting listener %d", port_idx);

  ptxt[0][0]=ptxt[1][0]='\0';
  if (!ph->ptab->ports[port_idx].s_port)
  {
    cs_log("%s: disabled", ph->desc);
    return(0);
  }
  is_udp=(ph->type==MOD_CONN_UDP);

  memset((char  *)&sad,0,sizeof(sad)); /* clear sockaddr structure   */
  sad.sin_family = AF_INET;            /* set family to Internet     */
  if (!ph->s_ip)
    ph->s_ip=cfg.srvip;
  if (ph->s_ip)
  {
    sad.sin_addr.s_addr=ph->s_ip;
    snprintf(ptxt[0], sizeof(ptxt[0]), ", ip=%s", inet_ntoa(sad.sin_addr));
  }
  else
    sad.sin_addr.s_addr=INADDR_ANY;
  timeout=cfg.bindwait;
  //ph->fd=0;
  ph->ptab->ports[port_idx].fd = 0;

  if (ph->ptab->ports[port_idx].s_port > 0)   /* test for illegal value    */
    sad.sin_port = htons((uint16_t)ph->ptab->ports[port_idx].s_port);
  else
  {
    cs_log("%s: Bad port %d", ph->desc, ph->ptab->ports[port_idx].s_port);
    return(0);
  }

  if ((ph->ptab->ports[port_idx].fd=socket(PF_INET,is_udp ? SOCK_DGRAM : SOCK_STREAM, is_udp ? IPPROTO_UDP : IPPROTO_TCP))<0)
  {
    cs_log("%s: Cannot create socket (errno=%d: %s)", ph->desc, errno, strerror(errno));
    return(0);
  }

  ov=1;
  if (setsockopt(ph->ptab->ports[port_idx].fd, SOL_SOCKET, SO_REUSEADDR, (void *)&ov, sizeof(ov))<0)
  {
    cs_log("%s: setsockopt failed (errno=%d: %s)", ph->desc, errno, strerror(errno));
    close(ph->ptab->ports[port_idx].fd);
    return(ph->ptab->ports[port_idx].fd=0);
  }

#ifdef SO_REUSEPORT
  setsockopt(ph->ptab->ports[port_idx].fd, SOL_SOCKET, SO_REUSEPORT, (void *)&ov, sizeof(ov));
#endif

#ifdef SO_PRIORITY
  if (cfg.netprio)
    if (!setsockopt(ph->ptab->ports[port_idx].fd, SOL_SOCKET, SO_PRIORITY, (void *)&cfg.netprio, sizeof(uint32_t)))
      snprintf(ptxt[1], sizeof(ptxt[1]), ", prio=%d", cfg.netprio);
#endif

  if( !is_udp )
  {
    int32_t keep_alive = 1;
    setsockopt(ph->ptab->ports[port_idx].fd, SOL_SOCKET, SO_KEEPALIVE,
               (void *)&keep_alive, sizeof(keep_alive));
  }

  while (timeout--)
  {
    if (bind(ph->ptab->ports[port_idx].fd, (struct sockaddr *)&sad, sizeof (sad))<0)
    {
      if (timeout)
      {
        cs_log("%s: Bind request failed, waiting another %d seconds",
               ph->desc, timeout);
        cs_sleepms(1000);
      }
      else
      {
        cs_log("%s: Bind request failed, giving up", ph->desc);
        close(ph->ptab->ports[port_idx].fd);
        return(ph->ptab->ports[port_idx].fd=0);
      }
    }
    else timeout=0;
  }

  if (!is_udp)
    if (listen(ph->ptab->ports[port_idx].fd, CS_QLEN)<0)
    {
      cs_log("%s: Cannot start listen mode (errno=%d: %s)", ph->desc, errno, strerror(errno));
      close(ph->ptab->ports[port_idx].fd);
      return(ph->ptab->ports[port_idx].fd=0);
    }

	cs_log("%s: initialized (fd=%d, port=%d%s%s%s)",
         ph->desc, ph->ptab->ports[port_idx].fd,
         ph->ptab->ports[port_idx].s_port,
         ptxt[0], ptxt[1], ph->logtxt ? ph->logtxt : "");

	for( i=0; i<ph->ptab->ports[port_idx].ftab.nfilts; i++ ) {
		int32_t j, pos=0;
		char buf[30 + (8*ph->ptab->ports[port_idx].ftab.filts[i].nprids)];
		pos += snprintf(buf, sizeof(buf), "-> CAID: %04X PROVID: ", ph->ptab->ports[port_idx].ftab.filts[i].caid );
		
		for( j=0; j<ph->ptab->ports[port_idx].ftab.filts[i].nprids; j++ )
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%06X, ", ph->ptab->ports[port_idx].ftab.filts[i].prids[j]);

		if(pos>2 && j>0)
			buf[pos-2] = '\0';

		cs_log("%s", buf);
	}

	return(ph->ptab->ports[port_idx].fd);
}

/* Resolves the ip of the hostname of the specified account and saves it in account->dynip.
   If the hostname is not configured, the ip is set to 0. */
void cs_user_resolve(struct s_auth *account){
	if (account->dyndns[0]){
		in_addr_t lastip = account->dynip;
		account->dynip = cs_getIPfromHost((char*)account->dyndns);
		
		if (lastip != account->dynip)  {
			cs_log("%s: resolved ip=%s", (char*)account->dyndns, cs_inet_ntoa(account->dynip));
		}
	} else account->dynip=0;
}

/* Starts a thread named nameroutine with the start function startroutine. */
void start_thread(void * startroutine, char * nameroutine) {
	pthread_t temp;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	cs_log("starting thread %s", nameroutine);
#ifndef TUXBOX
	pthread_attr_setstacksize(&attr, PTHREAD_STACK_SIZE);
#endif
	cs_writelock(&system_lock);
	int32_t ret = pthread_create(&temp, &attr, startroutine, NULL);
	if (ret)
		cs_log("ERROR: can't create %s thread (errno=%d %s)", nameroutine, ret, strerror(ret));
	else {
		cs_log("%s thread started", nameroutine);
		pthread_detach(temp);
	}
	pthread_attr_destroy(&attr);
	cs_writeunlock(&system_lock);
}

/* Allows to kill another thread specified through the client cl with locking.
  If the own thread has to be cancelled, cs_exit or cs_disconnect_client has to be used. */
void kill_thread(struct s_client *cl) {
	if (!cl || cl->kill) return;
	cl->kill=1;
	add_job(cl, ACTION_CLIENT_KILL, NULL, 0);
}

/* Removes a reader from the list of active readers so that no ecms can be requested anymore. */
void remove_reader_from_active(struct s_reader *rdr) {
	struct s_reader *rdr2, *prv = NULL;
	//cs_log("CHECK: REMOVE READER %s FROM ACTIVE", rdr->label);
	cs_writelock(&readerlist_lock);
	for (rdr2=first_active_reader; rdr2 ; prv=rdr2, rdr2=rdr2->next) {
		if (rdr2==rdr) {
			if (prv) prv->next = rdr2->next;
			else first_active_reader = rdr2->next;
			break;
		}
	}
	rdr->next = NULL;
	cs_writeunlock(&readerlist_lock);
}

/* Adds a reader to the list of active readers so that it can serve ecms. */
void add_reader_to_active(struct s_reader *rdr) {
	struct s_reader *rdr2, *rdr_prv=NULL, *rdr_tmp=NULL;
	int8_t at_first = 1;

	if (rdr->next)
		remove_reader_from_active(rdr);

	//cs_log("CHECK: ADD READER %s TO ACTIVE", rdr->label);
	cs_writelock(&readerlist_lock);
	cs_writelock(&clientlist_lock);

	//search configured position:
	LL_ITER it = ll_iter_create(configured_readers);
	while ((rdr2=ll_iter_next(&it))) {
		if(rdr2==rdr) break;
		if (rdr2->client && rdr2->enable) {
			rdr_prv = rdr2;
			at_first = 0;
		}
	}

	//insert at configured position:
	if (first_active_reader) {
		if (at_first) {
			rdr->next = first_active_reader;
			first_active_reader = rdr;

			//resort client list:
			struct s_client *prev, *cl;
			for (prev = first_client, cl = first_client->next;
					prev->next != NULL; prev = prev->next, cl = cl->next)
				if (rdr->client == cl)
					break;

			if (rdr->client == cl) {
				prev->next = cl->next; //remove client from list
				cl->next = first_client->next;
				first_client->next = cl;
			}
		}
		else
		{
			for (rdr2=first_active_reader; rdr2->next && rdr2 != rdr_prv ; rdr2=rdr2->next) ; //search last element
			rdr_prv = rdr2;
			rdr_tmp = rdr2->next;
			rdr2->next = rdr;
			rdr->next = rdr_tmp;

			//resort client list:
			struct s_client *prev, *cl;
			for (prev = first_client, cl = first_client->next;
					prev->next != NULL; prev = prev->next, cl = cl->next)
				if (rdr->client == cl)
					break;
			if (rdr->client == cl) {
				prev->next = cl->next; //remove client from list
				cl->next = rdr_prv->client->next;
				rdr_prv->client->next = cl;
			}
		}

	} else first_active_reader = rdr;

	cs_writeunlock(&clientlist_lock);
	cs_writeunlock(&readerlist_lock);
}

/* Starts or restarts a cardreader without locking. If restart=1, the existing thread is killed before restarting,
   if restart=0 the cardreader is only started. */
static int32_t restart_cardreader_int(struct s_reader *rdr, int32_t restart) {
	struct s_client *old_client = rdr->client;
	if (restart){
		remove_reader_from_active(rdr);		//remove from list
		struct s_client *cl = rdr->client;
		if (cl) {		//kill old thread
			kill_thread(cl);
			rdr->client = NULL;
		}
	}

	while (restart && old_client && old_client->reader == rdr) {
		//If we quick disable+enable a reader (webif), remove_reader_from_active is called from
		//cleanup. this could happen AFTER reader is restarted, so oscam crashes or reader is hidden
		//cs_log("CHECK: WAITING FOR CLEANUP READER %s", rdr->label);
		cs_sleepms(100);					  //Fixme, cleanup does old_client->reader = NULL
	}

	rdr->tcp_connected = 0;
	rdr->card_status = UNKNOWN;
	rdr->tcp_block_delay = 100;
	cs_ftime(&rdr->tcp_block_connect_till);

	if (rdr->device[0] && (rdr->typ & R_IS_CASCADING)) {
		if (!rdr->ph.num) {
			cs_log("Protocol Support missing. (typ=%d)", rdr->typ);
			return 0;
		}
		cs_debug_mask(D_TRACE, "reader %s protocol: %s", rdr->label, rdr->ph.desc);
	}

	if (!rdr->enable)
		return 0;

	if (rdr->device[0]) {
		if (restart) {
			cs_log("restarting reader %s", rdr->label);
		}
		struct s_client * cl = create_client(first_client->ip);
		if (cl == NULL) return 0;
		cl->reader=rdr;
		cs_log("creating thread for device %s", rdr->device);

		cl->sidtabok=rdr->sidtabok;
		cl->sidtabno=rdr->sidtabno;
		cl->grp = rdr->grp;

		rdr->client=cl;

		cl->typ='r';
		//client[i].ctyp=99;

		add_job(cl, ACTION_READER_INIT, NULL, 0);
		add_reader_to_active(rdr);

		return 1;
	}
	return 0;
}

/* Starts or restarts a cardreader with locking. If restart=1, the existing thread is killed before restarting,
   if restart=0 the cardreader is only started. */
int32_t restart_cardreader(struct s_reader *rdr, int32_t restart) {
	cs_writelock(&system_lock);
	int32_t result = restart_cardreader_int(rdr, restart);
	cs_writeunlock(&system_lock);
	return result;
}

static void init_cardreader() {

	cs_debug_mask(D_TRACE, "cardreader: Initializing");
	cs_writelock(&system_lock);
	struct s_reader *rdr;

#ifdef WITH_CARDREADER
	ICC_Async_Init_Locks();
#endif

	LL_ITER itr = ll_iter_create(configured_readers);
	while((rdr = ll_iter_next(&itr))) {
		if (rdr->enable) {
			restart_cardreader_int(rdr, 0);
		}
	}

#ifdef WITH_LB
	load_stat_from_file();
#endif
	cs_writeunlock(&system_lock);
}

static void cs_fake_client(struct s_client *client, char *usr, int32_t uniq, in_addr_t ip)
{
    /* Uniq = 1: only one connection per user
     *
     * Uniq = 2: set (new connected) user only to fake if source
     *           ip is different (e.g. for newcamd clients with
     *	         different CAID's -> Ports)
     *
     * Uniq = 3: only one connection per user, but only the last
     *           login will survive (old mpcs behavior)
     *
     * Uniq = 4: set user only to fake if source ip is
     *           different, but only the last login will survive
     */

	struct s_client *cl;
	struct s_auth *account;
	cs_writelock(&fakeuser_lock);
	for (cl=first_client->next; cl ; cl=cl->next)
	{
		account = cl->account;
		if (cl != client && (cl->typ == 'c') && !cl->dup && account && !strcmp(account->usr, usr)
		   && (uniq < 5) && ((uniq % 2) || (cl->ip != ip)))
		{
		        char buf[20];
			if (uniq  == 3 || uniq == 4)
			{
				cl->dup = 1;
				cl->aureader_list = NULL;
				cs_strncpy(buf, cs_inet_ntoa(cl->ip), sizeof(buf));
				cs_log("client(%8lX) duplicate user '%s' from %s (prev %s) set to fake (uniq=%d)",
					(unsigned long)cl->thread, usr, cs_inet_ntoa(ip), buf, uniq);
				if (cl->failban & BAN_DUPLICATE) {
					cs_add_violation(cl, usr);
				}
				if (cfg.dropdups){
					cs_writeunlock(&fakeuser_lock);
					kill_thread(cl);
					cs_writelock(&fakeuser_lock);
				}
			}
			else
			{
				client->dup = 1;
				client->aureader_list = NULL;
				cs_strncpy(buf, cs_inet_ntoa(ip), sizeof(buf));
				cs_log("client(%8lX) duplicate user '%s' from %s (current %s) set to fake (uniq=%d)",
					(unsigned long)pthread_self(), usr, cs_inet_ntoa(cl->ip), buf, uniq);
				if (client->failban & BAN_DUPLICATE) {
					cs_add_violation_by_ip(ip, ph[client->ctyp].ptab->ports[client->port_idx].s_port, usr);
				}
				if (cfg.dropdups){
					cs_writeunlock(&fakeuser_lock);		// we need to unlock here as cs_disconnect_client kills the current thread!
					cs_disconnect_client(client);
				}
				break;
			}
		}
	}
	cs_writeunlock(&fakeuser_lock);
}

int32_t cs_auth_client(struct s_client * client, struct s_auth *account, const char *e_txt)
{
	int32_t rc=0;
	unsigned char md5tmp[MD5_DIGEST_LENGTH];
	char buf[32];
	char *t_crypt="encrypted";
	char *t_plain="plain";
	char *t_grant=" granted";
	char *t_reject=" rejected";
	char *t_msg[]= { buf, "invalid access", "invalid ip", "unknown reason", "protocol not allowed" };
	memset(&client->grp, 0xff, sizeof(uint64_t));
	//client->grp=0xffffffffffffff;
	if ((intptr_t)account != 0 && (intptr_t)account != -1 && account->disabled){
		cs_add_violation(client, account->usr);
		cs_log("%s %s-client %s%s (%s%sdisabled account)",
				client->crypted ? t_crypt : t_plain,
				ph[client->ctyp].desc,
				client->ip ? cs_inet_ntoa(client->ip) : "",
				client->ip ? t_reject : t_reject+1,
				e_txt ? e_txt : "",
				e_txt ? " " : "");
		return(1);
	}

	// check whether client comes in over allowed protocol
	if ((intptr_t)account != 0 && (intptr_t)account != -1 && (intptr_t)account->allowedprotocols &&
			(((intptr_t)account->allowedprotocols & ph[client->ctyp].listenertype) != ph[client->ctyp].listenertype )){
		cs_add_violation(client, account->usr);
		cs_log("%s %s-client %s%s (%s%sprotocol not allowed)",
						client->crypted ? t_crypt : t_plain,
						ph[client->ctyp].desc,
						client->ip ? cs_inet_ntoa(client->ip) : "",
						client->ip ? t_reject : t_reject+1,
						e_txt ? e_txt : "",
						e_txt ? " " : "");
		return(1);
	}

	client->account=first_client->account;
	switch((intptr_t)account)
	{
	case 0:           // reject access
		rc=1;
		cs_add_violation(client, NULL);
		cs_log("%s %s-client %s%s (%s)",
				client->crypted ? t_crypt : t_plain,
				ph[client->ctyp].desc,
				client->ip ? cs_inet_ntoa(client->ip) : "",
				client->ip ? t_reject : t_reject+1,
				e_txt ? e_txt : t_msg[rc]);
		break;
	default:            // grant/check access
		if (client->ip && account->dyndns[0]) {
			if (client->ip != account->dynip)
				cs_user_resolve(account);
			if (client->ip != account->dynip) {
				cs_add_violation(client, account->usr);
				rc=2;
			}
		}

		client->monlvl=account->monlvl;
		client->account = account;
		if (!rc)
		{
			client->dup=0;
			if (client->typ=='c' || client->typ=='m')
				client->pcrc = crc32(0L, MD5((uchar *)account->pwd, strlen(account->pwd), md5tmp), MD5_DIGEST_LENGTH);
			if (client->typ=='c')
			{
				client->last_caid = 0xFFFE;
				client->last_srvid = 0xFFFE;
				client->expirationdate = account->expirationdate;
				client->disabled = account->disabled;
				client->allowedtimeframe[0] = account->allowedtimeframe[0];
				client->allowedtimeframe[1] = account->allowedtimeframe[1];
				if(account->firstlogin == 0) account->firstlogin = time((time_t*)0);
				client->failban = account->failban;
				client->c35_suppresscmd08 = account->c35_suppresscmd08;
				client->ncd_keepalive = account->ncd_keepalive;
				client->grp = account->grp;
				client->aureader_list = account->aureader_list;
				client->autoau = account->autoau;
				client->tosleep = (60*account->tosleep);
				client->c35_sleepsend = account->c35_sleepsend;
				memcpy(&client->ctab, &account->ctab, sizeof(client->ctab));
				if (account->uniq)
					cs_fake_client(client, account->usr, account->uniq, client->ip);
				client->ftab  = account->ftab;   // IDENT filter
				client->cltab = account->cltab;  // CLASS filter
				client->fchid = account->fchid;  // CHID filter
				client->sidtabok= account->sidtabok;   // services
				client->sidtabno= account->sidtabno;   // services
				memcpy(&client->ttab, &account->ttab, sizeof(client->ttab));
#ifdef CS_ANTICASC
				ac_init_client(client, account);
#endif
			}
		}
	case -1:            // anonymous grant access
		if (rc)
			t_grant=t_reject;
		else {
			if (client->typ=='m')
				snprintf(t_msg[0], sizeof(buf), "lvl=%d", client->monlvl);
			else {
				int32_t rcount = ll_count(client->aureader_list);
				snprintf(buf, sizeof(buf), "au=");
				if (!rcount)
					snprintf(buf+3, sizeof(buf)-3, "off");
				else {
					if (client->autoau)
						snprintf(buf+3, sizeof(buf)-3, "auto (%d reader)", rcount);
					else
						snprintf(buf+3, sizeof(buf)-3, "on (%d reader)", rcount);
				}
			}
		}

		cs_log("%s %s-client %s%s (%s, %s)",
			client->crypted ? t_crypt : t_plain,
			e_txt ? e_txt : ph[client->ctyp].desc,
			client->ip ? cs_inet_ntoa(client->ip) : "",
			client->ip ? t_grant : t_grant+1,
			username(client), t_msg[rc]);

		break;
	}
	return(rc);
}

void cs_disconnect_client(struct s_client * client)
{
	char buf[32]={0};
	if (client->ip)
		snprintf(buf, sizeof(buf), " from %s", cs_inet_ntoa(client->ip));
	cs_log("%s disconnected %s", username(client), buf);
	if (client == cur_client())
		cs_exit(0);
	else
		kill_thread(client);
}

#ifdef CS_CACHEEX
static void cs_cache_push_to_client(struct s_client *cl, ECM_REQUEST *er)
{
	ECM_REQUEST *erc = cs_malloc(&erc, sizeof(ECM_REQUEST), 0);
	memcpy(erc, er, sizeof(ECM_REQUEST));
	add_job(cl, ACTION_CACHE_PUSH_OUT, erc, sizeof(ECM_REQUEST));
}

/**
 * cacheex modes:
 *
 * cacheex=1 CACHE PULL:
 * Situation: oscam A reader1 has cacheex=1, oscam B account1 has cacheex=1
 *   oscam A gets a ECM request, reader1 send this request to oscam B, oscam B checks his cache
 *   a. not found in cache: return NOK
 *   a. found in cache: return OK+CW
 *   b. not found in cache, but found pending request: wait max cacheexwaittime and check again
 *   oscam B never requests new ECMs
 *
 *   CW-flow: B->A
 *
 * cacheex=2 CACHE PUSH:
 * Situation: oscam A reader1 has cacheex=2, oscam B account1 has cacheex=2
 *   if oscam B gets a CW, its pushed to oscam A
 *   reader has normal functionality and can request ECMs
 *
 *   Problem: oscam B can only push if oscam A is connected
 *   Problem or feature?: oscam A reader can request ecms from oscam B
 *
 *   CW-flow: B->A
 *
 * cacheex=3 REVERSE CACHE PUSH:
 * Situation: oscam A reader1 has cacheex=3, oscam B account1 has cacheex=3
 *   if oscam A gets a CW, its pushed to oscam B
 *
 *   oscam A never requests new ECMs
 *
 *   CW-flow: A->B
 */
void cs_cache_push(ECM_REQUEST *er)
{
	if (er->rc >= E_NOTFOUND) //Maybe later we could support other rcs
		return;

	if (er->cacheex_pushed || (er->ecmcacheptr && er->ecmcacheptr->cacheex_pushed))
		return;

	//cacheex=2 mode: push (server->remote)
	struct s_client *cl;
	for (cl=first_client->next; cl; cl=cl->next) {
		if (er->cacheex_src != cl) {
			if (cl->typ == 'c' && cl->account && cl->account->cacheex == 2) { //send cache over user
				if (ph[cl->ctyp].c_cache_push // cache-push able
						&& (!er->grp || (cl->grp & er->grp)) //Group-check
						&& chk_srvid(cl, er) //Service-check
						&& (chk_caid(er->caid, &cl->ctab) > 0))  //Caid-check
				{
					cs_cache_push_to_client(cl, er);
				}
			}
		}
	}

	//cacheex=3 mode: reverse push (reader->server)
	struct s_reader *rdr;
	for (rdr = first_active_reader; rdr; rdr = rdr->next) {
		struct s_client *cl = rdr->client;
		if (cl && er->cacheex_src != cl && rdr->cacheex == 3) { //send cache over reader
			if (rdr->ph.c_cache_push
				&& (!er->grp || (rdr->grp & er->grp)) //Group-check
				&& chk_srvid(cl, er) //Service-check
				&& chk_ctab(er->caid, &rdr->ctab))  //Caid-check
			{
				cs_cache_push_to_client(cl, er);
			}
		}
	}

	er->cacheex_pushed = 1;
	if (er->ecmcacheptr) er->ecmcacheptr->cacheex_pushed = 1;
}

static int8_t is_match_alias(struct s_client *cl, ECM_REQUEST *er)
{
	return cl && cl->account && cl->account->cacheex == 1 && is_cacheex_matcher_matching(NULL, er);
}

static int8_t match_alias(struct s_client *cl, ECM_REQUEST *er, ECM_REQUEST *ecm)
{
	if (cl && cl->account && cl->account->cacheex == 1) {
		struct s_cacheex_matcher *entry = is_cacheex_matcher_matching(ecm, er);
		if (entry) {
			int32_t diff = comp_timeb(&er->tps, &ecm->tps);
			if (diff > entry->valid_from && diff < entry->valid_to) {
				cs_debug_mask(D_TRACE, "cacheex-matching for: %04X:%06X:%04X:%04X:%04X:%02X = %04X:%06X:%04X:%04X:%04X:%02X valid %d/%d",
						entry->caid, entry->provid, entry->srvid, entry->pid, entry->chid, entry->ecmlen,
						entry->to_caid, entry->to_provid, entry->to_srvid, entry->to_pid, entry->to_chid, entry->to_ecmlen,
						entry->valid_from, entry->valid_to);
				return 1;
			}
		}
	}
	return 0;
}
#endif

/**
 * ecm cache
 **/
struct ecm_request_t *check_cwcache(ECM_REQUEST *er, struct s_client *cl)
{
	time_t now = time(NULL);
	//time_t timeout = now-(time_t)(cfg.ctimeout/1000)-CS_CACHE_TIMEOUT;
	time_t timeout = now-cfg.max_cache_time;
	struct ecm_request_t *ecm;
	uint64_t grp = cl?cl->grp:0;

	cs_readlock(&ecmcache_lock);
	for (ecm = ecmcwcache; ecm; ecm = ecm->next) {
		if (ecm->tps.time < timeout) {
			ecm = NULL;
			break;
		}

		if ((grp && ecm->grp && !(grp & ecm->grp)))
			continue;

#ifdef CS_CACHEEX
		if (!match_alias(cl, er, ecm)) {
#endif
			if (!(ecm->caid == er->caid || (ecm->ocaid && er->ocaid && ecm->ocaid == er->ocaid)))
				continue;

#ifdef CS_CACHEEX
			//CWs from csp have no ecms, so ecm->l=0. ecmd5 is invalid, so do not check!
			if (ecm->csp_hash != er->csp_hash)
				continue;

			if (ecm->l > 0 && memcmp(ecm->ecmd5, er->ecmd5, CS_ECMSTORESIZE))
				continue;

#else
			if (memcmp(ecm->ecmd5, er->ecmd5, CS_ECMSTORESIZE))
				continue;
#endif
#ifdef CS_CACHEEX
		}
#endif

		if (ecm->rc != E_99)
			break;
	}
	cs_readunlock(&ecmcache_lock);
	return ecm;
}

/*
 * write_ecm_request():
 */
static int32_t write_ecm_request(struct s_reader *rdr, ECM_REQUEST *er)
{
	add_job(rdr->client, ACTION_READER_ECM_REQUEST, (void*)er, sizeof(ECM_REQUEST));
	return 1;
}


/**
 * distributes found ecm-request to all clients with rc=99
 **/
static void distribute_ecm(ECM_REQUEST *er, int32_t rc)
{
	struct ecm_request_t *ecm;

	cs_readlock(&ecmcache_lock);
	for (ecm = ecmcwcache; ecm; ecm = ecm->next) {
		if (ecm != er && ecm->rc >= E_99 && ecm->ecmcacheptr == er)
			write_ecm_answer(er->selected_reader, ecm, rc, 0, er->cw, NULL);
	}
	cs_readunlock(&ecmcache_lock);
}

#ifdef CS_CACHEEX
void cs_add_cache(struct s_client *cl, ECM_REQUEST *er, int8_t csp)
{
	if (!cl)
		return;
	if (!csp && cl->reader && !cl->reader->cacheex==2) //from reader
		return;
	if (!csp && cl->account && !cl->account->cacheex==3) //from user
		return;
	if (!csp && !cl->reader && !cl->account) { //not active!
		cs_debug_mask(D_TRACE, "CACHEX received, but disabled for %s", username(cl));
		return;
	}

	uint8_t i, c;
	for (i = 0; i < 16; i += 4) {
		c = ((er->cw[i] + er->cw[i + 1] + er->cw[i + 2]) & 0xff);
		if (er->cw[i + 3] != c) {
			cs_ddump_mask(D_TRACE, er->cw, 16,
					"push received cw with chksum error from %s", csp?"csp":username(cl));
			return;
		}
	}


	uint16_t *lp;
	for (lp=(uint16_t *)er->ecm+(er->l>>2), er->checksum=0; lp>=(uint16_t *)er->ecm; lp--)
		er->checksum^=*lp;

	er->grp = cl->grp;
//	er->ocaid = er->caid;
	er->rc = E_CACHEEX;
	er->cacheex_src = cl;

	if (er->l > 0) {
		int32_t offset = 3;
		if ((er->caid >> 8) == 0x17)
			offset = 13;
		unsigned char md5tmp[MD5_DIGEST_LENGTH];
		memcpy(er->ecmd5, MD5(er->ecm+offset, er->l-offset, md5tmp), CS_ECMSTORESIZE);
		er->csp_hash = csp_ecm_hash(er);
		//csp has already initialized these hashcode
	}


	if( (er->caid & 0xFF00) == 0x600 && !er->chid )
		er->chid = (er->ecm[6]<<8)|er->ecm[7];

	struct ecm_request_t *ecm = check_cwcache(er, cl);

	if (!ecm) {
		cs_writelock(&ecmcache_lock);
		er->next = ecmcwcache;
		ecmcwcache = er;
		cs_writeunlock(&ecmcache_lock);

		er->selected_reader = cl->reader;

		cs_cache_push(er);  //cascade push!

		cl->cwcacheexgot++;
		if (cl->account)
			cl->account->cwcacheexgot++;
		first_client->cwcacheexgot++;

		cs_debug_mask(D_TRACE, "got pushed ECM %04X&%06X/%04X/%02X:%04X from %s",
			er->caid, er->prid, er->srvid, er->l, htons(er->checksum),  csp?"csp":username(cl));
	}
	else {
		if(er->rc < ecm->rc) {
			er->ecmcacheptr = ecm;
			er->client = ecm->client;
			write_ecm_answer(cl->reader, er, er->rc, er->rcEx, er->cw, ecm->msglog);
			//distribute_ecm(ecm, er->rc);
			//pthread_kill(timecheck_thread, OSCAM_SIGNAL_WAKEUP);
			cs_cache_push(er);  //cascade push!

			cs_add_cacheex_stats(cl, er->caid, er->srvid, er->prid, 1);
			cl->cwcacheexgot++;
			if (cl->account)
				cl->account->cwcacheexgot++;
			first_client->cwcacheexgot++;

			cs_debug_mask(D_TRACE, "replaced pushed ECM %04X&%06X/%04X/%02X:%04X from %s",
				er->caid, er->prid, er->srvid, er->l, htons(er->checksum),  csp?"csp":username(cl));
		}
		else
		{
			cs_debug_mask(D_TRACE, "ignored duplicate pushed ECM %04X&%06X/%04X/%02X:%04X from %s",
				er->caid, er->prid, er->srvid, er->l, htons(er->checksum),  csp?"csp":username(cl));
		}
		free_ecm(er);
	}
}
#endif


int32_t write_ecm_answer(struct s_reader * reader, ECM_REQUEST *er, int8_t rc, uint8_t rcEx, uchar *cw, char *msglog)
{
	int32_t i;
	uchar c;
	struct s_ecm_answer *ea = NULL, *ea_list, *ea_org = NULL;
	time_t now = time(NULL);

	if (er->tps.time <= now-(time_t)(cfg.ctimeout/1000+2)) { // drop real old answers, because er->parent is dropped !
#ifdef WITH_DEBUG
		if (er->tps.time <= now-(time_t)(cfg.max_cache_time))
			cs_log("dropped old reader answer rc=%d from %s time %ds",
				rc, reader?reader->label:"undef", (int32_t)(now-er->tps.time));
#endif
#ifdef WITH_LB
		send_reader_stat(reader, er, E_TIMEOUT);
#endif
		return 0;
	}

	for(ea_list = er->matching_rdr; reader && ea_list && !ea_org; ea_list = ea_list->next) {
		if (ea_list->reader == reader)
			ea_org = ea_list;
	}

	if (!ea_org)
		ea = cs_malloc(&ea, sizeof(struct s_ecm_answer), 0); //Free by ACTION_CLIENT_ECM_ANSWER!
	else
		ea = ea_org;

	if (cw)
		memcpy(ea->cw, cw, 16);

	if (msglog)
		memcpy(ea->msglog, msglog, MSGLOGSIZE);

	ea->rc = rc;
	ea->rcEx = rcEx;
	ea->reader = reader;
	ea->status |= REQUEST_ANSWERED;

	if (er->parent) {
		// parent is only set on reader->client->ecmtask[], but we want client->ecmtask[]
		er->rc = rc;
		er->idx = 0;
		er = er->parent;
	}

	ea->er = er;

	if (reader) {
        if (reader->disablecrccws == 0) {
           for (i=0; i<16; i+=4) {
               c=((ea->cw[i]+ea->cw[i+1]+ea->cw[i+2]) & 0xff);
               if (ea->cw[i+3]!=c) {
                   if (reader->dropbadcws) {
                      ea->rc = E_NOTFOUND;
                      ea->rcEx = E2_WRONG_CHKSUM;
                      break;
                   } else {
                      cs_debug_mask(D_TRACE, "notice: changed dcw checksum byte cw[%i] from %02x to %02x", i+3, ea->cw[i+3],c);
                      ea->cw[i+3]=c;
                   }
               }
          }
        }
        else {
              cs_debug_mask(D_TRACE, "notice: CW checksum check disabled");
        }
	}

	if (reader && ea->rc==E_FOUND) {
		/* CWL logging only if cwlogdir is set in config */
		if (cfg.cwlogdir != NULL)
			logCWtoFile(er, ea->cw);
	}

	int32_t res = 0;
	struct s_client *cl = er->client;
	if (cl && !cl->kill) {
		if (ea_org) { //duplicate for queue
			ea = cs_malloc(&ea, sizeof(struct s_ecm_answer), 0);
			memcpy(ea, ea_org, sizeof(struct s_ecm_answer));
		}
		add_job(cl, ACTION_CLIENT_ECM_ANSWER, ea, sizeof(struct s_ecm_answer));
		res = 1;
	} else { //client has disconnected. Distribute ecms to other waiting clients
		chk_dcw(NULL, ea);
		if (!ea_org)
			free(ea);
	}

	if (reader && rc == E_FOUND && reader->resetcycle > 0)
	{
		reader->resetcounter++;
		if (reader->resetcounter > reader->resetcycle) {
			reader->resetcounter = 0;
			cs_log("resetting reader %s resetcyle of %d ecms reached", reader->label, reader->resetcycle);
			reader->card_status = CARD_NEED_INIT;
#ifdef WITH_CARDREADER
			reader_reset(reader);
#endif
		}
	}

	return res;
}

ECM_REQUEST *get_ecmtask()
{
	ECM_REQUEST *er = NULL;
	struct s_client *cl = cur_client();
	if(!cl) return NULL;

	if(!cs_malloc(&er,sizeof(ECM_REQUEST), -1)) return NULL;

	cs_ftime(&er->tps);
	er->rc=E_UNHANDLED;
	er->client=cl;
	er->grp = cl->grp;
	//cs_log("client %s ECMTASK %d multi %d ctyp %d", username(cl), n, (ph[cl->ctyp].multi)?CS_MAXPENDING:1, cl->ctyp);

	return(er);
}

#ifdef WITH_LB
void send_reader_stat(struct s_reader *rdr, ECM_REQUEST *er, int32_t rc)
{
#ifdef CS_CACHEEX
	if (!rdr || rc>=E_99 || rdr->cacheex==1 || !rdr->client)
		return;
#else
	if (!rdr || rc>=E_99 || !rdr->client)
		return;
#endif
	struct timeb tpe;
	cs_ftime(&tpe);
	int32_t time = 1000*(tpe.time-er->tps.time)+tpe.millitm-er->tps.millitm;
	if (time < 1)
	        time = 1;

	add_stat(rdr, er, time, rc);
}
#endif

/**
 * Check for NULL CWs
 * Return them as "NOT FOUND"
 **/
static void checkCW(ECM_REQUEST *er)
{
	int8_t i;
	for (i=0;i<16;i++)
		if (er->cw[i]) return;
	er->rc = E_NOTFOUND;
}

static void add_cascade_data(struct s_client *client, ECM_REQUEST *er)
{
	if (!client->cascadeusers)
		client->cascadeusers = ll_create("cascade_data");
	LLIST *l = client->cascadeusers;
	LL_ITER it = ll_iter_create(l);
	time_t now = time(NULL);
	struct s_cascadeuser *cu;
	int8_t found=0;
	while ((cu=ll_iter_next(&it))) {
		if (er->caid==cu->caid && er->prid==cu->prid && er->srvid==cu->srvid) { //found it
			if (cu->time < now)
				cu->cwrate = now-cu->time;
			cu->time = now;
			found=1;
		}
		else if (cu->time+60 < now) //  old
			ll_iter_remove_data(&it);
	}
	if (!found) { //add it if not found
		cu = cs_malloc(&cu, sizeof(struct s_cascadeuser), 0);
		cu->caid = er->caid;
		cu->prid = er->prid;
		cu->srvid = er->srvid;
		cu->time = now;
		ll_append(l, cu);
	}
}

int32_t send_dcw(struct s_client * client, ECM_REQUEST *er)
{
	if (!client || client->kill || client->typ != 'c')
		return 0;

	static const char *stxt[]={"found", "cache1", "cache2", "cache3",
			"not found", "timeout", "sleeping",
			"fake", "invalid", "corrupt", "no card", "expdate", "disabled", "stopped"};
	static const char *stxtEx[16]={"", "group", "caid", "ident", "class", "chid", "queue", "peer", "sid", "", "", "", "", "", "", ""};
	static const char *stxtWh[16]={"", "user ", "reader ", "server ", "lserver ", "", "", "", "", "", "", "", "" ,"" ,"", ""};
	char sby[32]="", sreason[32]="", schaninfo[32]="";
	char erEx[32]="";
	char uname[38]="";
	char channame[32];
	struct timeb tpe;

	snprintf(uname,sizeof(uname)-1, "%s", username(client));

	if (er->rc < E_NOTFOUND)
		checkCW(er);

	struct s_reader *er_reader = er->selected_reader; //responding reader

	if (er_reader) {
		// add marker to reader if ECM_REQUEST was betatunneled
		if(er->ocaid)
			snprintf(sby, sizeof(sby)-1, " by %s(btun %04X)", er_reader->label, er->ocaid);
		else
			snprintf(sby, sizeof(sby)-1, " by %s", er_reader->label);
	}

	if (er->rc < E_NOTFOUND) er->rcEx=0;
	if (er->rcEx)
		snprintf(erEx, sizeof(erEx)-1, "rejected %s%s", stxtWh[er->rcEx>>4],
				stxtEx[er->rcEx&0xf]);

	if(cfg.mon_appendchaninfo)
		snprintf(schaninfo, sizeof(schaninfo)-1, " - %s", get_servicename(client, er->srvid, er->caid, channame));

	if(er->msglog[0])
		snprintf(sreason, sizeof(sreason)-1, " (%s)", er->msglog);

	cs_ftime(&tpe);
	client->cwlastresptime = 1000 * (tpe.time-er->tps.time) + tpe.millitm-er->tps.millitm;

#ifdef WEBIF
	cs_add_lastresponsetime(client, client->cwlastresptime,time((time_t*)0) ,er->rc); // add to ringbuffer
#endif

	if (er_reader){
		struct s_client *er_cl = er_reader->client;
		if(er_cl){
			er_cl->cwlastresptime = client->cwlastresptime;
#ifdef WEBIF
			cs_add_lastresponsetime(er_cl, client->cwlastresptime,time((time_t*)0) ,er->rc);
#endif
			er_cl->last_srvidptr=client->last_srvidptr;
		}
	}

#ifdef WEBIF
	if (er_reader) {
		if(er->rc == E_FOUND)
			cs_strncpy(client->lastreader, er_reader->label, sizeof(client->lastreader));
		else if (er->rc == E_CACHEEX)
			cs_strncpy(client->lastreader, "cache3", sizeof(client->lastreader));
		else if (er->rc < E_NOTFOUND)
			snprintf(client->lastreader, sizeof(client->lastreader)-1, "%s (cache)", er_reader->label);
		else
			cs_strncpy(client->lastreader, stxt[er->rc], sizeof(client->lastreader));
	}
	else
		cs_strncpy(client->lastreader, stxt[er->rc], sizeof(client->lastreader));
#endif
	client->last = time((time_t*)0);

	//cs_debug_mask(D_TRACE, "CHECK rc=%d er->cacheex_src=%s", er->rc, username(er->cacheex_src));
	switch(er->rc) {
		case E_FOUND:
					client->cwfound++;
			                client->account->cwfound++;
					first_client->cwfound++;
					break;

		case E_CACHE1:
		case E_CACHE2:
		case E_CACHEEX:
			client->cwcache++;
			client->account->cwcache++;
			first_client->cwcache++;
#ifdef CS_CACHEEX
			if (er->cacheex_src) {
				er->cacheex_src->cwcacheexhit++;
				if (er->cacheex_src->account)
					er->cacheex_src->account->cwcacheexhit++;
				first_client->cwcacheexhit++;
			}
#endif
			break;

		case E_NOTFOUND:
		case E_CORRUPT:
		case E_NOCARD:
			if (er->rcEx) {
				client->cwignored++;
				client->account->cwignored++;
				first_client->cwignored++;
			} else {
				client->cwnot++;
				client->account->cwnot++;
				first_client->cwnot++;
                        }
			break;

		case E_TIMEOUT:
			client->cwtout++;
			client->account->cwtout++;
			first_client->cwtout++;
			break;

		default:
			client->cwignored++;
			client->account->cwignored++;
			first_client->cwignored++;
	}

#ifdef CS_ANTICASC
	ac_chk(client, er, 1);
#endif

	int32_t is_fake = 0;
	if (er->rc==E_FAKE) {
		is_fake = 1;
		er->rc=E_FOUND;
	}

	if (cfg.double_check && er->rc < E_NOTFOUND) {
	  if (er->checked == 0) {//First CW, save it and wait for next one
	    er->checked = 1;
	    er->origin_reader = er->selected_reader;
	    memcpy(er->cw_checked, er->cw, sizeof(er->cw));
	    cs_log("DOUBLE CHECK FIRST CW by %s idx %d cpti %d", er->origin_reader->label, er->idx, er->msgid);
	  }
	  else if (er->origin_reader != er->selected_reader) { //Second (or third and so on) cw. We have to compare
	    if (memcmp(er->cw_checked, er->cw, sizeof(er->cw)) == 0) {
	    	er->checked++;
	    	cs_log("DOUBLE CHECKED! %d. CW by %s idx %d cpti %d", er->checked, er->selected_reader->label, er->idx, er->msgid);
	    }
	    else {
	    	cs_log("DOUBLE CHECKED NONMATCHING! %d. CW by %s idx %d cpti %d", er->checked, er->selected_reader->label, er->idx, er->msgid);
	    }
	  }

	  if (er->checked < 2) { //less as two same cw? mark as pending!
	    er->rc = E_UNHANDLED;
	    return 0;
	  }
	}

	ph[client->ctyp].send_dcw(client, er);

	add_cascade_data(client, er);

	if (is_fake)
		er->rc = E_FAKE;

	if (er->reader_avail == 1) {
		cs_log("%s (%04X&%06X/%04X/%02X:%04X): %s (%d ms)%s %s%s",
			uname, er->caid, er->prid, er->srvid, er->l, htons(er->checksum),
			er->rcEx?erEx:stxt[er->rc], client->cwlastresptime, sby, schaninfo, sreason);
	} else {
		cs_log("%s (%04X&%06X/%04X/%02X:%04X): %s (%d ms)%s (%d of %d)%s%s",
			uname, er->caid, er->prid, er->srvid, er->l, htons(er->checksum),
			er->rcEx?erEx:stxt[er->rc], client->cwlastresptime, sby, er->reader_count, er->reader_avail, schaninfo, sreason);
	}

	cs_ddump_mask (D_ATR, er->cw, 16, "cw:");

#ifdef ARM
	if(!er->rc &&cfg.enableled == 1) cs_switch_led(LED2, LED_BLINK_OFF);
#endif

#ifdef QBOXHD
	if(cfg.enableled == 2){
    if (er->rc < E_NOTFOUND) {
        qboxhd_led_blink(QBOXHD_LED_COLOR_GREEN, QBOXHD_LED_BLINK_MEDIUM);
    } else if (er->rc <= E_STOPPED) {
        qboxhd_led_blink(QBOXHD_LED_COLOR_RED, QBOXHD_LED_BLINK_MEDIUM);
    }
  }
#endif

	return 0;
}

/**
 * sends the ecm request to the readers
 * ECM_REQUEST er : the ecm
 * er->stage: 0 = no reader asked yet
 *            2 = ask only local reader (skipped without preferlocalcards)
 *            3 = ask any non fallback reader
 *            4 = ask fallback reader
 **/
static void request_cw(ECM_REQUEST *er)
{
	struct s_ecm_answer *ea;
	int8_t sent = 0;

	if (er->stage >= 4) return;

	while (1) {
		er->stage++;

#ifndef CS_CACHEEX
		if (er->stage == 1)
			er->stage++;
#endif
		if (er->stage == 2 && !cfg.preferlocalcards)
			er->stage++;

		for(ea = er->matching_rdr; ea; ea = ea->next) {

#ifdef CS_CACHEEX
			if (er->stage == 1) {
				// Cache-Echange
				if ((ea->status & REQUEST_SENT) ||
						(ea->status & (READER_CACHEEX|READER_ACTIVE)) != (READER_CACHEEX|READER_ACTIVE))
					continue;
			} else
#endif
			if (er->stage == 2) {
				// only local reader
				if ((ea->status & REQUEST_SENT) ||
						(ea->status & (READER_ACTIVE|READER_FALLBACK|READER_LOCAL)) != (READER_ACTIVE|READER_LOCAL))
					continue;
			} else if (er->stage == 3) {
				// any non fallback reader not asked yet
				if ((ea->status & REQUEST_SENT) ||
						(ea->status & (READER_ACTIVE|READER_FALLBACK)) != READER_ACTIVE)
					continue;
			} else {
				// only fallbacks
				// always send requests, regardless if asked
				if ((ea->status & READER_ACTIVE) != READER_ACTIVE)
					continue;
			}
			struct s_reader *rdr = ea->reader;
			cs_debug_mask(D_TRACE, "request_cw stage=%d to reader %s ecm=%04X", er->stage, rdr?rdr->label:"", htons(er->checksum));
			write_ecm_request(ea->reader, er);
			ea->status |= REQUEST_SENT;

			//set sent=1 only if reader is active/connected. If not, switch to next stage!			
			if (!sent && rdr) {
				struct s_client *rcl = rdr->client;
				if(rcl){
					if (rcl->typ=='r' && rdr->card_status==CARD_INSERTED)
						sent = 1;
					else if (rcl->typ=='p' && (rdr->card_status==CARD_INSERTED ||rdr->tcp_connected))
						sent = 1;
				}
			}
		}
		if (sent || er->stage >= 4)
			break;
	}
}

static void chk_dcw(struct s_client *cl, struct s_ecm_answer *ea)
{
	if (!ea || !ea->er)
		return;

	ECM_REQUEST *ert = ea->er;
	struct s_ecm_answer *ea_list;
	struct s_reader *eardr = ea->reader;
	if(!ert)
		return;

	if (eardr) {
		cs_debug_mask(D_TRACE, "ecm answer from reader %s for ecm %04X rc=%d", eardr->label, htons(ert->checksum), ea->rc);
		//cs_ddump_mask(D_TRACE, ea->cw, sizeof(ea->cw), "received cw from %s caid=%04X srvid=%04X hash=%08X",
		//		eardr->label, ert->caid, ert->srvid, ert->csp_hash);
		//cs_ddump_mask(D_TRACE, ert->ecm, ert->l, "received cw for ecm from %s caid=%04X srvid=%04X hash=%08X",
		//		eardr->label, ert->caid, ert->srvid, ert->csp_hash);
	}

	ea->status |= REQUEST_ANSWERED;

	if (eardr) {
		//Update reader stats:
		if (ea->rc == E_FOUND) {
			eardr->ecmsok++;
#ifdef CS_CACHEEX
			struct s_client *eacl = eardr?eardr->client:NULL; //reader is null on timeouts
			if (eardr->cacheex == 1 && !ert->cacheex_done && eacl) {
				eacl->cwcacheexgot++;
				cs_add_cacheex_stats(eacl, ea->er->caid, ea->er->srvid, ea->er->prid, 1);
				first_client->cwcacheexgot++;
			}
#endif
		}
		else if (ea->rc == E_NOTFOUND)
			eardr->ecmsnok++;

		//Reader ECMs Health Try (by Pickser)
		if (eardr->ecmsok != 0 || eardr->ecmsnok != 0)
		{
			eardr->ecmshealthok = ((double) eardr->ecmsok / (eardr->ecmsok + eardr->ecmsnok)) * 100;
			eardr->ecmshealthnok = ((double) eardr->ecmsnok / (eardr->ecmsok + eardr->ecmsnok)) * 100;
		}

		//Reader Dynamic Loadbalancer Try (by Pickser)
		/*
		 * todo: config-option!
		 *
#ifdef WITH_LB
		if (eardr->ecmshealthok >= 75) {
			eardr->lb_weight = 100;
		} else if (eardr->ecmshealthok >= 50) {
			eardr->lb_weight = 75;
		} else if (eardr->ecmshealthok >= 25) {
			eardr->lb_weight = 50;
		} else {
			eardr->lb_weight = 25;
		}
#endif
		*/
	}

	if (ert->rc<E_99) {
#ifdef WITH_LB
		send_reader_stat(eardr, ert, ea->rc);
#endif
		return; // already done
	}

	int32_t reader_left = 0, local_left = 0;
#ifdef CS_CACHEEX
	int8_t cacheex_left = 0;
#endif

	switch (ea->rc) {
		case E_FOUND:
		case E_CACHE2:
		case E_CACHE1:
		case E_CACHEEX:
			memcpy(ert->cw, ea->cw, 16);
			ert->rcEx=0;
			ert->rc = ea->rc;
			ert->selected_reader = eardr;
			break;
		case E_TIMEOUT:
			ert->rc = E_TIMEOUT;
			ert->rcEx = 0;
			break;
		case E_NOTFOUND:
			ert->rcEx=ea->rcEx;
			cs_strncpy(ert->msglog, ea->msglog, sizeof(ert->msglog));
			ert->selected_reader = eardr;
#ifdef CS_CACHEEX
			uchar has_cacheex = 0;
#endif
			for(ea_list = ert->matching_rdr; ea_list; ea_list = ea_list->next) {
#ifdef CS_CACHEEX
				if (((ea_list->status & READER_CACHEEX)) == READER_CACHEEX)
					has_cacheex = 1;
				if (((ea_list->status & (REQUEST_SENT|REQUEST_ANSWERED|READER_CACHEEX|READER_ACTIVE)) == (REQUEST_SENT|READER_CACHEEX|READER_ACTIVE)))
					cacheex_left++;
#endif
				if (((ea_list->status & (REQUEST_SENT|REQUEST_ANSWERED|READER_LOCAL|READER_ACTIVE)) == (REQUEST_SENT|READER_LOCAL|READER_ACTIVE)))
					local_left++;
				if (((ea_list->status & (REQUEST_ANSWERED|READER_ACTIVE)) == (READER_ACTIVE)))
					reader_left++;
			}

#ifdef CS_CACHEEX
			if (has_cacheex && !cacheex_left && !ert->cacheex_done) {
				ert->cacheex_done = 1;
				request_cw(ert);
			} else
#endif
			if (cfg.preferlocalcards && !local_left && !ert->locals_done) {
				ert->locals_done = 1;
				request_cw(ert);
			}

			break;
		default:
			cs_log("unexpected ecm answer rc=%d.", ea->rc);
			return;
			break;
	}

	if (ea->rc == E_NOTFOUND && !reader_left) {
		// no more matching reader
		ert->rc=E_NOTFOUND; //so we set the return code
	}

#ifdef WITH_LB
	send_reader_stat(eardr, ert, ea->rc);
#endif

#ifdef CS_CACHEEX
	if (ea->rc < E_NOTFOUND)
		cs_cache_push(ert);
#endif

	if (ert->rc < E_99) {
		if (cl) send_dcw(cl, ert);
		distribute_ecm(ert, (ert->rc == E_FOUND)?E_CACHE2:ert->rc);
	}

	return;
}

uint32_t chk_provid(uchar *ecm, uint16_t caid) {
	int32_t i, len, descriptor_length = 0;
	uint32_t provid = 0;

	switch(caid >> 8) {
		case 0x01:
			// seca
			provid = b2i(2, ecm+3);
			break;

		case 0x05:
			// viaccess
			i = (ecm[4] == 0xD2) ? ecm[5]+2 : 0;  // skip d2 nano
			if((ecm[5+i] == 3) && ((ecm[4+i] == 0x90) || (ecm[4+i] == 0x40)))
				provid = (b2i(3, ecm+6+i) & 0xFFFFF0);

			i = (ecm[6] == 0xD2) ? ecm[7]+2 : 0;  // skip d2 nano long ecm
			if((ecm[7+i] == 7) && ((ecm[6+i] == 0x90) || (ecm[6+i] == 0x40)))
				provid = (b2i(3, ecm+8+i) & 0xFFFFF0);

			break;

		case 0x0D:
			// cryptoworks
			len = (((ecm[1] & 0xf) << 8) | ecm[2])+3;
			for(i=8; i<len; i+=descriptor_length+2) {
				descriptor_length = ecm[i+1];
				if (ecm[i] == 0x83) {
					provid = (uint32_t)ecm[i+2] & 0xFE;
					break;
				}
			}
			break;

#ifdef WITH_LB
		default:
			for (i=0;i<CS_MAXCAIDTAB;i++) {
                            uint16_t tcaid = cfg.lb_noproviderforcaid.caid[i];
                            if (!tcaid) break;
                            if (tcaid == caid) {
                        	provid = 0;
                        	break;
                            }
                            if (tcaid < 0x0100 && (caid >> 8) == tcaid) {
                                provid = 0;
                                break;
                            }
			}
#endif
	}
	return(provid);
}

#ifdef IRDETO_GUESSING
void guess_irdeto(ECM_REQUEST *er)
{
  uchar  b3;
  int32_t    b47;
  //uint16_t chid;
  struct s_irdeto_quess *ptr;

  b3  = er->ecm[3];
  ptr = cfg.itab[b3];
  if( !ptr ) {
    cs_debug_mask(D_TRACE, "unknown irdeto byte 3: %02X", b3);
    return;
  }
  b47  = b2i(4, er->ecm+4);
  //chid = b2i(2, er->ecm+6);
  //cs_debug_mask(D_TRACE, "ecm: b47=%08X, ptr->b47=%08X, ptr->caid=%04X", b47, ptr->b47, ptr->caid);
  while( ptr )
  {
    if( b47==ptr->b47 )
    {
      if( er->srvid && (er->srvid!=ptr->sid) )
      {
        cs_debug_mask(D_TRACE, "sid mismatched (ecm: %04X, guess: %04X), wrong oscam.ird file?",
                  er->srvid, ptr->sid);
        return;
      }
      er->caid=ptr->caid;
      er->srvid=ptr->sid;
      er->chid=(uint16_t)ptr->b47;
//      cs_debug_mask(D_TRACE, "quess_irdeto() found caid=%04X, sid=%04X, chid=%04X",
//               er->caid, er->srvid, er->chid);
      return;
    }
    ptr=ptr->next;
  }
}
#endif

void convert_to_beta(struct s_client *cl, ECM_REQUEST *er, uint16_t caidto)
{
	static uchar headerN3[10] = {0xc7, 0x00, 0x00, 0x00, 0x01, 0x10, 0x10, 0x00, 0x87, 0x12};
	static uchar headerN2[10] = {0xc9, 0x00, 0x00, 0x00, 0x01, 0x10, 0x10, 0x00, 0x48, 0x12};

	er->ocaid = er->caid;
	er->caid = caidto;
	er->prid = 0;
	er->l = er->ecm[2] + 3;

	memmove(er->ecm + 13, er->ecm + 3, er->l - 3);

	if (er->l > 0x88) {
		memcpy(er->ecm + 3, headerN3, 10);

		if (er->ecm[0] == 0x81)
			er->ecm[12] += 1;

		er->ecm[1]=0x70;
	}
	else
		memcpy(er->ecm + 3, headerN2, 10);

	er->l += 10;
	er->ecm[2] = er->l - 3;
	er->btun = 1;

	cl->cwtun++;
	cl->account->cwtun++;
	first_client->cwtun++;

	cs_debug_mask(D_TRACE, "ECM converted from: 0x%X to BetaCrypt: 0x%X for service id:0x%X",
					er->ocaid, caidto, er->srvid);
}


void cs_betatunnel(ECM_REQUEST *er)
{
	int32_t n;
	struct s_client *cl = cur_client();
	uint32_t mask_all = 0xFFFF;

	TUNTAB *ttab;
	ttab = &cl->ttab;

	if (er->caid>>8 == 0x18)
		cs_ddump_mask(D_TRACE, er->ecm, 13, "betatunnel? ecmlen=%d", er->l);

	for (n = 0; n<ttab->n; n++) {
		if ((er->caid==ttab->bt_caidfrom[n]) && ((er->srvid==ttab->bt_srvid[n]) || (ttab->bt_srvid[n])==mask_all)) {

			convert_to_beta(cl, er, ttab->bt_caidto[n]);

			return;
		}
	}
}

static void guess_cardsystem(ECM_REQUEST *er)
{
  uint16_t last_hope=0;

  // viaccess - check by provid-search
  if( (er->prid=chk_provid(er->ecm, 0x500)) )
    er->caid=0x500;

  // nagra
  // is ecm[1] always 0x30 ?
  // is ecm[3] always 0x07 ?
  if ((er->ecm[6]==1) && (er->ecm[4]==er->ecm[2]-2))
    er->caid=0x1801;

  // seca2 - very poor
  if ((er->ecm[8]==0x10) && ((er->ecm[9]&0xF1)==1))
    last_hope=0x100;

  // is cryptoworks, but which caid ?
  if ((er->ecm[3]==0x81) && (er->ecm[4]==0xFF) &&
      (!er->ecm[5]) && (!er->ecm[6]) && (er->ecm[7]==er->ecm[2]-5))
    last_hope=0xd00;

#ifdef IRDETO_GUESSING
  if (!er->caid && er->ecm[2]==0x31 && er->ecm[0x0b]==0x28)
    guess_irdeto(er);
#endif

  if (!er->caid)    // guess by len ..
    er->caid=len4caid[er->ecm[2]+3];

  if (!er->caid)
    er->caid=last_hope;
}

void get_cw(struct s_client * client, ECM_REQUEST *er)
{
	int32_t i, j, m;
	time_t now = time((time_t*)0);
	uint32_t line = 0;

	er->client = client;
	client->lastecm = now;

	if (client == first_client || !client ->account || client->account == first_client->account) {
		//DVBApi+serial is allowed to request anonymous accounts:
		int16_t lt = ph[client->ctyp].listenertype;
		if (lt != LIS_DVBAPI && lt != LIS_SERIAL) {
			er->rc = E_INVALID;
			er->rcEx = E2_GLOBAL;
			snprintf(er->msglog, sizeof(er->msglog), "invalid user account %s", username(client));
		}
	}

	if (!client->grp) {
		er->rc = E_INVALID;
		er->rcEx = E2_GROUP;
		snprintf(er->msglog, sizeof(er->msglog), "invalid user group %s", username(client));
	}


	if (!er->caid)
		guess_cardsystem(er);

	/* Quickfix Area */

	if( (er->caid & 0xFF00) == 0x600 && !er->chid )
		er->chid = (er->ecm[6]<<8)|er->ecm[7];

	// quickfix for 0100:000065
	if (er->caid == 0x100 && er->prid == 0x65 && er->srvid == 0)
		er->srvid = 0x0642;

	// Quickfixes for Opticum/Globo HD9500
	// Quickfix for 0500:030300
	if (er->caid == 0x500 && er->prid == 0x030300)
		er->prid = 0x030600;

	// Quickfix for 0500:D20200
	if (er->caid == 0x500 && er->prid == 0xD20200)
		er->prid = 0x030600;

	//betacrypt ecm with nagra header
	if (er->caid == 0x1702 && er->l == 0x89 && er->ecm[3] == 0x07 && er->ecm[4] == 0x84)
		er->caid = 0x1833;

	//Ariva quickfix (invalid nagra provider)
	if (((er->caid & 0xFF00) == 0x1800) && er->prid > 0x00FFFF) er->prid=0;

	//Check for invalid provider, extract provider out of ecm:
	uint32_t prid = chk_provid(er->ecm, er->caid);
	if (!er->prid)
		er->prid = prid;
	else
	{
		if (prid && prid != er->prid) {
			cs_debug_mask(D_TRACE, "wrong provider found: %04X:%06X to %04X:%06X",er->caid, er->prid, er->caid, prid);
			//er->prid = prid;
			er->rc = E_INVALID;
			er->rcEx = E2_IDENT;
			snprintf( er->msglog, MSGLOGSIZE, "Wrong provider found in ECM" );
		}
	}

	// Set providerid for newcamd clients if none is given
	if( (!er->prid) && client->ncd_server ) {
		int32_t pi = client->port_idx;
		if( pi >= 0 && cfg.ncd_ptab.nports && cfg.ncd_ptab.nports >= pi )
			er->prid = cfg.ncd_ptab.ports[pi].ftab.filts[0].prids[0];
	}

	// CAID not supported or found
	if (!er->caid) {
		er->rc = E_INVALID;
		er->rcEx = E2_CAID;
		snprintf( er->msglog, MSGLOGSIZE, "CAID not supported or found" );
	}

	// user expired
	if(client->expirationdate && client->expirationdate < client->lastecm)
		er->rc = E_EXPDATE;

	// out of timeframe
	if(client->allowedtimeframe[0] && client->allowedtimeframe[1]) {
		struct tm acttm;
		localtime_r(&now, &acttm);
		int32_t curtime = (acttm.tm_hour * 60) + acttm.tm_min;
		int32_t mintime = client->allowedtimeframe[0];
		int32_t maxtime = client->allowedtimeframe[1];
		if(!((mintime <= maxtime && curtime > mintime && curtime < maxtime) || (mintime > maxtime && (curtime > mintime || curtime < maxtime)))) {
			er->rc = E_EXPDATE;
		}
		cs_debug_mask(D_TRACE, "Check Timeframe - result: %d, start: %d, current: %d, end: %d\n",er->rc, mintime, curtime, maxtime);
	}

	// user disabled
	if(client->disabled != 0) {
		if (client->failban & BAN_DISABLED){
			cs_add_violation(client, client->account->usr);
			cs_disconnect_client(client);
		}
		er->rc = E_DISABLED;
	}

	if (!chk_global_whitelist(er, &line)) {
		cs_debug_mask(D_TRACE, "whitelist filtered: %s (%04X&%06X/%04X/%02X:%04X) line %d",
					username(client), er->caid, er->prid, er->srvid, er->l, htons(er->checksum),
					line);
		er->rc = E_INVALID;
	}

	// rc<100 -> ecm error
	if (er->rc >= E_UNHANDLED) {
		m = er->caid;
		i = er->srvid;

		if ((i != client->last_srvid) || (!client->lastswitch)) {
			if(cfg.usrfileflag)
				cs_statistics(client);
			client->lastswitch = now;
		}

		// user sleeping
		if ((client->tosleep) && (now - client->lastswitch > client->tosleep)) {

			if (client->failban & BAN_SLEEPING) {
				cs_add_violation(client, client->account->usr);
				cs_disconnect_client(client);
			}

			if (client->c35_sleepsend != 0) {
				er->rc = E_STOPPED; // send stop command CMD08 {00 xx}
			} else {
				er->rc = E_SLEEPING;
			}
		}

		client->last_srvid = i;
		client->last_caid = m;

		int32_t ecm_len = (((er->ecm[1] & 0x0F) << 8) | er->ecm[2]) + 3;

		for (j = 0; (j < 6) && (er->rc >= E_UNHANDLED); j++)
		{
			switch(j) {

				case 0:
					// fake (uniq)
					if (client->dup)
						er->rc = E_FAKE;
					break;

				case 1:
					// invalid (caid)
					if (!chk_bcaid(er, &client->ctab)) {
						er->rc = E_INVALID;
						er->rcEx = E2_CAID;
						snprintf( er->msglog, MSGLOGSIZE, "invalid caid %x",er->caid );
						}
					break;

				case 2:
					// invalid (srvid)
					if (!chk_srvid(client, er))
					{
						er->rc = E_INVALID;
					    snprintf( er->msglog, MSGLOGSIZE, "invalid SID" );
					}

					break;

				case 3:
					// invalid (ufilters)
					if (!chk_ufilters(er))
						er->rc = E_INVALID;
					break;

				case 4:
					// invalid (sfilter)
					if (!chk_sfilter(er, ph[client->ctyp].ptab))
						er->rc = E_INVALID;
					break;

				case 5:
					// corrupt
					if( (i = er->l - ecm_len) ) {
						if (i > 0) {
							cs_debug_mask(D_TRACE, "warning: ecm size adjusted from 0x%X to 0x%X", er->l, ecm_len);
							er->l = ecm_len;
						}
						else
							er->rc = E_CORRUPT;
					}
					break;
			}
		}
	}

	//Schlocke: above checks could change er->rc so
	if (er->rc >= E_UNHANDLED) {
		/*BetaCrypt tunneling
		 *moved behind the check routines,
		 *because newcamd ECM will fail
		 *if ECM is converted before
		 */
		if (client->ttab.n)
			cs_betatunnel(er);

		// ignore ecm ...
		int32_t offset = 3;
		// ... and betacrypt header for cache md5 calculation
		if ((er->caid >> 8) == 0x17)
			offset = 13;
		unsigned char md5tmp[MD5_DIGEST_LENGTH];
		// store ECM in cache
		memcpy(er->ecmd5, MD5(er->ecm+offset, er->l-offset, md5tmp), CS_ECMSTORESIZE);
#ifdef CS_CACHEEX
		er->csp_hash = csp_ecm_hash(er);
#endif

#ifdef CS_ANTICASC
		ac_chk(client, er, 0);
#endif
	}

	struct s_ecm_answer *ea, *prv = NULL;
#ifdef CS_CACHEEX
	if(er->rc >= E_99 && !is_match_alias(client, er)) {
#else
	if(er->rc >= E_99) {
#endif
		er->reader_avail=0;
		struct s_reader *rdr;

		for (rdr=first_active_reader; rdr ; rdr=rdr->next) {
			int8_t match = matching_reader(er, rdr);
#ifdef WITH_LB
			//if this reader does not match, check betatunnel for it
			if (!match && cfg.lb_auto_betatunnel) {
				uint16_t caid = get_betatunnel_caid_to(er->caid);
				if (caid) {
					uint16_t save_caid = er->caid;
					er->caid = caid;
					match = matching_reader(er, rdr); //matching
					er->caid = save_caid;
				}
			}
#endif
			if (match) {
				cs_malloc(&ea, sizeof(struct s_ecm_answer), 0);
				ea->reader = rdr;
				if (prv)
					prv->next = ea;
				else
					er->matching_rdr=ea;

				prv = ea;

				ea->status = READER_ACTIVE;
				if (!(rdr->typ & R_IS_NETWORK))
					ea->status |= READER_LOCAL;
#ifdef CS_CACHEEX
				else if (rdr->cacheex == 1)
					ea->status |= READER_CACHEEX;
#endif
				else if (rdr->fallback)
					ea->status |= READER_FALLBACK;

#ifdef WITH_LB
				if (cfg.lb_mode || !rdr->fallback)
#else
				if (!rdr->fallback)
#endif
					er->reader_avail++;
			}
		}

#ifdef WITH_LB
		if (cfg.lb_mode && er->reader_avail) {
			cs_debug_mask(D_TRACE, "requesting client %s best reader for %04X/%06X/%04X",
				username(client), er->caid, er->prid, er->srvid);
			get_best_reader(er);
		}
#endif

		int32_t fallback_reader_count = 0;
		er->reader_count = 0;
		for (ea = er->matching_rdr; ea; ea = ea->next) {
			if (ea->status & READER_ACTIVE) {
				if (!(ea->status & READER_FALLBACK))
					er->reader_count++;
				else
					fallback_reader_count++;
			}
		}

		if ((er->reader_count + fallback_reader_count) == 0) { //no reader -> not found
			er->rc = E_NOTFOUND;
			if (!er->rcEx)
				er->rcEx = E2_GROUP;
			snprintf(er->msglog, MSGLOGSIZE, "no matching reader");
		}
	}

	//we have to go through matching_reader() to check services!
	struct ecm_request_t *ecm;
	if (er->rc == E_UNHANDLED) {
		ecm = check_cwcache(er, client);

		if (ecm) {
			if (ecm->rc < E_99) {
				memcpy(er->cw, ecm->cw, 16);
				er->selected_reader = ecm->selected_reader;
				er->rc = (ecm->rc == E_FOUND)?E_CACHE1:ecm->rc;
			} else {
				er->ecmcacheptr = ecm;
				er->rc = E_99;
			}
#ifdef CS_CACHEEX
			er->cacheex_src = ecm->cacheex_src;
#endif
		} else
			er->rc = E_UNHANDLED;
	}

#ifdef CS_CACHEEX
	int8_t cacheex = client->account?client->account->cacheex:0;

	if ((cacheex == 1 || cfg.csp_wait_time) && er->rc == E_UNHANDLED) { //not found in cache, so wait!
		uint32_t max_wait = (cacheex == 1)?cfg.cacheex_wait_time:cfg.csp_wait_time;
		while (max_wait > 0 && !client->kill) {
			cs_sleepms(50);
			max_wait -= 50;
			ecm = check_cwcache(er, client);
			if (ecm) {
				if (ecm->rc < E_99) { //Found cache!
					memcpy(er->cw, ecm->cw, 16);
					er->selected_reader = ecm->selected_reader;
					er->rc = (ecm->rc == E_FOUND)?E_CACHE1:ecm->rc;
				} else { //Found request!
					er->ecmcacheptr = ecm;
					er->rc = E_99;
				}
				er->cacheex_src = ecm->cacheex_src;
				break;
			}
		}
	}
#endif

	if (er->rc >= E_99) {
#ifdef CS_CACHEEX
		if (cacheex == 0 || er->rc == E_99) { //Cacheex should not add to the ecmcache:
#endif
			cs_writelock(&ecmcache_lock);
			er->next = ecmcwcache;
			ecmcwcache = er;
			ecmcwcache_size++;
			cs_writeunlock(&ecmcache_lock);

			if (er->rc == E_UNHANDLED) {
				ecm = check_cwcache(er, client);
				if (ecm && ecm != er) {
					er->rc = E_99;
					er->ecmcacheptr = ecm;
#ifdef CS_CACHEEX
					er->cacheex_src = ecm->cacheex_src;
#endif
				}
			}
#ifdef CS_CACHEEX
		}
#endif
	}

	uint16_t *lp;
	for (lp=(uint16_t *)er->ecm+(er->l>>2), er->checksum=0; lp>=(uint16_t *)er->ecm; lp--)
		er->checksum^=*lp;

	if (er->rc < E_99) {
#ifdef CS_CACHEEX
		if (cfg.delay && cacheex == 0) //No delay on cacheexchange!
			cs_sleepms(cfg.delay);

		if (cacheex == 1 && er->rc < E_NOTFOUND) {
			cs_add_cacheex_stats(client, er->caid, er->srvid, er->prid, 0);
			client->cwcacheexpush++;
			if (client->account)
				client->account->cwcacheexpush++;
			first_client->cwcacheexpush++;
		}
#else
		if (cfg.delay)
			cs_sleepms(cfg.delay);
#endif
		send_dcw(client, er);
		free_ecm(er);
		return; //ECM found/not found/error/invalid
	}

	if (er->rc == E_99) {
		er->stage=4;
		if(timecheck_client){
			pthread_mutex_lock(&timecheck_client->thread_lock);
			if(timecheck_client->thread_active == 2)
				pthread_kill(timecheck_client->thread, OSCAM_SIGNAL_WAKEUP);
			pthread_mutex_unlock(&timecheck_client->thread_lock);
		}
		return; //ECM already requested / found in ECM cache
	}

#ifdef CS_CACHEEX
	//er->rc == E_UNHANDLED
	//Cache Exchange never request cws from readers!
	if (cacheex == 1) {
		er->rc = E_NOTFOUND;
		er->rcEx = E2_OFFLINE;
		send_dcw(client, er);
		free_ecm(er);
		return;
	}
#endif

	er->rcEx = 0;
	request_cw(er);
	if(timecheck_client){
		pthread_mutex_lock(&timecheck_client->thread_lock);
		if(timecheck_client->thread_active == 2)
			pthread_kill(timecheck_client->thread, OSCAM_SIGNAL_WAKEUP);
		pthread_mutex_unlock(&timecheck_client->thread_lock);
	}
}

void do_emm(struct s_client * client, EMM_PACKET *ep)
{
	char *typtext[]={"unknown", "unique", "shared", "global"};
	char tmp[17];

	struct s_reader *aureader = NULL;
	cs_ddump_mask(D_EMM, ep->emm, ep->l, "emm:");

	LL_ITER itr = ll_iter_create(client->aureader_list);
	while ((aureader = ll_iter_next(&itr))) {
		if (!aureader->enable)
			continue;

		uint16_t caid = b2i(2, ep->caid);
		uint32_t provid = b2i(4, ep->provid);

		if (aureader->audisabled) {
			cs_debug_mask(D_EMM, "AU is disabled for reader %s", aureader->label);
			/* we have to write the log for blocked EMM here because
	  		 this EMM never reach the reader module where the rest
			 of EMM log is done. */
			if (aureader->logemm & 0x10)  {
				cs_log("%s emmtype=%s, len=%d, idx=0, cnt=1: audisabled (0 ms) by %s",
						client->account->usr,
						typtext[ep->type],
						ep->emm[2],
						aureader->label);
			}
			continue;
		}

		if (!(aureader->grp & client->grp)) {
			cs_debug_mask(D_EMM, "skip emm reader %s group mismatch", aureader->label);
			continue;
		}

		//TODO: provider possibly not set yet, this is done in get_emm_type()
		if (!emm_reader_match(aureader, caid, provid))
			continue;

		struct s_cardsystem *cs = NULL;

		if (aureader->typ & R_IS_CASCADING) { // network reader (R_CAMD35 R_NEWCAMD R_CS378X R_CCCAM)
			if (!aureader->ph.c_send_emm) // no emm support
				continue;

			cs = get_cardsystem_by_caid(caid);
			if (!cs) {
				cs_debug_mask(D_EMM, "unable to find cardsystem for caid %04X, reader %s", caid, aureader->label);
				continue;
			}
		} else { // local reader
			if (aureader->csystem.active)
				cs=&aureader->csystem;
		}

		if (cs && cs->get_emm_type) {
			if(!cs->get_emm_type(ep, aureader)) {
				cs_debug_mask(D_EMM, "emm skipped, get_emm_type() returns error, reader %s", aureader->label);
				client->emmnok++;
				if (client->account)
					client->account->emmnok++;
				first_client->emmnok++;
				continue;
			}
		}

		cs_debug_mask(D_EMM, "emmtype %s. Reader %s has serial %s.", typtext[ep->type], aureader->label, cs_hexdump(0, aureader->hexserial, 8, tmp, sizeof(tmp)));
		cs_ddump_mask(D_EMM, ep->hexserial, 8, "emm UA/SA:");

		uint32_t emmtype;
		if (ep->type == UNKNOWN)
			emmtype = EMM_UNKNOWN;
		else
			emmtype = 1 << (ep->type-1);
		client->last=time((time_t*)0);
		if ((1<<(ep->emm[0] % 0x80)) & aureader->s_nano || (aureader->saveemm & emmtype)) { //should this nano be saved?
			char token[256];
			char *tmp2;
			FILE *fp;
			time_t rawtime;
			time (&rawtime);
			struct tm timeinfo;
			localtime_r (&rawtime, &timeinfo);	/* to access LOCAL date/time info */
			int32_t emm_length = ((ep->emm[1] & 0x0f) << 8) | ep->emm[2];
			char buf[80];
			strftime (buf, sizeof(buf), "%Y/%m/%d %H:%M:%S", &timeinfo);
			snprintf (token, sizeof(token), "%s%s_emm.log", cfg.emmlogdir?cfg.emmlogdir:cs_confdir, aureader->label);
			
			if (!(fp = fopen (token, "a"))) {
				cs_log ("ERROR: Cannot open file '%s' (errno=%d: %s)\n", token, errno, strerror(errno));
			} else if(cs_malloc(&tmp2, (emm_length + 3)*2 + 1, -1)){
				fprintf (fp, "%s   %s   ", buf, cs_hexdump(0, ep->hexserial, 8, tmp, sizeof(tmp)));
				fprintf (fp, "%s\n", cs_hexdump(0, ep->emm, emm_length + 3, tmp2, (emm_length + 3)*2 + 1));
				free(tmp2);
				fclose (fp);
				cs_log ("Successfully added EMM to %s.", token);
			}

			snprintf (token, sizeof(token), "%s%s_emm.bin", cfg.emmlogdir?cfg.emmlogdir:cs_confdir, aureader->label);
			if (!(fp = fopen (token, "ab"))) {
				cs_log ("ERROR: Cannot open file '%s' (errno=%d: %s)\n", token, errno, strerror(errno));
			} else {
				if ((int)fwrite(ep->emm, 1, emm_length+3, fp) == emm_length+3)	{
					cs_log ("Successfully added binary EMM to %s.", token);
				} else {
					cs_log ("ERROR: Cannot write binary EMM to %s (errno=%d: %s)\n", token, errno, strerror(errno));
				}
				fclose (fp);
			}
		}

		int32_t is_blocked = 0;
		switch (ep->type) {
			case UNKNOWN: is_blocked = (aureader->blockemm & EMM_UNKNOWN) ? 1 : 0;
				break;
			case UNIQUE: is_blocked = (aureader->blockemm & EMM_UNIQUE) ? 1 : 0;
				break;
			case SHARED: is_blocked = (aureader->blockemm & EMM_SHARED) ? 1 : 0;
				break;
			case GLOBAL: is_blocked = (aureader->blockemm & EMM_GLOBAL) ? 1 : 0;
				break;
		}

		// if not already blocked we check for block by len
		if (!is_blocked) is_blocked = cs_emmlen_is_blocked( aureader, ep->emm[2] ) ;

		if (is_blocked != 0) {
#ifdef WEBIF
			aureader->emmblocked[ep->type]++;
			is_blocked = aureader->emmblocked[ep->type];
#endif
			/* we have to write the log for blocked EMM here because
	  		 this EMM never reach the reader module where the rest
			 of EMM log is done. */
			if (aureader->logemm & 0x08)  {
				cs_log("%s emmtype=%s, len=%d, idx=0, cnt=%d: blocked (0 ms) by %s",
						client->account->usr,
						typtext[ep->type],
						ep->emm[2],
						is_blocked,
						aureader->label);
			}
			continue;
		}

		client->lastemm = time((time_t*)0);

		client->emmok++;
		if (client->account)
			client->account->emmok++;
		first_client->emmok++;

		ep->client = cur_client();
		cs_debug_mask(D_EMM, "emm is being sent to reader %s.", aureader->label);

		EMM_PACKET *emm_pack = cs_malloc(&emm_pack, sizeof(EMM_PACKET), -1);
		memcpy(emm_pack, ep, sizeof(EMM_PACKET));
		add_job(aureader->client, ACTION_READER_EMM, emm_pack, sizeof(EMM_PACKET));
	}
}

int32_t process_input(uchar *buf, int32_t l, int32_t timeout)
{
	int32_t rc, i, pfdcount;
	struct pollfd pfd[2];
	struct s_client *cl = cur_client();

	time_t starttime = time(NULL);

	while (1) {
		pfdcount = 0;
		if (cl->pfd) {
			pfd[pfdcount].fd = cl->pfd;
			pfd[pfdcount++].events = POLLIN | POLLPRI;
		}

		int32_t p_rc = poll(pfd, pfdcount, 0);

		if (p_rc < 0) {
			if (errno==EINTR) continue;
			else return(0);
		}

		if (p_rc == 0 && (starttime+timeout) < time(NULL)) { // client maxidle reached
			rc=(-9);
			break;
		}

		for (i=0;i<pfdcount && p_rc > 0;i++) {
			if (pfd[i].revents & POLLHUP){	// POLLHUP is only valid in revents so it doesn't need to be set above in events
				return(0);
			}
			if (!(pfd[i].revents & (POLLIN | POLLPRI)))
				continue;

			if (pfd[i].fd == cl->pfd)
				return ph[cl->ctyp].recv(cl, buf, l);
		}
	}
	return(rc);
}

void cs_waitforcardinit()
{
	if (cfg.waitforcards)
	{
		cs_log("waiting for local card init");
		int32_t card_init_done;
		do {
			card_init_done = 1;
			struct s_reader *rdr;
			LL_ITER itr = ll_iter_create(configured_readers);
			while((rdr = ll_iter_next(&itr))) {
				if (rdr->enable && (!(rdr->typ & R_IS_CASCADING)) && (rdr->card_status == CARD_NEED_INIT || rdr->card_status == UNKNOWN)) {
					card_init_done = 0;
					break;
				}
			}

			if (!card_init_done)
				cs_sleepms(300); // wait a little bit
			//alarm(cfg.cmaxidle + cfg.ctimeout / 1000 + 1);
		} while (!card_init_done);
		if (cfg.waitforcards_extra_delay>0)
			cs_sleepms(cfg.waitforcards_extra_delay);
		cs_log("init for all local cards done");
	}
}

static void check_status(struct s_client *cl) {
	if (!cl || cl->kill || !cl->init_done)
		return;

	struct s_reader *rdr = cl->reader;

	switch (cl->typ) {
		case 'm':
		case 'c':
			//check clients for exceeding cmaxidle by checking cl->last
			if (!(cl->ncd_keepalive && (ph[cl->ctyp].listenertype & LIS_NEWCAMD))  && cl->last && cfg.cmaxidle && (time(NULL) - cl->last) > (time_t)cfg.cmaxidle) {
				add_job(cl, ACTION_CLIENT_IDLE, NULL, 0);
			}

			break;
#ifdef WITH_CARDREADER
		case 'r':
			//check for card inserted or card removed on pysical reader
			if (!rdr || !rdr->enable)
				break;
			add_job(cl, ACTION_READER_CHECK_HEALTH, NULL, 0);
			break;
#endif
		case 'p':
			//execute reader do idle on proxy reader after a certain time (rdr->tcp_ito = inactivitytimeout)
			//disconnect when no keepalive available
			if (!rdr || !rdr->enable)
				break;
			if (rdr->tcp_ito && (rdr->typ & R_IS_CASCADING)) {
				int32_t time_diff;
				time_diff = abs(time(NULL) - rdr->last_check);

				if (time_diff>60) { //check 1x per minute
					add_job(rdr->client, ACTION_READER_IDLE, NULL, 0);
					rdr->last_check = time(NULL);
				}
			}
			if (!rdr->tcp_connected && ((time(NULL) - rdr->last_check) > 30) && rdr->typ == R_CCCAM) {
				add_job(rdr->client, ACTION_READER_IDLE, NULL, 0);
				rdr->last_check = time(NULL);
			}
			break;
		default:
			break;
	}
}

void * work_thread(void *ptr) {
	struct s_data *data = (struct s_data *) ptr;
	struct s_client *cl = data->cl;
	struct s_reader *reader = cl->reader;

	struct s_data tmp_data;
	struct pollfd pfd[1];

	pthread_setspecific(getclient, cl);
	cl->thread=pthread_self();

	uint16_t bufsize = cl?ph[cl->ctyp].bufsize:1024; //CCCam needs more than 1024bytes!
	if (!bufsize) bufsize = 1024;
	uchar *mbuf = cs_malloc(&mbuf, bufsize, 0);
	int32_t n=0, rc=0, i, idx, s;
	uchar dcw[16];
	time_t now;

	while (1) {
		if (!cl || !is_valid_client(cl)) {
			if (data && data!=&tmp_data)
				free(data);
			data = NULL;
			free(mbuf);
			return NULL;
		}
		
		if (cl->kill && ll_count(cl->joblist) == 0) { //we need to process joblist to free data->ptr
			cs_debug_mask(D_TRACE, "ending thread");
			if (data && data!=&tmp_data)
				free(data);

			data = NULL;
			cleanup_thread(cl);
			free(mbuf);
			pthread_exit(NULL);
			return NULL;
		}
		
		if (data)
			cs_debug_mask(D_TRACE, "data from add_job action=%d client %c %s", data->action, cl->typ, username(cl));

		if (!data) {
			if(cl->typ != 'r') check_status(cl);	// do not call for physical readers as this might cause an endless job loop
			pthread_mutex_lock(&cl->thread_lock);
			if (cl->joblist && ll_count(cl->joblist)>0) {
				LL_ITER itr = ll_iter_create(cl->joblist);
				data = ll_iter_next_remove(&itr);
				//cs_debug_mask(D_TRACE, "start next job from list action=%d", data->action);
			}
			pthread_mutex_unlock(&cl->thread_lock);
		}

		if (!data) {
			if (!cl->pfd)
				break;
			pfd[0].fd = cl->pfd;
			pfd[0].events = POLLIN | POLLPRI | POLLHUP;
			
			pthread_mutex_lock(&cl->thread_lock);
			cl->thread_active = 2;
			pthread_mutex_unlock(&cl->thread_lock);
			rc = poll(pfd, 1, 3000);
			pthread_mutex_lock(&cl->thread_lock);
			cl->thread_active = 1;
			pthread_mutex_unlock(&cl->thread_lock);

			if (rc == -1)
				cs_debug_mask(D_TRACE, "poll wakeup");

			if (rc>0) {
				cs_debug_mask(D_TRACE, "data on socket");
				data=&tmp_data;
				data->ptr = NULL;
				
				if (reader)
					data->action = ACTION_READER_REMOTE;
				else {
					if (cl->is_udp)
						data->action = ACTION_CLIENT_UDP;
					else
						data->action = ACTION_CLIENT_TCP;
					if (pfd[0].revents & (POLLHUP | POLLNVAL))
						cl->kill = 1;
				}
			}
		}

		if (!data)
			continue;

		if (data->action < 20 && !reader) {
			if (data!=&tmp_data)
				free(data);
			data = NULL;
			break;
		}

		if (!data->action)
			break;
	
		now = time(NULL);
		if (data->time < now-(time_t)(cfg.ctimeout/1000)) {
			cs_log("dropping client data for %s time %ds", username(cl), (int32_t)(now-(time_t)(cfg.ctimeout/1000)));
			if (data!=&tmp_data)
				free(data);
			data = NULL;
			continue;
		}

		switch(data->action) {
			case ACTION_READER_IDLE:
				reader_do_idle(reader);
				break;
			case ACTION_READER_REMOTE:
				s = check_fd_for_data(cl->pfd);
				
				if (s == 0) // no data, another thread already read from fd?
					break;

				if (s < 0) {
					if (reader->ph.type==MOD_CONN_TCP)
						network_tcp_connection_close(reader, "disconnect");
					break;
				}

				rc = reader->ph.recv(cl, mbuf, bufsize);
				if (rc < 0) {
					if (reader->ph.type==MOD_CONN_TCP)
						network_tcp_connection_close(reader, "disconnect on receive");
					break;
				}

				cl->last=now;
				idx=reader->ph.c_recv_chk(cl, dcw, &rc, mbuf, rc);

				if (idx<0) break;  // no dcw received
				if (!idx) idx=cl->last_idx;

				reader->last_g=now; // for reconnect timeout

				for (i=0, n=0; i<CS_MAXPENDING && n == 0; i++) {
					if (cl->ecmtask[i].idx==idx) {
						cl->pending--;
						casc_check_dcw(reader, i, rc, dcw);
						n++;
					}
				}
				break;
			case ACTION_READER_REMOTELOG:
				casc_do_sock_log(reader);
 				break;
#ifdef WITH_CARDREADER
			case ACTION_READER_RESET:
		 		reader_reset(reader);
 				break;
#endif
			case ACTION_READER_ECM_REQUEST:
				reader_get_ecm(reader, data->ptr);
				break;
			case ACTION_READER_EMM:
				reader_do_emm(reader, data->ptr);
				free(data->ptr); // allocated in do_emm()
				break;
			case ACTION_READER_CARDINFO:
				reader_do_card_info(reader);
				break;
			case ACTION_READER_INIT:
				if (!cl->init_done)
					reader_init(reader);
				break;
			case ACTION_READER_RESTART:
				cleanup_thread(cl); //should close connections
				restart_cardreader(reader, 0);
				//original cl struct was destroyed by restart reader, so we exit here
				//init is done by a new thread

				if (data!=&tmp_data)
					free(data);
				data = NULL;
				free(mbuf);
				pthread_exit(NULL);
				return NULL;
				break;
#ifdef WITH_CARDREADER
			case ACTION_READER_RESET_FAST:
				reader->ins7e11_fast_reset = 1;
				reader->card_status = CARD_NEED_INIT;
				reader_reset(reader);
				reader->ins7e11_fast_reset = 0;
				break;
			case ACTION_READER_CHECK_HEALTH:
				reader_checkhealth(reader);
				break;
#endif
			case ACTION_CLIENT_UDP:
				n = ph[cl->ctyp].recv(cl, data->ptr, data->len);
				if (n<0) {
					free(data->ptr);
					break;
				}
				ph[cl->ctyp].s_handler(cl, data->ptr, n);
				free(data->ptr); // allocated in accept_connection()
				break;
			case ACTION_CLIENT_TCP:
				s = check_fd_for_data(cl->pfd);
				if (s == 0) // no data, another thread already read from fd?
					break;
				if (s < 0) { // system error or fd wants to be closed
					cl->kill=1; // kill client on next run
					continue;
				}

				n = ph[cl->ctyp].recv(cl, mbuf, bufsize);
				if (n < 0) {
					cl->kill=1; // kill client on next run
					continue;
				}
				ph[cl->ctyp].s_handler(cl, mbuf, n);
	
				break;
			case ACTION_CLIENT_ECM_ANSWER:
				chk_dcw(cl, data->ptr);
				free(data->ptr);
				break;
			case ACTION_CLIENT_INIT:
				if (ph[cl->ctyp].s_init)
					ph[cl->ctyp].s_init(cl);
				cl->init_done=1;
				break;
			case ACTION_CLIENT_IDLE:
				if (ph[cl->ctyp].s_idle)
					ph[cl->ctyp].s_idle(cl);
				else {
					cs_log("user %s reached %d sec idle limit.", username(cl), cfg.cmaxidle);
					cl->kill = 1;
				}
				break;
#ifdef CS_CACHEEX
			case ACTION_CACHE_PUSH_OUT: {
				ECM_REQUEST *er = data->ptr;

				int32_t res=0, stats = -1;
				if (reader) {
					struct s_client *cl = reader->client;
					if(cl){
						res = reader->ph.c_cache_push(cl, er);
						stats = cs_add_cacheex_stats(cl, er->caid, er->srvid, er->prid, 0);
					}
				}
				else
					res = ph[cl->ctyp].c_cache_push(cl, er);


				cs_debug_mask(
					D_TRACE,
					"pushed ECM %04X&%06X/%04X/%02X:%04X to %s res %d stats %d", er->caid, er->prid, er->srvid, er->l,
					htons(er->checksum), username(cl), res, stats);
				free(data->ptr);

				cl->cwcacheexpush++;
				if (cl->account)
					cl->account->cwcacheexpush++;
				first_client->cwcacheexpush++;

				break;
			}
#endif
		}

		if (data!=&tmp_data)
			free(data);

		data = NULL;
	}

	if (thread_pipe[1]){
		if(write(thread_pipe[1], mbuf, 1) == -1){ //wakeup client check
			cs_debug_mask(D_TRACE, "Writing to pipe failed (errno=%d %s)", errno, strerror(errno));
		}
	}
	
	cs_debug_mask(D_TRACE, "ending thread");
	cl->thread_active = 0;
	free(mbuf);
	pthread_exit(NULL);
	return NULL;
}

void add_job(struct s_client *cl, int8_t action, void *ptr, int32_t len) {

	if (!cl) {
		cs_log("WARNING: add_job failed.");
		return;
	}

	struct s_data *data = cs_malloc(&data, sizeof(struct s_data), -1);
	data->action = action;
	data->ptr = ptr;
	data->cl = cl;
	data->len = len;
	data->time = time(NULL);

	pthread_mutex_lock(&cl->thread_lock);
	if (cl->thread_active) {
		if (!cl->joblist)
			cl->joblist = ll_create("joblist");

		ll_append(cl->joblist, data);
		if(cl->thread_active == 2)
			pthread_kill(cl->thread, OSCAM_SIGNAL_WAKEUP);
		pthread_mutex_unlock(&cl->thread_lock);
		cs_debug_mask(D_TRACE, "add %s job action %d", action > ACTION_CLIENT_FIRST ? "client" : "reader", action);
		return;
	}


	pthread_attr_t attr;
	pthread_attr_init(&attr);
#if !defined(TUXBOX)
	/* pcsc doesn't like this either; segfaults on x86, x86_64 */
	struct s_reader *rdr = cl->reader;
	if(cl->typ != 'r' || !rdr || rdr->typ != R_PCSC)
		pthread_attr_setstacksize(&attr, PTHREAD_STACK_SIZE);
#endif

	cs_debug_mask(D_TRACE, "start %s thread action %d", action > ACTION_CLIENT_FIRST ? "client" : "reader", action);

	int32_t ret = pthread_create(&cl->thread, &attr, work_thread, (void *)data);
	if (ret) {
		cs_log("ERROR: can't create thread for %s (errno=%d %s)", action > ACTION_CLIENT_FIRST ? "client" : "reader", ret, strerror(ret));
	} else
		pthread_detach(cl->thread);

	pthread_attr_destroy(&attr);

	cl->thread_active = 1;
	pthread_mutex_unlock(&cl->thread_lock);
}

static void * check_thread(void) {
	int32_t time_to_check, next_check, ecmc_next, msec_wait = 3000;
	struct timeb t_now, tbc, ecmc_time;
#ifdef CS_ANTICASC
	int32_t ac_next;
	struct timeb ac_time;
#endif
	ECM_REQUEST *er = NULL;
	time_t ecm_timeout;
	time_t ecm_mintimeout;
	struct timespec ts;
	struct s_client *cl = create_client(first_client->ip);
	cl->typ = 's';
#ifdef WEBIF
	cl->wihidden = 1;
#endif
	cl->thread = pthread_self();

	timecheck_client = cl;

#ifdef CS_ANTICASC
	cs_ftime(&ac_time);
	add_ms_to_timeb(&ac_time, cfg.ac_stime*60*1000);
#endif

	cs_ftime(&ecmc_time);
	add_ms_to_timeb(&ecmc_time, 1000);

	while(1) {
		ts.tv_sec = msec_wait/1000;
		ts.tv_nsec = (msec_wait % 1000) * 1000000L;
		pthread_mutex_lock(&cl->thread_lock);
		cl->thread_active = 2;
		pthread_mutex_unlock(&cl->thread_lock);
		nanosleep(&ts, NULL);
		pthread_mutex_lock(&cl->thread_lock);
		cl->thread_active = 1;
		pthread_mutex_unlock(&cl->thread_lock);

		next_check = 0;
#ifdef CS_ANTICASC
		ac_next = 0;
#endif
		ecmc_next = 0;
		msec_wait = 0;

		cs_ftime(&t_now);
		cs_readlock(&ecmcache_lock);

		for (er = ecmcwcache; er; er = er->next) {
			if (er->rc < E_99)
				continue;

			tbc = er->tps;
#ifdef CS_CACHEEX
			time_to_check = add_ms_to_timeb(&tbc, (er->stage < 2) ? cfg.cacheex_wait_time:((er->stage < 4) ? cfg.ftimeout : cfg.ctimeout));
#else
			time_to_check = add_ms_to_timeb(&tbc, ((er->stage < 4) ? cfg.ftimeout : cfg.ctimeout));
#endif

			if (comp_timeb(&t_now, &tbc) >= 0) {
				if (er->stage < 4) {
					cs_debug_mask(D_TRACE, "fallback for %s %04X&%06X/%04X ecm=%04X", username(er->client), er->caid, er->prid, er->srvid, htons(er->checksum));
					if (er->rc >= E_UNHANDLED) //do not request rc=99
						request_cw(er);

					tbc = er->tps;
					time_to_check = add_ms_to_timeb(&tbc, cfg.ctimeout);
				} else {
					if (er->client) {
						cs_debug_mask(D_TRACE, "timeout for %s %04X&%06X/%04X ecm=%04X", username(er->client), er->caid, er->prid, er->srvid, htons(er->checksum));
						write_ecm_answer(NULL, er, E_TIMEOUT, 0, NULL, NULL);
					}
#ifdef WITH_LB		
						
					//because of lb, send E_TIMEOUT for all readers:
					struct s_ecm_answer *ea_list;

					for(ea_list = er->matching_rdr; ea_list; ea_list = ea_list->next) {
						if ((ea_list->status & (REQUEST_SENT|REQUEST_ANSWERED)) == REQUEST_SENT) //Request send, but no answer!
							send_reader_stat(ea_list->reader, er, E_TIMEOUT);
					}
#endif

					time_to_check = 0;
				}
			}
			if (!next_check || (time_to_check > 0 && time_to_check < next_check))
				next_check = time_to_check;		
		}
		cs_readunlock(&ecmcache_lock);

#ifdef CS_ANTICASC
		if ((ac_next = comp_timeb(&ac_time, &t_now)) <= 10) {
			if (cfg.ac_enabled)
				ac_do_stat();
			cs_ftime(&ac_time);
			ac_next = add_ms_to_timeb(&ac_time, cfg.ac_stime*60*1000);
		}
#endif

		if ((ecmc_next = comp_timeb(&ecmc_time, &t_now)) <= 10) {
			ecm_timeout = t_now.time-cfg.max_cache_time;
			ecm_mintimeout = t_now.time-(cfg.ctimeout/1000+2);
			uint32_t count = 0;

			struct ecm_request_t *ecm, *ecmt=NULL, *prv;
			cs_readlock(&ecmcache_lock);
			for (ecm = ecmcwcache, prv = NULL; ecm; prv = ecm, ecm = ecm->next, count++) {
				if (ecm->tps.time < ecm_timeout || (ecm->tps.time<ecm_mintimeout && count>cfg.max_cache_count)) {
					cs_readunlock(&ecmcache_lock);
					cs_writelock(&ecmcache_lock);
					ecmt = ecm;
					if (prv)
						prv->next = NULL;
					else
						ecmcwcache = NULL;
					cs_writeunlock(&ecmcache_lock);
					break;
				}
			}
			if (!ecmt)
				cs_readunlock(&ecmcache_lock);
			ecmcwcache_size = count;

			while (ecmt) {
				ecm = ecmt->next;
				free_ecm(ecmt);
				ecmt = ecm;
			}

			cs_ftime(&ecmc_time);
			ecmc_next = add_ms_to_timeb(&ecmc_time, 1000);
		}

		msec_wait = next_check;

#ifdef CS_ANTICASC
		if (!msec_wait || (ac_next > 0 && ac_next < msec_wait))
			msec_wait = ac_next;
#endif

		if (!msec_wait || (ecmc_next > 0 && ecmc_next < msec_wait))
			msec_wait = ecmc_next;

		if (!msec_wait)
			msec_wait = 3000;
	}
	add_garbage(cl);
	timecheck_client = NULL;
	return NULL;
}

static uint32_t resize_pfd_cllist(struct pollfd **pfd, struct s_client ***cl_list, uint32_t old_size, uint32_t new_size) {
	if (old_size != new_size) {
		struct pollfd *pfd_new = cs_malloc(&pfd_new, new_size*sizeof(struct pollfd), 0);
		struct s_client **cl_list_new = cs_malloc(&cl_list_new, new_size*sizeof(cl_list), 0);
		if (old_size > 0) {
			memcpy(pfd_new, *pfd, old_size*sizeof(struct pollfd));
			memcpy(cl_list_new, *cl_list, old_size*sizeof(cl_list));
			free(*pfd);
			free(*cl_list);
		}
		*pfd = pfd_new;
		*cl_list = cl_list_new;
	}
	return new_size;
}

static uint32_t chk_resize_cllist(struct pollfd **pfd, struct s_client ***cl_list, uint32_t cur_size, uint32_t chk_size) {
	chk_size++;
	if (chk_size > cur_size) {
		uint32_t new_size = ((chk_size % 100)+1) * 100; //increase 100 step
		cur_size = resize_pfd_cllist(pfd, cl_list, cur_size, new_size);
	}
	return cur_size;
}

void * client_check(void) {
	int32_t i, k, j, rc, pfdcount = 0;
	struct s_client *cl;
	struct s_reader *rdr;
	struct pollfd *pfd;
	struct s_client **cl_list;
	uint32_t cl_size = 0;

	char buf[10];

	if (pipe(thread_pipe) == -1) {
		printf("cannot create pipe, errno=%d\n", errno);
		exit(1);
	}

	cl_size = chk_resize_cllist(&pfd, &cl_list, 0, 100);

	pfd[pfdcount].fd = thread_pipe[0];
	pfd[pfdcount].events = POLLIN | POLLPRI | POLLHUP;
	cl_list[pfdcount] = NULL;

	while (!exit_oscam) {
		pfdcount = 1;

		//connected tcp clients
		for (cl=first_client->next; cl; cl=cl->next) {
			if (cl->init_done && !cl->kill && cl->pfd && cl->typ=='c' && !cl->is_udp) {
				if (cl->pfd && !cl->thread_active) {
					cl_size = chk_resize_cllist(&pfd, &cl_list, cl_size, pfdcount);
					cl_list[pfdcount] = cl;
					pfd[pfdcount].fd = cl->pfd;
					pfd[pfdcount++].events = POLLIN | POLLPRI | POLLHUP;
				}
			}
			//reader:
			//TCP:
			//	- TCP socket must be connected
			//	- no active init thread
			//UDP:
			//	- connection status ignored
			//	- no active init thread
			rdr = cl->reader;
			if (rdr && cl->typ=='p' && cl->init_done) {
				if (cl->pfd && !cl->thread_active && ((rdr->tcp_connected && rdr->ph.type==MOD_CONN_TCP)||(rdr->ph.type==MOD_CONN_UDP))) {
					cl_size = chk_resize_cllist(&pfd, &cl_list, cl_size, pfdcount);
					cl_list[pfdcount] = cl;
					pfd[pfdcount].fd = cl->pfd;
					pfd[pfdcount++].events = POLLIN | POLLPRI | POLLHUP;
				}
			}
		}

		//server (new tcp connections or udp messages)
		for (k=0; k < CS_MAX_MOD; k++) {
			if ( (ph[k].type & MOD_CONN_NET) && ph[k].ptab ) {
				for (j=0; j<ph[k].ptab->nports; j++) {
					if (ph[k].ptab->ports[j].fd) {
						cl_size = chk_resize_cllist(&pfd, &cl_list, cl_size, pfdcount);
						cl_list[pfdcount] = NULL;
						pfd[pfdcount].fd = ph[k].ptab->ports[j].fd;
						pfd[pfdcount++].events = POLLIN | POLLPRI | POLLHUP;

					}
				}
			}
		}

		if (pfdcount >= 1024)
			cs_log("WARNING: too many users!");

		rc = poll(pfd, pfdcount, 5000);

		if (rc<1)
			continue;

		for (i=0; i<pfdcount; i++) {
			//clients
			cl = cl_list[i];
			if (cl && !is_valid_client(cl))
				continue;

			if (pfd[i].fd == thread_pipe[0] && (pfd[i].revents & (POLLIN | POLLPRI))) {
				// a thread ended and cl->pfd should be added to pollfd list again (thread_active==0)
				if(read(thread_pipe[0], buf, sizeof(buf)) == -1){
					cs_debug_mask(D_TRACE, "Reading from pipe failed (errno=%d %s)", errno, strerror(errno));
				}
				continue;
			}

			//clients
			// message on an open tcp connection
			if (cl && cl->init_done && cl->pfd && (cl->typ == 'c' || cl->typ == 'm')) {
				if (pfd[i].fd == cl->pfd && (pfd[i].revents & (POLLHUP | POLLNVAL))) {
					//client disconnects
					kill_thread(cl);
					continue;
				}
				if (pfd[i].fd == cl->pfd && (pfd[i].revents & (POLLIN | POLLPRI))) {
					add_job(cl, ACTION_CLIENT_TCP, NULL, 0);
				}
			}


			//reader
			// either an ecm answer, a keepalive or connection closed from a proxy
			// physical reader ('r') should never send data without request
			rdr = NULL;
			struct s_client *cl2 = NULL;
			if (cl && cl->typ == 'p'){
				rdr = cl->reader;
				if(rdr)
					cl2 = rdr->client;
			}

			if (rdr && cl2 && cl2->init_done) {
				if (cl2->pfd && pfd[i].fd == cl2->pfd && (pfd[i].revents & (POLLHUP | POLLNVAL))) {
					//connection to remote proxy was closed
					//oscam should check for rdr->tcp_connected and reconnect on next ecm request sent to the proxy
					network_tcp_connection_close(rdr, "closed");
					cs_debug_mask(D_READER, "connection to %s closed.", rdr->label);
				}
				if (cl2->pfd && pfd[i].fd == cl2->pfd && (pfd[i].revents & (POLLIN | POLLPRI))) {
					add_job(cl2, ACTION_READER_REMOTE, NULL, 0);
				}
			}


			//server sockets
			// new connection on a tcp listen socket or new message on udp listen socket
			if (!cl && (pfd[i].revents & (POLLIN | POLLPRI))) {
				for (k=0; k<CS_MAX_MOD; k++) {
					if( (ph[k].type & MOD_CONN_NET) && ph[k].ptab ) {
						for ( j=0; j<ph[k].ptab->nports; j++ ) {
							if ( ph[k].ptab->ports[j].fd && pfd[i].fd == ph[k].ptab->ports[j].fd ) {
								accept_connection(k,j);
							}
						}
					}
				} // if (ph[i].type & MOD_CONN_NET)
			}
		}
		first_client->last=time((time_t *)0);
	}
	free(pfd);
	free(cl_list);
	return NULL;
}

void * reader_check(void) {
	struct s_client *cl;
	struct s_reader *rdr;
	while (1) {
		for (cl=first_client->next; cl ; cl=cl->next) {
			if (!cl->thread_active)
				check_status(cl);
		}
		cs_readlock(&readerlist_lock);
		for (rdr=first_active_reader; rdr; rdr=rdr->next) {
			if (rdr->enable) {
				cl = rdr->client;
				if (!cl || cl->kill)
					restart_cardreader(rdr, 0);
				else if (!cl->thread_active)
					check_status(cl);
			}
		}
		cs_readunlock(&readerlist_lock);
		cs_sleepms(1000);
	}
}

int32_t accept_connection(int32_t i, int32_t j) {
	struct   sockaddr_in cad;
	int32_t scad = sizeof(cad), n;

	if (ph[i].type==MOD_CONN_UDP) {
		uchar *buf = cs_malloc(&buf, 1024, -1);
		if ((n=recvfrom(ph[i].ptab->ports[j].fd, buf+3, 1024-3, 0, (struct sockaddr *)&cad, (socklen_t *)&scad))>0) {
			struct s_client *cl;
			cl=idx_from_ip(cad.sin_addr.s_addr, ntohs(cad.sin_port));

			uint16_t rl;
			rl=n;
			buf[0]='U';
			memcpy(buf+1, &rl, 2);

			if (cs_check_violation((uint32_t)cad.sin_addr.s_addr, ph[i].ptab->ports[j].s_port))
				return 0;

			cs_debug_mask(D_TRACE, "got %d bytes from ip %s:%d", n, cs_inet_ntoa(cad.sin_addr.s_addr), cad.sin_port);

			if (!cl) {
				cl = create_client(cad.sin_addr.s_addr);
				if (!cl) return 0;

				cl->ctyp=i;
				cl->port_idx=j;
				cl->udp_fd=ph[i].ptab->ports[j].fd;
				cl->udp_sa=cad;

				cl->port=ntohs(cad.sin_port);
				cl->typ='c';

				add_job(cl, ACTION_CLIENT_INIT, NULL, 0);
			}
			add_job(cl, ACTION_CLIENT_UDP, buf, n+3);
		} else
			free(buf);
	} else { //TCP
		int32_t pfd3;
		if ((pfd3=accept(ph[i].ptab->ports[j].fd, (struct sockaddr *)&cad, (socklen_t *)&scad))>0) {

			if (cs_check_violation((uint32_t)cad.sin_addr.s_addr, ph[i].ptab->ports[j].s_port)) {
				close(pfd3);
				return 0;
			}

			struct s_client * cl = create_client(cad.sin_addr.s_addr);
			if (cl == NULL) {
				close(pfd3);
				return 0;
			}

			int32_t flag = 1;
			setsockopt(pfd3, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
			setTCPTimeouts(pfd3);

			cl->ctyp=i;
			cl->udp_fd=pfd3;
			cl->port_idx=j;

			cl->pfd=pfd3;
			cl->port=ntohs(cad.sin_port);
			cl->typ='c';
			
			add_job(cl, ACTION_CLIENT_INIT, NULL, 0);
		}
	}
	return 0;
}

#ifdef WEBIF
static void restart_daemon()
{
  while (1) {

    //start client process:
    pid_t pid = fork();
    if (!pid)
      return; //client process=oscam process
    if (pid < 0)
      exit(1);

    //restart control process:
    int32_t res=0;
    int32_t status=0;
    do {
      res = waitpid(pid, &status, 0);
      if (res==-1) {
        if (errno!=EINTR)
          exit(1);
      }
    } while (res!=pid);

    if (cs_restart_mode==2 && WIFSIGNALED(status) && WTERMSIG(status)==SIGSEGV)
      status=99; //restart on segfault!
    else
      status = WEXITSTATUS(status);

    //status=99 restart oscam, all other->terminate
    if (status!=99) {
      exit(status);
    }
  }
}
#endif

int32_t main (int32_t argc, char *argv[])
{
	prog_name = argv[0];
	if (pthread_key_create(&getclient, NULL)) {
		fprintf(stderr, "Could not create getclient, exiting...");
		exit(1);
	}

#ifdef ARM
	cs_switch_led(LED1A, LED_DEFAULT);
	cs_switch_led(LED1A, LED_ON);
#endif

	int32_t      i, j, bg=0, gbdb=0;

  void (*mod_def[])(struct s_module *)=
  {
#ifdef MODULE_MONITOR
           module_monitor,
#endif
#ifdef MODULE_CAMD33
           module_camd33,
#endif
#ifdef MODULE_CAMD35
           module_camd35,
#endif
#ifdef MODULE_CAMD35_TCP
           module_camd35_tcp,
#endif
#ifdef MODULE_NEWCAMD
           module_newcamd,
#endif
#ifdef MODULE_CCCAM
           module_cccam,
#endif
#ifdef MODULE_PANDORA
           module_pandora,
#endif
#ifdef CS_CACHEEX
           module_csp,
#endif
#ifdef MODULE_GBOX
           module_gbox,
#endif
#ifdef MODULE_CONSTCW
           module_constcw,
#endif
#ifdef MODULE_RADEGAST
           module_radegast,
#endif
#ifdef MODULE_SERIAL
           module_oscam_ser,
#endif
#ifdef HAVE_DVBAPI
	   module_dvbapi,
#endif
           0
  };

  void (*cardsystem_def[])(struct s_cardsystem *)=
  {
#ifdef READER_NAGRA
	reader_nagra,
#endif
#ifdef READER_IRDETO
	reader_irdeto,
#endif
#ifdef READER_CONAX
	reader_conax,
#endif
#ifdef READER_CRYPTOWORKS
	reader_cryptoworks,
#endif
#ifdef READER_SECA
	reader_seca,
#endif
#ifdef READER_VIACCESS
	reader_viaccess,
#endif
#ifdef READER_VIDEOGUARD
	reader_videoguard1,
	reader_videoguard2,
	reader_videoguard12,
#endif
#ifdef READER_DRE
	reader_dre,
#endif
#ifdef READER_TONGFANG
	reader_tongfang,
#endif
	0
  };

  void (*cardreader_def[])(struct s_cardreader *)=
  {
#ifdef WITH_CARDREADER 
	cardreader_mouse,
	cardreader_smargo,
#ifdef WITH_STAPI
	cardreader_stapi,
#endif
#endif
	0
  };

  while ((i=getopt(argc, argv, "g:bsauc:t:d:r:w:hm:x"))!=EOF)
  {
	  switch(i) {
		  case 'g':
			  gbdb=atoi(optarg);
			  break;
		  case 'b':
			  bg=1;
			  break;
		  case 's':
		      cs_capture_SEGV=1;
		      break;
		  case 'a':
		      cs_dump_stack=1;
		      break;
		  case 'c':
			  cs_strncpy(cs_confdir, optarg, sizeof(cs_confdir));
			  break;
		  case 'd':
			  cs_dblevel=atoi(optarg);
			  break;
#ifdef WEBIF
		  case 'r':
			  cs_restart_mode=atoi(optarg);
			  break;
#endif
		  case 't':
			  mkdir(optarg, S_IRWXU);
			  j = open(optarg, O_RDONLY);
			  if (j >= 0) {
			 	close(j);
			 	cs_strncpy(cs_tmpdir, optarg, sizeof(cs_tmpdir));
			  } else {
				printf("WARNING: tmpdir does not exist. using default value.\n");
			  }
			  break;
			case 'w':
				cs_waittime=strtoul(optarg, NULL, 10);
				break;
#ifdef WEBIF 
			case 'u':
				cs_http_use_utf8 = 1;
				printf("WARNING: Web interface UTF-8 mode enabled. Carefully read documentation as bugs may arise.\n");
				break;
#endif
		  case 'm':
				printf("WARNING: -m parameter is deprecated, ignoring it.\n");
				break;
		  case 'h':
		  default :
			  usage();
	  }
  }


#ifdef OS_MACOSX
  if (bg && daemon_compat(1,0))
#else
  if (bg && daemon(1,0))
#endif
  {
    printf("Error starting in background (errno=%d: %s)", errno, strerror(errno));
    cs_exit(1);
  }

#ifdef WEBIF
  if (cs_restart_mode)
    restart_daemon();
#endif

  memset(&cfg, 0, sizeof(struct s_config));

  if (cs_confdir[strlen(cs_confdir)]!='/') strcat(cs_confdir, "/");
  init_signal_pre(); // because log could cause SIGPIPE errors, init a signal handler first
  init_first_client();
  init_config();
  init_check();
#ifdef WITH_LB
  init_stat();
#endif

  for (i=0; mod_def[i]; i++)  // must be later BEFORE init_config()
  {
    memset(&ph[i], 0, sizeof(struct s_module));
    mod_def[i](&ph[i]);
  }
  for (i=0; cardsystem_def[i]; i++)  // must be later BEFORE init_config()
  {
    memset(&cardsystem[i], 0, sizeof(struct s_cardsystem));
    cardsystem_def[i](&cardsystem[i]);
  }

  for (i=0; cardreader_def[i]; i++)  // must be later BEFORE init_config()
  {
    memset(&cardreader[i], 0, sizeof(struct s_cardreader));
    cardreader_def[i](&cardreader[i]);
  }

  init_rnd();
  init_sidtab();
  init_readerdb();
  cfg.account = init_userdb();
  init_signal();
  init_srvid();
  init_tierid();
  //Todo #ifdef CCCAM
  init_provid();

  start_garbage_collector(gbdb);

  init_len4caid();
#ifdef IRDETO_GUESSING
  init_irdeto_guess_tab();
#endif

  write_versionfile();
  server_pid = getpid();

#ifdef ARM
  arm_led_start_thread();
#endif

#if defined(AZBOX) && defined(HAVE_DVBAPI)
  openxcas_debug_message_onoff(1);  // debug

#ifdef WITH_CARDREADER
  if (openxcas_open_with_smartcard("oscamCAS") < 0) {
#else
  if (openxcas_open("oscamCAS") < 0) {
#endif
    cs_log("openxcas: could not init");
  }
#endif

  global_whitelist_read();
#ifdef CS_CACHEEX
  cacheex_matcher_read();
#endif

  for (i=0; i<CS_MAX_MOD; i++)
    if( (ph[i].type & MOD_CONN_NET) && ph[i].ptab )
      for(j=0; j<ph[i].ptab->nports; j++)
      {
        start_listener(&ph[i], j);
      }

	//set time for server to now to avoid 0 in monitor/webif
	first_client->last=time((time_t *)0);

#ifdef WEBIF
	if(cfg.http_port == 0)
		cs_log("http disabled");
	else
		start_thread((void *) &http_srv, "http");
#endif
	start_thread((void *) &reader_check, "reader check"); 
	start_thread((void *) &check_thread, "check"); 
#ifdef LCDSUPPORT
	if(cfg.enablelcd)
		start_lcd_thread();
#endif

	init_cardreader();

	cs_waitforcardinit();

#ifdef ARM
	if(cfg.enableled == 1){
		cs_switch_led(LED1A, LED_OFF);
		cs_switch_led(LED1B, LED_ON);
	}
#endif

#ifdef QBOXHD
	if(cfg.enableled == 2){
		cs_log("QboxHD LED enabled");
    qboxhd_led_blink(QBOXHD_LED_COLOR_YELLOW,QBOXHD_LED_BLINK_FAST);
    qboxhd_led_blink(QBOXHD_LED_COLOR_RED,QBOXHD_LED_BLINK_FAST);
    qboxhd_led_blink(QBOXHD_LED_COLOR_GREEN,QBOXHD_LED_BLINK_FAST);
    qboxhd_led_blink(QBOXHD_LED_COLOR_BLUE,QBOXHD_LED_BLINK_FAST);
    qboxhd_led_blink(QBOXHD_LED_COLOR_MAGENTA,QBOXHD_LED_BLINK_FAST);
  }
#endif

#ifdef CS_ANTICASC
	if( !cfg.ac_enabled )
		cs_log("anti cascading disabled");
	else {
		init_ac();
		ac_init_stat();
	}
#endif

	for (i=0; i<CS_MAX_MOD; i++)
		if (ph[i].type & MOD_CONN_SERIAL)   // for now: oscam_ser only
			if (ph[i].s_handler)
				ph[i].s_handler(NULL, NULL, i);

	// main loop function
	client_check();


#if defined(AZBOX) && defined(HAVE_DVBAPI)
  if (openxcas_close() < 0) {
    cs_log("openxcas: could not close");
  }
#endif

	cs_cleanup();
	while(ll_count(log_list) > 0)
		cs_sleepms(1);
	stop_garbage_collector();

	return exit_oscam;
}

void cs_exit_oscam()
{
  exit_oscam=1;
  cs_log("exit oscam requested");
}

#ifdef WEBIF
void cs_restart_oscam()
{
  exit_oscam=99;
  cs_log("restart oscam requested");
}

int32_t cs_get_restartmode() {
	return cs_restart_mode;
}

#endif

#ifdef ARM
static void cs_switch_led_from_thread(int32_t led, int32_t action) {

	if(action < 2) { // only LED_ON and LED_OFF
		char ledfile[256];
		FILE *f;

		#ifdef DOCKSTAR
			switch(led){
			case LED1A:snprintf(ledfile, 255, "/sys/class/leds/dockstar:orange:misc/brightness");
			break;
			case LED1B:snprintf(ledfile, 255, "/sys/class/leds/dockstar:green:health/brightness");
			break;
			case LED2:snprintf(ledfile, 255, "/sys/class/leds/dockstar:green:health/brightness");
			break;
			case LED3:snprintf(ledfile, 255, "/sys/class/leds/dockstar:orange:misc/brightness");
			break;
			}
		#elif WRT350NV2
			switch(led){
			case LED1A:snprintf(ledfile, 255, "/sys/class/leds/wrt350nv2:orange:power/brightness");
			break;
			case LED1B:snprintf(ledfile, 255, "/sys/class/leds/wrt350nv2:green:power/brightness");
			break;
			case LED2:snprintf(ledfile, 255, "/sys/class/leds/wrt350nv2:green:wireless/brightness");
			break;
			case LED3:snprintf(ledfile, 255, "/sys/class/leds/wrt350nv2:green:security/brightness");
			break;
			}
		#else
			switch(led){
			case LED1A:snprintf(ledfile, 255, "/sys/class/leds/nslu2:red:status/brightness");
			break;
			case LED1B:snprintf(ledfile, 255, "/sys/class/leds/nslu2:green:ready/brightness");
			break;
			case LED2:snprintf(ledfile, 255, "/sys/class/leds/nslu2:green:disk-1/brightness");
			break;
			case LED3:snprintf(ledfile, 255, "/sys/class/leds/nslu2:green:disk-2/brightness");
			break;
			}
		#endif

		if (!(f=fopen(ledfile, "w"))){
			// FIXME: sometimes cs_log was not available when calling cs_switch_led -> signal 11
			// cs_log("Cannot open file \"%s\" (errno=%d %s)", ledfile, errno, strerror(errno));
			return;
		}
		fprintf(f,"%d", action);
		fclose(f);
	} else { // LED Macros
		switch(action){
		case LED_DEFAULT:
			cs_switch_led_from_thread(LED1A, LED_OFF);
			cs_switch_led_from_thread(LED1B, LED_OFF);
			cs_switch_led_from_thread(LED2, LED_ON);
			cs_switch_led_from_thread(LED3, LED_OFF);
			break;
		case LED_BLINK_OFF:
			cs_switch_led_from_thread(led, LED_OFF);
			cs_sleepms(100);
			cs_switch_led_from_thread(led, LED_ON);
			break;
		case LED_BLINK_ON:
			cs_switch_led_from_thread(led, LED_ON);
			cs_sleepms(300);
			cs_switch_led_from_thread(led, LED_OFF);
			break;
		}
	}
}

static void* arm_led_thread_main() {
	uint8_t running = 1;
	while (running) {
		LL_ITER iter = ll_iter_create(arm_led_actions);
		struct s_arm_led *arm_led;
		while ((arm_led = ll_iter_next(&iter))) {
			int32_t led, action;
			time_t now, start;
			led = arm_led->led;
			action = arm_led->action;
			now = time((time_t)0);
			start = arm_led->start_time;
			ll_iter_remove_data(&iter);
			if (action == LED_STOP_THREAD) {
				running = 0;
				break;
			}
			if (now - start < ARM_LED_TIMEOUT) {
				cs_switch_led_from_thread(led, action);
			}
		}
		if (running) {
			sleep(60);
		}
	}
	ll_clear_data(arm_led_actions);
	pthread_exit(NULL);
	return NULL;
}

void arm_led_start_thread() {
	// call this after signal handling is done
	if ( ! arm_led_actions ) {
		arm_led_actions = ll_create("arm_led_actions");
	}
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	cs_log("starting thread arm_led_thread");
#ifndef TUXBOX
	pthread_attr_setstacksize(&attr, PTHREAD_STACK_SIZE);
#endif
	int32_t ret = pthread_create(&arm_led_thread, &attr, arm_led_thread_main, NULL);
	if (ret)
		cs_log("ERROR: can't create arm_led_thread thread (errno=%d %s)", ret, strerror(ret));
	else {
		cs_log("arm_led_thread thread started");
		pthread_detach(arm_led_thread);
	}
	pthread_attr_destroy(&attr);
}

void arm_led_stop_thread() {
	cs_switch_led(0, LED_STOP_THREAD);
}

void cs_switch_led(int32_t led, int32_t action) {
	struct s_arm_led *arm_led = (struct s_arm_led *)malloc(sizeof(struct s_arm_led));
	if (arm_led == NULL) {
		cs_log("ERROR: cs_switch_led out of memory");
		return;
	}
	arm_led->start_time = time((time_t)0);
	arm_led->led = led;
	arm_led->action = action;
	if ( ! arm_led_actions ) {
		arm_led_actions = ll_create("arm_led_actions");
	}
	ll_append(arm_led_actions, (void *)arm_led);
	if (arm_led_thread) {
		// arm_led_thread_main is not started at oscam startup
		// when first cs_switch_led calls happen
		pthread_kill(arm_led_thread, OSCAM_SIGNAL_WAKEUP);
	}
}
#endif

#ifdef QBOXHD
void qboxhd_led_blink(int32_t color, int32_t duration) {
    int32_t f;

    // try QboxHD-MINI first
    if ( (f = open ( QBOXHDMINI_LED_DEVICE,  O_RDWR |O_NONBLOCK )) > -1 ) {
        qboxhdmini_led_color_struct qbminiled;
        uint32_t qboxhdmini_color = 0x000000;

        if (color != QBOXHD_LED_COLOR_OFF) {
            switch(color) {
                case QBOXHD_LED_COLOR_RED:
                    qboxhdmini_color = QBOXHDMINI_LED_COLOR_RED;
                    break;
                case QBOXHD_LED_COLOR_GREEN:
                    qboxhdmini_color = QBOXHDMINI_LED_COLOR_GREEN;
                    break;
                case QBOXHD_LED_COLOR_BLUE:
                    qboxhdmini_color = QBOXHDMINI_LED_COLOR_BLUE;
                    break;
                case QBOXHD_LED_COLOR_YELLOW:
                    qboxhdmini_color = QBOXHDMINI_LED_COLOR_YELLOW;
                    break;
                case QBOXHD_LED_COLOR_MAGENTA:
                    qboxhdmini_color = QBOXHDMINI_LED_COLOR_MAGENTA;
                    break;
            }

            // set LED on with color
            qbminiled.red = (uchar)((qboxhdmini_color&0xFF0000)>>16);  // R
            qbminiled.green = (uchar)((qboxhdmini_color&0x00FF00)>>8); // G
            qbminiled.blue = (uchar)(qboxhdmini_color&0x0000FF);       // B

            ioctl(f,QBOXHDMINI_IOSET_RGB,&qbminiled);
            cs_sleepms(duration);
        }

        // set LED off
        qbminiled.red = 0;
        qbminiled.green = 0;
        qbminiled.blue = 0;

        ioctl(f,QBOXHDMINI_IOSET_RGB,&qbminiled);
        close(f);

    } else if ( (f = open ( QBOXHD_LED_DEVICE,  O_RDWR |O_NONBLOCK )) > -1 ) {

        qboxhd_led_color_struct qbled;

        if (color != QBOXHD_LED_COLOR_OFF) {
            // set LED on with color
            qbled.H = color;
            qbled.S = 99;
            qbled.V = 99;
            ioctl(f,QBOXHD_SET_LED_ALL_PANEL_COLOR, &qbled);
            cs_sleepms(duration);
        }

        // set LED off
        qbled.H = 0;
        qbled.S = 0;
        qbled.V = 0;
        ioctl(f,QBOXHD_SET_LED_ALL_PANEL_COLOR, &qbled);
        close(f);
    }

    return;
}
#endif
