#include "globals.h"
#include "reader-common.h"

char oscam_device[128];
int  oscam_card_detect;

uchar cta_cmd[272], cta_res[260], atr[64];
ushort cta_lr, atr_size=0;
static int cs_ptyp_orig; //reinit=1, 

#define SC_IRDETO 1
#define SC_CRYPTOWORKS 2
#define SC_VIACCESS 3
#define SC_CONAX 4
#define SC_SECA 5
#define SC_VIDEOGUARD2 6
#define SC_DRE 7
#define SC_NAGRA 8

static int reader_device_type(char *device, int typ)
{
  int rc=PORT_STD;
#ifdef TUXBOX
  struct stat sb;
#endif

  switch(reader[ridx].typ)
  {
    case R_MOUSE:
    case R_SMART:
      rc=PORT_STD;
#ifdef TUXBOX
      if (!stat(device, &sb))
      {
        if (S_ISCHR(sb.st_mode))
        {
          int dev_major, dev_minor;
          dev_major=major(sb.st_rdev);
          dev_minor=minor(sb.st_rdev);
          if ((cs_hw==CS_HW_DBOX2) && ((dev_major==4) || (dev_major==5)))
            switch(dev_minor & 0x3F)
            {
              case 0: rc=PORT_DB2COM1; break;
              case 1: rc=PORT_DB2COM2; break;
            }
          cs_debug("device is major: %d, minor: %d, typ=%d", dev_major, dev_minor, rc);
        }
      }
#endif
      break;
    case R_INTERNAL:
      rc=PORT_SCI;
      break;
  }
  return(rc);
}

static void reader_nullcard(void)
{
  reader[ridx].card_system=0;
  memset(reader[ridx].hexserial, 0   , sizeof(reader[ridx].hexserial));
  memset(reader[ridx].prid     , 0xFF, sizeof(reader[ridx].prid     ));
  memset(reader[ridx].caid     , 0   , sizeof(reader[ridx].caid     ));
  memset(reader[ridx].availkeys, 0   , sizeof(reader[ridx].availkeys));
  reader[ridx].acs=0;
  reader[ridx].nprov=0;
}

int reader_doapi(uchar dad, uchar *buf, int l, int dbg)
{
#ifdef HAVE_PCSC
	if (reader[ridx].typ == R_PCSC) {
		LONG rv;
		 SCARD_IO_REQUEST pioRecvPci;
		 DWORD dwSendLength, dwRecvLength;

		 dwSendLength = l;
		 dwRecvLength = sizeof(cta_res);

		 //cs_ddump(buf, dwSendLength, "sending %d bytes to PCSC", dwSendLength);

		 if(reader[ridx].dwActiveProtocol == SCARD_PROTOCOL_T0)
			 rv = SCardTransmit(reader[ridx].hCard, SCARD_PCI_T0, buf, dwSendLength, &pioRecvPci, &cta_res, &dwRecvLength);
		 else  if(reader[ridx].dwActiveProtocol == SCARD_PROTOCOL_T1)
			 rv = SCardTransmit(reader[ridx].hCard, SCARD_PCI_T1, buf, dwSendLength, &pioRecvPci, &cta_res, &dwRecvLength);
		 else {
			 cs_debug("PCSC invalid protocol (T=%d)", reader[ridx].dwActiveProtocol);
			 return ERR_INVALID;
		 }

		 cta_lr=dwRecvLength;
		 // cs_ddump(cta_res, cta_lr, "received %d bytes from PCSC with rv=%lx", cta_lr, rv);

		 cs_debug("PCSC doapi (%lx ) (T=%d)", rv, reader[ridx].dwActiveProtocol );
		 if ( rv  == SCARD_S_SUCCESS ){
			 return OK;
		 } else {
			 return ERR_INVALID;
		 }
	}

#endif
  int rc;
  uchar sad;

//  oscam_card_inserted=4;
  sad=2;
  cta_lr=sizeof(cta_res)-1;
  cs_ptyp_orig=cs_ptyp;
  cs_ptyp=dbg;
  //cs_ddump(buf, l, "send %d bytes to ctapi", l);
  rc=CT_data(1, &dad, &sad, l, buf, &cta_lr, cta_res);
  //cs_ddump(cta_res, cta_lr, "received %d bytes from ctapi with rc=%d", cta_lr, rc);
  cs_ptyp=cs_ptyp_orig;
  return(rc);
}

int reader_chkicc(uchar *buf, int l)
{
  return(reader_doapi(1, buf, l, D_WATCHDOG));
}

int reader_cmd2api(uchar *buf, int l)
{
  return(reader_doapi(1, buf, l, D_DEVICE));
}

int reader_cmd2icc(uchar *buf, int l)
{
    int rc;
    cs_ddump(buf, l, "write to cardreader %s:",reader[ridx].label);
    rc = reader_doapi(0, buf, l, D_DEVICE);
    cs_ddump(cta_res, cta_lr, "answer from cardreader %s:", reader[ridx].label);
    return rc;
}

#define CMD_LEN 5

int card_write(uchar *cmd, uchar *data)
{
  if (data) {
    uchar buf[256]; //only allocate buffer when its needed
    memcpy(buf, cmd, CMD_LEN);
    if (cmd[4]) memcpy(buf+CMD_LEN, data, cmd[4]);
    return(reader_cmd2icc(buf, CMD_LEN+cmd[4]));
  }
  else
    return(reader_cmd2icc(cmd, CMD_LEN));
}

static int reader_activate_card()
{
  int i;
  char ret;

#ifdef HAVE_PCSC
  if (reader[ridx].typ == R_PCSC) {
	  cs_debug("PCSC initializing card in (%s)", &reader[ridx].pcsc_name);
	  LONG rv;
	  DWORD dwState, dwAtrLen, dwReaderLen;
	  BYTE pbAtr[64];
	  dwAtrLen = sizeof(pbAtr);

	  cs_debug("PCSC resetting card in (%s)", &reader[ridx].pcsc_name);
	  rv = SCardReconnect(reader[ridx].hCard, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,  SCARD_RESET_CARD, &reader[ridx].dwActiveProtocol);
	  cs_debug("PCSC resetting done on card in (%s)", &reader[ridx].pcsc_name);
	  cs_debug("PCSC Protocol (T=%d)",reader[ridx].dwActiveProtocol);

	  if ( rv != SCARD_S_SUCCESS )  {
		  cs_debug("Error PCSC failed to reset card (%lx)", rv);
		  return(0);
	  }

        rv=SCardBeginTransaction(reader[ridx].hCard);
        if (rv!=SCARD_S_SUCCESS) {
        cs_log("PCSC reader %s Failed to begin transaction", reader[ridx].pcsc_name);
        return 0;
        }

	  cs_debug("PCSC getting ATR for card in (%s)", &reader[ridx].pcsc_name);
	  rv = SCardStatus(reader[ridx].hCard, NULL, &dwReaderLen, &dwState, &reader[ridx].dwActiveProtocol, pbAtr, &dwAtrLen);
	  if ( rv == SCARD_S_SUCCESS ) {
		  cs_debug("PCSC Protocol (T=%d)",reader[ridx].dwActiveProtocol);

		  /*
		  DWORD currentClk, currentClkLen;
		  currentClkLen = sizeof(currentClk);
		  rv = SCardGetAttrib(reader[ridx].hCard, SCARD_ATTR_CURRENT_CLK , &currentClk, &currentClkLen);
		  cs_debug("PCSC rv=(%lx) Current clk = %lx Khz",rv, currentClk);
			*/

		  // TODO: merge better
		  memcpy(atr, pbAtr, dwAtrLen);
		  atr_size=dwAtrLen;
		#ifdef CS_RDR_INIT_HIST
		  reader[ridx].init_history_pos=0;
		  memset(reader[ridx].init_history, 0, sizeof(reader[ridx].init_history));
		#endif
		  cs_ri_log("ATR: %s", cs_hexdump(1, (uchar *)pbAtr, dwAtrLen));
		  sleep(1);
		  return(1);

	  } else {
		  cs_debug("Error PCSC failed to get ATR for card (%lx)", rv);
		  return(0);
	  }
  }
#endif

  cta_cmd[0] = CTBCS_INS_RESET;
  cta_cmd[1] = CTBCS_P2_RESET_GET_ATR;
  cta_cmd[2] = 0x00;

  ret = reader_cmd2api(cta_cmd, 3);
  if (ret!=OK)
  {
    cs_log("Error reset terminal: %d", ret);
    return(0);
  }
  
  cta_cmd[0] = CTBCS_CLA;
  cta_cmd[1] = CTBCS_INS_STATUS;
  cta_cmd[2] = CTBCS_P1_CT_KERNEL;
  cta_cmd[3] = CTBCS_P2_STATUS_ICC;
  cta_cmd[4] = 0x00;

//  ret=reader_cmd2api(cmd, 11); warum 11 ??????
  ret=reader_cmd2api(cta_cmd, 5);
  if (ret!=OK)
  {
    cs_log("Error getting status of terminal: %d", ret);
    return(0);
  }
  if (cta_res[0]!=CTBCS_DATA_STATUS_CARD_CONNECT)
    return(0);

  /* Activate card */
//  for (i=0; (i<5) && ((ret!=OK)||(cta_res[cta_lr-2]!=0x90)); i++)
  for (i=0; i<5; i++)
  {
    //reader_irdeto_mode = i%2 == 1; //does not work when overclocking
    cta_cmd[0] = CTBCS_CLA;
    cta_cmd[1] = CTBCS_INS_REQUEST;
    cta_cmd[2] = CTBCS_P1_INTERFACE1;
    cta_cmd[3] = CTBCS_P2_REQUEST_GET_ATR;
    cta_cmd[4] = 0x00;

    ret=reader_cmd2api(cta_cmd, 5);
    if ((ret==OK)||(cta_res[cta_lr-2]==0x90))
    {
      i=100;
      break;
    }
    cs_log("Error activating card: %d", ret);
    cs_sleepms(500);
  }
  if (i<100) return(0);

  /* Store ATR */
  atr_size=cta_lr-2;
  memcpy(atr, cta_res, atr_size);
#ifdef CS_RDR_INIT_HIST
  reader[ridx].init_history_pos=0;
  memset(reader[ridx].init_history, 0, sizeof(reader[ridx].init_history));
#endif
  cs_ri_log("ATR: %s", cs_hexdump(1, atr, atr_size));
  sleep(1);
  return(1);
}

void do_emm_from_file(void)
{
  //now here check whether we have EMM's on file to load and write to card:
  if (reader[ridx].emmfile[0]) {//readnano has something filled in

    //handling emmfile
    char token[256];
    FILE *fp;
    size_t result;
    if ((reader[ridx].emmfile[0] == '/'))
      sprintf (token, "%s", reader[ridx].emmfile); //pathname included
    else
      sprintf (token, "%s%s", cs_confdir, reader[ridx].emmfile); //only file specified, look in confdir for this file
    
    if (!(fp = fopen (token, "rb")))
      cs_log ("ERROR: Cannot open EMM file '%s' (errno=%d)\n", token, errno);
    else {
      EMM_PACKET *eptmp;
      eptmp = malloc (sizeof(EMM_PACKET));
      result = fread (eptmp, sizeof(EMM_PACKET), 1, fp);      
      fclose (fp);

      uchar old_b_nano = reader[ridx].b_nano[eptmp->emm[0]]; //save old b_nano value
      reader[ridx].b_nano[eptmp->emm[0]] &= 0xfc; //clear lsb and lsb+1, so no blocking, and no saving for this nano      
          
      //if (!reader_do_emm (eptmp))
      if (!reader_emm (eptmp))
        cs_log ("ERROR: EMM read from file %s NOT processed correctly!", token);

      reader[ridx].b_nano[eptmp->emm[0]] = old_b_nano; //restore old block/save settings
      reader[ridx].emmfile[0] = 0; //clear emmfile, so no reading anymore

      free(eptmp);
      eptmp = NULL;
    }
  }
}

void reader_card_info()
{
//  int rc=-1;
  if (reader_checkhealth())
  //if (rc=reader_checkhealth())
  {
    client[cs_idx].last=time((time_t)0);
    cs_ri_brk(0);
    do_emm_from_file();
    switch(reader[ridx].card_system)
    {
      case SC_NAGRA:
        nagra2_card_info(); break;
      case SC_IRDETO:
        irdeto_card_info(); break;
      case SC_CRYPTOWORKS:
        cryptoworks_card_info(); break;
      case SC_VIACCESS:
        viaccess_card_info(); break;
      case SC_CONAX:
        conax_card_info(); break;
      case SC_VIDEOGUARD2:
        videoguard_card_info(); break;
      case SC_SECA:
         seca_card_info(); break;
      case SC_DRE:
	 dre_card_info(); break;
    }
    reader[ridx].online = 1; //do not check on rc, because error in cardinfo should not be fatal
  }
}

static int reader_get_cardsystem(void)
{
  if (nagra2_card_init(atr, atr_size))		reader[ridx].card_system=SC_NAGRA; else
  if (irdeto_card_init(atr, atr_size))		reader[ridx].card_system=SC_IRDETO; else
  if (conax_card_init(atr, atr_size))		reader[ridx].card_system=SC_CONAX; else
  if (cryptoworks_card_init(atr, atr_size))	reader[ridx].card_system=SC_CRYPTOWORKS; else
  if (seca_card_init(atr, atr_size))	reader[ridx].card_system=SC_SECA; else
  if (viaccess_card_init(atr, atr_size))	reader[ridx].card_system=SC_VIACCESS; else
  if (videoguard_card_init(atr, atr_size))  reader[ridx].card_system=SC_VIDEOGUARD2; else
  if (dre_card_init(atr, atr_size))  reader[ridx].card_system=SC_DRE; else
    cs_ri_log("card system not supported");
  cs_ri_brk(1);

  return(reader[ridx].card_system);
}

static int reader_reset(void)
{
  reader_nullcard();
  if (!reader_activate_card()) return(0);
  return(reader_get_cardsystem());
}

static int reader_card_inserted(void)
{
#ifdef HAVE_PCSC
    if (reader[ridx].typ == R_PCSC) {
        DWORD dwState, dwAtrLen, dwReaderLen;
        BYTE pbAtr[64];
        LONG rv;
        
        dwAtrLen = sizeof(pbAtr);
        
        // this is to take care of the case of a reader being started with no card ... we need something better.
        if (!reader[ridx].pcsc_has_card) {
            rv = SCardConnect(reader[ridx].hContext, &reader[ridx].pcsc_name, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &reader[ridx].hCard, &reader[ridx].dwActiveProtocol);
            if (rv==SCARD_E_NO_SMARTCARD) {
                reader[ridx].pcsc_has_card=0;
                cs_debug("PCSC card in %s removed / absent [dwstate=%lx rv=(%lx)]", reader[ridx].pcsc_name, dwState, rv );
                return 0;
            }
            else if( rv == SCARD_S_SUCCESS ) {
                reader[ridx].pcsc_has_card=1;
            }
            
        }

        rv = SCardStatus(reader[ridx].hCard, NULL, &dwReaderLen, &dwState, &reader[ridx].dwActiveProtocol, pbAtr, &dwAtrLen);
        cs_debug("PCSC rader %s dwstate=%lx rv=(%lx)", reader[ridx].pcsc_name, dwState, rv );

        if(rv==SCARD_E_INVALID_HANDLE){
              SCardEndTransaction(reader[ridx].hCard,SCARD_LEAVE_CARD);
              SCardDisconnect(reader[ridx].hCard,SCARD_LEAVE_CARD);
        }
		 if (rv == SCARD_S_SUCCESS && (dwState & (SCARD_PRESENT | SCARD_NEGOTIABLE | SCARD_POWERED ) )) {
			 cs_debug("PCSC card IS inserted in %s card state [dwstate=%lx rv=(%lx)]", reader[ridx].pcsc_name, dwState,rv);
			 //cs_debug("ATR: %s", cs_hexdump(1, (uchar *)pbAtr, dwAtrLen));
			  return 3;
		 } 
		 else {
			 if ( (rv==SCARD_W_RESET_CARD) && (dwState == 0) ) {
				 cs_debug("PCSC check card reinserted in %s [dwstate=%lx rv=(%lx)]", reader[ridx].pcsc_name, dwState, rv );
			 	 SCardDisconnect(reader[ridx].hCard,SCARD_LEAVE_CARD);
			 	 rv = SCardConnect(reader[ridx].hContext, &reader[ridx].pcsc_name, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &reader[ridx].hCard, &reader[ridx].dwActiveProtocol);
			 	 return  ((rv != SCARD_S_SUCCESS) ? 2 : 0);
			 } else  if ( rv == SCARD_W_REMOVED_CARD && (dwState | SCARD_ABSENT) ) {
				 cs_debug("PCSC card in %s removed / absent [dwstate=%lx rv=(%lx)]", reader[ridx].pcsc_name, dwState, rv );
			 } else {
				 cs_debug("PCSC card inserted FAILURE in %s (%lx) card state (%x) (T=%d)", reader[ridx].pcsc_name, rv, dwState, reader[ridx].dwActiveProtocol);
			 }
			 return 0;
		 }
	}
#endif

  cta_cmd[0]=CTBCS_CLA;
  cta_cmd[1]=CTBCS_INS_STATUS;
  cta_cmd[2]=CTBCS_P1_INTERFACE1;
  cta_cmd[3]=CTBCS_P2_STATUS_ICC;
  cta_cmd[4]=0x00;

  return(reader_chkicc(cta_cmd, 5) ? 0 : cta_res[0]);
}

int reader_device_init(char *device, int typ)
{
#ifdef HAVE_PCSC
	if (reader[ridx].typ == R_PCSC) {
		LONG rv;
		DWORD dwReaders;
		LPSTR mszReaders = NULL;
		char *ptr, **readers = NULL;
		int nbReaders;
		int reader_nb;
		
		cs_debug("PCSC establish context for PCSC reader %s", device);
		rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &reader[ridx].hContext);
		if ( rv == SCARD_S_SUCCESS ) {
			// here we need to list the pcsc readers and get the name from there,
			// the reader[ridx].device should contain the reader number
			// and after the actual device name is copied in reader[ridx].pcsc_name .
			rv = SCardListReaders(reader[ridx].hContext, NULL, NULL, &dwReaders);
			if( rv != SCARD_S_SUCCESS ) {
				cs_debug("PCSC failed listing readers [1] : (%lx)", rv);
				return  0;
			}
			mszReaders = malloc(sizeof(char)*dwReaders);
			if (mszReaders == NULL) {
				cs_debug("PCSC failed malloc");
				return  0;
			}
			rv = SCardListReaders(reader[ridx].hContext, NULL, mszReaders, &dwReaders);
			if( rv != SCARD_S_SUCCESS ) {
				cs_debug("PCSC failed listing readers [2]: (%lx)", rv);
				return  0;
			}
			/* Extract readers from the null separated string and get the total
			 * number of readers */
			nbReaders = 0;
			ptr = mszReaders;
			while (*ptr != '\0') {
				ptr += strlen(ptr)+1;
				nbReaders++;
			}
			
			if (nbReaders == 0) {
				cs_debug("PCSC : no reader found");
				return  0;
			}

			readers = calloc(nbReaders, sizeof(char *));
			if (readers == NULL) {
				cs_debug("PCSC failed malloc");
				return  0;
			}

			/* fill the readers table */
			nbReaders = 0;
			ptr = mszReaders;
			while (*ptr != '\0') {
				cs_debug("%d: %s\n", nbReaders, ptr);
				readers[nbReaders] = ptr;
				ptr += strlen(ptr)+1;
				nbReaders++;
			}
			reader_nb=atoi((const char *)&reader[ridx].device);
			if (reader_nb < 0 || reader_nb >= nbReaders) {
				cs_debug("Wrong reader index: %d\n", reader_nb);
				return  0;
			}
			snprintf(reader[ridx].pcsc_name,sizeof(reader[ridx].pcsc_name),"%s",readers[reader_nb]);
			cs_log("PCSC initializing reader (%s)", &reader[ridx].pcsc_name);
			rv = SCardConnect(reader[ridx].hContext, &reader[ridx].pcsc_name, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &reader[ridx].hCard, &reader[ridx].dwActiveProtocol);
			cs_debug("PCSC initializing result (%lx) protocol (T=%lx)", rv, reader[ridx].dwActiveProtocol );
            if (rv==SCARD_S_SUCCESS) {
                reader[ridx].pcsc_has_card=1;
                return 0;
            }
            else if (rv==SCARD_E_NO_SMARTCARD) {
                reader[ridx].pcsc_has_card=0;
                return 0;
            }
            else {
                reader[ridx].pcsc_has_card=0;
                return 2;
            }
                
		}
		else {
			cs_debug("PCSC failed establish context (%lx)", rv);
			return  0;
		}
	}
#endif
 
  int rc;
  oscam_card_detect=reader[ridx].detect;
  cs_ptyp_orig=cs_ptyp;
  cs_ptyp=D_DEVICE;
  snprintf(oscam_device, sizeof(oscam_device), "%s", device);
  if ((rc=CT_init(1, reader_device_type(device, typ),reader[ridx].typ,reader[ridx].mhz,reader[ridx].cardmhz))!=OK)
    cs_log("Cannot open device: %s", device);
  cs_debug("ct_init on %s: %d", device, rc);
  cs_ptyp=cs_ptyp_orig;
  return((rc!=OK) ? 2 : 0);
}

int reader_checkhealth(void)
{
  if (reader_card_inserted())
  {
    if (!(reader[ridx].card_status & CARD_INSERTED))
    {
      cs_log("card detected");
      reader[ridx].card_status  = CARD_NEED_INIT;
      reader[ridx].card_status = CARD_INSERTED | (reader_reset() ? 0 : CARD_FAILURE);
      if (reader[ridx].card_status & CARD_FAILURE)
      {
        cs_log("card initializing error");
      }
      else
      {
        client[cs_idx].au=ridx;
        reader_card_info();
      }

      int i;
      for( i=1; i<CS_MAXPID; i++ ) {
        if( client[i].pid && client[i].typ=='c' && client[i].usr[0] ) {
          kill(client[i].pid, SIGQUIT);
        }
      }
    }
  }
  else
  {
    if (reader[ridx].card_status & CARD_INSERTED)
    {
      reader_nullcard();
      client[cs_idx].lastemm=0;
      client[cs_idx].lastecm=0;
      client[cs_idx].au=-1;
      extern int io_serial_need_dummy_char;
      io_serial_need_dummy_char=0;
      cs_log("card ejected");
    }
    reader[ridx].card_status=0;
    reader[ridx].online=0;
  }
  return reader[ridx].card_status==CARD_INSERTED;
}

void reader_post_process(void)
{
  // some systems eg. nagra2/3 needs post process after receiving cw from card
  // To save ECM/CW time we added this function after writing ecm answer
  switch(reader[ridx].card_system)
    {
      case SC_NAGRA:
        nagra2_post_process(); break;
      default: break;
    }
}

int reader_ecm(ECM_REQUEST *er)
{
  int rc=-1;
  if( (rc=reader_checkhealth()) )
  {
    if( (reader[ridx].caid[0]>>8)==((er->caid>>8)&0xFF) )
    {
      client[cs_idx].last_srvid=er->srvid;
      client[cs_idx].last_caid=er->caid;
      client[cs_idx].last=time((time_t)0);
      switch(reader[ridx].card_system)
      {
      	case SC_NAGRA:
          rc=(nagra2_do_ecm(er)) ? 1 : 0; break;
        case SC_IRDETO:
          rc=(irdeto_do_ecm(er)) ? 1 : 0; break;
        case SC_CRYPTOWORKS:
          rc=(cryptoworks_do_ecm(er)) ? 1 : 0; break;
        case SC_VIACCESS:
          rc=(viaccess_do_ecm(er)) ? 1 : 0; break;
        case SC_CONAX:
          rc=(conax_do_ecm(er)) ? 1 : 0; break;
        case SC_SECA:
          rc=(seca_do_ecm(er)) ? 1 : 0; break;
        case SC_VIDEOGUARD2:
          rc=(videoguard_do_ecm(er)) ? 1 : 0; break;
	case SC_DRE:
	  rc=(dre_do_ecm(er)) ? 1: 0; break;
        default: rc=0;
      }
    }
    else
      rc=0;
  }
  return(rc);
}

int reader_emm(EMM_PACKET *ep)
{
  int rc=-1;
  if (rc=reader_checkhealth())
  {
    client[cs_idx].last=time((time_t)0);
    if (reader[ridx].b_nano[ep->emm[0]] & 0x02) //should this nano be saved?
    {
      char token[256];
      FILE *fp;

      time_t rawtime;
      time (&rawtime);
      struct tm *timeinfo;
      timeinfo = localtime (&rawtime);	/* to access LOCAL date/time info */
      char buf[80];
      strftime (buf, 80, "%Y%m%d_%H_%M_%S", timeinfo);

      sprintf (token, "%swrite_%s_%s.%s", cs_confdir, (ep->emm[0] == 0x82) ? "UNIQ" : "SHARED", buf, "txt");
      if (!(fp = fopen (token, "w")))
	cs_log ("ERROR: Cannot open EMM.txt file '%s' (errno=%d)\n", token, errno);
      else {
	cs_log ("Succesfully written text EMM to %s.", token);
	int emm_length = ((ep->emm[1] & 0x0f) << 8) | ep->emm[2];
	fprintf (fp, "%s", cs_hexdump (0, ep->emm, emm_length + 3));
	fclose (fp);
      }

      //sprintf (token, "%s%s.%s", cs_confdir, buf,"emm");
      sprintf (token, "%swrite_%s_%s.%s", cs_confdir, (ep->emm[0] == 0x82) ? "UNIQ" : "SHARED", buf, "emm");
      if (!(fp = fopen (token, "wb")))
	cs_log ("ERROR: Cannot open EMM.emm file '%s' (errno=%d)\n", token, errno);
      else {
	cs_log ("Succesfully written binary EMM to %s.", token);
	fwrite (ep, sizeof (*ep), 1, fp);
	fclose (fp);
      }
    }

    if (reader[ridx].b_nano[ep->emm[0]] & 0x01) //should this nano be blcoked?
      return 3;

    switch(reader[ridx].card_system)
    {
      case SC_NAGRA:
        rc=nagra2_do_emm(ep); break;
      case SC_IRDETO:
        rc=irdeto_do_emm(ep); break;
      case SC_CRYPTOWORKS:
        rc=cryptoworks_do_emm(ep); break;
      case SC_VIACCESS:
        rc=viaccess_do_emm(ep); break;
      case SC_CONAX:
        rc=conax_do_emm(ep); break;
      case SC_SECA:
        rc=seca_do_emm(ep); break;
      case SC_VIDEOGUARD2:
        rc=videoguard_do_emm(ep); break;
      case SC_DRE:
	rc=dre_do_emm(ep); break;
      default: rc=0;
    }
  }
  return(rc);
}
