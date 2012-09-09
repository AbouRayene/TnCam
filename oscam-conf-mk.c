#include "globals.h"

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <net/if_dl.h>
#include <ifaddrs.h>
#elif defined(__SOLARIS__)
#include <net/if.h>
#include <net/if_arp.h>
#include <sys/sockio.h>
#else
#include <net/if.h>
#endif

/*
 * Creates a string ready to write as a token into config or WebIf for CAIDs. You must free the returned value through free_mk_t().
 */
char *mk_t_caidtab(CAIDTAB *ctab) {
	int32_t i = 0, needed = 1, pos = 0;
	while(ctab->caid[i]) {
		if(ctab->mask[i]) needed += 10;
		else needed += 5;
		if(ctab->cmap[i]) needed += 5;
		++i;
	}
	char *value;
	if(needed == 1 || !cs_malloc(&value, needed * sizeof(char), -1)) return "";
	char *saveptr = value;
	i = 0;
	while(ctab->caid[i]) {
		if (ctab->caid[i] < 0x0100) { //for "ignore provider for" option, caid-shortcut, just first 2 bytes:
			if(i == 0) {
				snprintf(value + pos, needed-(value-saveptr), "%02X", ctab->caid[i]);
				pos += 2;
			} else {
				snprintf(value + pos, needed-(value-saveptr), ",%02X", ctab->caid[i]);
				pos += 3;
			}
		} else {
			if(i == 0) {
				snprintf(value + pos, needed-(value-saveptr), "%04X", ctab->caid[i]);
				pos += 4;
			} else {
				snprintf(value + pos, needed-(value-saveptr), ",%04X", ctab->caid[i]);
				pos += 5;
			}
		}

		if((ctab->mask[i]) && (ctab->mask[i] != 0xFFFF)) {
			snprintf(value + pos, needed-(value-saveptr), "&%04X", ctab->mask[i]);
			pos += 5;
		}
		if(ctab->cmap[i]) {
			snprintf(value + pos, needed-(value-saveptr), ":%04X", ctab->cmap[i]);
			pos += 5;
		}
		++i;
	}
	value[pos] = '\0';
	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf for TunTabs. You must free the returned value through free_mk_t().
 */
char *mk_t_tuntab(TUNTAB *ttab) {
	int32_t i, needed = 1, pos = 0;
	for (i=0; i<ttab->n; i++) {
		if(ttab->bt_srvid[i]) needed += 10;
		else needed += 5;
		if(ttab->bt_caidto[i]) needed += 5;
	}
	char *value;
	if(needed == 1 || !cs_malloc(&value, needed * sizeof(char), -1)) return "";
	char *saveptr = value;
	for (i=0; i<ttab->n; i++) {
		if(i == 0) {
			snprintf(value + pos, needed-(value-saveptr), "%04X", ttab->bt_caidfrom[i]);
			pos += 4;
		} else {
			snprintf(value + pos, needed-(value-saveptr), ",%04X", ttab->bt_caidfrom[i]);
			pos += 5;
		}
		if(ttab->bt_srvid[i]) {
			snprintf(value + pos, needed-(value-saveptr), ".%04X", ttab->bt_srvid[i]);
			pos += 5;
		}
		if(ttab->bt_caidto[i]) {
			snprintf(value + pos, needed-(value-saveptr), ":%04X", ttab->bt_caidto[i]);
			pos += 5;
		}
	}
	value[pos] = '\0';
	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf for groups. You must free the returned value through free_mk_t().
 */
char *mk_t_group(uint64_t grp) {
	int32_t i = 0, needed = 1, pos = 0, dot = 0;

	for(i = 0; i < 64; i++) {
		if (grp&((uint64_t)1<<i)) {
			needed += 2;
			if(i > 9) needed += 1;
		}
	}
	char *value;
	if(needed == 1 || !cs_malloc(&value, needed * sizeof(char), -1)) return "";
	char * saveptr = value;
	for(i = 0; i < 64; i++) {
		if (grp&((uint64_t)1<<i)) {
			if (dot == 0) {
				snprintf(value + pos, needed-(value-saveptr), "%d", i+1);
				if (i > 8)pos += 2;
				else pos += 1;
				dot = 1;
			} else {
				snprintf(value + pos, needed-(value-saveptr), ",%d", i+1);
				if (i > 8)pos += 3;
				else pos += 2;
			}
		}
	}
	value[pos] = '\0';
	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf for FTabs (CHID, Ident). You must free the returned value through free_mk_t().
 */
char *mk_t_ftab(FTAB *ftab) {
	int32_t i = 0, j = 0, needed = 1, pos = 0;

	if (ftab->nfilts != 0) {
		needed = ftab->nfilts * 5;
		for (i = 0; i < ftab->nfilts; ++i)
			needed += ftab->filts[i].nprids * 7;
	}

	char *value;
	if(needed == 1 || !cs_malloc(&value, needed * sizeof(char), -1)) return "";
	char *saveptr = value;
	char *dot="";
	for (i = 0; i < ftab->nfilts; ++i) {
		snprintf(value + pos, needed-(value-saveptr), "%s%04X", dot, ftab->filts[i].caid);
		pos += 4;
		if (i > 0) pos += 1;
		dot=":";
		for (j = 0; j < ftab->filts[i].nprids; ++j) {
			snprintf(value + pos, needed-(value-saveptr), "%s%06X", dot, ftab->filts[i].prids[j]);
			pos += 7;
			dot=",";
		}
		dot=";";
	}

	value[pos] = '\0';
	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf for the camd35 tcp ports. You must free the returned value through free_mk_t().
 */
char *mk_t_camd35tcp_port(void) {
	int32_t i, j, pos = 0, needed = 1;

	/* Precheck to determine how long the resulting string will maximally be (might be a little bit smaller but that shouldn't hurt) */
	for(i = 0; i < cfg.c35_tcp_ptab.nports; ++i) {
		/* Port is maximally 5 chars long, plus the @caid, plus the ";" between ports */
		needed += 11;
		if (cfg.c35_tcp_ptab.ports[i].ftab.filts[0].nprids > 1) {
			needed += cfg.c35_tcp_ptab.ports[i].ftab.filts[0].nprids * 7;
		}
	}
	char *value;
	if(needed == 1 || !cs_malloc(&value, needed * sizeof(char), -1)) return "";
	char *saveptr = value;
	char *dot1 = "", *dot2;
	for(i = 0; i < cfg.c35_tcp_ptab.nports; ++i) {

		if (cfg.c35_tcp_ptab.ports[i].ftab.filts[0].caid) {
			pos += snprintf(value + pos, needed-(value-saveptr), "%s%d@%04X", dot1,
					cfg.c35_tcp_ptab.ports[i].s_port,
					cfg.c35_tcp_ptab.ports[i].ftab.filts[0].caid);

			if (cfg.c35_tcp_ptab.ports[i].ftab.filts[0].nprids > 1) {
				dot2 = ":";
				for (j = 0; j < cfg.c35_tcp_ptab.ports[i].ftab.filts[0].nprids; ++j) {
					pos += snprintf(value + pos, needed-(value-saveptr), "%s%X", dot2, cfg.c35_tcp_ptab.ports[i].ftab.filts[0].prids[j]);
					dot2 = ",";
				}
			}
			dot1=";";
		} else {
			pos += snprintf(value + pos, needed-(value-saveptr), "%d", cfg.c35_tcp_ptab.ports[i].s_port);
		}
	}
	return value;
}

#ifdef MODULE_CCCAM
/*
 * Creates a string ready to write as a token into config or WebIf for the cccam tcp ports. You must free the returned value through free_mk_t().
 */
char *mk_t_cccam_port(void) {
	int32_t i, pos = 0, needed = CS_MAXPORTS*6+8;

	char *value;
	if(!cs_malloc(&value, needed * sizeof(char), -1)) return "";
	char *dot = "";
	for(i = 0; i < CS_MAXPORTS; i++) {
		if (!cfg.cc_port[i]) break;

		pos += snprintf(value + pos, needed-pos, "%s%d", dot, cfg.cc_port[i]);
		dot=",";
	}
	return value;
}
#endif


/*
 * Creates a string ready to write as a token into config or WebIf for AESKeys. You must free the returned value through free_mk_t().
 */
char *mk_t_aeskeys(struct s_reader *rdr) {
	AES_ENTRY *current = rdr->aes_list;
	int32_t i, pos = 0, needed = 1, prevKeyid = 0, prevCaid = 0;
	uint32_t prevIdent = 0;

	/* Precheck for the approximate size that we will need; it's a bit overestimated but we correct that at the end of the function */
	while(current) {
		/* The caid, ident, "@" and the trailing ";" need to be output when they are changing */
		if(prevCaid != current->caid || prevIdent != current->ident) needed += 12 + (current->keyid * 2);
		/* "0" keys are not saved so we need to check for gaps */
		else if(prevKeyid != current->keyid + 1) needed += (current->keyid - prevKeyid - 1) * 2;
		/* The 32 byte key plus either the (heading) ":" or "," */
		needed += 33;
		prevCaid = current->caid;
		prevIdent = current->ident;
		prevKeyid = current->keyid;
		current = current->next;
	}

	/* Set everything back and now create the string */
	current = rdr->aes_list;
	prevCaid = 0;
	prevIdent = 0;
	prevKeyid = 0;
	char tmp[needed];
	char dot;
	if(needed == 1) tmp[0] = '\0';
	char tmpkey[33];
	while(current) {
		/* A change in the ident or caid means that we need to output caid and ident */
		if(prevCaid != current->caid || prevIdent != current->ident) {
			if(pos > 0) {
				tmp[pos] = ';';
				++pos;
			}
			pos += snprintf(tmp+pos, sizeof(tmp)-pos, "%04X@%06X", current->caid, current->ident);
			prevKeyid = -1;
			dot = ':';
		} else dot = ',';
		/* "0" keys are not saved so we need to check for gaps and output them! */
		for (i = prevKeyid + 1; i < current->keyid; ++i) {
			pos += snprintf(tmp+pos, sizeof(tmp)-pos, "%c0", dot);
			dot = ',';
		}
		tmp[pos] = dot;
		++pos;
		for (i = 0; i < 16; ++i) snprintf(tmpkey + (i*2), sizeof(tmpkey) - (i*2), "%02X", current->plainkey[i]);
		/* A key consisting of only FFs has a special meaning (just return what the card outputted) and can be specified more compact */
		if(strcmp(tmpkey, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF") == 0) pos += snprintf(tmp+pos, sizeof(tmp)-pos, "FF");
		else pos += snprintf(tmp+pos, sizeof(tmp)-pos, "%s", tmpkey);
		prevCaid = current->caid;
		prevIdent = current->ident;
		prevKeyid = current->keyid;
		current = current->next;
	}

	/* copy to result array of correct size */
	char *value;
	if(pos == 0 || !cs_malloc(&value, (pos + 1) * sizeof(char), -1)) return "";
	memcpy(value, tmp, pos + 1);
	return(value);
}

/*
 * Creates a string ready to write as a token into config or WebIf for the Newcamd Port. You must free the returned value through free_mk_t().
 */
char *mk_t_newcamd_port(void) {
	int32_t i, j, k, pos = 0, needed = 1;

	/* Precheck to determine how long the resulting string will maximally be (might be a little bit smaller but that shouldn't hurt) */
	for(i = 0; i < cfg.ncd_ptab.nports; ++i) {
		/* Port is maximally 5 chars long, plus the @caid, plus the ";" between ports */
		needed += 11;
		if(cfg.ncd_ptab.ports[i].ncd_key_is_set) needed += 30;
		if (cfg.ncd_ptab.ports[i].ftab.filts[0].nprids > 0) {
			needed += cfg.ncd_ptab.ports[i].ftab.filts[0].nprids * 7;
		}
	}
	char *value;
	if(needed == 1 || !cs_malloc(&value, needed * sizeof(char), -1)) return "";
	char *dot1 = "", *dot2;

	for(i = 0; i < cfg.ncd_ptab.nports; ++i) {
		pos += snprintf(value + pos, needed-pos,  "%s%d", dot1, cfg.ncd_ptab.ports[i].s_port);

		// separate DES Key for this port
		if(cfg.ncd_ptab.ports[i].ncd_key_is_set) {
			pos += snprintf(value + pos, needed-pos, "{");
			for (k = 0; k < 14; k++)
				pos += snprintf(value + pos, needed-pos, "%02X", cfg.ncd_ptab.ports[i].ncd_key[k]);
			pos += snprintf(value + pos, needed-pos, "}");
		}

		pos += snprintf(value + pos, needed-pos, "@%04X", cfg.ncd_ptab.ports[i].ftab.filts[0].caid);

		if (cfg.ncd_ptab.ports[i].ftab.filts[0].nprids > 0) {
			dot2 = ":";
			for (j = 0; j < cfg.ncd_ptab.ports[i].ftab.filts[0].nprids; ++j) {
				pos += snprintf(value + pos, needed-pos, "%s%06X", dot2, (int)cfg.ncd_ptab.ports[i].ftab.filts[0].prids[j]);
				dot2 = ",";
			}
		}
		dot1=";";
	}
	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf for au readers. You must free the returned value through free_mk_t().
 */
char *mk_t_aureader(struct s_auth *account) {
	int32_t pos = 0;
	char *dot = "";

	char *value;
	if(ll_count(account->aureader_list) == 0 || !cs_malloc(&value, 256 * sizeof(char), -1)) return "";
	value[0] = '\0';

	struct s_reader *rdr;
	LL_ITER itr = ll_iter_create(account->aureader_list);
	while ((rdr = ll_iter_next(&itr))) {
		pos += snprintf(value + pos, 256-pos, "%s%s", dot, rdr->label);
		dot = ",";
	}

	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf for blocknano and savenano. You must free the returned value through free_mk_t().
 * flag 0x01 for blocknano or 0x02 for savenano
 */
char *mk_t_nano(struct s_reader *rdr, uchar flag) {
	int32_t i, pos = 0, needed = 0;
	uint16_t nano = 0;

	if (flag==0x01)
		nano=rdr->b_nano;
	else
		nano=rdr->s_nano;

	for (i=0; i<16; i++)
		if ((1 << i) & nano)
			needed++;

	char *value;
	if (nano == 0xFFFF) {
		if(!cs_malloc(&value, (3 * sizeof(char)) + 1, -1)) return "";
		snprintf(value, 4, "all");
	} else {
		if(needed == 0 || !cs_malloc(&value, (needed * 3 * sizeof(char)) + 1, -1)) return "";
		value[0] = '\0';
		for (i=0; i<16; i++) {
			if ((1 << i) & nano)
				pos += snprintf(value + pos, (needed*3)+1-pos, "%s%02x", pos ? "," : "", (i+0x80));
		}
	}
	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf for the sidtab. You must free the returned value through free_mk_t().
 */
char *mk_t_service( uint64_t sidtabok, uint64_t sidtabno) {
	int32_t i, pos;
	char *dot;
	char *value;
	struct s_sidtab *sidtab = cfg.sidtab;
	if(!sidtab || (!sidtabok && !sidtabno) || !cs_malloc(&value, 1024, -1)) return "";
	value[0] = '\0';

	for (i=pos=0,dot=""; sidtab; sidtab=sidtab->next,i++) {
		if (sidtabok&((SIDTABBITS)1<<i)) {
			pos += snprintf(value + pos, 1024 - pos, "%s%s", dot, sidtab->label);
			dot = ",";
		}
		if (sidtabno&((SIDTABBITS)1<<i)) {
			pos += snprintf(value + pos, 1024 - pos, "%s!%s", dot, sidtab->label);
			dot = ",";
		}
	}
	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf for the logfile parameter. You must free the returned value through free_mk_t().
 */
char *mk_t_logfile(void) {
	int32_t pos = 0, needed = 1;
	char *value, *dot = "";

	if(cfg.logtostdout == 1) needed += 7;
	if(cfg.logtosyslog == 1) needed += 7;
	if(cfg.logfile) needed += strlen(cfg.logfile);
	if(needed == 1 || !cs_malloc(&value, needed * sizeof(char), -1)) return "";

	if(cfg.logtostdout == 1) {
		pos += snprintf(value + pos, needed - pos, "stdout");
		dot = ";";
	}
	if(cfg.logtosyslog == 1) {
		pos += snprintf(value + pos, needed - pos, "%ssyslog", dot);
		dot = ";";
	}
	if(cfg.logfile) {
		pos += snprintf(value + pos, needed - pos, "%s%s", dot, cfg.logfile);
	}
	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf for the ecm whitelist. You must free the returned value through free_mk_t().
 */
char *mk_t_ecmwhitelist(struct s_ecmWhitelist *whitelist) {
	int32_t needed = 1, pos = 0;
	struct s_ecmWhitelist *cip;
	struct s_ecmWhitelistIdent *cip2;
	struct s_ecmWhitelistLen *cip3;
	char *value, *dot = "", *dot2 = "";
	for (cip = whitelist; cip; cip = cip->next) {
		needed += 7;
		for (cip2 = cip->idents; cip2; cip2 = cip2->next) {
			needed +=7;
			for (cip3 = cip2->lengths; cip3; cip3 = cip3->next) needed +=3;
		}
	}

	char tmp[needed];

	for (cip = whitelist; cip; cip = cip->next) {
		for (cip2 = cip->idents; cip2; cip2 = cip2->next) {
			if(cip2->lengths != NULL) {
				if(cip->caid != 0) {
					if(cip2->ident == 0)
						pos += snprintf(tmp + pos, needed - pos, "%s%04X:", dot, cip->caid);
					else
						pos += snprintf(tmp + pos, needed - pos, "%s%04X@%06X:", dot, cip->caid, cip2->ident);
				} else pos += snprintf(tmp + pos, needed - pos, "%s", dot);
			}
			dot2="";
			for (cip3 = cip2->lengths; cip3; cip3 = cip3->next) {
				pos += snprintf(tmp + pos, needed - pos, "%s%02X", dot2, cip3->len);
				dot2=",";
			}
			dot=";";
		}
	}
	if(pos == 0 || !cs_malloc(&value, (pos + 1) * sizeof(char), -1)) return "";
	memcpy(value, tmp, pos + 1);
	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf for an iprange. You must free the returned value through free_mk_t().
 */
char *mk_t_iprange(struct s_ip *range) {
	struct s_ip *cip;
	char *value, *dot = "";
	int32_t needed = 1, pos = 0;
	for (cip = range; cip; cip = cip->next) needed += 32;

	char tmp[needed];

	for (cip = range; cip; cip = cip->next) {
		pos += snprintf(tmp + pos, needed - pos, "%s%s", dot, cs_inet_ntoa(cip->ip[0]));
		if (!IP_EQUAL(cip->ip[0], cip->ip[1]))	pos += snprintf(tmp + pos, needed - pos, "-%s", cs_inet_ntoa(cip->ip[1]));
		dot=",";
	}
	if(pos == 0 || !cs_malloc(&value, (pos + 1) * sizeof(char), -1)) return "";
	memcpy(value, tmp, pos + 1);
	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf for the class attribute. You must free the returned value through free_mk_t().
 */
char *mk_t_cltab(CLASSTAB *clstab) {
	char *value, *dot = "";
	int32_t i, needed = 1, pos = 0;
	for(i = 0; i < clstab->an; ++i) needed += 3;
	for(i = 0; i < clstab->bn; ++i) needed += 4;

	char tmp[needed];

	for(i = 0; i < clstab->an; ++i) {
		pos += snprintf(tmp + pos, needed - pos, "%s%02x", dot, (int32_t)clstab->aclass[i]);
		dot=",";
	}
	for(i = 0; i < clstab->bn; ++i) {
		pos += snprintf(tmp + pos, needed - pos, "%s!%02x", dot, (int32_t)clstab->bclass[i]);
		dot=",";
	}

	if(pos == 0 || !cs_malloc(&value, (pos + 1) * sizeof(char), -1)) return "";
	memcpy(value, tmp, pos + 1);
	return value;
}

/*
 * Creates a string ready to write as a token into config or WebIf. You must free the returned value through free_mk_t().
 */
char *mk_t_caidvaluetab(CAIDVALUETAB *tab)
{
		if (!tab->n) return "";
		int32_t i, size = 2 + tab->n * (4 + 1 + 5 + 1); //caid + ":" + time + ","
		char *buf = cs_malloc(&buf, size, SIGINT);
		char *ptr = buf;

		for (i = 0; i < tab->n; i++) {
			if (tab->caid[i] < 0x0100) //Do not format 0D as 000D, its a shortcut for 0Dxx:
				ptr += snprintf(ptr, size-(ptr-buf), "%s%02X:%d", i?",":"", tab->caid[i], tab->value[i]);
			else
				ptr += snprintf(ptr, size-(ptr-buf), "%s%04X:%d", i?",":"", tab->caid[i], tab->value[i]);
		}
		*ptr = 0;
		return buf;
}

/*
 * returns string of comma separated values
 */
char *mk_t_emmbylen(struct s_reader *rdr) {

	char *value, *dot = "";
	int32_t pos = 0, needed = 0;
	int8_t i;

	needed = (CS_MAXEMMBLOCKBYLEN * 4) +1;

	if (!cs_malloc(&value, needed, -1)) return "";

	for( i = 0; i < CS_MAXEMMBLOCKBYLEN; i++ ) {
		if(rdr->blockemmbylen[i] != 0) {
			pos += snprintf(value + pos, needed, "%s%d", dot, rdr->blockemmbylen[i]);
			dot = ",";
		}
	}
	return value;
}

/*
 * makes string from binary structure
 */
char *mk_t_allowedprotocols(struct s_auth *account) {

	if (!account->allowedprotocols)
		return "";

	int16_t i, tmp = 1, pos = 0, needed = 255, tagcnt;
	char *tag[] = {"camd33", "camd35", "cs378x", "newcamd", "cccam", "gbox", "radegast", "dvbapi", "constcw", "serial"};
	char *value, *dot = "";

	if (!cs_malloc(&value, needed, -1))
		return "";

	tagcnt = sizeof(tag)/sizeof(char *);
	for (i = 0; i < tagcnt; i++) {
		if ((account->allowedprotocols & tmp) == tmp) {
			pos += snprintf(value + pos, needed, "%s%s", dot, tag[i]);
			dot = ",";
		}
		tmp = tmp << 1;
	}
	return value;
}

/*
 * mk_t-functions give back a constant empty string when allocation fails or when the result is an empty string.
 * This function thus checks the stringlength and only frees if necessary.
 */
void free_mk_t(char *value) {
	if(strlen(value) > 0) free(value);
}
