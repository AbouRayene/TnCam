#include "globals.h"
#include "oscam-conf-chk.h"

void chk_iprange(char *value, struct s_ip **base)
{
	int32_t i = 0;
	char *ptr1, *ptr2, *saveptr1 = NULL;
	struct s_ip *fip, *lip, *cip;

	cs_malloc(&cip, sizeof(struct s_ip), SIGINT);
	fip = cip;

	for (ptr1=strtok_r(value, ",", &saveptr1); ptr1; ptr1=strtok_r(NULL, ",", &saveptr1)) {
			if (i == 0)
				++i;
		else {
			cs_malloc(&cip, sizeof(struct s_ip), SIGINT);
			lip->next = cip;
		}

		if( (ptr2=strchr(trim(ptr1), '-')) ) {
			*ptr2++ ='\0';
			cs_inet_addr(trim(ptr1), &cip->ip[0]);
			cs_inet_addr(trim(ptr2), &cip->ip[1]);
		} else {
			cs_inet_addr(ptr1, &cip->ip[0]);
			IP_ASSIGN(cip->ip[1], cip->ip[0]);
		}
		lip = cip;
	}
	lip = *base;
	*base = fip;
	clear_sip(&lip);
}

void chk_caidtab(char *caidasc, CAIDTAB *ctab)
{
	int32_t i;
	char *ptr1, *ptr2, *ptr3, *saveptr1 = NULL;
	CAIDTAB newctab;
	memset(&newctab, 0, sizeof(CAIDTAB));
	for (i = 1; i < CS_MAXCAIDTAB; newctab.mask[i++] = 0xffff);

	for (i = 0, ptr1 = strtok_r(caidasc, ",", &saveptr1); (i < CS_MAXCAIDTAB) && (ptr1); ptr1 = strtok_r(NULL, ",", &saveptr1)) {
		uint32_t caid, mask, cmap;
		if( (ptr3 = strchr(trim(ptr1), ':')) )
			*ptr3++ = '\0';
		else
			ptr3 = "";

		if( (ptr2 = strchr(trim(ptr1), '&')) )
			*ptr2++ = '\0';
		else
			ptr2 = "";

		if (((caid = a2i(ptr1, 2)) | (mask = a2i(ptr2,-2)) | (cmap = a2i(ptr3, 2))) < 0x10000) {
			newctab.caid[i] = caid;
			newctab.mask[i] = mask;
			newctab.cmap[i++] = cmap;
		}
	}
	memcpy(ctab, &newctab, sizeof(CAIDTAB));
}

void chk_caidvaluetab(char *lbrlt, CAIDVALUETAB *tab, int32_t minvalue)
{
	int32_t i;
	char *ptr1, *ptr2, *saveptr1 = NULL;
	CAIDVALUETAB newtab;
	memset(&newtab, 0, sizeof(CAIDVALUETAB));

	for (i = 0, ptr1 = strtok_r(lbrlt, ",", &saveptr1); (i < CS_MAX_CAIDVALUETAB) && (ptr1); ptr1 = strtok_r(NULL, ",", &saveptr1)) {
		int32_t caid, value;

		if( (ptr2 = strchr(trim(ptr1), ':')) )
			*ptr2++ = '\0';
		else
			ptr2 = "";

		if (((caid = a2i(ptr1, 2)) < 0xFFFF) | ((value = atoi(ptr2)) < 10000)) {
			newtab.caid[i] = caid;
			if (value < minvalue) value = minvalue;
			newtab.value[i] = value;
			newtab.n = ++i;
		}
	}
	memcpy(tab, &newtab, sizeof(CAIDVALUETAB));
}

void chk_tuntab(char *tunasc, TUNTAB *ttab)
{
	int32_t i;
	char *ptr1, *ptr2, *ptr3, *saveptr1 = NULL;
	TUNTAB newttab;
	memset(&newttab, 0 , sizeof(TUNTAB));

	for (i = 0, ptr1 = strtok_r(tunasc, ",", &saveptr1); (i < CS_MAXTUNTAB) && (ptr1); ptr1 = strtok_r(NULL, ",", &saveptr1)) {
		uint32_t bt_caidfrom, bt_caidto, bt_srvid;
		if( (ptr3 = strchr(trim(ptr1), ':')) )
			*ptr3++ = '\0';
		else
			ptr3 = "";

		if( (ptr2 = strchr(trim(ptr1), '.')) )
			*ptr2++ = '\0';
		else
			ptr2 = "";

		if ((bt_caidfrom = a2i(ptr1, 2)) | (bt_srvid = a2i(ptr2,-2)) | (bt_caidto = a2i(ptr3, 2))) {
			newttab.bt_caidfrom[i] = bt_caidfrom;
			newttab.bt_caidto[i] = bt_caidto;
			newttab.bt_srvid[i++] = bt_srvid;
			newttab.n = i;
		}
	}
	memcpy(ttab, &newttab, sizeof(TUNTAB));
}

void chk_services(char *labels, SIDTABBITS *sidok, SIDTABBITS *sidno)
{
	int32_t i;
	char *ptr, *saveptr1 = NULL;
	SIDTAB *sidtab;
	SIDTABBITS newsidok, newsidno;
	newsidok = newsidno = 0;
	for (ptr=strtok_r(labels, ",", &saveptr1); ptr; ptr=strtok_r(NULL, ",", &saveptr1)) {
		for (trim(ptr), i = 0, sidtab = cfg.sidtab; sidtab; sidtab = sidtab->next, i++) {
			if (!strcmp(sidtab->label, ptr)) newsidok|=((SIDTABBITS)1<<i);
			if ((ptr[0]=='!') && (!strcmp(sidtab->label, ptr+1))) newsidno|=((SIDTABBITS)1<<i);
		}
	}
	*sidok = newsidok;
	*sidno = newsidno;
}

void chk_ftab(char *zFilterAsc, FTAB *ftab, const char *zType, const char *zName, const char *zFiltName)
{
	int32_t i, j;
	char *ptr1, *ptr2, *ptr3, *saveptr1 = NULL;
	char *ptr[CS_MAXFILTERS] = {0};
	FTAB newftab;
	memset(&newftab, 0, sizeof(FTAB));

	for( i = 0, ptr1 = strtok_r(zFilterAsc, ";", &saveptr1); (i < CS_MAXFILTERS) && (ptr1); ptr1 = strtok_r(NULL, ";", &saveptr1), i++ ) {
		ptr[i] = ptr1;
		if( (ptr2 = strchr(trim(ptr1), ':')) ) {
			*ptr2++ ='\0';
			newftab.filts[i].caid = (uint16_t)a2i(ptr1, 4);
			ptr[i] = ptr2;
		}
		else if (zFiltName && zFiltName[0] == 'c') {
			cs_log("PANIC: CAID field not found in CHID parameter!");
			return;
		}
		newftab.nfilts++;
	}

	if( newftab.nfilts ) {
	    cs_debug_mask(D_CLIENT, "%s '%s' %s filter(s):", zType, zName, zFiltName);
	}
	for( i = 0; i < newftab.nfilts; i++ ) {
		cs_debug_mask(D_CLIENT, "CAID #%d: %04X", i, newftab.filts[i].caid);
		for( j = 0, ptr3 = strtok_r(ptr[i], ",", &saveptr1); (j < CS_MAXPROV) && (ptr3); ptr3 = strtok_r(NULL, ",", &saveptr1), j++ ) {
			newftab.filts[i].prids[j] = a2i(ptr3,6);
			newftab.filts[i].nprids++;
			cs_debug_mask(D_CLIENT, "%s #%d: %06X", zFiltName, j, newftab.filts[i].prids[j]);
		}
	}
	memcpy(ftab, &newftab, sizeof(FTAB));
}

void chk_cltab(char *classasc, CLASSTAB *clstab)
{
	int32_t i;
	char *ptr1, *saveptr1 = NULL;
	CLASSTAB newclstab;
	memset(&newclstab, 0, sizeof(newclstab));
	newclstab.an = newclstab.bn = 0;
	for( i = 0, ptr1 = strtok_r(classasc, ",", &saveptr1); (i < CS_MAXCAIDTAB) && (ptr1); ptr1 = strtok_r(NULL, ",", &saveptr1) ) {
		ptr1 = trim(ptr1);
		if( ptr1[0] == '!' )
			newclstab.bclass[newclstab.bn++] = (uchar)a2i(ptr1+1, 2);
		else
			newclstab.aclass[newclstab.an++] = (uchar)a2i(ptr1, 2);
	}
	memcpy(clstab, &newclstab, sizeof(CLASSTAB));
}

void chk_port_tab(char *portasc, PTAB *ptab)
{
	int32_t i, j, nfilts, ifilt, iport;
	PTAB *newptab;
	char *ptr1, *ptr2, *ptr3, *saveptr1 = NULL;
	char *ptr[CS_MAXPORTS] = {0};
	int32_t port[CS_MAXPORTS] = {0};
	if(!cs_malloc(&newptab, sizeof(PTAB), -1)) return;

	for (nfilts = i = 0, ptr1 = strtok_r(portasc, ";", &saveptr1); (i < CS_MAXPORTS) && (ptr1); ptr1 = strtok_r(NULL, ";", &saveptr1), i++) {
		ptr[i] = ptr1;
		if( (ptr2=strchr(trim(ptr1), '@')) ) {
			*ptr2++ ='\0';
			newptab->ports[i].s_port = atoi(ptr1);

			//checking for des key for port
			newptab->ports[i].ncd_key_is_set = 0;   //default to 0
			if( (ptr3=strchr(trim(ptr1), '{')) ) {
				*ptr3++='\0';
				if (key_atob_l(ptr3, newptab->ports[i].ncd_key, 28))
					fprintf(stderr, "newcamd: error in DES Key for port %s -> ignored\n", ptr1);
				else
					newptab->ports[i].ncd_key_is_set = 1;
			}

			ptr[i] = ptr2;
			port[i] = newptab->ports[i].s_port;
			newptab->nports++;
		}
		nfilts++;
	}

	if( nfilts == 1 && strlen(portasc) < 6 && newptab->ports[0].s_port == 0 ) {
		newptab->ports[0].s_port = atoi(portasc);
		newptab->nports = 1;
	}

	iport = ifilt = 0;
	for (i=0; i<nfilts; i++) {
		if( port[i] != 0 )
			iport = i;
		for (j = 0, ptr3 = strtok_r(ptr[i], ",", &saveptr1); (j < CS_MAXPROV) && (ptr3); ptr3 = strtok_r(NULL, ",", &saveptr1), j++) {
			if( (ptr2=strchr(trim(ptr3), ':')) ) {
				*ptr2++='\0';
				newptab->ports[iport].ftab.nfilts++;
				ifilt = newptab->ports[iport].ftab.nfilts-1;
				newptab->ports[iport].ftab.filts[ifilt].caid = (uint16_t)a2i(ptr3, 4);
				newptab->ports[iport].ftab.filts[ifilt].prids[j] = a2i(ptr2, 6);
			} else {
				newptab->ports[iport].ftab.filts[ifilt].prids[j] = a2i(ptr3, 6);
			}
			newptab->ports[iport].ftab.filts[ifilt].nprids++;
		}
	}
	memcpy(ptab, newptab, sizeof(PTAB));
	free(newptab);
}
