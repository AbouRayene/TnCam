#include "globals.h"

#ifdef WEBIF
//
// OSCam HTTP server module
//

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

#include "module-cccam.h"
#include "module-cccshare.h"
#include "module-webif.h"

extern char *CSS;
extern char *entitlement_type[];

int32_t ssl_active = 0;
char noncekey[33];
pthread_key_t getkeepalive;
pthread_key_t getip;
pthread_key_t getssl;
CS_MUTEX_LOCK http_lock;
CS_MUTEX_LOCK *lock_cs;

static int8_t running = 1;
static pthread_t httpthread;

/* constants for menuactivating */
#define MNU_STATUS 0
#define MNU_CONFIG 1
#define MNU_READERS 2
#define MNU_USERS 3
#define MNU_SERVICES 4
#define MNU_FILES 5
#define MNU_FAILBAN 6
#define MNU_CACHEEX 7
#define MNU_SCRIPT 8
#define MNU_SHUTDOWN 9
#define MNU_TOTAL_ITEMS 10 // sum of items above
/* constants for submenuactivating */
#define MNU_CFG_GLOBAL 0
#define MNU_CFG_LOADBAL 1
#define MNU_CFG_CAMD33 2
#define MNU_CFG_CAMD35 3
#define MNU_CFG_CAMD35TCP 4
#define MNU_CFG_NEWCAMD 5
#define MNU_CFG_RADEGAST 6
#define MNU_CFG_CCCAM 7
#define MNU_CFG_ANTICASC 8
#define MNU_CFG_MONITOR 9
#define MNU_CFG_SERIAL 10
#define MNU_CFG_DVBAPI 11

#define MNU_CFG_FVERSION 12
#define MNU_CFG_FCONF 13
#define MNU_CFG_FUSER 14
#define MNU_CFG_FSERVER 15
#define MNU_CFG_FSERVICES 16
#define MNU_CFG_FSRVID 17
#define MNU_CFG_FPROVID 18
#define MNU_CFG_FTIERS 19
#define MNU_CFG_FLOGFILE 20
#define MNU_CFG_FUSERFILE 21
#define MNU_CFG_FACLOG 22
#define MNU_CFG_FDVBAPI 23
#define MNU_CFG_CSP 24
#define MNU_CFG_WHITELIST 25
#define MNU_CFG_TOTAL_ITEMS 26 // sum of items above. Use it for "All inactive" in function calls too.

static void refresh_oscam(enum refreshtypes refreshtype) {

	switch (refreshtype) {
		case REFR_ACCOUNTS:
		cs_log("Refresh Accounts requested by WebIF from %s", cs_inet_ntoa(GET_IP()));
		cs_accounts_chk();
		break;

		case REFR_CLIENTS:
		cs_log("Refresh Clients requested by WebIF from %s", cs_inet_ntoa(GET_IP()));
		cs_reinit_clients(cfg.account);
		break;

		case REFR_SERVER:
		cs_log("Refresh Server requested by WebIF from %s", cs_inet_ntoa(GET_IP()));
		//kill(first_client->pid, SIGHUP);
		//todo how I can refresh the server after global settings
		break;

		case REFR_SERVICES:
		cs_log("Refresh Services requested by WebIF from %s", cs_inet_ntoa(GET_IP()));
		//init_sidtab();
		cs_accounts_chk();
		break;

#ifdef CS_ANTICASC
		case REFR_ANTICASC:
		cs_log("Refresh Anticascading requested by WebIF from %s", cs_inet_ntoa(GET_IP()));
		ac_init_stat();
		struct s_client *cl;
		struct s_auth *account;
		for (cl=first_client->next; cl ; cl=cl->next){
			if (cl->typ=='c' && (account = cl->account)) {
				 cl->ac_limit	= (account->ac_users * 100 + 80) * cfg.ac_stime;
			}
		}
		break;
#endif
		default:
			break;
	}
}
/*
 * load historical values from ringbuffer and return it in the right order
 * as string. Value should be freed with free_mk_t()
 */
char *get_ecm_historystring(struct s_client *cl){

	if(cl){
		int32_t k, i, pos = 0, needed = 1, v;
		char *value, *dot = "";
		int32_t ptr = cl->cwlastresptimes_last;

		needed = CS_ECM_RINGBUFFER_MAX * 6; //5 digits + delimiter
		if(!cs_malloc(&value, needed * sizeof(char), -1)) return "";

		k = ptr + 1;
		for (i = 0; i < CS_ECM_RINGBUFFER_MAX; i++) {
			if (k >= CS_ECM_RINGBUFFER_MAX)
				k = 0;
			v = cl->cwlastresptimes[k].duration;
			if (v > 0 && v < (int32_t)cfg.ctimeout*5) {
				pos += snprintf(value + pos, needed-pos, "%s%d", dot, v);
				dot=",";
			}
			k++;
		}
		if(strlen(value) == 0){
			free(value);
			return "";
		} else return value;

	} else {
		return "";
	}
}
char *get_ecm_fullhistorystring(struct s_client *cl){

	if(cl){
		int32_t k, i, pos = 0, needed = 1, v;
		char *value, *dot = "";
		int32_t ptr = cl->cwlastresptimes_last;

		needed = CS_ECM_RINGBUFFER_MAX * 20; //5 digits + : + returncode(2) + : + time(10) + delimiter
		if(!cs_malloc(&value, needed * sizeof(char), -1)) return "";

		k = ptr + 1;
		for (i = 0; i < CS_ECM_RINGBUFFER_MAX; i++) {
			if (k >= CS_ECM_RINGBUFFER_MAX)
				k = 0;
			v = cl->cwlastresptimes[k].duration;
			if (v > 0 && v < (int32_t)cfg.ctimeout*5) {
				pos += snprintf(value + pos, needed-pos, "%s%d:%d:%ld", dot, cl->cwlastresptimes[k].duration, cl->cwlastresptimes[k].rc, cl->cwlastresptimes[k].timestamp);
				dot=",";
			}
			k++;
		}

		return (value);

	} else {
		return "";
	}
}

/*
 * Set the active menu to a different CSS class
 */
static void setActiveMenu(struct templatevars *vars, int8_t active)
{
	int8_t i;
	for(i = 0; i < MNU_TOTAL_ITEMS; i++) {
		tpl_printf(vars, TPLADD, "TMP", "MENUACTIVE%d", i);
		if (i == active)
			tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "menu_selected");
		else
			tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "menu");
	}
}

/*
 * Set the active submenu to a different CSS class
 */
static void setActiveSubMenu(struct templatevars *vars, int8_t active)
{
	int8_t i;
	for(i = 0; i < MNU_CFG_TOTAL_ITEMS; i++) {
		tpl_printf(vars, TPLADD, "TMP", "CMENUACTIVE%d", i);
		if (i == active)
			tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "configmenu_selected");
		else
			tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "configmenu");
	}
}

static void webif_save_config(char *section, struct templatevars *vars, struct uriparams *params) {
	if (!streq(getParam(params, "action"), "execute"))
		return;
	if (cfg.http_readonly) {
		tpl_addMsg(vars, "WebIf is in readonly mode. No changes are possible!");
		return;
	}
	int i;
	int cnt = (*params).paramcount;
	for (i = 0; i < cnt; i++) {
		char *token = (*params).params[i];
		char *value = (*params).values[i];
		if (!streq(token, "part") && !streq(token, "action"))
			config_set(section, token, value);
	}
	if (write_config() == 0) {
		tpl_addMsg(vars, "Configuration was saved. You should restart OSCam now.");
		enum refreshtypes ref_type = REFR_SERVER;
		if (streq(getParam(params, "part"), "anticasc"))
			ref_type = REFR_ANTICASC;
		refresh_oscam(ref_type);
	} else {
		tpl_addMsg(vars, "ERROR: Failed to write config file!!!");
	}
}

static char *send_oscam_config_global(struct templatevars *vars, struct uriparams *params) {
	setActiveSubMenu(vars, MNU_CFG_GLOBAL);

	webif_save_config("global", vars, params);

	if (IP_ISSET(cfg.srvip))
	tpl_addVar(vars, TPLADD, "SERVERIP", cs_inet_ntoa(cfg.srvip));
	tpl_printf(vars, TPLADD, "NICE", "%d", cfg.nice);
	tpl_printf(vars, TPLADD, "BINDWAIT", "%d", cfg.bindwait);
	tpl_printf(vars, TPLADD, "NETPRIO", "%d", cfg.netprio);


	if (cfg.usrfile != NULL) tpl_addVar(vars, TPLADD, "USERFILE", cfg.usrfile);
	if (cfg.disableuserfile == 1) tpl_addVar(vars, TPLADD, "DISABLEUSERFILECHECKED", "selected");
	if(cfg.usrfileflag == 1) tpl_addVar(vars, TPLADD, "USERFILEFLAGCHECKED", "selected");
	if (cfg.mailfile != NULL) tpl_addVar(vars, TPLADD, "MAILFILE", cfg.mailfile);
	if (cfg.disablemail == 1) tpl_addVar(vars, TPLADD, "DISABLEMAILCHECKED", "selected");

	char *value = mk_t_logfile();
	tpl_addVar(vars, TPLADD, "LOGFILE", value);
	free_mk_t(value);
	if(cfg.disablelog == 1) tpl_addVar(vars, TPLADD, "DISABLELOGCHECKED", "selected");
	tpl_printf(vars, TPLADD, "MAXLOGSIZE", "%d", cfg.max_log_size);

	if (cfg.cwlogdir != NULL) tpl_addVar(vars, TPLADD, "CWLOGDIR", cfg.cwlogdir);
	if (cfg.emmlogdir != NULL) tpl_addVar(vars, TPLADD, "EMMLOGDIR", cfg.emmlogdir);
	tpl_printf(vars, TPLADD, "LOGHISTORYSIZE", "%u", cfg.loghistorysize);

	tpl_printf(vars, TPLADD, "CLIENTTIMEOUT", "%u", cfg.ctimeout);
	tpl_printf(vars, TPLADD, "FALLBACKTIMEOUT", "%u", cfg.ftimeout);
	tpl_printf(vars, TPLADD, "CLIENTMAXIDLE", "%u", cfg.cmaxidle);
	tpl_printf(vars, TPLADD, "CACHEDELAY", "%u", cfg.delay);

	tpl_printf(vars, TPLADD, "SLEEP", "%d", cfg.tosleep);
	if (cfg.ulparent) tpl_addVar(vars, TPLADD, "UNLOCKPARENTALCHECKED", "selected");

	if (cfg.block_same_ip)   tpl_addVar(vars, TPLADD, "BLOCKSAMEIPCHECKED", "selected");
	if (cfg.block_same_name) tpl_addVar(vars, TPLADD, "BLOCKSAMENAMECHECKED", "selected");

	tpl_printf(vars, TPLADD, "SERIALTIMEOUT", "%d", cfg.srtimeout);


	if (cfg.waitforcards == 1)	tpl_addVar(vars, TPLADD, "WAITFORCARDSCHECKED", "selected");
	tpl_printf(vars, TPLADD, "EXTRADELAY", "%d", cfg.waitforcards_extra_delay);
	if (cfg.preferlocalcards == 1)	tpl_addVar(vars, TPLADD, "PREFERLOCALCARDSCHECKED", "selected");

	if (cfg.c35_suppresscmd08)
		tpl_addVar(vars, TPLADD, "SUPPRESSCMD08", "checked");

	if (cfg.reader_restart_seconds)
		tpl_printf(vars, TPLADD, "READERRESTARTSECONDS", "%d", cfg.reader_restart_seconds);

	tpl_printf(vars, TPLADD, "MAXCACHETIME", "%d", cfg.max_cache_time);

	tpl_printf(vars, TPLADD, "MAXCACHECOUNT", "%d", cfg.max_cache_count);

	if (cfg.dropdups)
		tpl_addVar(vars, TPLADD, "DROPDUPSCHECKED", "selected");

	if (cfg.resolve_gethostbyname == 1)
		tpl_addVar(vars, TPLADD, "RESOLVER1", "selected");
	else
		tpl_addVar(vars, TPLADD, "RESOLVER0", "selected");

	tpl_printf(vars, TPLADD, "FAILBANTIME", "%d", cfg.failbantime);
	tpl_printf(vars, TPLADD, "FAILBANCOUNT", "%d", cfg.failbancount);

	if(cfg.double_check == 1)
		tpl_addVar(vars, TPLADD, "DCHECKCSELECTED", "selected");
	value = mk_t_caidtab(&cfg.double_check_caid);
	tpl_addVar(vars, TPLADD, "DOUBLECHECKCAID", value);
	free_mk_t(value);
		
#if defined(QBOXHD) || defined(__arm__)
	if(cfg.enableled == 1)
		tpl_addVar(vars, TPLADD, "ENABLELEDSELECTED1", "selected");
	else if(cfg.enableled == 2)
		tpl_addVar(vars, TPLADD, "ENABLELEDSELECTED2", "selected");
#endif

#ifdef CS_CACHEEX
	tpl_printf(vars, TPLADD, "CACHEEXWAITTIME", "%d", cfg.cacheex_wait_time);

	if (cfg.cacheex_enable_stats == 1)
		tpl_addVar(vars, TPLADD, "CACHEEXSTATSSELECTED", "selected");

#endif

	return tpl_getTpl(vars, "CONFIGGLOBAL");
}

#ifdef WITH_LB
static char *send_oscam_config_loadbalancer(struct templatevars *vars, struct uriparams *params) {
	setActiveSubMenu(vars, MNU_CFG_LOADBAL);
	
	if(strlen(getParam(params, "button")) > 0){
		if(cfg.http_readonly) {
			tpl_addMsg(vars, "WebIf is in readonly mode. No changes are possible!");
		} else {
			if (strcmp(getParam(params, "button"), "Load Stats") == 0) {
				clear_all_stat();
				load_stat_from_file();
				tpl_addMsg(vars, "Stats loaded from file");
			}
		
			if (strcmp(getParam(params, "button"), "Save Stats") == 0) {
				save_stat_to_file(1);
				tpl_addMsg(vars, "Stats saved to file");
			}
		
			if (strcmp(getParam(params, "button"), "Clear Stats") == 0) {
				clear_all_stat();
				tpl_addMsg(vars, "Stats cleared completly");
			}
		
			if (strcmp(getParam(params, "button"), "Clear Timeouts") == 0) {
				clean_all_stats_by_rc(E_TIMEOUT, 0);
				tpl_addMsg(vars, "Stats cleared Timeouts");
			}
		
			if (strcmp(getParam(params, "button"), "Clear Not Founds") == 0) {
				clean_all_stats_by_rc(E_NOTFOUND, 0);
				tpl_addMsg(vars, "Stats cleared Not Founds");
			}
		}
	}

	webif_save_config("global", vars, params);

	tpl_printf(vars, TPLADD, "TMP", "LBMODE%d", cfg.lb_mode);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

	tpl_printf(vars, TPLADD, "LBSAVE", "%d",cfg.lb_save);
	if(cfg.lb_savepath) tpl_addVar(vars, TPLADD, "LBSAVEPATH", cfg.lb_savepath);

	tpl_printf(vars, TPLADD, "LBNBESTREADERS", "%d",cfg.lb_nbest_readers);
	char *value = mk_t_caidvaluetab(&cfg.lb_nbest_readers_tab);
	tpl_addVar(vars, TPLADD, "LBNBESTPERCAID", value);
	free_mk_t(value);
	tpl_printf(vars, TPLADD, "LBNFBREADERS", "%d",cfg.lb_nfb_readers);
	tpl_printf(vars, TPLADD, "LBMAXREADERS", "%d",cfg.lb_max_readers);
	tpl_printf(vars, TPLADD, "LBMINECMCOUNT", "%d",cfg.lb_min_ecmcount);
	tpl_printf(vars, TPLADD, "LBMAXECEMCOUNT", "%d",cfg.lb_max_ecmcount);
	tpl_printf(vars, TPLADD, "LBRETRYLIMIT", "%d",cfg.lb_retrylimit);

	value = mk_t_caidvaluetab(&cfg.lb_retrylimittab);
	tpl_addVar(vars, TPLADD, "LBRETRYLIMITS", value);
	free_mk_t(value);

	tpl_printf(vars, TPLADD, "LBREOPENSECONDS", "%d",cfg.lb_reopen_seconds);
	tpl_printf(vars, TPLADD, "LBCLEANUP", "%d",cfg.lb_stat_cleanup);
	if (cfg.lb_reopen_mode) tpl_addVar(vars, TPLADD, "REOPENMODE", "selected");

	value = mk_t_caidtab(&cfg.lb_noproviderforcaid);
	tpl_addVar(vars, TPLADD, "LBNOPROVIDERFORCAID", value);
	free_mk_t(value);

	if (cfg.lb_auto_betatunnel) tpl_addVar(vars, TPLADD, "LBAUTOBETATUNNEL", "selected");
	tpl_printf(vars, TPLADD, "LBPREFERBETA", "%d", cfg.lb_auto_betatunnel_prefer_beta);

	return tpl_getTpl(vars, "CONFIGLOADBALANCER");
}
#endif

#ifdef MODULE_CAMD33
static char *send_oscam_config_camd33(struct templatevars *vars, struct uriparams *params) {
	int32_t i;

	setActiveSubMenu(vars, MNU_CFG_CAMD33);

	webif_save_config("camd33", vars, params);

	if (cfg.c33_port) {
		tpl_printf(vars, TPLADD, "PORT", "%d", cfg.c33_port);
		if (IP_ISSET(cfg.c33_srvip))	tpl_addVar(vars, TPLADD, "SERVERIP", cs_inet_ntoa(cfg.c33_srvip));
		if (cfg.c33_passive == 1)		tpl_addVar(vars, TPLADD, "PASSIVECHECKED", "selected");

		for (i = 0; i < (int) sizeof(cfg.c33_key); ++i) tpl_printf(vars, TPLAPPEND, "KEY", "%02X",cfg.c33_key[i]);
		char *value = mk_t_iprange(cfg.c33_plain);
		tpl_addVar(vars, TPLADD, "NOCRYPT", value);
		free_mk_t(value);
	}

	return tpl_getTpl(vars, "CONFIGCAMD33");
}
#endif

#ifdef MODULE_CAMD35
static char *send_oscam_config_camd35(struct templatevars *vars, struct uriparams *params) {
	setActiveSubMenu(vars, MNU_CFG_CAMD35);

	webif_save_config("cs357x", vars, params);

	if (cfg.c35_port) {
		tpl_printf(vars, TPLADD, "PORT", "%d", cfg.c35_port);
		if (IP_ISSET(cfg.c35_srvip))
			tpl_addVar(vars, TPLAPPEND, "SERVERIP", cs_inet_ntoa(cfg.c35_srvip));

		if (cfg.c35_udp_suppresscmd08)
			tpl_addVar(vars, TPLADD, "SUPPRESSCMD08UDP", "checked");

	}
	return tpl_getTpl(vars, "CONFIGCAMD35");
}
#endif

#ifdef MODULE_CAMD35_TCP
static char *send_oscam_config_camd35tcp(struct templatevars *vars, struct uriparams *params) {
	setActiveSubMenu(vars, MNU_CFG_CAMD35TCP);

	webif_save_config("cs378x", vars, params);

	if ((cfg.c35_tcp_ptab.nports > 0) && (cfg.c35_tcp_ptab.ports[0].s_port > 0)) {

		char *value = mk_t_camd35tcp_port();
		tpl_addVar(vars, TPLADD, "PORT", value);
		free_mk_t(value);

		if (IP_ISSET(cfg.c35_tcp_srvip))
			tpl_addVar(vars, TPLAPPEND, "SERVERIP", cs_inet_ntoa(cfg.c35_tcp_srvip));

		if (cfg.c35_tcp_suppresscmd08)
			tpl_addVar(vars, TPLADD, "SUPPRESSCMD08TCP", "checked");
	}
	return tpl_getTpl(vars, "CONFIGCAMD35TCP");
}
#endif

#ifdef CS_CACHEEX
static char *send_oscam_config_csp(struct templatevars *vars, struct uriparams *params) {
	setActiveSubMenu(vars, MNU_CFG_CSP);

	webif_save_config("csp", vars, params);

	if (IP_ISSET(cfg.csp_srvip))
		tpl_addVar(vars, TPLAPPEND, "SERVERIP", cs_inet_ntoa(cfg.csp_srvip));

	if (cfg.csp_port)
		tpl_printf(vars, TPLADD, "PORT", "%d", cfg.csp_port);

	if (cfg.csp_wait_time)
		tpl_printf(vars, TPLADD, "WAIT_TIME", "%d", cfg.csp_wait_time);

	return tpl_getTpl(vars, "CONFIGCSP");
}
#endif

#ifdef MODULE_NEWCAMD
static char *send_oscam_config_newcamd(struct templatevars *vars, struct uriparams *params) {
	int32_t i;

	setActiveSubMenu(vars, MNU_CFG_NEWCAMD);

	webif_save_config("newcamd", vars, params);

	if ((cfg.ncd_ptab.nports > 0) && (cfg.ncd_ptab.ports[0].s_port > 0)) {

		char *value = mk_t_newcamd_port();
		tpl_addVar(vars, TPLADD, "PORT", value);
		free_mk_t(value);

		if (IP_ISSET(cfg.ncd_srvip))
			tpl_addVar(vars, TPLADD, "SERVERIP", cs_inet_ntoa(cfg.ncd_srvip));

		for (i = 0; i < 14; i++) tpl_printf(vars, TPLAPPEND, "KEY", "%02X", cfg.ncd_key[i]);

		value = mk_t_iprange(cfg.ncd_allowed);
		tpl_addVar(vars, TPLADD, "ALLOWED", value);
		free_mk_t(value);

		if (cfg.ncd_keepalive)
			tpl_addVar(vars, TPLADD, "KEEPALIVE", "checked");
		if (cfg.ncd_mgclient)
			tpl_addVar(vars, TPLADD, "MGCLIENTCHK", "checked");
	}
	return tpl_getTpl(vars, "CONFIGNEWCAMD");
}
#endif

#ifdef MODULE_RADEGAST
static char *send_oscam_config_radegast(struct templatevars *vars, struct uriparams *params) {
	setActiveSubMenu(vars, MNU_CFG_RADEGAST);

	webif_save_config("radegast", vars, params);

	tpl_printf(vars, TPLADD, "PORT", "%d", cfg.rad_port);
	if (IP_ISSET(cfg.rad_srvip))
	tpl_addVar(vars, TPLADD, "SERVERIP", cs_inet_ntoa(cfg.rad_srvip));
	tpl_addVar(vars, TPLADD, "USER", cfg.rad_usr);

	char *value = mk_t_iprange(cfg.rad_allowed);
	tpl_addVar(vars, TPLADD, "ALLOWED", value);
	free_mk_t(value);

	return tpl_getTpl(vars, "CONFIGRADEGAST");
}
#endif

#ifdef MODULE_CCCAM
static char *send_oscam_config_cccam(struct templatevars *vars, struct uriparams *params) {

	setActiveSubMenu(vars, MNU_CFG_CCCAM);

	if (strcmp(getParam(params, "button"), "Refresh global list") == 0) {
		cs_debug_mask(D_TRACE, "Entitlements: Refresh Shares start");
#ifdef MODULE_CCCSHARE
		refresh_shares();
#endif
		cs_debug_mask(D_TRACE, "Entitlements: Refresh Shares finished");
		tpl_addMsg(vars, "Refresh Shares started");
	}

	webif_save_config("cccam", vars, params);

	if (streq(getParam(params, "action"), "execute") && !cfg.http_readonly)
		cc_update_nodeid();

	char *value = mk_t_cccam_port();
	tpl_addVar(vars, TPLAPPEND, "PORT", value);
	free_mk_t(value);

	tpl_printf(vars, TPLADD, "RESHARE", "%d", cfg.cc_reshare);

	if (!strcmp((char*)cfg.cc_version,"2.0.11")) {
		tpl_addVar(vars, TPLADD, "VERSIONSELECTED0", "selected");
	} else if (!strcmp((char*)cfg.cc_version,"2.1.1")) {
		tpl_addVar(vars, TPLADD, "VERSIONSELECTED1", "selected");
	} else if (!strcmp((char*)cfg.cc_version,"2.1.2")) {
		tpl_addVar(vars, TPLADD, "VERSIONSELECTED2", "selected");
	} else if (!strcmp((char*)cfg.cc_version,"2.1.3")) {
		tpl_addVar(vars, TPLADD, "VERSIONSELECTED3", "selected");
	} else if (!strcmp((char*)cfg.cc_version,"2.1.4")) {
		tpl_addVar(vars, TPLADD, "VERSIONSELECTED4", "selected");
	} else if (!strcmp((char*)cfg.cc_version,"2.2.0")) {
		tpl_addVar(vars, TPLADD, "VERSIONSELECTED5", "selected");
	} else if (!strcmp((char*)cfg.cc_version,"2.2.1")) {
		tpl_addVar(vars, TPLADD, "VERSIONSELECTED6", "selected");
	} else if (!strcmp((char*)cfg.cc_version,"2.3.0")) {
		tpl_addVar(vars, TPLADD, "VERSIONSELECTED7", "selected");
	}

	tpl_printf(vars, TPLADD, "UPDATEINTERVAL", "%d", cfg.cc_update_interval);
	if (cfg.cc_stealth)
		tpl_addVar(vars, TPLADD, "STEALTH", "selected");

	tpl_printf(vars, TPLADD, "NODEID", "%02X%02X%02X%02X%02X%02X%02X%02X",
		cfg.cc_fixed_nodeid[0], cfg.cc_fixed_nodeid[1], cfg.cc_fixed_nodeid[2], cfg.cc_fixed_nodeid[3],
	    cfg.cc_fixed_nodeid[4], cfg.cc_fixed_nodeid[5], cfg.cc_fixed_nodeid[6], cfg.cc_fixed_nodeid[7]);

	tpl_printf(vars, TPLADD, "TMP", "MINIMIZECARDSELECTED%d", cfg.cc_minimize_cards);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

	tpl_printf(vars, TPLADD, "TMP", "RESHAREMODE%d", cfg.cc_reshare_services);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

	tpl_printf(vars, TPLADD, "TMP", "IGNRSHRSELECTED%d", cfg.cc_ignore_reshare);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

	if (cfg.cc_forward_origin_card)
		tpl_addVar(vars, TPLADD, "FORWARDORIGINCARD", "selected");

	if (cfg.cc_keep_connected)
		tpl_addVar(vars, TPLADD, "KEEPCONNECTED", "selected");


	return tpl_getTpl(vars, "CONFIGCCCAM");
}
#endif

static char *send_oscam_config_monitor(struct templatevars *vars, struct uriparams *params) {
	int32_t i;

	setActiveSubMenu(vars, MNU_CFG_MONITOR);

	if (strcmp(getParam(params, "action"),"execute") == 0) {
		if(cfg.http_readonly) {
			tpl_addMsg(vars, "WebIf is in readonly mode. No changes are possible!");
		} else {
			for(i = 0; i < (*params).paramcount; ++i) {
				if ((strcmp((*params).params[i], "part")) && (strcmp((*params).params[i], "action"))) {
					//we use the same function as used for parsing the config tokens
					if (strstr((*params).params[i], "lcd")) {
						config_set("lcd", (*params).params[i], (*params).values[i]);
					} else {
						// FIXME: The following is A HACK! It must be removed when monitor/webif/lcd
						// options are split.
						// The hack is added so we don't get such errors when saving webif:
						//     WARNING: In section [webif] unknown setting 'port=1122' tried.
						int r, ok = 1;
						const char *monitor_hack[4] = { "port", "serverip", "nocrypt", "monlevel" };
						for (r = 0; r < 4; r++) {
							if (streq(monitor_hack[r], (*params).params[i])) {
								ok = 0;
								break;
							}
						}
						if (ok)
							config_set("webif", (*params).params[i], (*params).values[i]);
#ifdef MODULE_MONITOR
						if (!strstr((*params).params[i], "http"))
							config_set("monitor", (*params).params[i], (*params).values[i]);
#endif
					}
				}
			}
			tpl_addMsg(vars, "Configuration was saved. You should restart OSCam now.");
			if(write_config()==0) refresh_oscam(REFR_SERVER);
			else tpl_addMsg(vars, "Write Config failed!");
		}
	}
	tpl_printf(vars, TPLADD, "MONPORT", "%d", cfg.mon_port);
	if (IP_ISSET(cfg.mon_srvip))
	tpl_addVar(vars, TPLADD, "SERVERIP", cs_inet_ntoa(cfg.mon_srvip));

	tpl_printf(vars, TPLADD, "AULOW", "%d", cfg.aulow);
	tpl_printf(vars, TPLADD, "HIDECLIENTTO", "%d", cfg.hideclient_to);
	if (cfg.appendchaninfo)
		tpl_addVar(vars, TPLADD, "APPENDCHANINFO", "checked");

	tpl_printf(vars, TPLADD, "HTTPPORT", "%s%d", cfg.http_use_ssl ? "+" : "", cfg.http_port);

	tpl_addVar(vars, TPLADD, "HTTPUSER", cfg.http_user);
	tpl_addVar(vars, TPLADD, "HTTPPASSWORD", cfg.http_pwd);

	// css style selector
	tpl_printf(vars, TPLADD, "CSSOPTIONS", "\t\t\t\t\t\t<option value=\"\"%s>embedded</option>\n",
		cfg.http_css ? " selected" : "");
		
	if(cfg.http_tpl) {
		char path[255];
		tpl_getFilePathInSubdir(cfg.http_tpl, "", "style", ".css", path, 255);
		if(file_exists(path))
			tpl_printf(vars, TPLAPPEND, "CSSOPTIONS", "\t\t\t\t\t\t<option value=\"%s\"%s>%s (template)</option>\n",
				path,
				cfg.http_css && strstr(cfg.http_css, path) ? " selected" : "",
				path);
	}

	DIR *hdir;
	struct dirent entry;
	struct dirent *result;
	if((hdir = opendir(cs_confdir)) != NULL){
		while(cs_readdir_r(hdir, &entry, &result) == 0 && result != NULL){
			if (strCmpSuffix(entry.d_name, ".css")) {
				tpl_printf(vars, TPLAPPEND, "CSSOPTIONS", "\t\t\t\t\t\t<option value=\"%s%s\"%s>%s%s</option>\n",
					cs_confdir,
					entry.d_name,
					cfg.http_css && strstr(cfg.http_css, entry.d_name) ? " selected" : "",
					cs_confdir,entry.d_name);
			}
		}
		closedir(hdir);
	}

	if(cfg.http_prepend_embedded_css)
		tpl_addVar(vars, TPLADD, "HTTPPREPENDEMBEDDEDCSS", "checked");

	tpl_addVar(vars, TPLADD, "HTTPHELPLANG", cfg.http_help_lang);
	tpl_printf(vars, TPLADD, "HTTPREFRESH", "%d", cfg.http_refresh);
	tpl_addVar(vars, TPLADD, "HTTPTPL", cfg.http_tpl);
	tpl_addVar(vars, TPLADD, "HTTPSCRIPT", cfg.http_script);
	tpl_addVar(vars, TPLADD, "HTTPJSCRIPT", cfg.http_jscript);

	if (cfg.http_hide_idle_clients > 0) tpl_addVar(vars, TPLADD, "CHECKED", "checked");
	if (cfg.http_showpicons > 0) tpl_addVar(vars, TPLADD, "SHOWPICONSCHECKED", "checked");

	char *value = mk_t_iprange(cfg.mon_allowed);
	tpl_addVar(vars, TPLADD, "NOCRYPT", value);
	free_mk_t(value);

	value = mk_t_iprange(cfg.http_allowed);
	tpl_addVar(vars, TPLADD, "HTTPALLOW", value);
	free_mk_t(value);

	for(i = 0; i < MAX_HTTP_DYNDNS; i++){
		if(cfg.http_dyndns[i][0]){
			tpl_addVar(vars, TPLAPPEND, "HTTPDYNDNS", i>0 ? "," : "");
			tpl_addVar(vars, TPLAPPEND, "HTTPDYNDNS", (char*)cfg.http_dyndns[i]);
		}
	}

	//Monlevel selector
	tpl_printf(vars, TPLADD, "TMP", "MONSELECTED%d", cfg.mon_level);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

	if (cfg.http_full_cfg)
		tpl_addVar(vars, TPLADD, "HTTPSAVEFULLSELECT", "selected");

#ifdef WITH_SSL
	if (cfg.http_force_sslv3)
		tpl_addVar(vars, TPLADD, "HTTPFORCESSLV3SELECT", "selected");
#endif

#ifdef LCDSUPPORT
	if(cfg.enablelcd)
		tpl_addVar(vars, TPLADD, "ENABLELCDSELECTED", "selected");
	if (cfg.lcd_output_path != NULL)
		tpl_addVar(vars, TPLADD, "LCDOUTPUTPATH", cfg.lcd_output_path);
	if (cfg.lcd_hide_idle)
		tpl_addVar(vars, TPLADD, "LCDHIDEIDLE", "selected");
	tpl_printf(vars, TPLADD, "LCDREFRESHINTERVAL", "%d", cfg.lcd_write_intervall);
#endif

	return tpl_getTpl(vars, "CONFIGMONITOR");
}

#ifdef MODULE_SERIAL
static char *send_oscam_config_serial(struct templatevars *vars, struct uriparams *params) {
	setActiveSubMenu(vars, MNU_CFG_SERIAL);

	webif_save_config("serial", vars, params);

	if (cfg.ser_device) {
		char sdevice[strlen(cfg.ser_device)];
		cs_strncpy(sdevice, cfg.ser_device, sizeof(sdevice));
		char *ptr, *saveptr1 = NULL;
		char delimiter[2]; delimiter[0] = 1; delimiter[1] = '\0';
		for(ptr = strtok_r(sdevice, delimiter, &saveptr1); ptr; ptr = strtok_r(NULL, delimiter, &saveptr1)){
			tpl_addVar(vars, TPLADD, "SERIALDEVICE", ptr);
			tpl_addVar(vars, TPLAPPEND, "DEVICES", tpl_getTpl(vars, "CONFIGSERIALDEVICEBIT"));
		}
	}

	tpl_addVar(vars, TPLADD, "SERIALDEVICE", "");
	tpl_addVar(vars, TPLAPPEND, "DEVICES", tpl_getTpl(vars, "CONFIGSERIALDEVICEBIT"));

	return tpl_getTpl(vars, "CONFIGSERIAL");
}
#endif

#ifdef HAVE_DVBAPI
static char *send_oscam_config_dvbapi(struct templatevars *vars, struct uriparams *params) {
	int32_t i;

	setActiveSubMenu(vars, MNU_CFG_DVBAPI);

	webif_save_config("dvbapi", vars, params);

	if (cfg.dvbapi_enabled > 0)
		tpl_addVar(vars, TPLADD, "ENABLEDCHECKED", "checked");

	if (cfg.dvbapi_au > 0)
		tpl_addVar(vars, TPLADD, "AUCHECKED", "checked");

	if (cfg.dvbapi_reopenonzap > 0)
		tpl_addVar(vars, TPLADD, "REOPENONZAPCHECKED", "checked");

	if (cfg.dvbapi_delayer > 0)
		tpl_printf(vars, TPLADD, "DELAYER", "%d", cfg.dvbapi_delayer);

	tpl_printf(vars, TPLADD, "BOXTYPE", "<option value=\"\"%s>None</option>\n", cfg.dvbapi_boxtype == 0 ? " selected" : "");
	for (i=1; i<=BOXTYPES; i++) {
		tpl_printf(vars, TPLAPPEND, "BOXTYPE", "<option%s>%s</option>\n", cfg.dvbapi_boxtype == i ? " selected" : "", boxdesc[i]);
	}

	tpl_addVar(vars, TPLADD, "USER", cfg.dvbapi_usr);

	//PMT Mode
	tpl_printf(vars, TPLADD, "TMP", "PMTMODESELECTED%d", cfg.dvbapi_pmtmode);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

	//Request Mode
	tpl_printf(vars, TPLADD, "TMP", "REQMODESELECTED%d", cfg.dvbapi_requestmode);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

	return tpl_getTpl(vars, "CONFIGDVBAPI");
}
#endif

#ifdef CS_ANTICASC
static char *send_oscam_config_anticasc(struct templatevars *vars, struct uriparams *params) {
	setActiveSubMenu(vars, MNU_CFG_ANTICASC);

	webif_save_config("anticasc", vars, params);

	if (cfg.ac_enabled > 0) tpl_addVar(vars, TPLADD, "CHECKED", "checked");
	tpl_printf(vars, TPLADD, "NUMUSERS", "%d", cfg.ac_users);
	tpl_printf(vars, TPLADD, "SAMPLETIME", "%d", cfg.ac_stime);
	tpl_printf(vars, TPLADD, "SAMPLES", "%d", cfg.ac_samples);

	tpl_printf(vars, TPLADD, "TMP", "PENALTY%d", cfg.ac_penalty);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

	if (cfg.ac_logfile)
		tpl_addVar(vars, TPLADD, "ACLOGFILE", cfg.ac_logfile);
	tpl_printf(vars, TPLADD, "FAKEDELAY", "%d", cfg.ac_fakedelay);
	tpl_printf(vars, TPLADD, "DENYSAMPLES", "%d", cfg.ac_denysamples);
	return tpl_getTpl(vars, "CONFIGANTICASC");
}
#endif

static char *send_oscam_config(struct templatevars *vars, struct uriparams *params) {

	setActiveMenu(vars, MNU_CONFIG);

	char *part = getParam(params, "part");
	if (!strcmp(part,"monitor")) return send_oscam_config_monitor(vars, params);
#ifdef MODULE_CAMD33
	else if (!strcmp(part,"camd33")) return send_oscam_config_camd33(vars, params);
#endif
#ifdef MODULE_CAMD35
	else if (!strcmp(part,"camd35")) return send_oscam_config_camd35(vars, params);
#endif
#ifdef MODULE_CAMD35_TCP
	else if (!strcmp(part,"camd35tcp")) return send_oscam_config_camd35tcp(vars, params);
#endif
#ifdef CS_CACHEEX
	else if (!strcmp(part,"csp")) return send_oscam_config_csp(vars, params);
#endif
#ifdef MODULE_NEWCAMD
	else if (!strcmp(part,"newcamd")) return send_oscam_config_newcamd(vars, params);
#endif
#ifdef MODULE_RADEGAST
	else if (!strcmp(part,"radegast")) return send_oscam_config_radegast(vars, params);
#endif
#ifdef MODULE_CCCAM
	else if (!strcmp(part,"cccam")) return send_oscam_config_cccam(vars, params);
#endif
#ifdef HAVE_DVBAPI
	else if (!strcmp(part,"dvbapi")) return send_oscam_config_dvbapi(vars, params);
#endif
#ifdef CS_ANTICASC
	else if (!strcmp(part,"anticasc")) return send_oscam_config_anticasc(vars, params);
#endif
#ifdef MODULE_SERIAL
	else if (!strcmp(part,"serial")) return send_oscam_config_serial(vars, params);
#endif
#ifdef WITH_LB
	else if (!strcmp(part,"loadbalancer")) return send_oscam_config_loadbalancer(vars, params);
#endif
	else return send_oscam_config_global(vars, params);
}

static void inactivate_reader(struct s_reader *rdr)
{
	struct s_client *cl = rdr->client;
	if (cl)
		kill_thread(cl);
}

static char *send_oscam_reader(struct templatevars *vars, struct uriparams *params, int32_t apicall) {
	struct s_reader *rdr;
	int32_t i;

	if(!apicall) setActiveMenu(vars, MNU_READERS);

	if ((strcmp(getParam(params, "action"), "disable") == 0) || (strcmp(getParam(params, "action"), "enable") == 0)) {
		if(cfg.http_readonly) {
			tpl_addMsg(vars, "WebIf is in readonly mode. Enabling or disabling readers is not possible!");
		} else {
			rdr = get_reader_by_label(getParam(params, "label"));
			if (rdr) {
				if (strcmp(getParam(params, "action"), "enable") == 0) {
					if (!rdr->enable) {
						rdr->enable = 1;
					}
				} else {
					if (rdr->enable) {
						rdr->enable = 0;
					}
				}
				restart_cardreader(rdr, 1);
				if(write_server() != 0) tpl_addMsg(vars, "Write Config failed!");
			}
		}
	}

	if (strcmp(getParam(params, "action"), "delete") == 0) {
		if(cfg.http_readonly) {
			tpl_addMsg(vars, "WebIf is in readonly mode. No deletion will be made!");
		} else {
			rdr = get_reader_by_label(getParam(params, "label"));
			if (rdr) {
				inactivate_reader(rdr);
				ll_remove(configured_readers, rdr);

				free_reader(rdr);

				if(write_server()!=0) tpl_addMsg(vars, "Write Config failed!");
			}
		}
	}

	if (strcmp(getParam(params, "action"), "reread") == 0) {
		rdr = get_reader_by_label(getParam(params, "label"));
		if (rdr) {
			struct s_client *cl = rdr->client;
			//reset the counters
			for (i = 0; i < 4; i++) {
				rdr->emmerror[i] = 0;
				rdr->emmwritten[i] = 0;
				rdr->emmskipped[i] = 0;
				rdr->emmblocked[i] = 0;
			}

			if(rdr->enable == 1 && cl && cl->typ == 'r') {
				add_job(cl, ACTION_READER_CARDINFO, NULL, 0);
			}
		}
	}

	LL_ITER itr = ll_iter_create(configured_readers);

	if(!apicall) {
		for (i = 0, rdr = ll_iter_next(&itr); rdr && rdr->label[0]; rdr = ll_iter_next(&itr), i++);
		tpl_printf(vars, TPLADD, "NEXTREADER", "Reader-%d", i); //Next Readername
	}

	ll_iter_reset(&itr); //going to iterate all configured readers
	while ((rdr = ll_iter_next(&itr))) {
		struct s_client *cl = rdr->client;
		if(rdr->label[0] && rdr->typ) {

			// used for API and WebIf
			tpl_addVar(vars, TPLADD, "READERNAME", xml_encode(vars, rdr->label));
			tpl_addVar(vars, TPLADD, "READERNAMEENC", urlencode(vars, rdr->label));
			tpl_addVar(vars, TPLADD, "CTYP", reader_get_type_desc(rdr, 0));

			// used only for WebIf
			if(!apicall){
				if (rdr->enable)
					tpl_addVar(vars, TPLADD, "READERCLASS", "enabledreader");
				else
					tpl_addVar(vars, TPLADD, "READERCLASS", "disabledreader");

				tpl_printf(vars, TPLADD, "EMMERRORUK", "%d", rdr->emmerror[UNKNOWN]);
				tpl_printf(vars, TPLADD, "EMMERRORG", "%d", rdr->emmerror[GLOBAL]);
				tpl_printf(vars, TPLADD, "EMMERRORS", "%d", rdr->emmerror[SHARED]);
				tpl_printf(vars, TPLADD, "EMMERRORUQ", "%d", rdr->emmerror[UNIQUE]);

				tpl_printf(vars, TPLADD, "EMMWRITTENUK", "%d", rdr->emmwritten[UNKNOWN]);
				tpl_printf(vars, TPLADD, "EMMWRITTENG", "%d", rdr->emmwritten[GLOBAL]);
				tpl_printf(vars, TPLADD, "EMMWRITTENS", "%d", rdr->emmwritten[SHARED]);
				tpl_printf(vars, TPLADD, "EMMWRITTENUQ", "%d", rdr->emmwritten[UNIQUE]);

				tpl_printf(vars, TPLADD, "EMMSKIPPEDUK", "%d", rdr->emmskipped[UNKNOWN]);
				tpl_printf(vars, TPLADD, "EMMSKIPPEDG", "%d", rdr->emmskipped[GLOBAL]);
				tpl_printf(vars, TPLADD, "EMMSKIPPEDS", "%d", rdr->emmskipped[SHARED]);
				tpl_printf(vars, TPLADD, "EMMSKIPPEDUQ", "%d", rdr->emmskipped[UNIQUE]);

				tpl_printf(vars, TPLADD, "EMMBLOCKEDUK", "%d", rdr->emmblocked[UNKNOWN]);
				tpl_printf(vars, TPLADD, "EMMBLOCKEDG", "%d", rdr->emmblocked[GLOBAL]);
				tpl_printf(vars, TPLADD, "EMMBLOCKEDS", "%d", rdr->emmblocked[SHARED]);
				tpl_printf(vars, TPLADD, "EMMBLOCKEDUQ", "%d", rdr->emmblocked[UNIQUE]);

				tpl_printf(vars, TPLADD, "ECMSOK", "%d (%.2f%%)", rdr->ecmsok, rdr->ecmshealthok);
				tpl_printf(vars, TPLADD, "ECMSNOK", "%d (%.2f%%)", rdr->ecmsnok, rdr->ecmshealthnok);
				tpl_printf(vars, TPLADD, "ECMSFILTEREDHEAD", "%d", rdr->ecmsfilteredhead);
				tpl_printf(vars, TPLADD, "ECMSFILTEREDLEN", "%d", rdr->ecmsfilteredlen);
#ifdef WITH_LB
				tpl_printf(vars, TPLADD, "LBWEIGHT", "%d", rdr->lb_weight);
#endif
				if (!(rdr->typ & R_IS_NETWORK)) { //reader is physical
					tpl_addVar(vars, TPLADD, "REFRICO", "image?i=ICREF");
					tpl_addVar(vars, TPLADD, "READERREFRESH", tpl_getTpl(vars, "READERREFRESHBIT"));
					tpl_addVar(vars, TPLADD, "ENTICO", "image?i=ICENT");
					tpl_addVar(vars, TPLADD, "ENTITLEMENT", tpl_getTpl(vars, "READERENTITLEBIT"));
				} else {
					tpl_addVar(vars, TPLADD, "READERREFRESH","");
					if (rdr->typ == R_CCCAM) {
						tpl_addVar(vars, TPLADD, "ENTICO", "image?i=ICENT");
						tpl_addVar(vars, TPLADD, "ENTITLEMENT", tpl_getTpl(vars, "READERENTITLEBIT"));
					} else {
						tpl_addVar(vars, TPLADD, "ENTITLEMENT","");
					}
				}

				if(rdr->enable == 0) {
					tpl_addVar(vars, TPLADD, "SWITCHICO", "image?i=ICENA");
					tpl_addVar(vars, TPLADD, "SWITCHTITLE", "enable this reader");
					tpl_addVar(vars, TPLADD, "SWITCH", "enable");
				} else {
					tpl_addVar(vars, TPLADD, "SWITCHICO", "image?i=ICDIS");
					tpl_addVar(vars, TPLADD, "SWITCHTITLE", "disable this reader");
					tpl_addVar(vars, TPLADD, "SWITCH", "disable");
				}

				// Add to WebIf Template
				tpl_addVar(vars, TPLAPPEND, "READERLIST", tpl_getTpl(vars, "READERSBIT"));

			} else {

				// used only for API
				tpl_addVar(vars, TPLADD, "APIREADERENABLED", !rdr->enable ? "0": "1");
				if(cl)
					tpl_printf(vars, TPLADD, "APIREADERTYPE", "%c", cl->typ ? cl->typ :'x');

				// Add to API Template
				tpl_addVar(vars, TPLAPPEND, "APIREADERLIST", tpl_getTpl(vars, "APIREADERSBIT"));
			}
		}
	}

	if(!apicall) {
#ifdef WITH_PCSC
		tpl_addVar(vars, TPLAPPEND, "ADDPROTOCOL", "<option>pcsc</option>\n");
#endif
#ifdef MODULE_CAMD33
		tpl_addVar(vars, TPLAPPEND, "ADDPROTOCOL", "<option>camd33</option>\n");
#endif
#ifdef MODULE_CAMD35
		tpl_addVar(vars, TPLAPPEND, "ADDPROTOCOL", "<option>camd35</option>\n");
#endif
#ifdef MODULE_CAMD35_TCP
		tpl_addVar(vars, TPLAPPEND, "ADDPROTOCOL", "<option>cs378x</option>\n");
#endif
#ifdef MODULE_NEWCAMD
		tpl_addVar(vars, TPLAPPEND, "ADDPROTOCOL", "<option>newcamd</option>\n");
		tpl_addVar(vars, TPLAPPEND, "ADDPROTOCOL", "<option>newcamd524</option>\n");
#endif
#ifdef MODULE_CCCAM
		tpl_addVar(vars, TPLAPPEND, "ADDPROTOCOL", "<option>cccam</option>\n");
#endif
#ifdef MODULE_GBOX
		tpl_addVar(vars, TPLAPPEND, "ADDPROTOCOL", "<option>gbox</option>\n");
#endif
#ifdef MODULE_RADEGAST
		tpl_addVar(vars, TPLAPPEND, "ADDPROTOCOL", "<option>radegast</option>\n");
#endif
#ifdef MODULE_SERIAL
		tpl_addVar(vars, TPLAPPEND, "ADDPROTOCOL", "<option>serial</option>\n");
#endif
#ifdef MODULE_CONSTCW
		tpl_addVar(vars, TPLAPPEND, "ADDPROTOCOL", "<option>constcw</option>\n");
#endif

		for (i=0; i<CS_MAX_MOD; i++) {
			if (cardreader[i].desc)
				tpl_printf(vars, TPLAPPEND, "ADDPROTOCOL", "<option>%s</option>\n", xml_encode(vars, cardreader[i].desc));
		}
		return tpl_getTpl(vars, "READERS");
	} else {
		return tpl_getTpl(vars, "APIREADERS");
	}
}

static char *send_oscam_reader_config(struct templatevars *vars, struct uriparams *params) {
	int32_t i;
	int32_t apicall = 0;
	char *reader_ = getParam(params, "label");
	char *value;

	struct s_reader *rdr;

	if(!apicall) setActiveMenu(vars, MNU_READERS);

	if(strcmp(getParam(params, "action"), "Add") == 0) {
		// Add new reader
		struct s_reader *newrdr;
		if(!cs_malloc(&newrdr,sizeof(struct s_reader), -1)) return "0";
		for (i = 0; i < (*params).paramcount; ++i) {
			if (strcmp((*params).params[i], "action"))
				chk_reader((*params).params[i], (*params).values[i], newrdr);
		}
		if (newrdr->typ & R_IS_CASCADING) {
			for (i=0; i<CS_MAX_MOD; i++) {
				if (ph[i].num && newrdr->typ==ph[i].num) {
					newrdr->ph=ph[i];
					if(newrdr->device[0]) newrdr->ph.active=1;
				}
			}
		}
		reader_ = newrdr->label;
		reader_set_defaults(newrdr);
		newrdr->enable = 0; // do not start the reader because must configured before
		ll_append(configured_readers, newrdr);
	} else if(strcmp(getParam(params, "action"), "Save") == 0) {

		rdr = get_reader_by_label(getParam(params, "label"));
		//if (rdr->typ & R_IS_NETWORK)
		//	inactivate_reader(rdr); //Stop reader before reinitialization
		char servicelabels[1024]="";

		for(i = 0; i < (*params).paramcount; ++i) {
			if ((strcmp((*params).params[i], "reader")) && (strcmp((*params).params[i], "action"))) {
				if (!strcmp((*params).params[i], "services"))
					snprintf(servicelabels + strlen(servicelabels), sizeof(servicelabels) - strlen(servicelabels), "%s,", (*params).values[i]);
				else
					/*if(strlen((*params).values[i]) > 0)*/
						chk_reader((*params).params[i], (*params).values[i], rdr);
			}
			//printf("param %s value %s\n",(*params).params[i], (*params).values[i]);
		}
		chk_reader("services", servicelabels, rdr);

		if (rdr->typ & R_IS_NETWORK) { //physical readers make trouble if re-started
			restart_cardreader(rdr, 1);
		}

		if(write_server()!=0) tpl_addMsg(vars, "Write Config failed!");
	}

	rdr = get_reader_by_label(reader_);
	if (!rdr)
		return NULL;

	tpl_addVar(vars, TPLADD, "READERNAME", rdr->label);
	tpl_addVar(vars, TPLADD, "DESCRIPTION", rdr->description?rdr->description:"");

	// enabled
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "ENABLED", (rdr->enable == 1) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "ENABLEDVALUE", (rdr->enable == 1) ? "1" : "0");
	}

	// Account
	tpl_addVar(vars, TPLADD, "ACCOUNT", rdr->r_usr);
	tpl_addVar(vars, TPLADD, "USER", rdr->r_usr);
	tpl_addVar(vars, TPLADD, "PASS", rdr->r_pwd);
	tpl_addVar(vars, TPLADD, "PASSWORD", rdr->r_pwd);

	// Key Newcamd
	for (i=0; i<14; i++)
		tpl_printf(vars, TPLAPPEND, "NCD_KEY", "%02X", rdr->ncd_key[i]);

	// Pincode
	tpl_addVar(vars, TPLADD, "PINCODE", rdr->pincode);

	// Emmfile Path
	if (rdr->emmfile) tpl_addVar(vars, TPLADD, "EMMFILE", (char *)rdr->emmfile);

	// Inactivity timeout
	tpl_printf(vars, TPLADD, "INACTIVITYTIMEOUT", "%d", rdr->tcp_ito);

	// Receive timeout
	tpl_printf(vars, TPLADD, "RECEIVETIMEOUT", "%d", rdr->tcp_rto);

	// Reset Cycle
	tpl_printf(vars, TPLADD, "RESETCYCLE", "%d", rdr->resetcycle);

	// Disable Serverfilter
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "DISABLESERVERFILTERCHECKED", (rdr->ncd_disable_server_filt == 1) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "DISABLESERVERFILTERVALUE", (rdr->ncd_disable_server_filt == 1) ? "1" : "0");
	}

	// Fallback
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "FALLBACKCHECKED", (rdr->fallback == 1) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "FALLBACKVALUE", (rdr->fallback == 1) ? "1" : "0");
	}

#ifdef CS_CACHEEX
	// Cacheex
	if(!apicall) {
		tpl_printf(vars, TPLADD, "TMP", "CACHEEXSELECTED%d", rdr->cacheex);
		tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");
	} else {
		tpl_printf(vars, TPLADD, "CACHEEX", "%d", rdr->cacheex);
	}
	tpl_printf(vars, TPLADD, "CACHEEX_MAXHOP", "%d", rdr->cacheex_maxhop);
#endif

#ifdef WITH_COOLAPI
	tpl_printf(vars, TPLADD, "COOLTIMEOUTINIT", "%d", rdr->cool_timeout_init);
	tpl_printf(vars, TPLADD, "COOLTIMEOUTAFTERINIT", "%d", rdr->cool_timeout_after_init);
#endif

	// Logport
	tpl_printf(vars, TPLADD, "LOGPORT", "%d", rdr->log_port);

	// BoxID
	if(rdr->boxid)
		tpl_printf(vars, TPLADD, "BOXID", "%08X", rdr->boxid);

	// Fix 9993
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "FIX9993CHECKED", (rdr->fix_9993 == 1) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "FIX9993VALUE", (rdr->fix_9993 == 1) ? "1" : "0");
	}

	// Drop CWs with wrong checksum:
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "DROPBADCWSCHECKED", (rdr->dropbadcws == 1) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "DROPBADCWSVALUE", (rdr->dropbadcws == 1) ? "1" : "0");
	}

    // Disable CWs checksum test:
    if(!apicall) {
        tpl_addVar(vars, TPLADD, "DISABLECRCCWSCHECKED", (rdr->disablecrccws == 1) ? "checked" : "");
    } else {
        tpl_addVar(vars, TPLADD, "DISABLECRCCWSVALUE", (rdr->disablecrccws == 1) ? "1" : "0");
    }

    // Set reader to use GPIO
    if(!apicall) {
        tpl_addVar(vars, TPLADD, "USE_GPIOCHECKED", rdr->use_gpio ? "checked" : "");
    } else {
        tpl_addVar(vars, TPLADD, "USE_GPIOVALUE", rdr->use_gpio ? "1" : "0");
    }

	// AUdisabled
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "AUDISABLED", (rdr->audisabled == 1) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "AUDISABLEDVALUE", (rdr->audisabled == 1) ? "1" : "0");
	}

	// AUprovid
	if(rdr->auprovid)
		tpl_printf(vars, TPLADD, "AUPROVID", "%06X", rdr->auprovid);

	// Force Irdeto
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "FORCEIRDETOCHECKED", (rdr->force_irdeto == 1) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "FORCEIRDETOVALUE", (rdr->force_irdeto == 1) ? "1" : "0");
	}

	// RSA Key
	int32_t len = check_filled(rdr->rsa_mod, 120);
	if(len > 0) {
		if(len > 64) len = 120;
		else len = 64;
		for (i = 0; i < len; i++) tpl_printf(vars, TPLAPPEND, "RSAKEY", "%02X", rdr->rsa_mod[i]);
	}

	// BoxKey
	len = check_filled(rdr->nagra_boxkey, 8);
	if(len > 0) {
		for (i = 0; i < 8 ; i++) tpl_printf(vars, TPLAPPEND, "BOXKEY", "%02X", rdr->nagra_boxkey[i]);
	}

	// ins7E
	if(rdr->ins7E[0x1A]) {
		for (i = 0; i < 26 ; i++) tpl_printf(vars, TPLAPPEND, "INS7E", "%02X", rdr->ins7E[i]);
	}

	// ins7E11
	if(rdr->ins7E11[0x01]) {
		tpl_printf(vars, TPLAPPEND, "INS7E11", "%02X", rdr->ins7E11[0]);
	}

	// ATR
	if ( rdr->atr[0])
		for (i = 0; i < rdr->atrlen/2; i++)
			tpl_printf(vars, TPLAPPEND, "ATR", "%02X", rdr->atr[i]);

	// ECM Whitelist
	value = mk_t_ecmwhitelist(rdr->ecmWhitelist);
	tpl_addVar(vars, TPLADD, "ECMWHITELIST", value);
	free_mk_t(value);

	// ECM Header Whitelist
        value = mk_t_ecmheaderwhitelist(rdr->ecmHeaderwhitelist); 
        tpl_addVar(vars, TPLADD, "ECMHEADERWHITELIST", value);
        free_mk_t(value); 

	// Deprecated
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "DEPRECATEDCHECKED", (rdr->deprecated == 1) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "DEPRECATEDVALUE", (rdr->deprecated == 1) ? "1" : "0");
	}

	// Smargopatch
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "SMARGOPATCHCHECKED", (rdr->smargopatch == 1) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "SMARGOPATCHVALUE", (rdr->smargopatch == 1) ? "1" : "0");
	}

	// sc8in1 dtrrts patch
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "SC8IN1DTRRTSPATCHCHECKED", (rdr->sc8in1_dtrrts_patch == 1) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "SC8IN1DTRRTSPATCHVALUE", (rdr->sc8in1_dtrrts_patch == 1) ? "1" : "0");
	}

	// Detect
	if (rdr->detect&0x80)
		tpl_printf(vars, TPLADD, "DETECT", "!%s", RDR_CD_TXT[rdr->detect&0x7f]);
	else
		tpl_addVar(vars, TPLADD, "DETECT", RDR_CD_TXT[rdr->detect&0x7f]);

	// Ratelimit
	if(rdr->ratelimitecm){
		tpl_printf(vars, TPLADD, "RATELIMITECM", "%d", rdr->ratelimitecm);
		tpl_printf(vars, TPLADD, "RATELIMITSECONDS", "%d", rdr->ratelimitseconds);
	}
	// Cooldown
	if(rdr->cooldown[0] && rdr->cooldown[1]){
		tpl_printf(vars, TPLADD, "COOLDOWNDELAY", "%d", rdr->cooldown[0]);
		tpl_printf(vars, TPLADD, "COOLDOWNTIME", "%d", rdr->cooldown[1]);
	}
	// Frequencies
	tpl_printf(vars, TPLADD, "MHZ", "%d", rdr->mhz);
	tpl_printf(vars, TPLADD, "CARDMHZ", "%d", rdr->cardmhz);

	// Device
	tpl_addVar(vars, TPLADD, "DEVICE", rdr->device);
	if(rdr->r_port)
		tpl_printf(vars, TPLAPPEND, "DEVICE", ",%d", rdr->r_port);
	if(rdr->l_port) {
		if(rdr->r_port)
			tpl_printf(vars, TPLAPPEND, "DEVICE", ",%d", rdr->l_port);
		else
			tpl_printf(vars, TPLAPPEND, "DEVICE", ",,%d", rdr->l_port);
	}

	// Group
	value = mk_t_group(rdr->grp);
	tpl_addVar(vars, TPLADD, "GRP", value);
	free_mk_t(value);

#ifdef WITH_LB
	if(rdr->lb_weight)
		tpl_printf(vars, TPLADD, "LBWEIGHT", "%d", rdr->lb_weight);
#endif

	//services
	if(!apicall) {
		struct s_sidtab *sidtab = cfg.sidtab;
		//build matrix
		i = 0;
		while(sidtab != NULL) {
			tpl_addVar(vars, TPLADD, "SIDLABEL", sidtab->label);
			if(rdr->sidtabok&((SIDTABBITS)1<<i)) tpl_addVar(vars, TPLADD, "CHECKED", "checked");
			else tpl_addVar(vars, TPLADD, "CHECKED", "");
			tpl_addVar(vars, TPLAPPEND, "SIDS", tpl_getTpl(vars, "READERCONFIGSIDOKBIT"));
			if(rdr->sidtabno&((SIDTABBITS)1<<i)) tpl_addVar(vars, TPLADD, "CHECKED", "checked");
			else tpl_addVar(vars, TPLADD, "CHECKED", "");
			tpl_addVar(vars, TPLAPPEND, "SIDS", tpl_getTpl(vars, "READERCONFIGSIDNOBIT"));
			sidtab=sidtab->next;
			i++;
		}
	} else {
		value = mk_t_service((uint64_t)rdr->sidtabok, (uint64_t)rdr->sidtabno);
		if (strlen(value) > 0)
			tpl_addVar(vars, TPLADD, "SERVICES", value);
		free_mk_t(value);
	}

	// CAID
	value = mk_t_caidtab(&rdr->ctab);
	tpl_addVar(vars, TPLADD, "CAIDS", value);
	free_mk_t(value);

	// AESkeys
	value = mk_t_aeskeys(rdr);
	tpl_addVar(vars, TPLADD, "AESKEYS", value);
	free_mk_t(value);

	//ident
	value = mk_t_ftab(&rdr->ftab);
	tpl_addVar(vars, TPLADD, "IDENTS", value);
	free_mk_t(value);

	//CHID
	value = mk_t_ftab(&rdr->fchid);
	tpl_addVar(vars, TPLADD, "CHIDS", value);
	free_mk_t(value);

	//class
	value = mk_t_cltab(&rdr->cltab);
	tpl_addVar(vars, TPLADD, "CLASS", value);
	free_mk_t(value);

	if(rdr->cachemm)
		tpl_printf(vars, TPLADD, "EMMCACHE", "%d,%d,%d", rdr->cachemm, rdr->rewritemm, rdr->logemm);

	//savenano
	value = mk_t_nano(rdr, 0x02);
	tpl_addVar(vars, TPLADD, "SAVENANO", value);
	free_mk_t(value);

	//blocknano
	value = mk_t_nano(rdr, 0x01);
	tpl_addVar(vars, TPLADD, "BLOCKNANO", value);
	free_mk_t(value);

	// Blocke EMM
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "BLOCKEMMUNKNOWNCHK", (rdr->blockemm & EMM_UNKNOWN) ? "checked" : "");
		tpl_addVar(vars, TPLADD, "BLOCKEMMUNIQCHK", (rdr->blockemm & EMM_UNIQUE) ? "checked" : "");
		tpl_addVar(vars, TPLADD, "BLOCKEMMSHAREDCHK", (rdr->blockemm & EMM_SHARED) ? "checked" : "");
		tpl_addVar(vars, TPLADD, "BLOCKEMMGLOBALCHK", (rdr->blockemm & EMM_GLOBAL) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "BLOCKEMMUNKNOWNVALUE", (rdr->blockemm & EMM_UNKNOWN) ? "1" : "0");
		tpl_addVar(vars, TPLADD, "BLOCKEMMUNIQVALUE", (rdr->blockemm & EMM_UNIQUE) ? "1" : "0");
		tpl_addVar(vars, TPLADD, "BLOCKEMMSHAREDVALUE", (rdr->blockemm & EMM_SHARED) ? "1" : "0");
		tpl_addVar(vars, TPLADD, "BLOCKEMMGLOBALVALUE", (rdr->blockemm & EMM_GLOBAL) ? "1" : "0");
	}

	// Save EMM
	if(!apicall) {
		tpl_addVar(vars, TPLADD, "SAVEEMMUNKNOWNCHK", (rdr->saveemm & EMM_UNKNOWN) ? "checked" : "");
		tpl_addVar(vars, TPLADD, "SAVEEMMUNIQCHK", (rdr->saveemm & EMM_UNIQUE) ? "checked" : "");
		tpl_addVar(vars, TPLADD, "SAVEEMMSHAREDCHK", (rdr->saveemm & EMM_SHARED) ? "checked" : "");
		tpl_addVar(vars, TPLADD, "SAVEEMMGLOBALCHK", (rdr->saveemm & EMM_GLOBAL) ? "checked" : "");
	} else {
		tpl_addVar(vars, TPLADD, "SAVEEMMUNKNOWNVALUE", (rdr->saveemm & EMM_UNKNOWN) ? "1" : "0");
		tpl_addVar(vars, TPLADD, "SAVEEMMUNIQVALUE", (rdr->saveemm & EMM_UNIQUE) ? "1" : "0");
		tpl_addVar(vars, TPLADD, "SAVEEMMSHAREDVALUE", (rdr->saveemm & EMM_SHARED) ? "1" : "0");
		tpl_addVar(vars, TPLADD, "SAVEEMMGLOBALVALUE", (rdr->saveemm & EMM_GLOBAL) ? "1" : "0");
	}

	value = mk_t_emmbylen(rdr);
	if (strlen(value) > 0)
		tpl_addVar(vars, TPLADD, "BLOCKEMMBYLEN", value);
	free_mk_t(value);

#ifdef MODULE_CCCAM
	if (!strcmp(rdr->cc_version, "2.0.11")) {
		tpl_addVar(vars, TPLADD, "CCCVERSIONSELECTED0", "selected");
	} else if (!strcmp(rdr->cc_version, "2.1.1")) {
		tpl_addVar(vars, TPLADD, "CCCVERSIONSELECTED1", "selected");
	} else if (!strcmp(rdr->cc_version, "2.1.2")) {
		tpl_addVar(vars, TPLADD, "CCCVERSIONSELECTED2", "selected");
	} else if (!strcmp(rdr->cc_version, "2.1.3")) {
		tpl_addVar(vars, TPLADD, "CCCVERSIONSELECTED3", "selected");
	} else if (!strcmp(rdr->cc_version, "2.1.4")) {
		tpl_addVar(vars, TPLADD, "CCCVERSIONSELECTED4", "selected");
	} else if (!strcmp(rdr->cc_version, "2.2.0")) {
		tpl_addVar(vars, TPLADD, "CCCVERSIONSELECTED5", "selected");
	} else if (!strcmp(rdr->cc_version, "2.2.1")) {
		tpl_addVar(vars, TPLADD, "CCCVERSIONSELECTED6", "selected");
	} else if (!strcmp(rdr->cc_version, "2.3.0")) {
		tpl_addVar(vars, TPLADD, "CCCVERSIONSELECTED7", "selected");
	}
#endif

#ifdef WITH_LIBUSB
	if(!rdr->device_endpoint) {
		tpl_addVar(vars, TPLADD, "DEVICEOUTEP0", "selected");
	} else if (rdr->device_endpoint == 0x82) {
		tpl_addVar(vars, TPLADD, "DEVICEOUTEP1", "selected");
	} else if (rdr->device_endpoint == 0x81) {
		tpl_addVar(vars, TPLADD, "DEVICEOUTEP2", "selected");
	}
	tpl_addVar(vars, TPLADD, "DEVICEEP", tpl_getTpl(vars, "READERCONFIGDEVICEEPBIT"));
#else
	tpl_addVar(vars, TPLADD, "DEVICEEP", "not avail LIBUSB");
#endif

	tpl_printf(vars, TPLADD, "TMP", "NDSVERSION%d", rdr->ndsversion);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

	tpl_printf(vars, TPLADD, "TMP", "NAGRAREAD%d", rdr->nagra_read);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

#ifdef MODULE_CCCAM
	tpl_printf(vars, TPLADD, "CCCMAXHOP",    "%d", rdr->cc_maxhop);
	tpl_printf(vars, TPLADD, "CCCMINDOWN",   "%d", rdr->cc_mindown);
	tpl_printf(vars, TPLADD, "CCCRESHARE",   "%d", rdr->cc_reshare);
	tpl_printf(vars, TPLADD, "RESHARE",      "%d", cfg.cc_reshare);
	tpl_printf(vars, TPLADD, "CCCRECONNECT", "%d", rdr->cc_reconnect);

	if(rdr->cc_want_emu)
		tpl_addVar(vars, TPLADD, "CCCWANTEMUCHECKED", "checked");
	if(rdr->cc_keepalive)
		tpl_addVar(vars, TPLADD, "KEEPALIVECHECKED", "selected");
#endif

	// Show only parameters which needed for the reader
	switch (rdr->typ) {
		case R_CONSTCW:
			tpl_addVar(vars, TPLADD, "PROTOCOL", "constcw");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGSTDHWREADERBIT"));
			break;
		case R_DB2COM1:
		case R_DB2COM2:
		case R_MOUSE :
			tpl_addVar(vars, TPLADD, "PROTOCOL", "mouse");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGSTDHWREADERBIT"));
			break;
		case R_MP35:
			tpl_addVar(vars, TPLADD, "PROTOCOL", "mp35");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGSTDHWREADERBIT"));
			break;
		case R_SC8in1 :
			tpl_addVar(vars, TPLADD, "PROTOCOL", "sc8in1");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGSTDHWREADERBIT"));
			break;
		case R_SMART :
			tpl_addVar(vars, TPLADD, "PROTOCOL", "smartreader");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGSTDHWREADERBIT"));
			break;
		case R_INTERNAL:
			tpl_addVar(vars, TPLADD, "PROTOCOL", "internal");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGSTDHWREADERBIT"));
			break;
		case R_SERIAL :
			tpl_addVar(vars, TPLADD, "PROTOCOL", "serial");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGSTDHWREADERBIT"));
			break;
		case R_CAMD35 :
			tpl_addVar(vars, TPLADD, "PROTOCOL", "camd35");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGCAMD35BIT"));
			break;
		case R_CS378X :
			tpl_addVar(vars, TPLADD, "PROTOCOL", "cs378x");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGCS378XBIT"));
			break;
		case R_RADEGAST:
			tpl_addVar(vars, TPLADD, "PROTOCOL", "radegast");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGRADEGASTBIT"));
			break;
		case R_NEWCAMD :
			if ( rdr->ncd_proto == NCD_525 ){
				tpl_addVar(vars, TPLADD, "PROTOCOL", "newcamd525");
				tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGNCD525BIT"));
			} else if ( rdr->ncd_proto == NCD_524 ) {
				tpl_addVar(vars, TPLADD, "PROTOCOL", "newcamd524");
				tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGNCD524BIT"));
			}
			break;
#ifdef MODULE_CCCAM
		case R_CCCAM :
			tpl_addVar(vars, TPLADD, "PROTOCOL", "cccam");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGCCCAMBIT"));
			break;
#endif
#ifdef WITH_PCSC
		case R_PCSC :
			tpl_addVar(vars, TPLADD, "PROTOCOL", "pcsc");
			tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGSTDHWREADERBIT"));
			break;
#endif
		default :
			tpl_addMsg(vars, "Error: protocol not resolvable");
			tpl_addMsg(vars, tpl_printf(vars, TPLADD, "TMP", "Error: protocol number: %d readername: %s", rdr->typ, xml_encode(vars, rdr->label)));
			break;

	}

#ifdef MODULE_CCCAM
	if(rdr->typ != R_CCCAM){
		tpl_printf(vars, TPLADD, "CCCHOP", "%d", rdr->cc_hop);
		tpl_addVar(vars, TPLAPPEND, "READERDEPENDINGCONFIG", tpl_getTpl(vars, "READERCONFIGHOPBIT"));
	}
#endif

	return tpl_getTpl(vars, "READERCONFIG");
}

static char *send_oscam_reader_stats(struct templatevars *vars, struct uriparams *params, int32_t apicall) {

	if(!apicall) setActiveMenu(vars, MNU_READERS);

	int8_t error;
	struct s_client *cl = NULL;
	struct s_reader *rdr;

	rdr = get_reader_by_label(getParam(params, "label"));
	error = (rdr ? 0 : 1);

	if(!error && rdr){
		cl = rdr->client;
		error = (cl ? 0 : 1);
	}

	if(error){
		tpl_addVar(vars, TPLAPPEND, "READERSTATSROW","<TR><TD colspan=\"8\"> No statistics found - Reader exist and active?</TD></TR>");
		if(!apicall)
			return tpl_getTpl(vars, "READERSTATS");
		else
			return tpl_getTpl(vars, "APIREADERSTATS");
	}

#ifdef WITH_LB
	char *stxt[]={"found", "cache1", "cache2", "cache3",
			"not found", "timeout", "sleeping",
			"fake", "invalid", "corrupt", "no card", "expdate",
			"disabled", "stopped"};

	if (strcmp(getParam(params, "action"), "resetstat") == 0) {
		char *rcs = getParam(params, "rc");
		int32_t retval = 0;
		if(strlen(rcs) > 0) {
			int8_t rc;
			rc = atoi(rcs);
			retval = clean_stat_by_rc(rdr, rc, 0);
			cs_log("Reader %s stats %d %s entr%s deleted by WebIF from %s",
								rdr->label, retval, stxt[rc],
								retval == 1 ? "y":"ies",
								cs_inet_ntoa(GET_IP()));
		} else {
			clear_reader_stat(rdr);
			cs_log("Reader %s stats resetted by WebIF from %s", rdr->label, cs_inet_ntoa(GET_IP()));
		}

	}

	if (strcmp(getParam(params, "action"), "deleterecord") == 0) {
		char *record = getParam(params, "record");
		if(strlen(record) > 0) {
			int32_t retval = 0;
			uint32_t caid, provid, sid, cid, len;
			sscanf(record, "%4x:%6x:%4x:%4x:%4x", &caid, &provid, &sid, &cid, &len);
			retval = clean_stat_by_id(rdr, caid, provid, sid, cid, len);
			cs_log("Reader %s stats %d entr%s deleted by WebIF from %s",
					rdr->label, retval,
					retval == 1 ? "y":"ies",
					cs_inet_ntoa(GET_IP()));
		}
	}

	if (strcmp(getParam(params, "action"), "updateecmlen") == 0) {
		update_ecmlen_from_stat(rdr);
		write_server();
	}

#endif

	if (!apicall){
		tpl_addVar(vars, TPLADD, "LABEL", rdr->label);
		tpl_addVar(vars, TPLADD, "ENCODEDLABEL", urlencode(vars, rdr->label));
	} else {
		tpl_addVar(vars, TPLADD, "READERNAME", rdr->label);
	}



	if (apicall) {
		int32_t i, emmcount = 0;
		char *ttxt[]={"unknown", "unique", "shared", "global"};

		for (i=0; i<4; i++) {
			tpl_addVar(vars, TPLADD, "EMMRESULT", "error");
			tpl_addVar(vars, TPLADD, "EMMTYPE", ttxt[i]);
			tpl_printf(vars, TPLADD, "EMMCOUNT", "%d", rdr->emmerror[i]);
			tpl_addVar(vars, TPLAPPEND, "EMMSTATS", tpl_getTpl(vars, "APIREADERSTATSEMMBIT"));
			emmcount += rdr->emmerror[i];
			tpl_printf(vars, TPLADD, "TOTALERROR", "%d", emmcount);
		}
		emmcount = 0;
		for (i=0; i<4; i++) {
			tpl_addVar(vars, TPLADD, "EMMRESULT", "written");
			tpl_addVar(vars, TPLADD, "EMMTYPE", ttxt[i]);
			tpl_printf(vars, TPLADD, "EMMCOUNT", "%d", rdr->emmwritten[i]);
			tpl_addVar(vars, TPLAPPEND, "EMMSTATS", tpl_getTpl(vars, "APIREADERSTATSEMMBIT"));
			emmcount += rdr->emmwritten[i];
			tpl_printf(vars, TPLADD, "TOTALWRITTEN", "%d", emmcount);
		}
		emmcount = 0;
		for (i=0; i<4; i++) {
			tpl_addVar(vars, TPLADD, "EMMRESULT", "skipped");
			tpl_addVar(vars, TPLADD, "EMMTYPE", ttxt[i]);
			tpl_printf(vars, TPLADD, "EMMCOUNT", "%d", rdr->emmskipped[i]);
			tpl_addVar(vars, TPLAPPEND, "EMMSTATS", tpl_getTpl(vars, "APIREADERSTATSEMMBIT"));
			emmcount += rdr->emmskipped[i];
			tpl_printf(vars, TPLADD, "TOTALSKIPPED", "%d", emmcount);
		}
		emmcount = 0;
		for (i=0; i<4; i++) {
			tpl_addVar(vars, TPLADD, "EMMRESULT", "blocked");
			tpl_addVar(vars, TPLADD, "EMMTYPE", ttxt[i]);
			tpl_printf(vars, TPLADD, "EMMCOUNT", "%d", rdr->emmblocked[i]);
			tpl_addVar(vars, TPLAPPEND, "EMMSTATS", tpl_getTpl(vars, "APIREADERSTATSEMMBIT"));
			emmcount += rdr->emmblocked[i];
			tpl_printf(vars, TPLADD, "TOTALBLOCKED", "%d", emmcount);
		}
	}

	if (apicall) {
		char *txt = "UNDEF";
		switch (rdr->card_status) {
		case NO_CARD:
			txt = "OFF";
			break;
		case UNKNOWN:
			txt = "UNKNOWN";
			break;
		case CARD_NEED_INIT:
			txt = "NEEDINIT";
			break;
		case CARD_INSERTED:
			if (cl->typ == 'p')
				txt = "CONNECTED";
			else
				txt = "CARDOK";
			break;
		case CARD_FAILURE:
			txt = "ERROR";
			break;
		default:
			txt = "UNDEF";
		}
		tpl_addVar(vars, TPLADD, "READERSTATUS", txt);
		tpl_printf(vars, TPLADD, "READERCAID", "%04X", rdr->caid);
	}

	int32_t rowcount = 0;
	uint64_t ecmcount = 0;
	time_t lastaccess = 0;

#ifdef WITH_LB
	int32_t rc2hide = (-1);
	if (strlen(getParam(params, "hide")) > 0)
		rc2hide = atoi(getParam(params, "hide"));

	if (rdr->lb_stat) {
		int32_t statsize;
		// @todo alno: sort by click, 0=ascending, 1=descending (maybe two buttons or reverse on second click)
		READER_STAT **statarray = get_sorted_stat_copy(rdr, 0, &statsize);
		char channame[32];
		for(; rowcount < statsize; ++rowcount){
			READER_STAT *stat = statarray[rowcount];
			if (!(stat->rc == rc2hide)) {
				struct tm lt;
				localtime_r(&stat->last_received, &lt);
				ecmcount += stat->ecm_count;
				if (!apicall) {
					tpl_printf(vars, TPLADD, "CHANNEL", "%04X:%06X:%04X:%04X", stat->caid, stat->prid, stat->srvid, stat->chid);
					tpl_addVar(vars, TPLADD, "CHANNELNAME", xml_encode(vars, get_servicename(cur_client(), stat->srvid, stat->caid, channame)));
					tpl_printf(vars, TPLADD, "ECMLEN","%04hX", stat->ecmlen);
					tpl_addVar(vars, TPLADD, "RC", stxt[stat->rc]);
					tpl_printf(vars, TPLADD, "TIME", "%dms", stat->time_avg);
					if (stat->time_stat[stat->time_idx])
						tpl_printf(vars, TPLADD, "TIMELAST", "%dms", stat->time_stat[stat->time_idx]);
					else
						tpl_addVar(vars, TPLADD, "TIMELAST", "");
					tpl_printf(vars, TPLADD, "COUNT", "%d", stat->ecm_count);

					if(stat->last_received) {
						tpl_printf(vars, TPLADD, "LAST", "%02d.%02d.%02d %02d:%02d:%02d", lt.tm_mday, lt.tm_mon+1, lt.tm_year%100, lt.tm_hour, lt.tm_min, lt.tm_sec);

					} else {
						tpl_addVar(vars, TPLADD, "LAST","never");
					}
				} else {
					tpl_printf(vars, TPLADD, "ECMCAID", "%04X", stat->caid);
					tpl_printf(vars, TPLADD, "ECMPROVID", "%06X", stat->prid);
					tpl_printf(vars, TPLADD, "ECMSRVID", "%04X", stat->srvid);
					tpl_printf(vars, TPLADD, "ECMLEN", "%04hX", stat->ecmlen);
					tpl_addVar(vars, TPLADD, "ECMCHANNELNAME", xml_encode(vars, get_servicename(cur_client(), stat->srvid, stat->caid, channame)));
					tpl_printf(vars, TPLADD, "ECMTIME", "%d", stat->time_avg);
					tpl_printf(vars, TPLADD, "ECMTIMELAST", "%d", stat->time_stat[stat->time_idx]);
					tpl_printf(vars, TPLADD, "ECMRC", "%d", stat->rc);
					tpl_addVar(vars, TPLADD, "ECMRCS", stxt[stat->rc]);
					if(stat->last_received) {
						char tbuffer [30];
						strftime(tbuffer, 30, "%Y-%m-%dT%H:%M:%S%z", &lt);
						tpl_addVar(vars, TPLADD, "ECMLAST", tbuffer);
					} else {
						tpl_addVar(vars, TPLADD, "ECMLAST", "");
					}
					tpl_printf(vars, TPLADD, "ECMCOUNT", "%d", stat->ecm_count);

					if (stat->last_received > lastaccess)
						lastaccess = stat->last_received;
				}

				if (!apicall) {
					if (stat->rc == 4) {
						tpl_addVar(vars, TPLAPPEND, "READERSTATSROWNOTFOUND", tpl_getTpl(vars, "READERSTATSBIT"));
						tpl_addVar(vars, TPLADD, "READERSTATSNFHEADLINE", "\t\t<TR><TD CLASS=\"subheadline\" colspan=\"6\">Not found</TD>");
						tpl_printf(vars, TPLAPPEND, "READERSTATSNFHEADLINE", "<TD CLASS=\"subheadline\" colspan=\"2\"><A HREF=\"readerstats.html?label=%s&amp;action=resetstat&amp;rc=4\">delete all %s</A></TD></TR>\n",
								urlencode(vars, rdr->label),
								stxt[stat->rc]);
					} else if (stat->rc == 5) {
						tpl_addVar(vars, TPLAPPEND, "READERSTATSROWTIMEOUT", tpl_getTpl(vars, "READERSTATSBIT"));
						tpl_addVar(vars, TPLADD, "READERSTATSTOHEADLINE", "\t\t<TR><TD CLASS=\"subheadline\" colspan=\"6\">Timeout</TD>");
						tpl_printf(vars, TPLAPPEND, "READERSTATSTOHEADLINE", "<TD CLASS=\"subheadline\" colspan=\"2\"><A HREF=\"readerstats.html?label=%s&amp;action=resetstat&amp;rc=5\">delete all %s</A></TD></TR>\n",
								urlencode(vars, rdr->label),
								stxt[stat->rc]);
					}
					else
						tpl_addVar(vars, TPLAPPEND, "READERSTATSROWFOUND", tpl_getTpl(vars, "READERSTATSBIT"));
				} else {

					tpl_addVar(vars, TPLAPPEND, "ECMSTATS", tpl_getTpl(vars, "APIREADERSTATSECMBIT"));
				}
			}
		}
		free(statarray);
	} else
#endif
		tpl_addVar(vars, TPLAPPEND, "READERSTATSROW","<TR><TD colspan=\"8\"> No statistics found </TD></TR>");

	tpl_printf(vars, TPLADD, "ROWCOUNT", "%d", rowcount);

	if (lastaccess > 0){
		char tbuffer [30];
		struct tm lt;
		localtime_r(&lastaccess, &lt);
		strftime(tbuffer, 30, "%Y-%m-%dT%H:%M:%S%z", &lt);
		tpl_addVar(vars, TPLADD, "LASTACCESS", tbuffer);
	} else {
		tpl_addVar(vars, TPLADD, "LASTACCESS", "");
	}

	if(apicall) {
		if(cl){
			char *value = get_ecm_historystring(cl);
			tpl_addVar(vars, TPLADD, "ECMHISTORY", value);
			free_mk_t(value);
		}
	}

	tpl_printf(vars, TPLADD, "TOTALECM", "%" PRIu64, ecmcount);

	if(!apicall)
		return tpl_getTpl(vars, "READERSTATS");
	else
		return tpl_getTpl(vars, "APIREADERSTATS");
}

static char *send_oscam_user_config_edit(struct templatevars *vars, struct uriparams *params, int32_t apicall) {
	struct s_auth *account, *ptr;
	char user[sizeof(first_client->account->usr)];

	int32_t i;

	if(!apicall) setActiveMenu(vars, MNU_USERS);

	if (strcmp(getParam(params, "action"), "Save As") == 0) cs_strncpy(user, getParam(params, "newuser"), sizeof(user)/sizeof(char));
	else cs_strncpy(user, getParam(params, "user"), sizeof(user)/sizeof(char));

	for (account = cfg.account; account != NULL && strcmp(user, account->usr) != 0; account = account->next);

	// Create a new user if it doesn't yet
	if (account == NULL) {
		i = 1;
		while(strlen(user) < 1) {
			snprintf(user, sizeof(user)/sizeof(char) - 1, "NEWUSER%d", i);
			for (account = cfg.account; account != NULL && strcmp(user, account->usr) != 0; account = account->next);
			if(account != NULL) user[0] = '\0';
			++i;
		}
		if (!cs_malloc(&account, sizeof(struct s_auth), -1)) return "0";
		if(cfg.account == NULL) cfg.account = account;
		else {
			for (ptr = cfg.account; ptr != NULL && ptr->next != NULL; ptr = ptr->next);
			ptr->next = account;
		}
		account_set_defaults(account);
		account->disabled = 1;
		cs_strncpy((char *)account->usr, user, sizeof(account->usr));
		if (!account->grp)
			account->grp = 1;
		tpl_addMsg(vars, "New user has been added with default settings");

		if (write_userdb()!=0) tpl_addMsg(vars, "Write Config failed!");
		// no need to refresh anything here as the account is disabled by default and there's no client with this new account anyway!
	}

	if((strcmp(getParam(params, "action"), "Save") == 0) || (strcmp(getParam(params, "action"), "Save As") == 0)) {
		char servicelabels[1024]= "";

		for(i = 0; i < (*params).paramcount; i++) {
			if ((strcmp((*params).params[i], "action")) &&
					(strcmp((*params).params[i], "user")) &&
					(strcmp((*params).params[i], "newuser")) &&
					(strcmp((*params).params[i], "part"))) {

				if (!strcmp((*params).params[i], "services"))
					snprintf(servicelabels + strlen(servicelabels), sizeof(servicelabels) - strlen(servicelabels), "%s,", (*params).values[i]);
				else
					chk_account((*params).params[i], (*params).values[i], account);
			}
		}
		chk_account("services", servicelabels, account);
		tpl_addMsg(vars, "Account updated");

		refresh_oscam(REFR_CLIENTS);

		if (write_userdb()!=0) tpl_addMsg(vars, "Write Config failed!");
	}

	tpl_addVar(vars, TPLADD, "USERNAME", account->usr);
	tpl_addVar(vars, TPLADD, "PASSWORD", account->pwd);
	if(account->description)
		tpl_addVar(vars, TPLADD, "DESCRIPTION", account->description);

	//Disabled
	if(!apicall) {
		if(account->disabled)
			tpl_addVar(vars, TPLADD, "DISABLEDCHECKED", "selected");
	} else {
		tpl_printf(vars, TPLADD, "DISABLEDVALUE", "%d", account->disabled);
	}

	//Expirationdate
	struct tm timeinfo;
	cs_gmtime_r (&account->expirationdate, &timeinfo);
	char buf [80];
	strftime (buf,80,"%Y-%m-%d",&timeinfo);
	if(strcmp(buf,"1970-01-01")) tpl_addVar(vars, TPLADD, "EXPDATE", buf);

	//Allowed TimeFrame
	if(account->allowedtimeframe[0] && account->allowedtimeframe[1]) {
		tpl_printf(vars, TPLADD, "ALLOWEDTIMEFRAME", "%02d:%02d-%02d:%02d",
				account->allowedtimeframe[0]/60,
				account->allowedtimeframe[0]%60,
				account->allowedtimeframe[1]/60,
				account->allowedtimeframe[1]%60 );
	}

	//Group
	char *value = mk_t_group(account->grp);
	tpl_addVar(vars, TPLADD, "GROUPS", value);
	free_mk_t(value);

	// allowed protocols
	value = mk_t_allowedprotocols(account);
	tpl_addVar(vars, TPLADD, "ALLOWEDPROTOCOLS", value);
	free_mk_t(value);

	//Hostname
	tpl_addVar(vars, TPLADD, "DYNDNS", account->dyndns);

	//Uniq
	if(!apicall) {
		tpl_printf(vars, TPLADD, "TMP", "UNIQSELECTED%d", account->uniq);
		tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");
	} else {
		tpl_printf(vars, TPLADD, "UNIQVALUE", "%d", account->uniq);
	}

	//Sleep
	if(!account->tosleep) tpl_addVar(vars, TPLADD, "SLEEP", "0");
	else tpl_printf(vars, TPLADD, "SLEEP", "%d", account->tosleep);

	//Monlevel selector
	if(!apicall) {
		tpl_printf(vars, TPLADD, "TMP", "MONSELECTED%d", account->monlvl);
		tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");
	} else {
		tpl_printf(vars, TPLADD, "MONVALUE", "%d", account->monlvl);
	}

	//Au
	if (account->autoau == 1)
		tpl_addVar(vars, TPLADD, "AUREADER", "1");
	else if (account->aureader_list) {
		value = mk_t_aureader(account);
		tpl_addVar(vars, TPLADD, "AUREADER", value);
		free_mk_t(value);
	}

	if(!apicall) {
		/* SERVICES */
		struct s_sidtab *sidtab = cfg.sidtab;
		//build matrix
		i=0;
		while(sidtab != NULL) {
			tpl_addVar(vars, TPLADD, "SIDLABEL", sidtab->label);
			if(account->sidtabok&((SIDTABBITS)1<<i)) tpl_addVar(vars, TPLADD, "CHECKED", "checked");
			else tpl_addVar(vars, TPLADD, "CHECKED", "");
			tpl_addVar(vars, TPLAPPEND, "SIDS", tpl_getTpl(vars, "USEREDITSIDOKBIT"));
			if(account->sidtabno&((SIDTABBITS)1<<i)) tpl_addVar(vars, TPLADD, "CHECKED", "checked");
			else tpl_addVar(vars, TPLADD, "CHECKED", "");
			tpl_addVar(vars, TPLAPPEND, "SIDS", tpl_getTpl(vars, "USEREDITSIDNOBIT"));
			sidtab=sidtab->next;
			i++;
		}
	} else {
		value = mk_t_service((uint64_t)account->sidtabok, (uint64_t)account->sidtabno);
		if (strlen(value) > 0)
			tpl_addVar(vars, TPLADD, "SERVICES", value);
		free_mk_t(value);
	}

	// CAID
	value = mk_t_caidtab(&account->ctab);
	tpl_addVar(vars, TPLADD, "CAIDS", value);
	free_mk_t(value);

	//ident
	value = mk_t_ftab(&account->ftab);
	tpl_addVar(vars, TPLADD, "IDENTS", value);
	free_mk_t(value);

	//CHID
	value = mk_t_ftab(&account->fchid);
	tpl_addVar(vars, TPLADD, "CHIDS",  value);
	free_mk_t(value);

	//class
	value = mk_t_cltab(&account->cltab);
	tpl_addVar(vars, TPLADD, "CLASS", value);
	free_mk_t(value);

	//Betatunnel
	value = mk_t_tuntab(&account->ttab);
	tpl_addVar(vars, TPLADD, "BETATUNNELS", value);
	free_mk_t(value);

	//SUPPRESSCMD08
	if(!apicall){
		if (account->c35_suppresscmd08)
			tpl_addVar(vars, TPLADD, "SUPPRESSCMD08", "selected");
	} else {
		tpl_printf(vars, TPLADD, "SUPPRESSCMD08VALUE", "%d", account->c35_suppresscmd08);
	}

	//Sleepsend
	tpl_printf(vars, TPLADD, "SLEEPSEND", "%u", account->c35_sleepsend);

#ifdef CS_CACHEEX
	// Cacheex
	if(!apicall) {
		tpl_printf(vars, TPLADD, "TMP", "CACHEEXSELECTED%d", account->cacheex);
		tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

	} else {
		tpl_printf(vars, TPLADD, "CACHEEX", "%d", account->cacheex);
	}
	tpl_printf(vars, TPLADD, "CACHEEX_MAXHOP", "%d", account->cacheex_maxhop);
#endif

	//Keepalive
	if(!apicall){
		if (account->ncd_keepalive)
			tpl_addVar(vars, TPLADD, "KEEPALIVE", "selected");
	} else {
		tpl_printf(vars, TPLADD, "KEEPALIVEVALUE", "%d", account->ncd_keepalive);
	}

#ifdef CS_ANTICASC
	tpl_printf(vars, TPLADD, "AC_USERS", "%d", account->ac_users);
	tpl_printf(vars, TPLADD, "CFGNUMUSERS", "%d", cfg.ac_users);
	if(!apicall){
		tpl_printf(vars, TPLADD, "TMP", "PENALTY%d", account->ac_penalty);
		tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");
		char *tmp = NULL;
		switch(cfg.ac_penalty) {
			case 0: tmp = "(0) Only write to log"; break;
			case 1: tmp = "(1) Fake DW delayed"; break;
			case 2: tmp = "(2) Ban"; break;
			case 3: tmp = "(3) Real DW delayed"; break;
		}
		tpl_addVar(vars, TPLADD, "CFGPENALTY", tmp);
	} else {
		tpl_printf(vars, TPLADD, "PENALTYVALUE", "%d", account->ac_penalty);
	}
#endif

#ifdef MODULE_CCCAM
	tpl_printf(vars, TPLADD, "CCCMAXHOPS", "%d", account->cccmaxhops);
	tpl_printf(vars, TPLADD, "CCCRESHARE", "%d", account->cccreshare);
	tpl_printf(vars, TPLADD, "RESHARE",    "%d", cfg.cc_reshare);

	//CCcam Ignore Reshare
	tpl_printf(vars, TPLADD, "TMP", "CCCIGNRSHRSELECTED%d", account->cccignorereshare);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");
	tpl_addVar(vars, TPLADD, "CFGIGNORERESHARE", 
			   cfg.cc_ignore_reshare == 0 ?
			   "0 - use reshare level of Server" : "1 - use reshare level of Reader or User");

	//CCcam Stealth Mode
	tpl_printf(vars, TPLADD, "TMP", "CCCSTEALTHSELECTED%d", account->cccstealth);
	tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMP"), "selected");

	tpl_addVar(vars, TPLADD, "STEALTH", cfg.cc_stealth ? "enable" : "disable");
#endif

	//Failban
	tpl_printf(vars, TPLADD, "FAILBAN", "%d", account->failban);

	if(!apicall)
		return tpl_getTpl(vars, "USEREDIT");
	else
		return tpl_getTpl(vars, "APIUSEREDIT");

}

static void webif_add_client_proto(struct templatevars *vars, struct s_client *cl, char *proto) {
#ifdef MODULE_NEWCAMD
	if (streq(proto, "newcamd") && cl->typ == 'c') {
		tpl_printf(vars, TPLADDONCE, "CLIENTPROTO","%s (%s)", proto, newcamd_get_client_name(cl->ncd_client_id));
		return;
	}
#endif
#ifdef MODULE_CCCAM
	if (strncmp(proto, "cccam", 5) == 0) {
		struct cc_data *cc = cl->cc;
		if (cc && cc->remote_version && cc->remote_build) {
			tpl_printf(vars, TPLADDONCE, "CLIENTPROTO", "%s (%s-%s)", proto, cc->remote_version, cc->remote_build);
			tpl_addVar(vars, TPLADDONCE, "CLIENTPROTOTITLE", cc->extended_mode ? cc->remote_oscam : "");
			return;
		}
	}
#endif
	(void)cl; // Prevent warning when NEWCAMD and CCCAM are both disabled
	tpl_addVar(vars, TPLADDONCE, "CLIENTPROTO", proto);
	tpl_addVar(vars, TPLADDONCE, "CLIENTPROTOTITLE", "");
}

static char *send_oscam_user_config(struct templatevars *vars, struct uriparams *params, int32_t apicall) {
	struct s_auth *account;
	struct s_client *cl;
	char *user = getParam(params, "user");
	int32_t found = 0;

	if(!apicall) setActiveMenu(vars, MNU_USERS);

	if (strcmp(getParam(params, "action"), "reinit") == 0) {
		if(!cfg.http_readonly)
			refresh_oscam(REFR_ACCOUNTS);
	}

	if (strcmp(getParam(params, "action"), "delete") == 0) {
		if(cfg.http_readonly) {
			tpl_addMsg(vars, "WebIf is in readonly mode. No deletion will be made!");
		} else {
			struct s_auth *account_prev = NULL;

			for(account = cfg.account; (account); account = account->next){
				if(strcmp(account->usr, user) == 0) {
					if(account_prev == NULL)
						cfg.account = account->next;
					else
						account_prev->next = account->next;
					ll_clear(account->aureader_list);
					for (cl=first_client->next; cl ; cl=cl->next){
						if(cl->account == account){
							if (ph[cl->ctyp].type & MOD_CONN_NET) {
								kill_thread(cl);
							} else {
								cl->account = first_client->account;
							}
						}
					}
					add_garbage(account);
					found = 1;
					break;
				}
				account_prev = account;
			}
			if (found > 0) {
				if (write_userdb()!=0) tpl_addMsg(vars, "Write Config failed!");
			} else tpl_addMsg(vars, "Sorry but the specified user doesn't exist. No deletion will be made!");
		}
	}

	if ((strcmp(getParam(params, "action"), "disable") == 0) || (strcmp(getParam(params, "action"), "enable") == 0)) {
		account = get_account_by_name(getParam(params, "user"));
		if (account) {
			if(strcmp(getParam(params, "action"), "disable") == 0){
				account->disabled = 1;
				for (cl=first_client->next; cl ; cl=cl->next){
					if(cl->account == account){
						if (ph[cl->ctyp].type & MOD_CONN_NET) {
							kill_thread(cl);
						} else {
							cl->account = first_client->account;
						}
					}
				}
			} else
				account->disabled = 0;
			if (write_userdb() != 0) tpl_addMsg(vars, "Write Config failed!");
		} else {
			tpl_addMsg(vars, "Sorry but the specified user doesn't exist. No deletion will be made!");
		}
	}

	if (strcmp(getParam(params, "action"), "resetstats") == 0) {
		account = get_account_by_name(getParam(params, "user"));
		if (account) clear_account_stats(account);
	}

	if (strcmp(getParam(params, "action"), "resetserverstats") == 0) {
		clear_system_stats();
	}

	if (strcmp(getParam(params, "action"), "resetalluserstats") == 0) {
		clear_all_account_stats();
	}

	if ((strcmp(getParam(params, "part"), "adduser") == 0) && (!cfg.http_readonly)) {
		tpl_addVar(vars, TPLAPPEND, "NEWUSERFORM", tpl_getTpl(vars, "ADDNEWUSER"));
	} else {
		if(cfg.http_refresh > 0) {
			tpl_printf(vars, TPLADD, "REFRESHTIME", "%d", cfg.http_refresh);
			tpl_addVar(vars, TPLADD, "REFRESHURL", "userconfig.html");
			tpl_addVar(vars, TPLADD, "REFRESH", tpl_getTpl(vars, "REFRESH"));
		}
	}


	/* List accounts*/
	char *status, *expired, *classname, *lastchan;
	time_t now = time((time_t*)0);
	int32_t isec = 0, chsec = 0;

	char *filter = NULL;
	int32_t clientcount = 0;
	if (apicall) {
		filter = getParam(params, "label");
	}

	int32_t total_users = 0;
	int32_t disabled_users = 0;
	int32_t expired_users = 0;
	int32_t active_users = 0;
	int32_t connected_users = 0;
	int32_t online_users = 0;
	int8_t isactive;
	int32_t casc_users = 0;
	int32_t casc_users2 = 0;

	if (cfg.http_showpicons) tpl_addVar(vars, TPLADD, "PICONHEADER", "<TH>Image</TH>");

	for (account=cfg.account; (account); account=account->next) {
		//clear for next client
		total_users++;
		isactive=1;

		status = "offline"; lastchan = "&nbsp;", expired = ""; classname = "offline";
		isec = 0;
		chsec = 0;

		//reset caid/srevid template variables
		tpl_addVar(vars, TPLADD, "CLIENTCAID", "");
		tpl_addVar(vars, TPLADD, "CLIENTSRVID", "");
		tpl_addVar(vars, TPLADD, "LASTCHANNEL", "");

		if(account->expirationdate && account->expirationdate < now) {
			expired = " (expired)";
			classname = "expired";
			expired_users++;
			isactive=0;
		} else {
			expired = "";
		}

		if(account->disabled != 0) {
			expired = " (disabled)"; classname = "disabled";
			tpl_addVar(vars, TPLADD, "SWITCHICO", "image?i=ICENA");
			tpl_addVar(vars, TPLADD, "SWITCHTITLE", "enable this account");
			tpl_addVar(vars, TPLADD, "SWITCH", "enable");
			disabled_users++;
			isactive=0;
		} else {
			tpl_addVar(vars, TPLADD, "SWITCHICO", "image?i=ICDIS");
			tpl_addVar(vars, TPLADD, "SWITCHTITLE", "disable this account");
			tpl_addVar(vars, TPLADD, "SWITCH", "disable");
		}

		if (isactive)
			active_users++;

		int32_t lastresponsetm = 0, latestactivity=0;
		char *proto = "";
		double cwrate = 0.0, cwrate2 = 0.0;

		//search account in active clients
		int32_t isactive = 0;
		int16_t nrclients = 0;
		struct s_client *cl, *latestclient=NULL;
		for (cl=first_client->next; cl ; cl=cl->next) {
			if (cl->account && !strcmp(cl->account->usr, account->usr)) {
				if(cl->lastecm > latestactivity || cl->login > latestactivity){
					if(cl->lastecm > cl->login) latestactivity = cl->lastecm;
					else latestactivity = cl->login;
					latestclient = cl;
				}
				nrclients++;
			}
		}
		if (account->cwfound + account->cwnot + account->cwcache > 0) {
			cwrate = now - account->firstlogin;
			cwrate /= (account->cwfound + account->cwnot + account->cwcache);
		}

		casc_users = 0;
		casc_users2 = 0;
		if(latestclient != NULL) {
			char channame[32];
			status = (!apicall) ? "<b>connected</b>" : "connected";
			if(account->expirationdate && account->expirationdate < now) classname = "expired";
			else classname = "connected";
			proto = monitor_get_proto(latestclient);
			if (latestclient->last_srvid != NO_SRVID_VALUE || latestclient->last_caid != NO_CAID_VALUE)
				lastchan = xml_encode(vars, get_servicename(latestclient, latestclient->last_srvid, latestclient->last_caid, channame));
			else
				lastchan = "";
			lastresponsetm = latestclient->cwlastresptime;
			tpl_addVar(vars, TPLADDONCE, "CLIENTIP", cs_inet_ntoa(latestclient->ip));
			connected_users++;
			casc_users = ll_count(latestclient->cascadeusers);
			LL_ITER it = ll_iter_create(latestclient->cascadeusers);
			struct s_cascadeuser *cu;
			while ((cu=ll_iter_next(&it))) {
				if (cu->cwrate > 0)
					casc_users2++;
			}
		}
		if(latestactivity > 0 && latestclient != NULL){
			isec = now - latestactivity;
			chsec = latestclient->lastswitch ? now - latestclient->lastswitch : 0;
			if (isec < cfg.hideclient_to) {
				isactive = 1;
				status = (!apicall) ? "<b>online</b>" : "online";
				if(account->expirationdate && account->expirationdate < now) classname = "expired";
				else classname = "online";
				if (latestclient->cwfound + latestclient->cwnot + latestclient->cwcache > 0) {
					cwrate2 = now - latestclient->login;
					cwrate2 /= (latestclient->cwfound + latestclient->cwnot + latestclient->cwcache);
					tpl_printf(vars, TPLADDONCE, "CWRATE2", " (%.2f)", cwrate2);
					online_users++;
				}
			}
		}

		tpl_printf(vars, TPLADD, "CWOK", "%d", account->cwfound);
		tpl_printf(vars, TPLADD, "CWNOK", "%d", account->cwnot);
		tpl_printf(vars, TPLADD, "CWIGN", "%d", account->cwignored);
		tpl_printf(vars, TPLADD, "CWTOUT", "%d", account->cwtout);
		tpl_printf(vars, TPLADD, "CWCACHE", "%d", account->cwcache);
		tpl_printf(vars, TPLADD, "CWTUN", "%d", account->cwtun);
		tpl_printf(vars, TPLADD, "EMMOK", "%d", account->emmok);
		tpl_printf(vars, TPLADD, "EMMNOK", "%d", account->emmnok);
		tpl_printf(vars, TPLADD, "CWRATE", "%.2f", cwrate);
		tpl_printf(vars, TPLADD, "CASCUSERS", "%d", casc_users);
		tpl_printf(vars, TPLADD, "CASCUSERS2", "%d", casc_users2);
		tpl_printf(vars, TPLADD, "CASCUSERSCOMB", "%d/%d", casc_users, casc_users2);

		if ( isactive > 0 || !cfg.http_hide_idle_clients) {

			tpl_printf(vars, TPLADDONCE, "CWLASTRESPONSET", "%d", lastresponsetm);
			tpl_addVar(vars, TPLADDONCE, "IDLESECS", sec2timeformat(vars, isec));

			if (isactive > 0) {
				tpl_printf(vars, TPLADDONCE, "CLIENTTIMEONCHANNELAPI", "%d", chsec);
				tpl_addVar(vars, TPLADDONCE, "CLIENTTIMEONCHANNEL", sec2timeformat(vars, chsec));
				if (account->tosleep){
					tpl_printf(vars, TPLADDONCE, "CLIENTTIMETOSLEEP", "Sleeping in %d minutes", account->tosleep - (chsec / 60));
					tpl_printf(vars, TPLADDONCE, "CLIENTTIMETOSLEEPAPI", "%d", account->tosleep - (chsec / 60));
				} else {
					tpl_addVar(vars, TPLADDONCE, "CLIENTTIMETOSLEEP", "No sleep defined");
					tpl_addVar(vars, TPLADDONCE, "CLIENTTIMETOSLEEPAPI", "undefined");
				}
				if(latestclient){
					tpl_printf(vars, TPLADD, "CLIENTCAID", "%04X", latestclient->last_caid);
					tpl_printf(vars, TPLADD, "CLIENTSRVID", "%04X", latestclient->last_srvid);

					if (cfg.http_showpicons) {
						tpl_printf(vars, TPLADD, "LASTCHANNEL", "<img class=\"clientpicon\" src=\"image?i=IC_%04X_%04X\" alt=\"%s\" title=\"%s\">",
																latestclient->last_caid,
																latestclient->last_srvid,
																lastchan,
																lastchan);
					} else {
						tpl_addVar(vars, TPLADDONCE, "LASTCHANNEL", lastchan);
					}
				}

			} else {
				tpl_addVar(vars, TPLADDONCE, "CLIENTTIMEONCHANNELAPI", "");
				tpl_addVar(vars, TPLADDONCE, "CLIENTTIMEONCHANNEL", "");
				tpl_addVar(vars, TPLADDONCE, "CLIENTTIMETOSLEEP", "");
				tpl_addVar(vars, TPLADDONCE, "CLIENTTIMETOSLEEPAPI", "");
				tpl_addVar(vars, TPLADD, "CLIENTCAID", "");
				tpl_addVar(vars, TPLADD, "CLIENTSRVID", "");
				tpl_addVar(vars, TPLADD, "CLIENTPICON", "");
			}

			webif_add_client_proto(vars, latestclient, proto);
		}

		tpl_addVar(vars, TPLADD, "CLASSNAME", classname);
		tpl_addVar(vars, TPLADD, "USER", xml_encode(vars, account->usr));
		tpl_addVar(vars, TPLADD, "USERENC", urlencode(vars, account->usr));
		tpl_addVar(vars, TPLADD, "DESCRIPTION", xml_encode(vars, account->description?account->description:""));
		tpl_addVar(vars, TPLADD, "STATUS", status);
		tpl_addVar(vars, TPLAPPEND, "STATUS", expired);
		if(nrclients > 1) tpl_printf(vars, TPLADDONCE, "CLIENTCOUNTNOTIFIER", "<SPAN CLASS=\"span_notifier\">%d</SPAN>", nrclients);

		// append row to table template
		if (!apicall)
			tpl_addVar(vars, TPLAPPEND, "USERCONFIGS", tpl_getTpl(vars, "USERCONFIGLISTBIT"));
		else
			if (!filter || strcmp(filter, account->usr) == 0 || strcmp(filter, "all") == 0 || strlen(filter) == 0) {
				tpl_addVar(vars, TPLAPPEND, "APIUSERCONFIGS", tpl_getTpl(vars, "APIUSERCONFIGLISTBIT"));
				++clientcount;
			}
	}

	tpl_printf(vars, TPLADD, "TOTAL_USERS", "%d", total_users);
	tpl_printf(vars, TPLADD, "TOTAL_DISABLED", "%d", disabled_users);
	tpl_printf(vars, TPLADD, "TOTAL_EXPIRED", "%d", expired_users);
	tpl_printf(vars, TPLADD, "TOTAL_ACTIVE", "%d", active_users);
	tpl_printf(vars, TPLADD, "TOTAL_CONNECTED", "%d", connected_users);
	tpl_printf(vars, TPLADD, "TOTAL_ONLINE", "%d", online_users);

	tpl_printf(vars, TPLADD, "TOTAL_CWOK", "%d", first_client->cwfound);
	tpl_printf(vars, TPLADD, "TOTAL_CWNOK", "%d", first_client->cwnot);
	tpl_printf(vars, TPLADD, "TOTAL_CWIGN", "%d", first_client->cwignored);
	tpl_printf(vars, TPLADD, "TOTAL_CWTOUT", "%d", first_client->cwtout);
	tpl_printf(vars, TPLADD, "TOTAL_CWCACHE", "%d", first_client->cwcache);
	tpl_printf(vars, TPLADD, "TOTAL_CWTUN", "%d", first_client->cwtun);

	float ecmsum = first_client->cwfound + first_client->cwnot + first_client->cwignored + first_client->cwtout+ first_client->cwcache + first_client->cwtun;
	if (ecmsum < 1) {
		ecmsum = 1;
	}
	tpl_printf(vars, TPLADD, "REL_CWOK", "%.2f", first_client->cwfound * 100 / ecmsum);
	tpl_printf(vars, TPLADD, "REL_CWNOK", "%.2f", first_client->cwnot * 100 / ecmsum);
	tpl_printf(vars, TPLADD, "REL_CWIGN", "%.2f", first_client->cwignored * 100 / ecmsum);
	tpl_printf(vars, TPLADD, "REL_CWTOUT", "%.2f", first_client->cwtout * 100 / ecmsum);
	tpl_printf(vars, TPLADD, "REL_CWCACHE", "%.2f", first_client->cwcache * 100 / ecmsum);
	tpl_printf(vars, TPLADD, "REL_CWTUN", "%.2f", first_client->cwtun * 100 / ecmsum);


	if (!apicall)
		return tpl_getTpl(vars, "USERCONFIGLIST");
	else {
		if (!filter || clientcount > 0) {
			return tpl_getTpl(vars, "APIUSERCONFIGLIST");
		} else {
			tpl_printf(vars, TPLADD, "APIERRORMESSAGE", "Invalid client %s", xml_encode(vars, filter));
			return tpl_getTpl(vars, "APIERROR");
		}
	}

}

#define ENTITLEMENT_PAGE_SIZE 500

#ifdef MODULE_CCCAM
static void print_cards(struct templatevars *vars, struct uriparams *params, struct cc_card **cardarray, int32_t cardsize,
		int8_t show_global_list, struct s_reader *rdr, int32_t offset, int32_t apicall)
{
	if (cardarray) {
		uint8_t serbuf[8];
		int32_t i, count = 0;
		char provname[83];
		struct cc_card *card;
		int32_t cardcount = 0;
		int32_t providercount = 0;
		int32_t nodecount = 0;

		char *provider = "";

		// @todo alno: sort by click, 0=ascending, 1=descending (maybe two buttons or reverse on second click)
		for(i = offset; i < cardsize; ++i) {
			card = cardarray[i];
			if (count == ENTITLEMENT_PAGE_SIZE)
				break;
			count++;

			if (!apicall) {
				if (show_global_list)
					rdr = card->origin_reader;
				if (rdr)
					tpl_printf(vars, TPLADD, "HOST", "%s:%d", rdr->device, rdr->r_port);
				tpl_printf(vars, TPLADD, "CAID", "%04X", card->caid);
				tpl_printf(vars, TPLADD, "CARDTYPE", "%02X", card->card_type);
			} else {
				tpl_printf(vars, TPLADD, "APICARDNUMBER", "%d", cardcount);
				tpl_printf(vars, TPLADD, "APICAID", "%04X", card->caid);
				tpl_printf(vars, TPLADD, "APICARDTYPE", "%02X", card->card_type);
			}

			if (cc_UA_valid(card->hexserial)) { //Add UA:
				cc_UA_cccam2oscam(card->hexserial, serbuf, card->caid);
				char tmp[20];
				tpl_printf(vars, TPLAPPEND, "HOST", "<BR>\nUA_Oscam:%s", cs_hexdump(0, serbuf, 8, tmp, 20));
				tpl_printf(vars, TPLAPPEND, "HOST", "<BR>\nUA_CCcam:%s", cs_hexdump(0, card->hexserial, 8, tmp, 20));
			}
				if (!apicall) {
						int32_t n;
						LL_ITER its = ll_iter_create(card->goodsids);
						struct cc_srvid *srv;
						n=0;
						tpl_addVar(vars, TPLADD, "SERVICESGOOD", "");
						while ((srv=ll_iter_next(&its))) {
								tpl_printf(vars, TPLAPPEND, "SERVICESGOOD", "%04X%s", srv->sid, ++n%10==0?"<BR>\n":" ");
						}

						its = ll_iter_create(card->badsids);
						n=0;
						tpl_addVar(vars, TPLADD, "SERVICESBAD", "");
						while ((srv=ll_iter_next(&its))) {
								tpl_printf(vars, TPLAPPEND, "SERVICESBAD", "%04X%s", srv->sid, ++n%10==0?"<BR>\n":" ");
						}
			}

			struct s_cardsystem *cs = get_cardsystem_by_caid(card->caid);

			if (cs)
				tpl_addVar(vars, TPLADD, "SYSTEM", cs->desc);
			else
				tpl_addVar(vars, TPLADD, "SYSTEM", "???");

            tpl_printf(vars, TPLADD, "SHAREID", "%08X", card->id);
            tpl_printf(vars, TPLADD, "REMOTEID", "%08X", card->remote_id);
			tpl_printf(vars, TPLADD, "UPHOPS", "%d", card->hop);
			tpl_printf(vars, TPLADD, "MAXDOWN", "%d", card->reshare);

			LL_ITER pit = ll_iter_create(card->providers);
			struct cc_provider *prov;

			providercount = 0;

			if (!apicall)
				tpl_addVar(vars, TPLADD, "PROVIDERS", "");
			else
				tpl_addVar(vars, TPLADD, "PROVIDERLIST", "");

			while ((prov = ll_iter_next(&pit))) {
				provider = xml_encode(vars, get_provider(card->caid, prov->prov, provname, sizeof(provname)));

				if (!apicall) {
					if (prov->sa[0] || prov->sa[1] || prov->sa[2] || prov->sa[3]) {
						tpl_printf(vars, TPLAPPEND, "PROVIDERS", "%s SA:%02X%02X%02X%02X<BR>\n", provider, prov->sa[0], prov->sa[1], prov->sa[2], prov->sa[3]);
					} else {
						tpl_printf(vars, TPLAPPEND, "PROVIDERS", "%s<BR>\n", provider);
					}
				} else {
					if (prov->sa[0] || prov->sa[1] || prov->sa[2] || prov->sa[3])
						tpl_printf(vars, TPLADD, "APIPROVIDERSA", "%02X%02X%02X%02X", prov->sa[0], prov->sa[1], prov->sa[2], prov->sa[3]);
					else
						tpl_addVar(vars, TPLADD, "APIPROVIDERSA","");
					tpl_printf(vars, TPLADD, "APIPROVIDERCAID", "%04X", card->caid);
					tpl_printf(vars, TPLADD, "APIPROVIDERPROVID", "%06X", prov->prov);
					tpl_printf(vars, TPLADD, "APIPROVIDERNUMBER", "%d", providercount);
					tpl_addVar(vars, TPLADD, "APIPROVIDERNAME", xml_encode(vars, provider));
					tpl_addVar(vars, TPLAPPEND, "PROVIDERLIST", tpl_getTpl(vars, "APICCCAMCARDPROVIDERBIT"));

				}
				providercount++;
				tpl_printf(vars, TPLADD, "APITOTALPROVIDERS", "%d", providercount);
			}

			LL_ITER nit = ll_iter_create(card->remote_nodes);
			uint8_t *node;

			nodecount = 0;
			if (!apicall) tpl_addVar(vars, TPLADD, "NODES", "");
			else tpl_addVar(vars, TPLADD, "NODELIST", "");

			while ((node = ll_iter_next(&nit))) {

				if (!apicall) {
					tpl_printf(vars, TPLAPPEND, "NODES", "%02X%02X%02X%02X%02X%02X%02X%02X<BR>\n",
							node[0], node[1], node[2], node[3], node[4], node[5], node[6], node[7]);
				} else {
					tpl_printf(vars, TPLADD, "APINODE", "%02X%02X%02X%02X%02X%02X%02X%02X", node[0], node[1], node[2], node[3], node[4], node[5], node[6], node[7]);
					tpl_printf(vars, TPLADD, "APINODENUMBER", "%d", nodecount);
					tpl_addVar(vars, TPLAPPEND, "NODELIST", tpl_getTpl(vars, "APICCCAMCARDNODEBIT"));
				}
				nodecount++;
				tpl_printf(vars, TPLADD, "APITOTALNODES", "%d", nodecount);
			}

			if (!apicall)
				tpl_addVar(vars, TPLAPPEND, "CCCAMSTATSENTRY", tpl_getTpl(vars, "ENTITLEMENTCCCAMENTRYBIT"));
			else
				tpl_addVar(vars, TPLAPPEND, "CARDLIST", tpl_getTpl(vars, "APICCCAMCARDBIT"));

			cardcount++;
		}
		// set previous Link if needed
		if (offset >= ENTITLEMENT_PAGE_SIZE) {
			tpl_printf(vars, TPLAPPEND, "CONTROLS", "<A HREF=\"entitlements.html?offset=%d&globallist=%s&amp;label=%s\"> << PREVIOUS < </A>",
					offset - ENTITLEMENT_PAGE_SIZE,
					getParam(params, "globallist"),
					getParam(params, "label"));
		}

		// set next link if needed
		if (cardsize > count && offset < cardsize) {
			tpl_printf(vars, TPLAPPEND, "CONTROLS", "<A HREF=\"entitlements.html?offset=%d&globallist=%s&amp;label=%s\"> > NEXT >> </A>",
					offset + ENTITLEMENT_PAGE_SIZE,
					getParam(params, "globallist"),
					getParam(params, "label"));
		}

		if (!apicall) {
			tpl_printf(vars, TPLADD, "TOTALS", "card count=%d", cardsize);
			tpl_addVar(vars, TPLADD, "ENTITLEMENTCONTENT", tpl_getTpl(vars, "ENTITLEMENTCCCAMBIT"));
		} else {
			tpl_printf(vars, TPLADD, "APITOTALCARDS", "%d", cardsize);
		}

	} else {
		if (!apicall) {
			tpl_addVar(vars, TPLADD, "ENTITLEMENTCONTENT", tpl_getTpl(vars, "ENTITLEMENTGENERICBIT"));
			tpl_addVar(vars, TPLADD, "LOGHISTORY", "no cards found<BR>\n");
		} else {
			tpl_printf(vars, TPLADD, "APITOTALCARDS", "%d", 0);
		}
	}

}
#endif

static char *send_oscam_entitlement(struct templatevars *vars, struct uriparams *params, int32_t apicall) {
	if(!apicall) setActiveMenu(vars, MNU_READERS);
	char *reader_ = getParam(params, "label");
#ifdef MODULE_CCCAM
	char *sharelist_ = getParam(params, "globallist");
	int32_t show_global_list = sharelist_ && sharelist_[0]=='1';

	int32_t offset = atoi(getParam(params, "offset")); //should be 0 if parameter is missed on very first call

	struct s_reader *rdr = get_reader_by_label(getParam(params, "label"));
	if (show_global_list || strlen(reader_) || (rdr && rdr->typ == R_CCCAM)) {

		if (show_global_list || (rdr && rdr->typ == R_CCCAM && rdr->enable)) {

			if (show_global_list) {
					tpl_addVar(vars, TPLADD, "READERNAME", "GLOBAL");
					tpl_addVar(vars, TPLADD, "APIHOST", "GLOBAL");
					tpl_addVar(vars, TPLADD, "APIHOSTPORT", "GLOBAL");
			} else {
					tpl_addVar(vars, TPLADD, "READERNAME", rdr->label);
					tpl_addVar(vars, TPLADD, "APIHOST", rdr->device);
					tpl_printf(vars, TPLADD, "APIHOSTPORT", "%d", rdr->r_port);
			}

			int32_t cardsize;
			if (show_global_list) {
#ifdef MODULE_CCCSHARE
				int32_t i;
				LLIST **sharelist = get_and_lock_sharelist();
				LLIST *sharelist2 = ll_create("web-sharelist");
				for (i=0;i<CAID_KEY;i++) {
					if (sharelist[i])
						ll_putall(sharelist2, sharelist[i]);
				}
				unlock_sharelist();
				struct cc_card **cardarray = get_sorted_card_copy(sharelist2, 0, &cardsize);
				ll_destroy(sharelist2);

				print_cards(vars, params, cardarray, cardsize, 1, NULL, offset, apicall);

				free(cardarray);

			} else {
				struct s_client *rc = rdr->client;
				struct cc_data *rcc = (rc)?rc->cc:NULL;

				if (rcc && rcc->cards) {
					struct cc_card **cardarray = get_sorted_card_copy(rcc->cards, 0, &cardsize);
					print_cards(vars, params, cardarray, cardsize, 0, rdr, offset, apicall);
					free(cardarray);
				}
#endif
				}


		} else {
#else
	if (strlen(reader_)) {
		{
			struct s_reader *rdr;
#endif
			tpl_addVar(vars, TPLADD, "LOGHISTORY", "->");
			// normal non-cccam reader

			rdr = get_reader_by_label(reader_);

			if (rdr) {
				struct s_client *cl = rdr->client;
				if (rdr->ll_entitlements) {

					time_t now = (time((time_t*)0)/84600)*84600;

					struct tm start_t, end_t;
					LL_ITER itr = ll_iter_create(rdr->ll_entitlements);
					S_ENTITLEMENT *item;

					tpl_addVar(vars, TPLAPPEND, "LOGHISTORY", "<BR><BR>New Structure:<BR>");
					char tbuffer[83];
					while ((item = ll_iter_next(&itr))) {

						localtime_r(&item->start, &start_t);
						localtime_r(&item->end, &end_t);

						if(!apicall)
							strftime(tbuffer, 30, "%Y-%m-%d", &start_t);
						else
							strftime(tbuffer, 30, "%Y-%m-%dT%H:%M:%S%z", &start_t);
						tpl_addVar(vars, TPLADD, "ENTSTARTDATE", tbuffer);

						if(!apicall)
							strftime(tbuffer, 30, "%Y-%m-%d", &end_t);
						else
							strftime(tbuffer, 30, "%Y-%m-%dT%H:%M:%S%z", &end_t);
						tpl_addVar(vars, TPLADD, "ENTENDDATE", tbuffer);

						tpl_addVar(vars, TPLADD, "ENTEXPIERED", item->end > now ? "e_valid" : "e_expired");
						tpl_printf(vars, TPLADD, "ENTCAID", "%04X", item->caid);
						tpl_printf(vars, TPLADD, "ENTPROVID", "%06X", item->provid);
						tpl_printf(vars, TPLADD, "ENTID", "%08X%08X", (uint32_t)(item->id >> 32), (uint32_t)item->id);
						tpl_printf(vars, TPLADD, "ENTCLASS", "%08X", item->class);
						tpl_addVar(vars, TPLADD, "ENTTYPE", entitlement_type[item->type]);

						char *entresname;
						entresname = xml_encode(vars, get_tiername((uint16_t)(item->id & 0xFFFF), item->caid, tbuffer));
						if (!tbuffer[0])
							entresname = xml_encode(vars, get_provider(item->caid, item->provid, tbuffer, sizeof(tbuffer)));
						tpl_addVar(vars, TPLADD, "ENTRESNAME", entresname);

						if ((strcmp(getParam(params, "hideexpired"), "1") != 0) || (item->end > now))
							tpl_addVar(vars, TPLAPPEND, "READERENTENTRY", tpl_getTpl(vars, "ENTITLEMENTITEMBIT"));

					}
				}

				if (cl && cl->typ)
					tpl_printf(vars, TPLADD, "READERTYPE", "%c", cl->typ);
				else
					tpl_addVar(vars, TPLADD, "READERTYPE", "null");
				tpl_addVar(vars, TPLADD, "READERNAME", rdr->label);

				int8_t i, j;
				for(i = 0; i < 15; i++)	tpl_printf(vars, TPLAPPEND, "READERROM", "%c", rdr->rom[i]);
				for(i = 0; i < 8; i++)	tpl_printf(vars, TPLAPPEND, "READERSERIAL", "%02X", rdr->hexserial[i]);
				for (i = 0; i < rdr->nprov; i++) {
					for(j = 0; j < 4; j++)	tpl_printf(vars, TPLAPPEND, "READERPROVIDS", "%02X ", rdr->prid[i][j]);
					tpl_addVar(vars, TPLAPPEND, "READERPROVIDS", i==0 ? "(sysid)<br>\n" : "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<br>\n");
				}



				if (rdr->card_valid_to) {
					struct tm vto_t;
					char vtobuffer[30];
					localtime_r(&rdr->card_valid_to, &vto_t);
					strftime(vtobuffer, 30, "%Y-%m-%d", &vto_t);
					tpl_addVar(vars, TPLADD, "READERCARDVALIDTO", vtobuffer);
				} else {
					tpl_addVar(vars, TPLADD, "READERCARDVALIDTO", "n/a");
				}

				if (rdr->irdId[0]){
					for(i = 0; i < 4; i++)	tpl_printf(vars, TPLAPPEND, "READERIRDID", "%02X ", rdr->irdId[i]);
				} else {
					tpl_addVar(vars, TPLADD, "READERIRDID", "n/a");
				}

				if(rdr->card_atr_length)
					for(i = 0; i < rdr->card_atr_length; i++) tpl_printf(vars, TPLAPPEND, "READERATR", "%02X ", rdr->card_atr[i]);

				tpl_addVar(vars, TPLADD, "READERCSYSTEM", rdr->csystem.desc);

				tpl_addVar(vars, TPLADD, "ENTITLEMENTCONTENT", tpl_getTpl(vars, "ENTITLEMENTBIT"));

			} else {
				tpl_addMsg(vars, "Reader does not exist or is not started!");
			}
		}

	} else {
		tpl_addVar(vars, TPLADD, "ENTITLEMENTCONTENT", tpl_getTpl(vars, "ENTITLEMENTGENERICBIT"));
	}

	if (!apicall)
		return tpl_getTpl(vars, "ENTITLEMENTS");
	else
		return tpl_getTpl(vars, "APICCCAMCARDLIST");
}

static char *send_oscam_status(struct templatevars *vars, struct uriparams *params, int32_t apicall) {
	int32_t i;
	char *usr;
	int32_t lsec, isec, chsec, con, cau = 0;
	time_t now = time((time_t*)0);
	struct tm lt;

	if(!apicall) setActiveMenu(vars, MNU_STATUS);

	if (strcmp(getParam(params, "action"), "kill") == 0) {
		char *cptr = getParam(params, "threadid");
		struct s_client *cl = NULL;
		if (strlen(cptr)>1)
			sscanf(cptr, "%p", (void**)(void*)&cl);

		if (cl && is_valid_client(cl)) {
			kill_thread(cl);
			cs_log("Client %s killed by WebIF from %s", cl->account->usr, cs_inet_ntoa(GET_IP()));
		}
	}

	if (strcmp(getParam(params, "action"), "restart") == 0) {
		struct s_reader *rdr = get_reader_by_label(getParam(params, "label"));
		if(rdr) {
			add_job(rdr->client, ACTION_READER_RESTART, NULL, 0);
			cs_log("Reader %s restarted by WebIF from %s", rdr->label, cs_inet_ntoa(GET_IP()));
		}
	}

	char *debuglvl = getParam(params, "debug");
	if(strlen(debuglvl) > 0) {
#ifndef WITH_DEBUG
		cs_log("*** Warning: Debug Support not compiled in ***");
#else
		int32_t dblvl = atoi(debuglvl);
		if(dblvl >= 0 && dblvl <= 65535) cs_dblevel = dblvl;
		cs_log("%s debug_level=%d", "all", cs_dblevel);
#endif
	}

	char *hide = getParam(params, "hide");
	if(strlen(hide) > 0) {
		struct s_client *hideidx = NULL;
		sscanf(hide, "%p", (void**)(void*)&hideidx);

		if(hideidx && is_valid_client(hideidx))
			hideidx->wihidden = 1;
	}

	char *hideidle = getParam(params, "hideidle");
	if(strlen(hideidle) > 0) {
		if (atoi(hideidle) == 2) {
			struct s_client *cl;
			for (cl=first_client; cl ; cl=cl->next)
				cl->wihidden = 0;
		}
		else {
			int32_t oldval = cfg.http_hide_idle_clients;
			config_set("webif", "httphideidleclients", hideidle);
			if(oldval != cfg.http_hide_idle_clients) {
				refresh_oscam(REFR_SERVER);
			}
		}
	}

	if(cfg.http_hide_idle_clients > 0) tpl_addVar(vars, TPLADD, "HIDEIDLECLIENTSSELECTED1", "selected");
	else tpl_addVar(vars, TPLADD, "HIDEIDLECLIENTSSELECTED0", "selected");

	int32_t user_count_all = 0, user_count_shown = 0, user_count_active = 0;
	int32_t reader_count_all = 0, reader_count_conn = 0;
	int32_t proxy_count_all = 0, proxy_count_conn = 0;
	int32_t shown;

	struct s_client *cl;
	cs_readlock(&readerlist_lock);
	cs_readlock(&clientlist_lock);
	for (i=0, cl=first_client; cl ; cl=cl->next, i++) {
		if (cl->kill) continue;
#ifdef CS_CACHEEX
		if (ph[cl->ctyp].listenertype != LIS_CSPUDP) {
#endif

		// Reset template variables
		tpl_addVar(vars, TPLADD, "CLIENTLBVALUE","");
		tpl_addVar(vars, TPLADD, "LASTREADER", "");
		tpl_addVar(vars, TPLADD, "CLIENTPROTO", "");
		tpl_addVar(vars, TPLADD, "CLIENTDESCRIPTION", "");
		tpl_addVar(vars, TPLADD, "CLIENTLASTRESPONSETIME", "");
		tpl_addVar(vars, TPLADD, "CLIENTLASTRESPONSETIMEHIST", "");
		tpl_addVar(vars, TPLADD, "JSONARRAYDELIMITER", cl->next ? "," : "");

		if (cl->typ=='c')
			user_count_all++;
		else if (cl->typ=='p')
			proxy_count_all++;
		else if (cl->typ=='r')
			reader_count_all++;

		shown = 0;
		if (cl->wihidden != 1) {
			if (cfg.http_hide_idle_clients != 1 || cl->typ != 'c' || (now - cl->lastecm) <= cfg.hideclient_to) {
				if (cl->typ=='c'){
					user_count_shown++;
					if (cfg.http_hide_idle_clients != 1 && cfg.hideclient_to > 0 && (now - cl->lastecm) <= cfg.hideclient_to) {
						user_count_active++;
						tpl_addVar(vars, TPLADD, "CLIENTTYPE", "a");
					} else tpl_addVar(vars, TPLADD, "CLIENTTYPE", "c");
				} else {
					if (cl->typ=='r' && cl->reader->card_status==CARD_INSERTED)
						reader_count_conn++;
					else if (cl->typ=='p' && (cl->reader->card_status==CARD_INSERTED ||cl->reader->tcp_connected))
						proxy_count_conn++;
					tpl_printf(vars, TPLADD, "CLIENTTYPE", "%c", cl->typ);
				}
				if(cl->typ == 'c' || cl->typ == 'r' || cl->typ == 'p'){
					if(cl->lastecm > cl->login) isec = now - cl->lastecm;
					else isec = now - cl->login;
				} else isec = now - cl->last;

				shown = 1;
				lsec = now - cl->login;
				chsec = now - cl->lastswitch;
				usr = username(cl);

				if ((cl->typ=='r') || (cl->typ=='p')) usr=cl->reader->label;

				if (cl->dup) con=2;
				else if ((cl->tosleep) && (now-cl->lastswitch>cl->tosleep)) con=1;
				else con=0;

				// no AU reader == 0 / AU ok == 1 / Last EMM > aulow == -1
				if(cl->typ == 'c' || cl->typ == 'p' || cl->typ == 'r'){
					if ((cl->typ == 'c' && ll_count(cl->aureader_list) == 0) || ((cl->typ == 'p' || cl->typ == 'r') && cl->reader->audisabled)) cau = 0;
					else if ((now-cl->lastemm) / 60 > cfg.aulow) cau = -1;
					else cau = 1;

					if (!apicall){
						if (cau == 0) {
							tpl_addVar(vars, TPLADD, "CLIENTCAUHTTP", "OFF");
						} else {
							if (cau == -1)
								tpl_addVar(vars, TPLADD, "CLIENTCAUHTTP", "<a href=\"#\" class=\"tooltip\">ON");
							else
								tpl_addVar(vars, TPLADD, "CLIENTCAUHTTP", "<a href=\"#\" class=\"tooltip\">ACTIVE");
							tpl_addVar(vars, TPLAPPEND, "CLIENTCAUHTTP", "<span>");
							if (cl->typ == 'c'){
								struct s_reader *rdr;
								LL_ITER itr = ll_iter_create(cl->aureader_list);
								while ((rdr = ll_iter_next(&itr))) {
									if(rdr->audisabled)
										tpl_printf(vars, TPLAPPEND, "CLIENTCAUHTTP", "(%s)<br>", rdr->label);
									else
										tpl_printf(vars, TPLAPPEND, "CLIENTCAUHTTP", "%s<br>", rdr->label);
								}
							} else tpl_addVar(vars, TPLAPPEND, "CLIENTCAUHTTP", cl->reader->label);
							tpl_addVar(vars, TPLAPPEND, "CLIENTCAUHTTP", "</span></a>");
						}
					}
				} else {
					cau = 0;
					tpl_addVar(vars, TPLADD, "CLIENTCAUHTTP", "");
				}

				localtime_r(&cl->login, &lt);

				tpl_printf(vars, TPLADD, "HIDEIDX", "%p", cl);

				if(!apicall) {
					if(cl->typ == 'c' && !cfg.http_readonly) {
						tpl_printf(vars, TPLADD, "CSIDX", "<A HREF=\"status.html?action=kill&threadid=%p\" TITLE=\"Kill this client\"><IMG CLASS=\"icon\" SRC=\"image?i=ICKIL\" ALT=\"Kill\"></A>", cl);
					}
					else if((cl->typ == 'p') && !cfg.http_readonly) {
						tpl_printf(vars, TPLADD, "CSIDX", "<A HREF=\"status.html?action=restart&amp;label=%s\" TITLE=\"Restart this reader/ proxy\"><IMG CLASS=\"icon\" SRC=\"image?i=ICKIL\" ALT=\"Restart\"></A>", urlencode(vars, cl->reader->label));
					}
					else {
						tpl_printf(vars, TPLADD, "CSIDX", "%p&nbsp;", cl);
					}
				} else {
					tpl_printf(vars, TPLADD, "CSIDX", "%p", cl);
				}

				tpl_printf(vars, TPLADD, "CLIENTTYPE", "%c", cl->typ);
				tpl_printf(vars, TPLADD, "CLIENTCNR", "%d", get_threadnum(cl));
				tpl_addVar(vars, TPLADD, "CLIENTUSER", xml_encode(vars, usr));

				if(cl->typ == 'c') {
					tpl_addVar(vars, TPLADD, "CLIENTDESCRIPTION", xml_encode(vars, (cl->account && cl->account->description)?cl->account->description:""));
				}
				else if(cl->typ == 'p' || cl->typ == 'r') {
					tpl_addVar(vars, TPLADD, "CLIENTDESCRIPTION", xml_encode(vars, cl->reader->description?cl->reader->description:""));
				}

				tpl_printf(vars, TPLADD, "CLIENTCAU", "%d", cau);
				if(!apicall){
					if(cl->typ == 'c' || cl->typ == 'p' || cl->typ == 'r'){
						if(cl->crypted) tpl_addVar(vars, TPLADD, "CLIENTCRYPTED", "ON");
						else tpl_addVar(vars, TPLADD, "CLIENTCRYPTED", "OFF");
					} else tpl_addVar(vars, TPLADD, "CLIENTCRYPTED", "");
				} else tpl_printf(vars, TPLADD, "CLIENTCRYPTED", "%d", cl->crypted);
				tpl_addVar(vars, TPLADD, "CLIENTIP", cs_inet_ntoa(cl->ip));
				tpl_printf(vars, TPLADD, "CLIENTPORT", "%d", cl->port);
				char *proto = monitor_get_proto(cl);
				webif_add_client_proto(vars, cl, proto);

				if (!apicall) {
					if((cl->typ != 'p' && cl->typ != 'r') || cl->reader->card_status == CARD_INSERTED){
						tpl_printf(vars, TPLADD, "CLIENTLOGINDATE", "%02d.%02d.%02d  %02d:%02d:%02d", lt.tm_mday, lt.tm_mon+1, lt.tm_year%100, lt.tm_hour, lt.tm_min, lt.tm_sec);
						tpl_addVar(vars, TPLADD, "CLIENTLOGINSECS", sec2timeformat(vars, lsec));
					} else {
						tpl_addVar(vars, TPLADD, "CLIENTLOGINDATE", "");
						tpl_addVar(vars, TPLADD, "CLIENTLOGINSECS", "");
					}
				} else {
					char tbuffer [30];
					strftime(tbuffer, 30, "%Y-%m-%dT%H:%M:%S%z", &lt);
					tpl_addVar(vars, TPLADD, "CLIENTLOGINDATE", tbuffer);
					tpl_printf(vars, TPLADD, "CLIENTLOGINSECS", "%d", lsec);
				}

				//load historical values from ringbuffer
				char *value = get_ecm_historystring(cl);
				tpl_addVar(vars, TPLADD, "CLIENTLASTRESPONSETIMEHIST", value);
				free_mk_t(value);

				if (isec < cfg.hideclient_to || cfg.hideclient_to == 0) {
					if (((cl->typ!='r') || (cl->typ!='p')) && (cl->lastreader[0])) {
						tpl_printf(vars, TPLADD, "CLIENTLBVALUE", "by %s", cl->lastreader);
						tpl_printf(vars, TPLAPPEND, "CLIENTLBVALUE", "&nbsp;(%dms)", cl->cwlastresptime);
						if (apicall)
							tpl_addVar(vars, TPLADD, "LASTREADER", cl->lastreader);
					}

					if (cl->last_caid != NO_CAID_VALUE)
						tpl_printf(vars, TPLADD, "CLIENTCAID", "%04X", cl->last_caid);
					else
						tpl_addVar(vars, TPLADD, "CLIENTCAID", "none");
					if (cl->last_srvid != NO_SRVID_VALUE)
						tpl_printf(vars, TPLADD, "CLIENTSRVID", "%04X", cl->last_srvid);
					else
						tpl_printf(vars, TPLADD, "CLIENTSRVID", "none");
					tpl_printf(vars, TPLADD, "CLIENTLASTRESPONSETIME", "%d", cl->cwlastresptime?cl->cwlastresptime:1);

					if (!cfg.appendchaninfo) {
						char channame[32];
						get_servicename(cl, cl->last_srvid, cl->last_caid, channame);
					}

					tpl_printf(vars, TPLADD, "CLIENTSRVPROVIDER","%s%s", cl->last_srvidptr && cl->last_srvidptr->prov ? xml_encode(vars, cl->last_srvidptr->prov) : "", cl->last_srvidptr && cl->last_srvidptr->prov ? ": " : "");
					tpl_addVar(vars, TPLADD, "CLIENTSRVNAME", cl->last_srvidptr && cl->last_srvidptr->name ? xml_encode(vars, cl->last_srvidptr->name) : "");
					tpl_addVar(vars, TPLADD, "CLIENTSRVTYPE", cl->last_srvidptr && cl->last_srvidptr->type ? xml_encode(vars, cl->last_srvidptr->type) : "");
					tpl_addVar(vars, TPLADD, "CLIENTSRVDESCRIPTION", cl->last_srvidptr && cl->last_srvidptr->desc ? xml_encode(vars, cl->last_srvidptr->desc) : "");
					tpl_addVar(vars, TPLADD, "CLIENTTIMEONCHANNEL", sec2timeformat(vars, chsec));
				} else {
					tpl_addVar(vars, TPLADD, "CLIENTCAID", "0000");
					tpl_addVar(vars, TPLADD, "CLIENTSRVID", "0000");
					tpl_addVar(vars, TPLADD, "CLIENTSRVPROVIDER","");
					tpl_addVar(vars, TPLADD, "CLIENTSRVNAME","");
					tpl_addVar(vars, TPLADD, "CLIENTSRVTYPE","");
					tpl_addVar(vars, TPLADD, "CLIENTSRVDESCRIPTION","");
					tpl_addVar(vars, TPLADD, "CLIENTLBVALUE","");
					tpl_addVar(vars, TPLADD, "CLIENTTIMEONCHANNEL", "");

				}

				if (!apicall) {
					tpl_addVar(vars, TPLADD, "CLIENTIDLESECS", sec2timeformat(vars, isec));

					if((cl->typ != 'p' && cl->typ != 'r') || cl->reader->card_status == CARD_INSERTED)
						tpl_addVar(vars, TPLADD, "CLIENTIDLESECSCLASS", "idlesec_normal");
					else
						tpl_addVar(vars, TPLADD, "CLIENTIDLESECSCLASS", "idlesec_alert");
				} else {
					tpl_printf(vars, TPLADD, "CLIENTIDLESECS", "%d", isec);
				}

				if(con == 2) tpl_addVar(vars, TPLADD, "CLIENTCON", "Duplicate");
				else if (con == 1) tpl_addVar(vars, TPLADD, "CLIENTCON", "Sleep");
				else
				{
					struct s_reader *rdr = cl->reader;
					char *txt = "OK";
					if(!rdr && (cl->typ == 'r' || cl->typ == 'p')) txt = "UNKNOWN";
					else if (cl->typ == 'r' || cl->typ == 'p') //reader or proxy
					{
						if (rdr->lbvalue)
							tpl_printf(vars, TPLADD, "CLIENTLBVALUE", "<A HREF=\"readerstats.html?label=%s&amp;hide=4\" TITLE=\"Show statistics for this reader/ proxy\">%d</A>", urlencode(vars, rdr->label), rdr->lbvalue);
						else
							tpl_printf(vars, TPLADD, "CLIENTLBVALUE", "<A HREF=\"readerstats.html?label=%s&amp;hide=4\" TITLE=\"Show statistics for this reader/ proxy\">%s</A>", urlencode(vars, rdr->label), "no data");

						switch(rdr->card_status)
						{
							case NO_CARD: txt = "OFF"; break;
							case UNKNOWN: txt = "UNKNOWN"; break;
							case CARD_NEED_INIT: txt = "NEEDINIT"; break;
							case CARD_INSERTED:
								if (cl->typ=='p')
									txt = "CONNECTED";
								else
									txt = "CARDOK";
								break;
							case CARD_FAILURE: txt = "ERROR"; break;
							default: txt = "UNDEF";
						}
					}
					tpl_addVar(vars, TPLADD, "CLIENTCON", txt);

					if (rdr && (cl->typ == 'r') && (!apicall)) //reader
					{
						if (rdr->ll_entitlements)
						{
							LL_ITER itr = ll_iter_create(rdr->ll_entitlements);
							S_ENTITLEMENT *ent;
							uint16_t total_ent = 0;
							uint16_t active_ent = 0;
							time_t now = (time((time_t*)0)/84600)*84600;
							struct tm end_t;

							tpl_addVar(vars, TPLADD, "TMPSPAN", "<SPAN>");
							while((ent = ll_iter_next(&itr)))
							{
								total_ent++;
								if ((ent->end > now) && (ent->type != 7))
								{
									if (active_ent) tpl_addVar(vars, TPLAPPEND, "TMPSPAN", "<BR><BR>");
									active_ent++;
									localtime_r(&ent->end, &end_t);
									tpl_printf(vars, TPLAPPEND, "TMPSPAN", "%04X:%06X<BR>exp:%04d/%02d/%02d",
									    ent->caid, ent->provid,
									    end_t.tm_year + 1900, end_t.tm_mon + 1, end_t.tm_mday);
								}
							}

							if (((total_ent) && (active_ent == 0)) || (total_ent == 0))
							{
								tpl_addVar(vars, TPLAPPEND, "TMPSPAN", "No active entitlements found");
							}

							tpl_addVar(vars, TPLAPPEND, "TMPSPAN", "</SPAN>");

							if (active_ent)
							{
								tpl_printf(vars, TPLADD, "TMP", "(%d entitlement%s)", active_ent, (active_ent != 1)?"s":"");
							}
							else
							{
								tpl_addVar(vars, TPLADD, "TMP", "(no entitlements)");

							}

							tpl_printf(vars, TPLAPPEND, "CLIENTCON", " <A HREF=\"entitlements.html?label=%s&hideexpired=1\" class=\"tooltip%s\">%s%s</A>",
													urlencode(vars, cl->reader->label),
													active_ent > 0 ? "": "1",
													tpl_getVar(vars, "TMP"),
													tpl_getVar(vars, "TMPSPAN"));
						}
						else
						{
							tpl_printf(vars, TPLAPPEND, "CLIENTCON", " <A HREF=\"entitlements.html?label=%s&hideexpired=1\" class=\"tooltip\">(no entitlements)"
												    "<SPAN>No active entitlements found</SPAN></A>",
													urlencode(vars, cl->reader->label));
						}
					}

#ifdef MODULE_CCCAM
					if (!apicall) {
						if(rdr && (cl->typ == 'r' || cl->typ == 'p') && strncmp(proto,"cccam", 5) == 0 && rdr->tcp_connected && rdr->card_status != CARD_FAILURE){
							struct cc_data *rcc = cl->cc;
							if(rcc){
								LLIST *cards = rcc->cards;
								if (cards) {
									int32_t cnt = ll_count(cards);
									int32_t locals = rcc->num_hop1;
									tpl_printf(vars, TPLADD, "TMP", "(%d of %d card%s)", locals, cnt, (cnt > 1)? "s": "");
									tpl_printf(vars, TPLADD, "TMPSPAN","<SPAN>card count=%d<BR>hop1=%d<BR>hop2=%d<BR>hopx=%d<BR>currenthops=%d<BR><BR>reshare0=%d<BR>reshare1=%d<BR>reshare2=%d<BR>resharex=%d</SPAN>",
											cnt,
											rcc->num_hop1,
											rcc->num_hop2,
											rcc->num_hopx,
											cl->reader->cc_currenthops,
											rcc->num_reshare0,
											rcc->num_reshare1,
											rcc->num_reshare2,
											rcc->num_resharex);

									tpl_printf(vars, TPLAPPEND, "CLIENTCON", " <A HREF=\"entitlements.html?label=%s\" class=\"tooltip%s\">%s%s</A>",
																urlencode(vars, cl->reader->label),
																rcc->num_reshare0 > 0 ? "1": "",
																tpl_getVar(vars, "TMP"),
																tpl_getVar(vars, "TMPSPAN"));
								}
							}
						}
					}
#endif
				}
			}
		}

		if (!apicall) {
			// select right suborder
			if (cl->typ == 'c') {
				if (shown) tpl_addVar(vars, TPLAPPEND, "CLIENTSTATUS", tpl_getTpl(vars, "CLIENTSTATUSBIT"));
				if (cfg.http_hide_idle_clients == 1 || cfg.hideclient_to < 1) {
					tpl_printf(vars, TPLADD, "CLIENTHEADLINE", "\t\t<TR><TD CLASS=\"subheadline\" colspan=\"17\">Clients %d/%d</TD></TR>\n",
						user_count_shown, user_count_all);
				} else {
					tpl_printf(vars, TPLADD, "CLIENTHEADLINE", "\t\t<TR><TD CLASS=\"subheadline\" colspan=\"17\">Clients %d/%d (%d with ECM within last %d seconds)</TD></TR>\n",
						user_count_shown, user_count_all, user_count_active, cfg.hideclient_to);
				}
			}
			else if (cl->typ == 'r') {
				if (shown) tpl_addVar(vars, TPLAPPEND, "READERSTATUS", tpl_getTpl(vars, "CLIENTSTATUSBIT"));
				tpl_printf(vars, TPLADD, "READERHEADLINE", "\t\t<TR><TD CLASS=\"subheadline\" colspan=\"17\">Readers %d/%d</TD></TR>\n",
						reader_count_conn, reader_count_all);
			}
			else if (cl->typ == 'p') {
				if (shown) tpl_addVar(vars, TPLAPPEND, "PROXYSTATUS", tpl_getTpl(vars, "CLIENTSTATUSBIT"));
				tpl_printf(vars, TPLADD, "PROXYHEADLINE", "\t\t<TR><TD CLASS=\"subheadline\" colspan=\"17\">Proxies %d/%d</TD></TR>\n",
						proxy_count_conn, proxy_count_all);
			}
			else
				if (shown) tpl_addVar(vars, TPLAPPEND, "SERVERSTATUS", tpl_getTpl(vars, "CLIENTSTATUSBIT"));

		} else {
			if (shown){
				if(apicall == 1)
					tpl_addVar(vars, TPLAPPEND, "APISTATUSBITS", tpl_getTpl(vars, "APISTATUSBIT"));
				if(apicall == 2)
					tpl_addVar(vars, TPLAPPEND, "JSONSTATUSBITS", tpl_getTpl(vars, "JSONSTATUSBIT"));
			}
		}

#ifdef CS_CACHEEX
		}
#endif

	}
	cs_readunlock(&clientlist_lock);
	cs_readunlock(&readerlist_lock);

	if (cfg.loghistorysize) {
		char *t_loghistptr = loghistptr, *ptr1 = NULL;
		if(loghistptr >= loghist + (cfg.loghistorysize) - 1)
			t_loghistptr = loghist;
		int32_t d = 0, l1 = strlen(t_loghistptr+1) + 2;
		char *lastpos = loghist + (cfg.loghistorysize)-1;

		for (ptr1 = t_loghistptr + l1, i=0; i<200; i++, ptr1 = ptr1+l1) {
			l1 = strlen(ptr1)+1;
			if (!d && ((ptr1 >= lastpos) || (l1 < 2))) {
				ptr1 = loghist;
				l1 = strlen(ptr1)+1;
				d++;
			}

			if (d && ((ptr1 >= t_loghistptr) || (l1 < 2)))
				break;

			char p_usr[32];
			size_t pos1 = strcspn (ptr1, "\t")+1;
			cs_strncpy(p_usr, ptr1 , pos1 > sizeof(p_usr) ? sizeof(p_usr) : pos1);

			char *p_txt = ptr1 + pos1;

			if (!apicall) {
				if (p_txt[0]) tpl_printf(vars, TPLAPPEND, "LOGHISTORY", "\t\t<span class=\"%s\">%s\t\t</span><br>\n", p_usr, p_txt);
			} else {
				if (apicall == 1)
					if (strcmp(getParam(params, "appendlog"), "1") == 0)
						tpl_addVar(vars, TPLAPPEND, "LOGHISTORY", p_txt);
			}
		}
	} else {
		tpl_addVar(vars, TPLADD, "LOGHISTORY", "loghistorysize is set to 0 in your configuration<BR>\n");
	}

#ifdef WITH_DEBUG
	// Debuglevel Selector
	int32_t lvl;
	for (i = 0; i < MAX_DEBUG_LEVELS; i++) {
		lvl = 1 << i;
		tpl_printf(vars, TPLADD, "TMPC", "DCLASS%d", lvl);
		tpl_printf(vars, TPLADD, "TMPV", "DEBUGVAL%d", lvl);
		if (cs_dblevel & lvl) {
			tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMPC"), "debugls");
			tpl_printf(vars, TPLADD, tpl_getVar(vars, "TMPV"), "%d", cs_dblevel - lvl);
		} else {
			tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMPC"), "debugl");
			tpl_printf(vars, TPLADD, tpl_getVar(vars, "TMPV"), "%d", cs_dblevel + lvl);
		}
	}

	if (cs_dblevel == 255)
		tpl_addVar(vars, TPLADD, "DCLASS65535", "debugls");
	else
		tpl_addVar(vars, TPLADD, "DCLASS65535", "debugl");

	tpl_addVar(vars, TPLADD, "NEXTPAGE", "status.html");
	tpl_addVar(vars, TPLADD, "DCLASS", "debugl"); //default
	tpl_printf(vars, TPLADD, "ACTDEBUG", "%d", cs_dblevel);
	tpl_addVar(vars, TPLADD, "SDEBUG", tpl_getTpl(vars, "DEBUGSELECT"));
#endif

	if(apicall) {
		if(apicall == 1)
			return tpl_getTpl(vars, "APISTATUS");
		if(apicall == 2)
			return tpl_getTpl(vars, "JSONSTATUS");
	}
	return tpl_getTpl(vars, "STATUS");
}

static char *send_oscam_services_edit(struct templatevars *vars, struct uriparams *params) {
	struct s_sidtab *sidtab,*ptr;
	char label[sizeof(cfg.sidtab->label)];
	int32_t i;

	setActiveMenu(vars, MNU_SERVICES);

	cs_strncpy(label, strtolower(getParam(params, "service")), sizeof(label));
++cfg_sidtab_generation;
	for (sidtab = cfg.sidtab; sidtab != NULL && strcmp(label, sidtab->label) != 0; sidtab=sidtab->next);

	if (sidtab == NULL) {
		i = 1;
		while(strlen(label) < 1) {
			snprintf(label, sizeof(label)/sizeof(char) - 1, "newservice%d", i);
			for (sidtab = cfg.sidtab; sidtab != NULL && strcmp(label, sidtab->label) != 0; sidtab = sidtab->next);
			if(sidtab != NULL) label[0] = '\0';
			++i;
		}
		if (!cs_malloc(&sidtab, sizeof(struct s_sidtab), -1)) return "0";

		if(cfg.sidtab == NULL) cfg.sidtab = sidtab;
		else {
			for (ptr = cfg.sidtab; ptr != NULL && ptr->next != NULL; ptr = ptr->next);
			ptr->next = sidtab;
		}
		cs_strncpy((char *)sidtab->label, label, sizeof(sidtab->label));
		++cfg_sidtab_generation;
		tpl_addMsg(vars, "New service has been added");
		// Adding is uncritical as the new service is appended to sidtabok/sidtabno and accounts/clients/readers have zeros there
		if (write_services()!=0) tpl_addMsg(vars, "Writing services to disk failed!");
	}

	if (strcmp(getParam(params, "action"), "Save") == 0) {
		for(i=0;i<(*params).paramcount;i++) {
			if ((strcmp((*params).params[i], "action")) && (strcmp((*params).params[i], "service"))) {
				chk_sidtab((*params).params[i], (*params).values[i], sidtab);
			}
		}
		++cfg_sidtab_generation;
		tpl_addMsg(vars, "Services updated");
		// We don't need any refresh here as accounts/clients/readers sidtabok/sidtabno are unaffected!
		if (write_services()!=0) tpl_addMsg(vars, "Write Config failed!");

		for (sidtab = cfg.sidtab; sidtab != NULL && strcmp(label, sidtab->label) != 0; sidtab=sidtab->next);
	}

	tpl_addVar(vars, TPLADD, "LABEL", xml_encode(vars, sidtab->label));
	tpl_addVar(vars, TPLADD, "LABELENC", urlencode(vars, sidtab->label));


	for (i=0; i<sidtab->num_caid; i++) {
		if (i==0) tpl_printf(vars, TPLADD, "CAIDS", "%04X", sidtab->caid[i]);
		else tpl_printf(vars, TPLAPPEND, "CAIDS", ",%04X", sidtab->caid[i]);
	}
	for (i=0; i<sidtab->num_provid; i++) {
		if (i==0) tpl_printf(vars, TPLADD, "PROVIDS", "%06X", sidtab->provid[i]);
		else tpl_printf(vars, TPLAPPEND, "PROVIDS", ",%06X", sidtab->provid[i]);
	}
	for (i=0; i<sidtab->num_srvid; i++) {
		if (i==0) tpl_printf(vars, TPLADD, "SRVIDS", "%04X", sidtab->srvid[i]);
		else tpl_printf(vars, TPLAPPEND, "SRVIDS", ",%04X", sidtab->srvid[i]);
	}
	return tpl_getTpl(vars, "SERVICEEDIT");
}

static void delete_from_SIDTABBITS(SIDTABBITS *orgsidtab, int32_t position, int32_t sidtablength){
	if(*orgsidtab){
		int32_t i;
		SIDTABBITS newsidtab = 0;
		for(i = 0; i < position; ++i){
			if(*orgsidtab&((SIDTABBITS)1<<i))
				newsidtab|=((SIDTABBITS)1<<i);
		}
		for(; i < sidtablength; ++i){
			if(*orgsidtab&((SIDTABBITS)1<<(i+1)))
				newsidtab|=((SIDTABBITS)1<<i);
		}
		*orgsidtab = newsidtab;
	}
}

static char *send_oscam_services(struct templatevars *vars, struct uriparams *params) {
	struct s_sidtab *sidtab;
	char *service = getParam(params, "service");
	char channame[32];
	int32_t i, counter = 0;

	setActiveMenu(vars, MNU_SERVICES);

	if (strcmp(getParam(params, "action"), "delete") == 0) {
		if(cfg.http_readonly) {
			tpl_addMsg(vars, "Sorry, Webif is in readonly mode. No deletion will be made!");
		} else {
			struct s_sidtab *sidtab_prev = NULL;
			int32_t sidtablength = -1;
			int32_t position = 0;
			sidtab=cfg.sidtab;

			// Calculate sidtablength before deletion so that updating sidtabs is faster
			for (sidtab=cfg.sidtab; sidtab; sidtab = sidtab->next)
				++sidtablength;

			for (sidtab=cfg.sidtab; sidtab; sidtab = sidtab->next){
				if(strcmp(sidtab->label, service) == 0) {
					struct s_auth *account;
					struct s_client *cl;
					struct s_reader *rdr;

					if(!sidtab_prev)
						cfg.sidtab = sidtab->next;
					else
						sidtab_prev->next = sidtab->next;

					for (account = cfg.account; (account); account = account->next) {
						delete_from_SIDTABBITS(&account->sidtabok, position, sidtablength);
						delete_from_SIDTABBITS(&account->sidtabno, position, sidtablength);
					
						for (cl=first_client->next; cl ; cl=cl->next){
							if(account == cl->account){
								cl->sidtabok = account->sidtabok;
								cl->sidtabno = account->sidtabno;
							}
						}
					}

					LL_ITER itr = ll_iter_create(configured_readers);
					while((rdr = ll_iter_next(&itr))){
						delete_from_SIDTABBITS(&rdr->sidtabok, position, sidtablength);
						delete_from_SIDTABBITS(&rdr->sidtabno, position, sidtablength);
					}
					free_sidtab(sidtab);
					++counter;
					break;
				}
				sidtab_prev = sidtab;
				position++;
			}
			if (counter > 0) {
				++cfg_sidtab_generation;
				tpl_addMsg(vars, "Service has been deleted!");
				if (write_services() != 0) tpl_addMsg(vars, "Writing services to disk failed!");
			} else tpl_addMsg(vars, "Sorry but the specified service doesn't exist. No deletion will be made!");
		}
	}

	sidtab = cfg.sidtab;
	// Show List
	counter = 0;
	while(sidtab != NULL) {
		tpl_addVar(vars, TPLADD, "SID","");
		if ((strcmp(getParam(params, "service"), sidtab->label) == 0) && (strcmp(getParam(params, "action"), "list") == 0) ) {
			tpl_addVar(vars, TPLADD, "SIDCLASS","sidlist");
			tpl_addVar(vars, TPLAPPEND, "SID", "<div style=\"float:right;background-color:red;color:white\"><A HREF=\"services.html\" style=\"color:white;text-decoration:none\">X</A></div>");
			for (i=0; i<sidtab->num_srvid; i++) {
				tpl_printf(vars, TPLAPPEND, "SID", "%04X : %s<BR>", sidtab->srvid[i], xml_encode(vars, get_servicename(cur_client(), sidtab->srvid[i], sidtab->caid[0], channame)));
			}
		} else {
			tpl_addVar(vars, TPLADD, "SIDCLASS","");
			tpl_printf(vars, TPLADD, "SID","<A HREF=\"services.html?service=%s&amp;action=list\">Show Services</A>", urlencode(vars, sidtab->label));
		}
		tpl_addVar(vars, TPLADD, "LABELENC", urlencode(vars, sidtab->label));
		tpl_addVar(vars, TPLADD, "LABEL", xml_encode(vars, sidtab->label));
		tpl_addVar(vars, TPLADD, "SIDLIST", tpl_getTpl(vars, "SERVICECONFIGSIDBIT"));

		tpl_addVar(vars, TPLAPPEND, "SERVICETABS", tpl_getTpl(vars, "SERVICECONFIGLISTBIT"));
		sidtab=sidtab->next;
		counter++;
	}
	if(counter >= MAX_SIDBITS) {
		tpl_addVar(vars, TPLADD, "BTNDISABLED", "DISABLED");
		tpl_addMsg(vars, "Maximum Number of Services is reached");
	}
	return tpl_getTpl(vars, "SERVICECONFIGLIST");
}

static char *send_oscam_savetpls(struct templatevars *vars) {
	if(cfg.http_tpl) {
		tpl_printf(vars, TPLADD, "CNT", "%d", tpl_saveIncludedTpls(cfg.http_tpl));
		tpl_addVar(vars, TPLADD, "PATH", cfg.http_tpl);
	} else tpl_addVar(vars, TPLADD, "CNT", "0");
	return tpl_getTpl(vars, "SAVETEMPLATES");
}

static char *send_oscam_shutdown(struct templatevars *vars, FILE *f, struct uriparams *params, int8_t apicall, int8_t *keepalive) {
	if(!apicall) setActiveMenu(vars, MNU_SHUTDOWN);
	if (strcmp(strtolower(getParam(params, "action")), "shutdown") == 0) {
		*keepalive = 0;
		if(!apicall){
			tpl_addVar(vars, TPLADD, "STYLESHEET", CSS);
			tpl_printf(vars, TPLADD, "REFRESHTIME", "%d", SHUTDOWNREFRESH);
			tpl_addVar(vars, TPLADD, "REFRESHURL", "status.html");
			tpl_addVar(vars, TPLADD, "REFRESH", tpl_getTpl(vars, "REFRESH"));
			tpl_printf(vars, TPLADD, "SECONDS", "%d", SHUTDOWNREFRESH);
			char *result = tpl_getTpl(vars, "SHUTDOWN");
			send_headers(f, 200, "OK", NULL, "text/html", 0, strlen(result), NULL, 0);
			webif_write(result, f);
			cs_log("Shutdown requested by WebIF from %s", cs_inet_ntoa(GET_IP()));
		} else {
			tpl_addVar(vars, TPLADD, "APICONFIRMMESSAGE", "shutdown");
			cs_log("Shutdown requested by XMLApi from %s", cs_inet_ntoa(GET_IP()));
		}
		running = 0;
		pthread_kill(httpthread, SIGPIPE);		// send signal to master thread to wake up from accept()
		cs_exit_oscam();

		if(!apicall)
			return "1";
		else
			return tpl_getTpl(vars, "APICONFIRMATION");

	}
	else if (strcmp(strtolower(getParam(params, "action")), "restart") == 0) {
		*keepalive = 0;
		if(!apicall){
			tpl_addVar(vars, TPLADD, "STYLESHEET", CSS);
			tpl_addVar(vars, TPLADD, "REFRESHTIME", "5");
			tpl_addVar(vars, TPLADD, "REFRESHURL", "status.html");
			tpl_addVar(vars, TPLADD, "REFRESH", tpl_getTpl(vars, "REFRESH"));
			tpl_addVar(vars, TPLADD, "SECONDS", "5");
			char *result = tpl_getTpl(vars, "SHUTDOWN");
			send_headers(f, 200, "OK", NULL, "text/html", 0,strlen(result), NULL, 0);
			webif_write(result, f);
			cs_log("Restart requested by WebIF from %s", cs_inet_ntoa(GET_IP()));
		} else {
			tpl_addVar(vars, TPLADD, "APICONFIRMMESSAGE", "restart");
			cs_log("Restart requested by XMLApi from %s", cs_inet_ntoa(GET_IP()));
		}
		running = 0;
		pthread_kill(httpthread, SIGPIPE);		// send signal to master thread to wake up from accept()
		cs_restart_oscam();

		if(!apicall)
			return "1";
		else
			return tpl_getTpl(vars, "APICONFIRMATION");

	} else {
		return tpl_getTpl(vars, "PRESHUTDOWN");
	}
}

static char *send_oscam_script(struct templatevars *vars) {
	setActiveMenu(vars, MNU_SCRIPT);
	char *result = "not found";
	int32_t rc = 0;
	if(!cfg.http_readonly) {
		if (cfg.http_script) {
			tpl_addVar(vars, TPLADD, "SCRIPTNAME",cfg.http_script);
			rc = system(cfg.http_script);
			if(rc == -1) {
				result = "done";
			} else {
				result = "failed";
			}
		} else {
			tpl_addVar(vars, TPLADD, "SCRIPTNAME", "no script defined");
		}
		tpl_addVar(vars, TPLADD, "SCRIPTRESULT", result);
		tpl_printf(vars, TPLADD, "CODE", "%d", rc);
	} else {
		tpl_addMsg(vars, "Sorry, Webif is in readonly mode. No script execution possible!");
	}
	return tpl_getTpl(vars, "SCRIPT");

}

static char *send_oscam_scanusb(struct templatevars *vars) {
	setActiveMenu(vars, MNU_READERS);
#if !defined(__CYGWIN__)
	FILE *fp;
	int32_t err=0;
	char path[1035];

	fp = popen("lsusb -v | egrep '^Bus|^ *iSerial|^ *iProduct'", "r");
	if (fp == NULL) {
		tpl_addVar(vars, TPLADD, "USBENTRY", "Failed to run lusb");
		tpl_addVar(vars, TPLADD, "USBENTRY", path);
		tpl_addVar(vars, TPLAPPEND, "USBBIT", tpl_getTpl(vars, "SCANUSBBIT"));
		err = 1;
	}

	if(!err) {
		while (fgets(path, sizeof(path)-1, fp) != NULL) {
			tpl_addVar(vars, TPLADD, "USBENTRYCLASS", "");
			if (strstr(path,"Bus ")) {
				tpl_addVar(vars, TPLADD, "USBENTRY", path);
				tpl_addVar(vars, TPLADD, "USBENTRYCLASS", "CLASS=\"scanusbsubhead\"");
			} else {
				tpl_printf(vars, TPLADD, "USBENTRY", "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;%s", path);
			}
			tpl_addVar(vars, TPLAPPEND, "USBBIT", tpl_getTpl(vars, "SCANUSBBIT"));
		}
	}
	pclose(fp);
#else
	tpl_addMsg(vars, "Function not supported in CYGWIN environment");
#endif
	return tpl_getTpl(vars, "SCANUSB");
}

static char *send_oscam_files(struct templatevars *vars, struct uriparams *params, int8_t apicall) {

	int32_t writable=0;
	//int8_t apicall = 0; //remove before flight

	if(!apicall) setActiveMenu(vars, MNU_FILES);

	tpl_addVar(vars, TPLADD, "APIFILENAME", "null");
	tpl_addVar(vars, TPLADD, "APIWRITABLE", "0");

	char *stoplog = getParam(params, "stoplog");
	if(strlen(stoplog) > 0)
		cs_disable_log(atoi(stoplog));

	char *stopusrlog = getParam(params, "stopusrlog");
	if(strlen(stopusrlog) > 0)
		cfg.disableuserfile = atoi(stopusrlog);

	char *debuglvl = getParam(params, "debug");
	if(strlen(debuglvl) > 0) {
#ifndef WITH_DEBUG
		cs_log("*** Warning: Debug Support not compiled in ***");
#else
		int32_t dblvl = atoi(debuglvl);
		if(dblvl >= 0 && dblvl <= 65535) cs_dblevel = dblvl;
		cs_log("%s debug_level=%d", "all", cs_dblevel);
#endif
	}

	char targetfile[256];

	if (strcmp(getParam(params, "file"), "conf") == 0) {
		if(!apicall) setActiveSubMenu(vars, MNU_CFG_FCONF);
		snprintf(targetfile, 255,"%s%s", cs_confdir, "oscam.conf");
		tpl_addVar(vars, TPLADD, "APIFILENAME", "oscam.conf");
		tpl_addVar(vars, TPLADD, "APIWRITABLE", "1");
		writable = 1;
	}
	else if (strcmp(getParam(params, "file"), "version") == 0) {
		if(!apicall) setActiveSubMenu(vars, MNU_CFG_FVERSION);
		snprintf(targetfile, 255,"%s%s", get_tmp_dir(), "/oscam.version");
		tpl_addVar(vars, TPLADD, "APIFILENAME", "oscam.version");
		tpl_addVar(vars, TPLADD, "APIWRITABLE", "0");
	}

	else if (strcmp(getParam(params, "file"), "user") == 0) {
		if(!apicall) setActiveSubMenu(vars, MNU_CFG_FUSER);
		snprintf(targetfile, 255,"%s%s", cs_confdir, "oscam.user");
		tpl_addVar(vars, TPLADD, "APIFILENAME", "oscam.user");
		tpl_addVar(vars, TPLADD, "APIWRITABLE", "1");
		writable = 1;
	}
	else if (strcmp(getParam(params, "file"), "server") == 0) {
		if(!apicall) setActiveSubMenu(vars, MNU_CFG_FSERVER);
		snprintf(targetfile, 255,"%s%s", cs_confdir, "oscam.server");
		tpl_addVar(vars, TPLADD, "APIFILENAME", "oscam.server");
		tpl_addVar(vars, TPLADD, "APIWRITABLE", "1");
		writable = 1;
	}
	else if (strcmp(getParam(params, "file"), "services") == 0) {
		if(!apicall) setActiveSubMenu(vars, MNU_CFG_FSERVICES);
		snprintf(targetfile, 255,"%s%s", cs_confdir, "oscam.services");
		tpl_addVar(vars, TPLADD, "APIFILENAME", "oscam.services");
		tpl_addVar(vars, TPLADD, "APIWRITABLE", "1");
		writable = 1;
	}
	else if (strcmp(getParam(params, "file"), "whitelist") == 0) {
		if(!apicall) setActiveSubMenu(vars, MNU_CFG_WHITELIST);
		snprintf(targetfile, 255,"%s%s", cs_confdir, "oscam.whitelist");
		tpl_addVar(vars, TPLADD, "APIFILENAME", "oscam.whitelist");
		tpl_addVar(vars, TPLADD, "APIWRITABLE", "1");
		writable = 1;
	}
	else if (strcmp(getParam(params, "file"), "srvid") == 0) {
		if(!apicall) setActiveSubMenu(vars, MNU_CFG_FSRVID);
		snprintf(targetfile, 255,"%s%s", cs_confdir, "oscam.srvid");
		tpl_addVar(vars, TPLADD, "APIFILENAME", "oscam.srvid");
		tpl_addVar(vars, TPLADD, "APIWRITABLE", "1");
		writable = 1;
	}
	else if (strcmp(getParam(params, "file"), "provid") == 0) {
		if(!apicall) setActiveSubMenu(vars, MNU_CFG_FPROVID);
		snprintf(targetfile, 255,"%s%s", cs_confdir, "oscam.provid");
		tpl_addVar(vars, TPLADD, "APIFILENAME", "oscam.provid");
		tpl_addVar(vars, TPLADD, "APIWRITABLE", "1");
		writable = 1;
	}
	else if (strcmp(getParam(params, "file"), "tiers") == 0) {
		if(!apicall) setActiveSubMenu(vars, MNU_CFG_FTIERS);
		snprintf(targetfile, 255,"%s%s", cs_confdir, "oscam.tiers");
		tpl_addVar(vars, TPLADD, "APIFILENAME", "oscam.tiers");
		tpl_addVar(vars, TPLADD, "APIWRITABLE", "1");
		writable = 1;
	}
	else if (!apicall && strcmp(getParam(params, "file"), "logfile") == 0) {
		setActiveSubMenu(vars, MNU_CFG_FLOGFILE);
		snprintf(targetfile, 255,"%s", cfg.logfile);

		if (strcmp(getParam(params, "clear"), "logfile") == 0) {
			if(strlen(targetfile) > 0) {
				FILE *file = fopen(targetfile,"w");
				fclose(file);
			}
		}

#ifdef WITH_DEBUG
		// Debuglevel Selector
		int32_t i, lvl;
		for (i = 0; i < MAX_DEBUG_LEVELS; i++) {
			lvl = 1 << i;
			tpl_printf(vars, TPLADD, "TMPC", "DCLASS%d", lvl);
			tpl_printf(vars, TPLADD, "TMPV", "DEBUGVAL%d", lvl);
			if (cs_dblevel & lvl) {
				tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMPC"), "debugls");
				tpl_printf(vars, TPLADD, tpl_getVar(vars, "TMPV"), "%d", cs_dblevel - lvl);
			} else {
				tpl_addVar(vars, TPLADD, tpl_getVar(vars, "TMPC"), "debugl");
				tpl_printf(vars, TPLADD, tpl_getVar(vars, "TMPV"), "%d", cs_dblevel + lvl);
			}
		}

		if (cs_dblevel == D_ALL_DUMP)
			tpl_addVar(vars, TPLADD, "DCLASS65535", "debugls");
		else
			tpl_addVar(vars, TPLADD, "DCLASS65535", "debugl");

		tpl_addVar(vars, TPLADD, "CUSTOMPARAM", "&file=logfile");
		tpl_printf(vars, TPLADD, "ACTDEBUG", "%d", cs_dblevel);
		tpl_addVar(vars, TPLADD, "SDEBUG", tpl_getTpl(vars, "DEBUGSELECT"));
		tpl_addVar(vars, TPLADD, "NEXTPAGE", "files.html");
#endif

		if(!cfg.disablelog)
			tpl_printf(vars, TPLADD, "LOGMENU", "<A HREF=\"files.html?file=logfile&amp;stoplog=%d\">%s</A><SPAN CLASS=\"debugt\">&nbsp;&nbsp;|&nbsp;&nbsp;</SPAN>\n", 1, "Stop Log");
		else
			tpl_printf(vars, TPLADD, "LOGMENU", "<A HREF=\"files.html?file=logfile&amp;stoplog=%d\">%s</A><SPAN CLASS=\"debugt\">&nbsp;&nbsp;|&nbsp;&nbsp;</SPAN>\n", 0, "Start Log");

		tpl_addVar(vars, TPLAPPEND, "LOGMENU", "<A HREF=\"files.html?file=logfile&amp;clear=logfile\">Clear Log</A>");

	}
	else if (!apicall && strcmp(getParam(params, "file"), "userfile") == 0) {
		setActiveSubMenu(vars, MNU_CFG_FUSERFILE);
		snprintf(targetfile, 255,"%s", cfg.usrfile);
		if (strcmp(getParam(params, "clear"), "usrfile") == 0) {
			if(strlen(targetfile) > 0) {
				FILE *file = fopen(targetfile,"w");
				fclose(file);
			}
		}

		if(!cfg.disableuserfile)
			tpl_printf(vars, TPLADD, "LOGMENU", "<A HREF=\"files.html?file=userfile&amp;stopusrlog=%d\">%s</A>&nbsp;&nbsp;|&nbsp;&nbsp;\n", 1, "Stop Log");
		else
			tpl_printf(vars, TPLADD, "LOGMENU", "<A HREF=\"files.html?file=userfile&amp;stopusrlog=%d\">%s</A>&nbsp;&nbsp;|&nbsp;&nbsp;\n", 0, "Start Log");

		tpl_addVar(vars, TPLAPPEND, "LOGMENU", "<A HREF=\"files.html?file=userfile&amp;clear=usrfile\">Clear Log</A>");

		tpl_printf(vars, TPLADD, "FILTERFORMOPTIONS", "<OPTION value=\"%s\">%s</OPTION>\n", "all", "all");
		struct s_auth *account;
		for (account = cfg.account; (account); account = account->next) {
			tpl_printf(vars, TPLAPPEND, "FILTERFORMOPTIONS", "<OPTION value=\"%s\" %s>%s</OPTION>\n", account->usr, strcmp(getParam(params, "filter"), account->usr) ? "":"selected", account->usr);
		}
		tpl_addVar(vars, TPLADD, "FILTERFORM", tpl_getTpl(vars, "FILTERFORM"));
	}
#ifdef CS_ANTICASC
	else if (!apicall && strcmp(getParam(params, "file"), "anticasc") == 0){
		setActiveSubMenu(vars, MNU_CFG_FACLOG);
		snprintf(targetfile, 255, "%s", cfg.ac_logfile ? cfg.ac_logfile : "");
	}
#endif

#ifdef HAVE_DVBAPI
	else if (strcmp(getParam(params, "file"), "dvbapi") == 0) {
		if(!apicall) setActiveSubMenu(vars, MNU_CFG_FDVBAPI);
		snprintf(targetfile, 255, "%s%s", cs_confdir, "oscam.dvbapi");
		tpl_addVar(vars, TPLADD, "APIFILENAME", "oscam.dvbapi");
		tpl_addVar(vars, TPLADD, "APIWRITABLE", "1");
		writable = 1;
	}
#endif
	else {
		if(!apicall) setActiveSubMenu(vars, MNU_CFG_FVERSION);
		snprintf(targetfile, 255,"%s%s", get_tmp_dir(), "/oscam.version");
		tpl_addVar(vars, TPLADD, "APIFILENAME", "oscam.version");
		tpl_addVar(vars, TPLADD, "APIWRITABLE", "0");
	}

	if (!strstr(targetfile, "/dev/")) {

		if (strcmp(getParam(params, "action"), "Save") == 0) {
			if((strlen(targetfile) > 0) /*&& (file_exists(targetfile) == 1)*/) {
				FILE *fpsave;
				char *fcontent = getParam(params, "filecontent");

				if((fpsave = fopen(targetfile,"w"))){
					int32_t i, lastpos = 0, len = strlen(fcontent) + 1;
					//write submitted file line by line to disk and remove windows linebreaks
					for(i = 0; i < len; ++i){
						char tmp = fcontent[i];
						if(tmp == '\r' || tmp == '\n' || tmp == 0){
							fcontent[i] = 0;
							fprintf(fpsave,"%s%s",fcontent + lastpos, tmp == 0?"":"\n");
							if(tmp == '\r' && fcontent[i+1] == '\n') ++i;
							lastpos = i + 1;
						}
					}
					fclose(fpsave);

					if (strcmp(getParam(params, "file"), "srvid") == 0)
						init_srvid();

					if (strcmp(getParam(params, "file"), "user") == 0)
						cs_accounts_chk();

#ifdef HAVE_DVBAPI
					if (strcmp(getParam(params, "file"), "dvbapi") == 0)
						dvbapi_read_priority();
#endif

					if (strcmp(getParam(params, "file"), "whitelist") == 0)
						global_whitelist_read();

				}
			}
		}

		if((strlen(targetfile) > 0) && (file_exists(targetfile) == 1)) {
			FILE *fp;
			char buffer[256];

			if((fp = fopen(targetfile,"r")) == NULL) return "0";
			while (fgets(buffer, sizeof(buffer), fp) != NULL)
				if (!strcmp(getParam(params, "filter"), "all"))
					tpl_addVar(vars, TPLAPPEND, "FILECONTENT", buffer);
				else
					if(strstr(buffer,getParam(params, "filter")))
						tpl_addVar(vars, TPLAPPEND, "FILECONTENT", buffer);
			fclose (fp);
		} else {
			tpl_addVar(vars, TPLAPPEND, "FILECONTENT", "File does not exist or no file selected!");
		}
	} else {
		tpl_addVar(vars, TPLAPPEND, "FILECONTENT", "File not valid!");
	}

	tpl_addVar(vars, TPLADD, "PART", getParam(params, "file"));

	if (!writable) {
		tpl_addVar(vars, TPLADD, "WRITEPROTECTION", tpl_getTpl(vars, "WRITEPROTECTION"));
		tpl_addVar(vars, TPLADD, "BTNDISABLED", "DISABLED");
	}

	if (!apicall)
		return tpl_getTpl(vars, "FILE");
	else
		return tpl_getTpl(vars, "APIFILE");
}

static char *send_oscam_failban(struct templatevars *vars, struct uriparams *params, int8_t apicall) {
	IN_ADDR_T ip2delete;
	set_null_ip(&ip2delete);
	LL_ITER itr = ll_iter_create(cfg.v_list);
	V_BAN *v_ban_entry;
	//int8_t apicall = 0; //remove before flight

	if(!apicall) setActiveMenu(vars, MNU_FAILBAN);

	if (strcmp(getParam(params, "action"), "delete") == 0) {

		if(strcmp(getParam(params, "intip"), "all") == 0){
			// clear whole list
			while ((v_ban_entry=ll_iter_next(&itr))) {
				ll_iter_remove_data(&itr);
			}

		} else {
			//we have a single IP
			cs_inet_addr(getParam(params, "intip"), &ip2delete);
			while ((v_ban_entry=ll_iter_next(&itr))) {
				if (IP_EQUAL(v_ban_entry->v_ip, ip2delete)) {
					ll_iter_remove_data(&itr);
					break;
				}
			}
		}
	}
	ll_iter_reset(&itr);

	time_t now = time((time_t*)0);

	while ((v_ban_entry=ll_iter_next(&itr))) {

		tpl_printf(vars, TPLADD, "IPADDRESS", "%s : %d", cs_inet_ntoa(v_ban_entry->v_ip), v_ban_entry->v_port);
		tpl_addVar(vars, TPLADD, "VIOLATIONUSER", v_ban_entry->info?v_ban_entry->info:"unknown");
		struct tm st ;
		localtime_r(&v_ban_entry->v_time, &st);
		if (!apicall) {
			tpl_printf(vars, TPLADD, "VIOLATIONDATE", "%02d.%02d.%02d %02d:%02d:%02d",
					st.tm_mday, st.tm_mon+1,
					st.tm_year%100, st.tm_hour,
					st.tm_min, st.tm_sec);
		} else {
			char tbuffer [30];
			strftime(tbuffer, 30, "%Y-%m-%dT%H:%M:%S%z", &st);
			tpl_addVar(vars, TPLADD, "VIOLATIONDATE", tbuffer);
		}

		tpl_printf(vars, TPLADD, "VIOLATIONCOUNT", "%d", v_ban_entry->v_count);

		if (!apicall)
			tpl_addVar(vars, TPLADD, "LEFTTIME", sec2timeformat(vars, (cfg.failbantime * 60) - (now - v_ban_entry->v_time)));
		else
			tpl_printf(vars, TPLADD, "LEFTTIME", "%ld", (cfg.failbantime * 60) - (now - v_ban_entry->v_time));

		tpl_printf(vars, TPLADD, "INTIP", "%s", cs_inet_ntoa(v_ban_entry->v_ip));

		if (!apicall)
			tpl_addVar(vars, TPLAPPEND, "FAILBANROW", tpl_getTpl(vars, "FAILBANBIT"));
		else
			tpl_addVar(vars, TPLAPPEND, "APIFAILBANROW", tpl_getTpl(vars, "APIFAILBANBIT"));
	}
	if (!apicall)
		return tpl_getTpl(vars, "FAILBAN");
	else
		return tpl_getTpl(vars, "APIFAILBAN");
}

static char *send_oscam_api(struct templatevars *vars, FILE *f, struct uriparams *params, int8_t *keepalive, int8_t apicall) {
	if (strcmp(getParam(params, "part"), "status") == 0) {
		return send_oscam_status(vars, params, apicall);
	}
	else if (strcmp(getParam(params, "part"), "userstats") == 0) {
		return send_oscam_user_config(vars, params, apicall);
	}
	else if (strcmp(getParam(params, "part"), "failban") == 0) {
		return send_oscam_failban(vars, params, apicall);
	}
	else if (strcmp(getParam(params, "part"), "files") == 0) {
		return send_oscam_files(vars, params, apicall);
	}
	else if (strcmp(getParam(params, "part"), "readerlist") == 0) {
		return send_oscam_reader(vars, params, apicall);
	}
	else if (strcmp(getParam(params, "part"), "serverconfig") == 0) {
		//Send Errormessage
		tpl_addVar(vars, TPLADD, "APIERRORMESSAGE", "serverconfig not yet avail");
		return tpl_getTpl(vars, "APIERROR");
	}
	else if (strcmp(getParam(params, "part"), "userconfig") == 0) {
		if(((strcmp(getParam(params, "action"), "Save") == 0) ||
				(strcmp(getParam(params, "action"), "Save As") == 0)) && cfg.http_readonly == 1) {
			//Send Errormessage
			tpl_addVar(vars, TPLADD, "APIERRORMESSAGE", "API is in readonly mode");
			return tpl_getTpl(vars, "APIERROR");
		} else {
			struct s_auth *account = get_account_by_name(getParam(params, "user"));
			if (!account && strcmp(getParam(params, "action"), "Save")) {
				//Send Errormessage
				tpl_addVar(vars, TPLADD, "APIERRORMESSAGE", "user not exist");
				return tpl_getTpl(vars, "APIERROR");
			} else {
				return send_oscam_user_config_edit(vars, params, apicall);
			}
		}
	}
	else if (strcmp(getParam(params, "part"), "entitlement") == 0) {

		if (strcmp(getParam(params, "label"),"")) {
			struct s_reader *rdr = get_reader_by_label(getParam(params, "label"));
			if (rdr) {
				if (rdr->typ == R_CCCAM && rdr->enable == 1) {
					return send_oscam_entitlement(vars, params, apicall);
				} else {
					//Send Errormessage
					tpl_addVar(vars, TPLADD, "APIERRORMESSAGE", "no cccam reader or disabled");
					return tpl_getTpl(vars, "APIERROR");
				}
			} else {
				//Send Errormessage
				tpl_addVar(vars, TPLADD, "APIERRORMESSAGE", "reader not exist");
				return tpl_getTpl(vars, "APIERROR");
			}
		} else {
			//Send Errormessage
			tpl_addVar(vars, TPLADD, "APIERRORMESSAGE", "no reader selected");
			return tpl_getTpl(vars, "APIERROR");
		}
	} else if (strcmp(getParam(params, "part"), "ecmhistory") == 0) {
		int32_t i;
		int32_t isec;
		int32_t shown;
		time_t now = time((time_t*)0);
		char *usr;
		struct s_client *cl;
		for (i=0, cl=first_client; cl ; cl=cl->next, i++) {
			if (cl->wihidden != 1) {
				isec = now - cl->lastecm;
				usr=username(cl);
				shown = 0;
				if (strcmp(getParam(params, "label"),"") == 0) {
					if (strcmp(getParam(params, "type"),"servers") == 0) {
						if (cl->typ == 'p' || cl->typ=='r')
							shown = 1;
					} else if (strcmp(getParam(params, "type"),"users") == 0) {
						if (cl->typ == 'c')
							shown = 1;
					} else {
						shown = 1;
					}
				} else if (strcmp(getParam(params, "label"),usr) == 0) {
					shown = 1;
				}
				if ( shown == 1 ) {
					tpl_printf(vars, TPLADD, "CLIENTTYPE", "%c", cl->typ);
					tpl_addVar(vars, TPLADD, "CLIENTUSER", xml_encode(vars, usr));
					if(cl->typ == 'c') {
						tpl_addVar(vars, TPLADD, "CLIENTDESCRIPTION", xml_encode(vars, (cl->account && cl->account->description)?cl->account->description:""));
					}
					else if(cl->typ == 'p' || cl->typ == 'r') {
						tpl_addVar(vars, TPLADD, "CLIENTDESCRIPTION", xml_encode(vars, cl->reader->description?cl->reader->description:""));
					}
					tpl_printf(vars, TPLADD, "CLIENTLASTRESPONSETIME", "%d", cl->cwlastresptime?cl->cwlastresptime:-1);
					tpl_printf(vars, TPLADD, "CLIENTIDLESECS", "%d", isec);

					//load historical values from ringbuffer
					char *value = get_ecm_fullhistorystring(cl);
					tpl_addVar(vars, TPLADD, "CLIENTLASTRESPONSETIMEHIST", value);
					free_mk_t(value);

					tpl_addVar(vars, TPLAPPEND, "APISTATUSBITS", tpl_getTpl(vars, "APISTATUSBIT"));
				}
			}
		}
		return tpl_getTpl(vars, "APISTATUS");
	} else if (strcmp(getParam(params, "part"), "readerstats") == 0) {
		if (strcmp(getParam(params, "label"),"")) {
			struct s_reader *rdr = get_reader_by_label(getParam(params, "label"));
			if (rdr) {
				return send_oscam_reader_stats(vars, params, apicall);
			} else {
				//Send Errormessage
				tpl_addVar(vars, TPLADD, "APIERRORMESSAGE", "reader not exist");
				return tpl_getTpl(vars, "APIERROR");
			}
		} else {
			//Send Errormessage
			tpl_addVar(vars, TPLADD, "APIERRORMESSAGE", "no reader selected");
			return tpl_getTpl(vars, "APIERROR");
		}
	} else if (strcmp(getParam(params, "part"), "shutdown") == 0) {
		if ((strcmp(strtolower(getParam(params, "action")), "restart") == 0) ||
				(strcmp(strtolower(getParam(params, "action")), "shutdown") == 0)){
			if(!cfg.http_readonly) {
				return send_oscam_shutdown(vars, f, params, apicall, keepalive);
			} else {
				tpl_addVar(vars, TPLADD, "APIERRORMESSAGE", "webif readonly mode");
				return tpl_getTpl(vars, "APIERROR");
			}
		} else {
			tpl_addVar(vars, TPLADD, "APIERRORMESSAGE", "missing parameter action");
			return tpl_getTpl(vars, "APIERROR");
		}

	}
	else {
		tpl_addVar(vars, TPLADD, "APIERRORMESSAGE", "part not found");
		return tpl_getTpl(vars, "APIERROR");
	}
}

static char *send_oscam_image(struct templatevars *vars, FILE *f, struct uriparams *params, char *image, time_t modifiedheader, uint32_t etagheader) {
	char *wanted;
	if(image == NULL) wanted = getParam(params, "i");
	else wanted = image;
	if(strlen(wanted) > 3 && wanted[0] == 'I' && wanted[1] == 'C'){
		if(etagheader == 0){
			int8_t disktpl = 0;
			if (cfg.http_tpl) {
		  	char path[255];
		  	if(strlen(tpl_getTplPath(wanted, cfg.http_tpl, path, 255)) > 0 && file_exists(path)){
		  		struct stat st;
		  		disktpl = 1;
					stat(path, &st);
					if(st.st_mtime < modifiedheader){
						send_header304(f);
						return "1";
					}
		  	}
	  	}
	  	if(disktpl == 0 && first_client->login < modifiedheader){
				send_header304(f);
				return "1";
			}
		}
		char *header = strstr(tpl_getTpl(vars, wanted), "data:");
		if(header != NULL){
			char *ptr = header + 5;
			while (ptr[0] != ';' && ptr[0] != '\0') ++ptr;
			if(ptr[0] != '\0' && ptr[1] != '\0') ptr[0] = '\0';
			else return "0";
			ptr = strstr(ptr + 1, "base64,");
			if(ptr != NULL){
				int32_t len = b64decode((uchar *)ptr + 7);
				if(len > 0){
					if((uint32_t)crc32(0L, (uchar *)ptr + 7, len) == etagheader){
						send_header304(f);
					} else {
						send_headers(f, 200, "OK", NULL, header + 5, 1, len, ptr + 7, 0);
						webif_write_raw(ptr + 7, f, len);
					}
					return "1";
				}
			}
		}
	}
	return "0";
}

static char *send_oscam_graph(struct templatevars *vars) {
	return tpl_getTpl(vars, "GRAPH");
}

#ifdef CS_CACHEEX
static uint64_t get_cacheex_node(struct s_client *cl) {
	uint64_t node = 0x00;
	struct s_module *p;
	if (cl->reader) p = &cl->reader->ph;
	else p = &ph[cl->ctyp];
#ifdef MODULE_CCCAM
	if (p->num == R_CCCAM && cl->cc) {
		struct cc_data *cc = cl->cc;
		memcpy(&node, cc->peer_node_id, 8);
	}
	else
#endif
#ifdef MODULE_CAMD35
	if (p->num == R_CAMD35) {
		memcpy(&node, cl->ncd_skey, 8);
	}
	else
#endif
#ifdef MODULE_CAMD35_TCP
	if (p->num == R_CS378X) {
		memcpy(&node, cl->ncd_skey, 8);
	} else
#endif
	{}
	return node;
}	


static char *send_oscam_cacheex(struct templatevars *vars, struct uriparams *params, int8_t apicall) {

	if(!apicall) setActiveMenu(vars, MNU_CACHEEX);

	if (strcmp(getParam(params, "x"), "x") == 0) {
		// avoid compilerwarning unused vars
	}

	char *level[]= {"NONE","CACHE PULL","CACHE PUSH","REVERSE CACHE PUSH"};
	char *getting = "<IMG SRC=\"image?i=ICARRL\" ALT=\"Getting\">";
	char *pushing = "<IMG SRC=\"image?i=ICARRR\" ALT=\"Pushing\">";
	char *rowvariable = "";

	int16_t i, write = 0;
	struct s_client *cl;
	time_t now = time((time_t*)0);

	tpl_printf(vars, TPLADD, "OWN_CACHEEX_NODEID", "%" PRIu64 "X", cnode(cacheex_peer_id));
	
	for (i = 0, cl = first_client; cl ; cl = cl->next, i++) {
		if (cl->typ=='c' && cl->account && cl->account->cacheex){
			tpl_addVar(vars, TPLADD, "TYPE", "Client");
			tpl_addVar(vars, TPLADD, "NAME", cl->account->usr);
			tpl_addVar(vars, TPLADD, "IP", cs_inet_ntoa(cl->ip));
			tpl_printf(vars, TPLADD, "NODE", "%" PRIu64 "X", get_cacheex_node(cl));
			tpl_addVar(vars, TPLADD, "LEVEL", level[cl->account->cacheex]);
			tpl_printf(vars, TPLADD, "PUSH", "%d", cl->account->cwcacheexpush);
			tpl_printf(vars, TPLADD, "GOT", "%d", cl->account->cwcacheexgot);
			tpl_printf(vars, TPLADD, "HIT", "%d", cl->account->cwcacheexhit);
			tpl_printf(vars, TPLADD, "ERR", "%d", cl->account->cwcacheexerr);
			tpl_printf(vars, TPLADD, "ERRCW", "%d", cl->account->cwcacheexerrcw);
			tpl_addVar(vars, TPLADD, "DIRECTIONIMG", (cl->account->cacheex == 3) ? getting : pushing);
			rowvariable = "TABLECLIENTROWS";
			write = 1;
		}
		else if ((cl->typ=='p' || cl->typ=='r') && (cl->reader && cl->reader->cacheex)) {
			tpl_addVar(vars, TPLADD, "TYPE", "Reader");
			tpl_addVar(vars, TPLADD, "NAME", cl->reader->label);
			tpl_addVar(vars, TPLADD, "IP", cs_inet_ntoa(cl->ip));
			tpl_printf(vars, TPLADD, "NODE", "%" PRIu64 "X", get_cacheex_node(cl));
			tpl_addVar(vars, TPLADD, "LEVEL", level[cl->reader->cacheex]);
			tpl_printf(vars, TPLADD, "PUSH", "%d", cl->cwcacheexpush);
			tpl_printf(vars, TPLADD, "GOT", "%d", cl->cwcacheexgot);
			tpl_printf(vars, TPLADD, "HIT", "%d", cl->cwcacheexhit);
			tpl_printf(vars, TPLADD, "ERR", "%d", cl->cwcacheexerr);
			tpl_printf(vars, TPLADD, "ERRCW", "%d", cl->cwcacheexerrcw);
			tpl_addVar(vars, TPLADD, "DIRECTIONIMG", (cl->reader->cacheex == 3) ? pushing : getting);
			rowvariable = "TABLEREADERROWS";
			write = 1;
		}
		else if (ph[cl->ctyp].listenertype == LIS_CSPUDP) {
			tpl_addVar(vars, TPLADD, "TYPE", "csp");
			tpl_addVar(vars, TPLADD, "NAME", "csp");
			tpl_addVar(vars, TPLADD, "IP", cs_inet_ntoa(cl->ip));
			tpl_addVar(vars, TPLADD, "NODE", "csp");
			tpl_addVar(vars, TPLADD, "LEVEL", "csp");
			tpl_printf(vars, TPLADD, "PUSH", "%d", cl->cwcacheexpush);
			tpl_printf(vars, TPLADD, "GOT", "%d", cl->cwcacheexgot);
			tpl_printf(vars, TPLADD, "HIT", "%d", cl->cwcacheexhit);
			tpl_addVar(vars, TPLADD, "DIRECTIONIMG", getting);
			rowvariable = "TABLECLIENTROWS";
			write = 1;
		}

		if (write) {
			tpl_addVar(vars, TPLAPPEND, rowvariable, tpl_getTpl(vars, "CACHEEXTABLEROW"));

			if (cl->ll_cacheex_stats) {
				LL_ITER itr = ll_iter_create(cl->ll_cacheex_stats);
				S_CACHEEX_STAT_ENTRY *cacheex_stats_entry;

				while ((cacheex_stats_entry = ll_iter_next(&itr))) {

					tpl_addVar(vars, TPLADD, "DIRECTIONIMG","");
					if (now - cacheex_stats_entry->cache_last < 20)
						tpl_addVar(vars, TPLADD, "TYPE", cacheex_stats_entry->cache_direction == 0 ? pushing : getting);
					else
						tpl_addVar(vars, TPLADD, "TYPE","");
					tpl_printf(vars, TPLADD, "NAME", "%04X:%06X:%04X", cacheex_stats_entry->cache_caid,
							cacheex_stats_entry->cache_prid,
							cacheex_stats_entry->cache_srvid);
					if(cacheex_stats_entry->cache_direction == 0){
						tpl_printf(vars, TPLADD, "PUSH", "%d", cacheex_stats_entry->cache_count);
						tpl_addVar(vars, TPLADD, "GOT","");
					} else {
						tpl_printf(vars, TPLADD, "GOT", "%d", cacheex_stats_entry->cache_count);
						tpl_addVar(vars, TPLADD, "PUSH","");
					}
					tpl_addVar(vars, TPLADD, "HIT","");
					char channame[32];
					char *lastchan = xml_encode(vars, get_servicename(cl, cacheex_stats_entry->cache_srvid, cacheex_stats_entry->cache_caid, channame));
					tpl_addVar(vars, TPLADD, "LEVEL", lastchan);
					tpl_addVar(vars, TPLAPPEND, rowvariable, tpl_getTpl(vars, "CACHEEXTABLEROW"));

				}
			}
			write = 0;
		}
	}

	float cachesum = first_client->cwcacheexgot;
	if (cachesum < 1) {
		cachesum = 1;
	}
	tpl_printf(vars, TPLADD, "TOTAL_CACHEXPUSH", "%d", first_client->cwcacheexpush);
	tpl_addVar(vars, TPLADD, "TOTAL_CACHEXPUSH_IMG", pushing);
	tpl_printf(vars, TPLADD, "TOTAL_CACHEXGOT", "%d", first_client->cwcacheexgot);
	tpl_addVar(vars, TPLADD, "TOTAL_CACHEXGOT_IMG", getting);
	tpl_printf(vars, TPLADD, "TOTAL_CACHEXHIT", "%d", first_client->cwcacheexhit);
	tpl_printf(vars, TPLADD, "TOTAL_CACHESIZE", "%d", ecmcwcache_size);

	tpl_printf(vars, TPLADD, "REL_CACHEXHIT", "%.2f", first_client->cwcacheexhit * 100 / cachesum);

	return tpl_getTpl(vars, "CACHEEXPAGE");
}
#endif

static int8_t check_httpip(IN_ADDR_T addr) {
	int8_t i = 0;
	// check all previously dyndns resolved addresses
	for(i = 0; i < MAX_HTTP_DYNDNS; i++) {
		if(IP_ISSET(cfg.http_dynip[i]) && IP_EQUAL(cfg.http_dynip[i], addr))
			return 1;
	}
	return 0;
}

static int8_t check_httpdyndns(IN_ADDR_T addr) {

	// check all previously dyndns resolved addresses
	if(check_httpip(addr))
		return 1;

	// we are not ok, so resolve all dyndns entries into IP's - maybe outdated IP's

	if(cfg.http_dyndns[0][0]) {
		int8_t i = 0;
		for(i = 0; i < MAX_HTTP_DYNDNS; i++) {
			if(cfg.http_dyndns[i][0]){
#ifdef IPV6SUPPORT
				cs_getIPv6fromHost((char*)cfg.http_dyndns[i], &cfg.http_dynip[i], NULL);
#else
				cfg.http_dynip[i] = cs_getIPfromHost((char*)cfg.http_dyndns[i]);
#endif
				cs_debug_mask(D_TRACE, "WebIf: httpdyndns [%d] resolved %s to %s ", i, (char*)cfg.http_dyndns[i], cs_inet_ntoa(cfg.http_dynip[i]));
			}
		}
	} else {
		cs_debug_mask(D_TRACE, "WebIf: No dyndns addresses found");
		return 0;
	}

	// again check all dyndns resolved addresses
	if(check_httpip(addr))
		return 1;

	return 0;
}

static int8_t check_valid_origin(IN_ADDR_T addr) {

	// check whether requesting IP is in allowed IP ranges
	if(check_ip(cfg.http_allowed, addr))
		return 1;

	// we havn't found the requesting IP in allowed range. So we check for allowed httpdyndns as last chance
	if (cfg.http_dyndns[0][0]) {
		int8_t ok;
		ok = check_httpdyndns(addr);
		return ok;
	}
	return 0;
}

static int8_t check_request(char *result, int32_t read) {
	if(read < 50) return 0;
	result[read]='\0';
	int8_t method;
	if (strncmp(result, "POST", 4) == 0) method = 1;
	else method = 0;
	char *headerEnd = strstr(result, "\r\n\r\n");
	if(headerEnd == NULL) return 0;
	else if(method == 0) return 1;
	else {
		char *ptr = strstr(result, "Content-Length: ");
		if(ptr != NULL){
			ptr += 16;
			if(ptr < result + read){
				uint32_t length = atoi(ptr);
				if(strlen(headerEnd+4) >= length) return 1;
			}
		}
	}
	return 0;
}

#ifdef IPV6SUPPORT
static int32_t readRequest(FILE *f, struct in6_addr in, char **result, int8_t forcePlain)
#else
static int32_t readRequest(FILE *f, struct in_addr in, char **result, int8_t forcePlain)
#endif
{
	int32_t n, bufsize=0, errcount = 0;
	char buf2[1024];
	struct pollfd pfd2[1];
#ifdef WITH_SSL
	int8_t is_ssl = 0;
	if (ssl_active && !forcePlain)
		is_ssl = 1;
#endif

	while (1) {
		errno = 0;
		if(forcePlain)
			n=read(fileno(f), buf2, sizeof(buf2));
		else
			n=webif_read(buf2, sizeof(buf2), f);
		if (n <= 0) {
			if ((errno == 0 || errno == EINTR)){
				if(errcount++ < 10){
					cs_sleepms(5);
					continue;
				} else return -1;
			}
#ifdef WITH_SSL
			if (is_ssl){
				if(errno != ECONNRESET) {
					int32_t errcode = ERR_peek_error();
					char errstring[128];
					ERR_error_string_n(errcode, errstring, sizeof(errstring) - 1);
					cs_debug_mask(D_TRACE, "WebIf: read error ret=%d (%d%s%s)", n, SSL_get_error(cur_ssl(), n), errcode?" ":"", errcode?errstring:"");
				}
				return -1;
			}
#else
			if(errno != ECONNRESET)
				cs_debug_mask(D_TRACE, "WebIf: read error ret=%d (errno=%d %s)", n, errno, strerror(errno));
#endif
			return -1;
		}
		if(!cs_realloc(result, bufsize+n+1, -1)){
			send_error500(f);
			return -1;
		}

		memcpy(*result+bufsize, buf2, n);
		bufsize+=n;

		//max request size 100kb
		if (bufsize>102400) {
#ifdef IPV6SUPPORT
			cs_log("error: too much data received from %s", cs_inet_ntoa(in));
#else
			cs_log("error: too much data received from %s", inet_ntoa(in));
#endif
			free(*result);
			return -1;
		}

#ifdef WITH_SSL
		if (ssl_active && !forcePlain) {
			int32_t len = 0;
			len = SSL_pending((SSL*)f);

			if (len>0)
				continue;

			pfd2[0].fd = SSL_get_fd((SSL*)f);

		} else
#endif
			pfd2[0].fd = fileno(f);

		pfd2[0].events = (POLLIN | POLLPRI);

		int32_t rc = poll(pfd2, 1, 100);
		if (rc>0 || !check_request(*result, bufsize))
			continue;
		else
			break;
	}
	return bufsize;
}
#ifdef IPV6SUPPORT
static int32_t process_request(FILE *f, struct in6_addr in) {
#else
static int32_t process_request(FILE *f, struct in_addr in) {
#endif
	int32_t ok=0,v=cv();
	int8_t *keepalive = (int8_t *)pthread_getspecific(getkeepalive);
#ifdef IPV6SUPPORT
	struct in6_addr addr = GET_IP();
#else
	in_addr_t addr = GET_IP();
#endif

	do {
#ifdef WITH_SSL
		if (!ssl_active && *keepalive) fflush(f);
#else
		if (*keepalive) fflush(f);
#endif

		// at this point we do all checks related origin IP, ranges and dyndns stuff
		ok = check_valid_origin(addr) ? v : 0;
		cs_debug_mask(D_TRACE, "WebIf: Origin checked. Result: access from %s => %s", cs_inet_ntoa(addr), (!ok)? "forbidden" : "allowed");

		// based on the failed origin checks we send a 403 to calling browser
		if (!ok) {
			send_error(f, 403, "Forbidden", NULL, "Access denied.", 0);
			cs_log("unauthorized access from %s flag %d", cs_inet_ntoa(addr), v);
			return 0;
		}

		int32_t authok = 0;
		char expectednonce[(MD5_DIGEST_LENGTH * 2) + 1];

		char *method, *path, *protocol, *str1, *saveptr1=NULL, *authheader = NULL, *filebuf = NULL;
		char *pch, *tmp, *buf, *nameInUrl, subdir[32];
		/* List of possible pages */
		char *pages[]= {
			"/config.html",
			"/readers.html",
			"/entitlements.html",
			"/status.html",
			"/userconfig.html",
			"/readerconfig.html",
			"/services.html",
			"/user_edit.html",
			"/site.css",
			"/services_edit.html",
			"/savetemplates.html",
			"/shutdown.html",
			"/script.html",
			"/scanusb.html",
			"/files.html",
			"/readerstats.html",
			"/failban.html",
			"/oscam.js",
			"/oscamapi.html",
			"/image",
			"/favicon.ico",
			"/graph.svg",
			"/oscamapi.xml",
			"/cacheex.html",
			"/oscamapi.json"	};

		int32_t pagescnt = sizeof(pages)/sizeof(char *); // Calculate the amount of items in array
		int32_t i, bufsize, len, pgidx = -1;
		uint32_t etagheader = 0;
		struct uriparams params;
		params.paramcount = 0;
		time_t modifiedheader = 0;

		bufsize = readRequest(f, in, &filebuf, 0);

		if (!filebuf || bufsize < 1) {
			if(!*keepalive) cs_debug_mask(D_CLIENT, "WebIf: No data received from client %s. Closing connection.", cs_inet_ntoa(addr));
			return -1;
		}

		buf=filebuf;

		if((method = strtok_r(buf, " ", &saveptr1)) != NULL){
			if((path = strtok_r(NULL, " ", &saveptr1)) != NULL){
				if((protocol = strtok_r(NULL, "\r", &saveptr1)) == NULL){
					free(filebuf);
					return -1;
				}
			} else {
				free(filebuf);
				return -1;
			}
		} else {
			free(filebuf);
			return -1;
		}
		tmp=protocol+strlen(protocol)+2;

		pch=path;
		/* advance pointer to beginning of query string */
		while(pch[0] != '?' && pch[0] != '\0') ++pch;
		if(pch[0] == '?') {
			pch[0] = '\0';
			++pch;
		}

		nameInUrl = pch-1;
		while(nameInUrl != path && nameInUrl[0] != '/') --nameInUrl;

		/* allow only alphanumeric sub-folders */
		int32_t subdirLen = nameInUrl-path;
		subdir[0] = '\0';
		if (subdirLen > 0 && subdirLen < 32) {
			cs_strncpy(subdir, path+1, subdirLen);

			int32_t invalidSubdir = 0;
			for (i=0; i < subdirLen-1; i++) {
				if (!( (subdir[i] >= '0' && subdir[i] <= '9')
					|| (subdir[i] >= 'a' && subdir[i] <= 'z')
					|| (subdir[i] >= 'A' && subdir[i] <= 'Z'))) {

					invalidSubdir = 1;
					subdir[0] = '\0';
					break;
				}
			}

			if (!invalidSubdir) {
				subdir[subdirLen] = '\0';
				#ifdef WIN32
				subdir[subdirLen-1] = '\\';
				#else
				subdir[subdirLen-1] = '/';
				#endif
			}
		}

		/* Map page to our static page definitions */
		for (i=0; i<pagescnt; i++) {
			if (!strcmp(nameInUrl, pages[i])) pgidx = i;
		}

		parseParams(&params, pch);

		if (!cfg.http_user || !cfg.http_pwd)
			authok = 1;
		else
			calculate_nonce(expectednonce);

		for (str1=strtok_r(tmp, "\n", &saveptr1); str1; str1=strtok_r(NULL, "\n", &saveptr1)) {
			len = strlen(str1);
			if(str1[len - 1] == '\r'){
				str1[len - 1] = '\0';
				--len;
			}
			if (len==0) {
				if (strcmp(method, "POST")==0) {
					parseParams(&params, str1+2);
				}
				break;
			}
			if(authok == 0 && len > 50 && cs_strnicmp(str1, "Authorization:", 14) == 0 && strstr(str1, "Digest") != NULL) {
				if (cs_dblevel & D_CLIENT){
					if(cs_realloc(&authheader, len + 1, -1))
						cs_strncpy(authheader, str1, len);
				}
				authok = check_auth(str1, method, path, expectednonce);
			} else if (len > 40 && cs_strnicmp(str1, "If-Modified-Since:", 18) == 0){
				modifiedheader = parse_modifiedsince(str1);
			} else if (len > 20 && cs_strnicmp(str1, "If-None-Match:", 14) == 0){
				for(pch = str1 + 14; pch[0] != '"' && pch[0] != '\0'; ++pch);
				if(strlen(pch) > 5) etagheader = (uint32_t)strtoul(++pch, NULL, 10);
			} else if (len > 12 && cs_strnicmp(str1, "Connection: Keep-Alive", 22) == 0 && strcmp(method, "POST")){
				*keepalive = 1;
			}
		}

		if(authok != 1) {
			if(authok == 2)
				cs_debug_mask(D_TRACE, "WebIf: Received stale header from %s.", cs_inet_ntoa(addr));
			else if(authheader){
				cs_debug_mask(D_CLIENT, "WebIf: Received wrong auth header from %s:", cs_inet_ntoa(addr));
				cs_debug_mask(D_CLIENT, "%s", authheader);
			} else
				cs_debug_mask(D_CLIENT, "WebIf: Received no auth header from %s.", cs_inet_ntoa(addr));
			char temp[sizeof(AUTHREALM) + sizeof(expectednonce) + 100];
			snprintf(temp, sizeof(temp), "WWW-Authenticate: Digest algorithm=\"MD5\", realm=\"%s\", qop=\"auth\", opaque=\"\", nonce=\"%s\"", AUTHREALM, expectednonce);
			if(authok == 2) strncat(temp, ", stale=true", sizeof(temp));
			char *msg = "Access denied.\n";
			send_headers(f, 401, "Unauthorized", temp, "text/html", 0, strlen(msg), msg, 0);
			webif_write(msg, f);
			NULLFREE(authheader);
			free(filebuf);
			if(*keepalive) continue;
			else return 0;
		} else NULLFREE(authheader);

		/*build page*/
		if(pgidx == 8) {
			send_file(f, "CSS", subdir, modifiedheader, etagheader);
		} else if (pgidx == 17) {
			send_file(f, "JS", subdir, modifiedheader, etagheader);
		} else {
			time_t t;
			struct templatevars *vars = tpl_create();
			if(vars == NULL){
				send_error500(f);
				free(filebuf);
				return 0;
			}

			tpl_addVar(vars, TPLADD, "SUBDIR", subdir);

			struct tm lt, st;
			time(&t);

			localtime_r(&t, &lt);

			tpl_addVar(vars, TPLADD, "CS_VERSION", CS_VERSION);
			tpl_addVar(vars, TPLADD, "CS_SVN_VERSION", CS_SVN_VERSION);
			tpl_addVar(vars, TPLADD, "HTTP_CHARSET", cs_http_use_utf8?"UTF-8":"ISO-8859-1");
			if(cfg.http_refresh > 0 && (pgidx == 3 || pgidx == -1)) {
				tpl_printf(vars, TPLADD, "REFRESHTIME", "%d", cfg.http_refresh);
				tpl_addVar(vars, TPLADD, "REFRESHURL", "status.html");
				tpl_addVar(vars, TPLADD, "REFRESH", tpl_getTpl(vars, "REFRESH"));
			}

			tpl_printf(vars, TPLADD, "CURDATE", "%02d.%02d.%02d", lt.tm_mday, lt.tm_mon+1, lt.tm_year%100);
			tpl_printf(vars, TPLADD, "CURTIME", "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
			localtime_r(&first_client->login, &st);
			tpl_printf(vars, TPLADD, "STARTDATE", "%02d.%02d.%02d", st.tm_mday, st.tm_mon+1, st.tm_year%100);
			tpl_printf(vars, TPLADD, "STARTTIME", "%02d:%02d:%02d", st.tm_hour, st.tm_min, st.tm_sec);
			tpl_printf(vars, TPLADD, "PROCESSID", "%d", server_pid);

			time_t now = time((time_t*)0);
			// XMLAPI
			if (pgidx == 18 || pgidx == 22 || pgidx == 24) {
				char tbuffer [30];
				strftime(tbuffer, 30, "%Y-%m-%dT%H:%M:%S%z", &st);
				tpl_addVar(vars, TPLADD, "APISTARTTIME", tbuffer);
				tpl_printf(vars, TPLADD, "APIUPTIME", "%ld", now - first_client->login);
				tpl_printf(vars, TPLADD, "APIREADONLY", "%d", cfg.http_readonly);
				if (strcmp(getParam(&params, "callback"),"")) {
					tpl_addVar(vars, TPLADD, "CALLBACK", getParam(&params, "callback"));
				}

			}

			// language code in helplink
			tpl_addVar(vars, TPLADD, "LANGUAGE", cfg.http_help_lang);
			tpl_addVar(vars, TPLADD, "UPTIME", sec2timeformat(vars, (now - first_client->login)));
			tpl_addVar(vars, TPLADD, "CURIP", cs_inet_ntoa(addr));
			if(cfg.http_readonly)
				tpl_addVar(vars, TPLAPPEND, "BTNDISABLED", "DISABLED");

			i = ll_count(cfg.v_list);
			if(i > 0)tpl_printf(vars, TPLADD, "FAILBANNOTIFIER", "<SPAN CLASS=\"span_notifier\">%d</SPAN>", i);

			char *result = NULL;

			// WebIf allows modifying many things. Thus, all pages except images/css are excpected to be non-threadsafe!
			if(pgidx != 19 && pgidx != 20) cs_writelock(&http_lock);
			switch(pgidx) {
				case 0: result = send_oscam_config(vars, &params); break;
				case 1: result = send_oscam_reader(vars, &params, 0); break;
				case 2: result = send_oscam_entitlement(vars, &params, 0); break;
				case 3: result = send_oscam_status(vars, &params, 0); break;
				case 4: result = send_oscam_user_config(vars, &params, 0); break;
				case 5: result = send_oscam_reader_config(vars, &params); break;
				case 6: result = send_oscam_services(vars, &params); break;
				case 7: result = send_oscam_user_config_edit(vars, &params, 0); break;
				//case  8: css file
				case 9: result = send_oscam_services_edit(vars, &params); break;
				case 10: result = send_oscam_savetpls(vars); break;
				case 11: result = send_oscam_shutdown(vars, f, &params, 0, keepalive); break;
				case 12: result = send_oscam_script(vars); break;
				case 13: result = send_oscam_scanusb(vars); break;
				case 14: result = send_oscam_files(vars, &params, 0); break;
				case 15: result = send_oscam_reader_stats(vars, &params, 0); break;
				case 16: result = send_oscam_failban(vars, &params, 0); break;
				//case  17: js file
				case 18: result = send_oscam_api(vars, f, &params, keepalive, 1); break; //oscamapi.html
				case 19: result = send_oscam_image(vars, f, &params, NULL, modifiedheader, etagheader); break;
				case 20: result = send_oscam_image(vars, f, &params, "ICMAI", modifiedheader, etagheader); break;
				case 21: result = send_oscam_graph(vars); break;
				case 22: result = send_oscam_api(vars, f, &params, keepalive, 1); break; //oscamapi.xml
#ifdef CS_CACHEEX
				case 23: result = send_oscam_cacheex(vars, &params, 0); break;
#endif
				case 24: result = send_oscam_api(vars, f, &params, keepalive, 2); break; //oscamapi.json
				default: result = send_oscam_status(vars, &params, 0); break;
			}
			if(pgidx != 19 && pgidx != 20) cs_writeunlock(&http_lock);

			if(result == NULL || !strcmp(result, "0") || strlen(result) == 0) send_error500(f);
			else if (strcmp(result, "1")) {
				//it doesn't make sense to check for modified etagheader here as standard template has timestamp in output and so site changes on every request
				if (pgidx == 18)
					send_headers(f, 200, "OK", NULL, "text/xml", 0, strlen(result), NULL, 0);
				else if (pgidx == 21)
					send_headers(f, 200, "OK", NULL, "image/svg+xml", 0, strlen(result), NULL, 0);
				else if (pgidx == 24)
					send_headers(f, 200, "OK", NULL, "text/javascript", 0, strlen(result), NULL, 0);
				else
					send_headers(f, 200, "OK", NULL, "text/html", 0, strlen(result), NULL, 0);
				webif_write(result, f);
			}
			tpl_clear(vars);
		}
		free(filebuf);
	} while (*keepalive == 1);
	return 0;
}

static void *serve_process(void *conn){
	struct s_connection *myconn = (struct s_connection*)conn;
	int32_t s = myconn->socket;
	struct s_client *cl = myconn->cl;
#ifdef IPV6SUPPORT
	struct in6_addr in = myconn->remote;
#else
	struct in_addr in = myconn->remote;
#endif

#ifdef WITH_SSL
	SSL *ssl = myconn->ssl;
	pthread_setspecific(getssl, ssl);
#endif
	free(myconn);
#ifdef IPV6SUPPORT
	pthread_setspecific(getip, &in.s6_addr);
#else
	pthread_setspecific(getip, &in.s_addr);
#endif
	pthread_setspecific(getclient, cl);

	int8_t keepalive = 0;
	pthread_setspecific(getkeepalive, &keepalive);

#ifdef WITH_SSL
	if (ssl_active) {
		if(SSL_set_fd(ssl, s)){
			int32_t ok = (SSL_accept(ssl) != -1);
			if (!ok) {
				int8_t tries = 100;
				while (!ok && tries--) {
					int32_t err = SSL_get_error(ssl, -1);
					if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
						break;
					else {
						struct pollfd pfd;
						pfd.fd = s;
						pfd.events = POLLIN | POLLPRI;
						int32_t rc = poll(&pfd, 1, -1);
						if (rc < 0) {
							if (errno==EINTR || errno==EAGAIN) continue;
							break;
						}
						if (rc == 1)
							ok = (SSL_accept(ssl) != -1);
					}
				}
			}
			if (ok){
				process_request((FILE *)ssl, in);
			} else {
				FILE *f;
				f = fdopen(s, "r+");
				if(f != NULL) {
					char *ptr, *filebuf = NULL, *host = NULL;
					int32_t bufsize = readRequest(f, in, &filebuf, 1);

					if (filebuf) {
						filebuf[bufsize]='\0';
						host = strstr(filebuf, "Host: ");
						if(host){
							host += 6;
							ptr = strchr(host, '\r');
							if(ptr) ptr[0] = '\0';
						}
					}
					if(host){
						char extra[strlen(host) + 20];
						snprintf(extra, sizeof(extra), "Location: https://%s", host);
						send_error(f, 301, "Moved Permanently", extra, "This web server is running in SSL mode.", 1);
					} else
						send_error(f, 200, "Bad Request", NULL, "This web server is running in SSL mode.", 1);
					fflush(f);
					fclose(f);
				} else cs_log("WebIf: Error opening file descriptor using fdopen() (errno=%d %s)", errno, strerror(errno));
			}
		} else cs_log("WebIf: Error calling SSL_set_fd().");
		SSL_shutdown(ssl);
		close(s);
		SSL_free(ssl);
	} else
#endif
	{
		FILE *f;
		f = fdopen(s, "r+");
		if(f != NULL) {
			process_request(f, in);
			fflush(f);
			fclose(f);
		} else cs_log("WebIf: Error opening file descriptor using fdopen() (errno=%d %s)", errno, strerror(errno));
		shutdown(s, SHUT_WR);
		close(s);
	}

	return NULL;
}

void http_srv(void) {
	pthread_t workthread;
	pthread_attr_t attr;
	struct s_client * cl = create_client(first_client->ip);
	if (cl == NULL) return;
	httpthread = cl->thread = pthread_self();
	pthread_setspecific(getclient, cl);
	cl->typ = 'h';
	int32_t sock, s, reuse = 1;
	struct s_connection *conn;

	/* Create random string for nonce value generation */
	create_rand_str(noncekey,32);

	/* Prepare base64 decoding array */
	b64prepare();
	prepareTplChecksums();

	tpl_checkDiskRevisions();

	cs_lock_create(&http_lock, 10, "http_lock");

	if (pthread_key_create(&getip, NULL)) {
		cs_log("Could not create getip");
		return;
	}
	if (pthread_key_create(&getkeepalive, NULL)) {
		cs_log("Could not create getkeepalive");
		return;
	}

#ifdef IPV6SUPPORT
	static uint8_t ipv4fallback = 0;
	struct sockaddr sin;
	struct sockaddr_in6 *ia;
	struct sockaddr remote;
	struct sockaddr_in6 *ra;

	socklen_t len = sizeof(remote);

	ia = (struct sockaddr_in6 *)&sin;
	ra = (struct sockaddr_in6 *)&remote;

	/* Startup server */
	if((sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		cs_log("HTTP Server: Creating IPv6 socket failed! (errno=%d %s)", errno, strerror(errno));
		cs_log("HTTP Server: Trying fallback to IPv4.");
		if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
			cs_log("HTTP Server: Creating socket failed! (errno=%d %s)", errno, strerror(errno));
			return;
		}
		ipv4fallback = 1;
	}
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		cs_log("HTTP Server: Setting SO_REUSEADDR via setsockopt failed! (errno=%d %s)", errno, strerror(errno));
	}

	memset(&sin, 0, sizeof sin);

	ia->sin6_family = AF_INET6;
	ia->sin6_addr = in6addr_any;
	ia->sin6_port = htons(cfg.http_port);

	if((bind(sock, &sin, sizeof(struct sockaddr_in6))) < 0) {
		cs_log("HTTP Server couldn't bind on port %d (errno=%d %s). Not starting HTTP!", cfg.http_port, errno, strerror(errno));
		close(sock);
		return;
	}
#else
	struct sockaddr_in sin;
	struct sockaddr_in remote;


	socklen_t len = sizeof(remote);

	/* Startup server */
	if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		cs_log("HTTP Server: Creating socket failed! (errno=%d %s)", errno, strerror(errno));
		return;
	}
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		cs_log("HTTP Server: Setting SO_REUSEADDR via setsockopt failed! (errno=%d %s)", errno, strerror(errno));
	}

	memset(&sin, 0, sizeof sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(cfg.http_port);
	if((bind(sock, (struct sockaddr *) &sin, sizeof(sin))) < 0) {
		cs_log("HTTP Server couldn't bind on port %d (errno=%d %s). Not starting HTTP!", cfg.http_port, errno, strerror(errno));
		close(sock);
		return;
	}
#endif
	if (listen(sock, SOMAXCONN) < 0) {
		cs_log("HTTP Server: Call to listen() failed! (errno=%d %s)", errno, strerror(errno));
		close(sock);
		return;
	}

#ifdef WITH_SSL
	SSL_CTX *ctx = NULL;
	if (cfg.http_use_ssl){
		ctx = SSL_Webif_Init();
		if (ctx==NULL)
			cs_log("SSL could not be initialized. Starting WebIf in plain mode.");
		else ssl_active = 1;
	} else ssl_active = 0;
	cs_log("HTTP Server listening on port %d%s", cfg.http_port, ssl_active ? " (SSL)" : "");
#else
	cs_log("HTTP Server listening on port %d", cfg.http_port);
#endif

	memset(&remote, 0, sizeof(remote));

	while (running) {
		if((s = accept(sock, (struct sockaddr *) &remote, &len)) < 0) {
			if(errno != EAGAIN && errno != EINTR){
				cs_log("HTTP Server: Error calling accept() (errno=%d %s)", errno, strerror(errno));
				cs_sleepms(100);
			} else cs_sleepms(5);
			continue;
		} else {
			getpeername(s, (struct sockaddr *) &remote, &len);
			if(!cs_malloc(&conn, sizeof(struct s_connection), -1)){
				close(s);
				continue;
			}
			setTCPTimeouts(s);
			cur_client()->last = time((time_t*)0); //reset last busy time
			conn->cl = cur_client();
#ifdef IPV6SUPPORT
			if (ipv4fallback)
			{
				struct sockaddr_in *fba = (struct sockaddr_in *)&remote;
				struct in6_addr taddr;
				memset(&taddr, 0, sizeof(taddr));
				taddr.s6_addr32[3] = fba->sin_addr.s_addr;
				memcpy(&conn->remote, &taddr, sizeof(struct in6_addr));
			}
			else
			{
				memcpy(&conn->remote, &ra->sin6_addr, sizeof(struct in6_addr));
			}
#else
			memcpy(&conn->remote, &remote.sin_addr, sizeof(struct in_addr));
#endif
			conn->socket = s;
#ifdef WITH_SSL
			conn->ssl = NULL;
			if (ssl_active){
				conn->ssl = SSL_new(ctx);
				if(conn->ssl == NULL){
					close(s);
					cs_log("WebIf: Error calling SSL_new().");
					continue;
				}
			}
#endif
			pthread_attr_init(&attr);
			pthread_attr_setstacksize(&attr, PTHREAD_STACK_SIZE);
			int32_t ret = pthread_create(&workthread, &attr, serve_process, (void *)conn);
			if (ret) {
				cs_log("ERROR: can't create thread for webif (errno=%d %s)", ret, strerror(ret));
				free(conn);
			}
			else
				pthread_detach(workthread);
			pthread_attr_destroy(&attr);
		}
	}
	// Wait a bit so that we don't close ressources while http threads are active
	cs_sleepms(300);
#ifdef WITH_SSL
	if (ssl_active){
		SSL_CTX_free(ctx);
		CRYPTO_set_dynlock_create_callback(NULL);
		CRYPTO_set_dynlock_lock_callback(NULL);
		CRYPTO_set_dynlock_destroy_callback(NULL);
		CRYPTO_set_locking_callback(NULL);
		CRYPTO_set_id_callback(NULL);
		OPENSSL_free(lock_cs);
		lock_cs = NULL;
	}
#endif
	cs_log("HTTP Server: Shutdown requested.");
	close(sock);
	//exit(SIGQUIT);
}

#endif
