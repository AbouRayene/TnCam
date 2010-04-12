#include "globals.h"
#include "reader-common.h"
#include "defines.h"
#include "atr.h"
#include "icc_async_exports.h"
#include "csctapi/ifd_sc8in1.h"

static int cs_ptyp_orig; //reinit=1, 

#define SC_IRDETO 1
#define SC_CRYPTOWORKS 2
#define SC_VIACCESS 3
#define SC_CONAX 4
#define SC_SECA 5
#define SC_VIDEOGUARD2 6
#define SC_DRE 7
#define SC_NAGRA 8

#ifdef TUXBOX
static int reader_device_type(struct s_reader * reader)
{
  int rc=reader->typ;
  struct stat sb;
  if (reader->typ == R_MOUSE)
  {
      if (!stat(reader->device, &sb))
      {
        if (S_ISCHR(sb.st_mode))
        {
          int dev_major, dev_minor;
          dev_major=major(sb.st_rdev);
          dev_minor=minor(sb.st_rdev);
          if ((cs_hw==CS_HW_DBOX2) && ((dev_major==4) || (dev_major==5)))
            switch(dev_minor & 0x3F)
            {
              case 0: rc=R_DB2COM1; break;
              case 1: rc=R_DB2COM2; break;
            }
          cs_debug("device is major: %d, minor: %d, typ=%d", dev_major, dev_minor, rc);
        }
      }
  }
	reader->typ = rc;
  return(rc);
}
#endif

static void reader_nullcard(struct s_reader * reader)
{
  reader->card_system=0;
  memset(reader->hexserial, 0   , sizeof(reader->hexserial));
  memset(reader->prid     , 0xFF, sizeof(reader->prid     ));
  memset(reader->caid     , 0   , sizeof(reader->caid     ));
  memset(reader->availkeys, 0   , sizeof(reader->availkeys));
  reader->acs=0;
  reader->nprov=0;
}

int reader_cmd2icc(struct s_reader * reader, uchar *buf, int l, uchar * cta_res, ushort * p_cta_lr)
{
	int rc;
#ifdef HAVE_PCSC
	if (reader->typ == R_PCSC) {
 	  return (pcsc_reader_do_api(reader, buf, cta_res, p_cta_lr,l)); 
	}

#endif

	cs_ddump(buf, l, "write to cardreader %s:",reader->label);
	*p_cta_lr=CTA_RES_LEN-1; //FIXME not sure whether this one is necessary 
	cs_ptyp_orig=cs_ptyp;
	cs_ptyp=D_DEVICE;
	if (reader->typ == R_SC8in1) {
		pthread_mutex_lock(&sc8in1);
		cs_debug("SC8in1: locked for CardWrite of slot %i", reader->slot);
		Sc8in1_Selectslot(reader, reader->slot);
	}
	rc=ICC_Async_CardWrite(reader, buf, (unsigned short)l, cta_res, p_cta_lr);
	if (reader->typ == R_SC8in1) {
		cs_debug("SC8in1: unlocked for CardWrite of slot %i", reader->slot);
		pthread_mutex_unlock(&sc8in1);
	}
	cs_ptyp=cs_ptyp_orig;
	cs_ddump(cta_res, *p_cta_lr, "answer from cardreader %s:", reader->label);
	return rc;
}

#define CMD_LEN 5

int card_write(struct s_reader * reader, uchar *cmd, uchar *data, uchar *response, ushort * response_length)
{
  if (data) {
    uchar buf[256]; //only allocate buffer when its needed
    memcpy(buf, cmd, CMD_LEN);
    if (cmd[4]) memcpy(buf+CMD_LEN, data, cmd[4]);
    return(reader_cmd2icc(reader, buf, CMD_LEN+cmd[4], response, response_length));
  }
  else
    return(reader_cmd2icc(reader, cmd, CMD_LEN, response, response_length));
}

static int reader_card_inserted(struct s_reader * reader)
{
#ifdef HAVE_PCSC
	if (reader->typ == R_PCSC) {
		return(pcsc_check_card_inserted(reader));
	}
#endif
	int card;
	cs_ptyp_orig=cs_ptyp;
	cs_ptyp=D_IFD;
	if (ICC_Async_GetStatus (reader, &card)) {
		cs_log("Error getting status of terminal.");
		return 0; //corresponds with no card inside!!
	}
	cs_ptyp=cs_ptyp_orig;
	return (card);
}

static int reader_activate_card(struct s_reader * reader, ATR * atr, unsigned short deprecated)
{
      int i;
#ifdef HAVE_PCSC
    unsigned char atrarr[64];
    ushort atr_size = 0;
    if (reader->typ == R_PCSC) {
        if (pcsc_activate_card(reader, atrarr, &atr_size))
            return (ATR_InitFromArray (atr, atrarr, atr_size) == ATR_OK);
        else
            return 0;
    }
#endif
	if (!reader_card_inserted(reader))
		return 0;

  /* Activate card */
	cs_ptyp_orig=cs_ptyp;
	cs_ptyp=D_DEVICE;
	if (reader->typ == R_SC8in1) {
		pthread_mutex_lock(&sc8in1);
		cs_debug_mask(D_ATR, "SC8in1: locked for Activation of slot %i", reader->slot);
		Sc8in1_Selectslot(reader, reader->slot);
	}
  for (i=0; i<5; i++) {
		if (!ICC_Async_Activate(reader, atr, deprecated)) {
			i = 100;
			break;
		}
		cs_log("Error activating card.");
  	cs_sleepms(500);
	}
	if (reader->typ == R_SC8in1) {
		cs_debug_mask(D_ATR, "SC8in1: unlocked for Activation of slot %i", reader->slot);
		pthread_mutex_unlock(&sc8in1);
	}
	cs_ptyp=cs_ptyp_orig;
  if (i<100) return(0);

#ifdef CS_RDR_INIT_HIST
  reader->init_history_pos=0;
  memset(reader->init_history, 0, sizeof(reader->init_history));
#endif
//  cs_ri_log("ATR: %s", cs_hexdump(1, atr, atr_size));//FIXME
  cs_sleepms(1000);
  return(1);
}

void do_emm_from_file(struct s_reader * reader)
{
  //now here check whether we have EMM's on file to load and write to card:
  if (reader->emmfile != NULL) {//readnano has something filled in

    //handling emmfile
    char token[256];
    FILE *fp;
    size_t result;
    if ((reader->emmfile[0] == '/'))
      sprintf (token, "%s", reader->emmfile); //pathname included
    else
      sprintf (token, "%s%s", cs_confdir, reader->emmfile); //only file specified, look in confdir for this file
    
    if (!(fp = fopen (token, "rb")))
      cs_log ("ERROR: Cannot open EMM file '%s' (errno=%d)\n", token, errno);
    else {
      EMM_PACKET *eptmp;
      eptmp = malloc (sizeof(EMM_PACKET));
      result = fread (eptmp, sizeof(EMM_PACKET), 1, fp);      
      fclose (fp);

      uchar old_b_nano = reader->b_nano[eptmp->emm[0]]; //save old b_nano value
      reader->b_nano[eptmp->emm[0]] &= 0xfc; //clear lsb and lsb+1, so no blocking, and no saving for this nano      
          
      //if (!reader_do_emm (eptmp))
      if (!reader_emm (reader, eptmp))
        cs_log ("ERROR: EMM read from file %s NOT processed correctly!", token);

      reader->b_nano[eptmp->emm[0]] = old_b_nano; //restore old block/save settings
			free (reader->emmfile);
      reader->emmfile = NULL; //clear emmfile, so no reading anymore

      free(eptmp);
      eptmp = NULL;
    }
  }
}

void reader_card_info(struct s_reader * reader)
{
//  int rc=-1;
  if (reader_checkhealth(reader))
  //if (rc=reader_checkhealth())
  {
    client[cs_idx].last=time((time_t)0);
    cs_ri_brk(reader, 0);
    do_emm_from_file(reader);
    switch(reader->card_system)
    {
      case SC_NAGRA:
        nagra2_card_info(reader); break;
      case SC_IRDETO:
        irdeto_card_info(reader); break;
      case SC_CRYPTOWORKS:
        cryptoworks_card_info(reader); break;
      case SC_VIACCESS:
        viaccess_card_info(reader); break;
      case SC_CONAX:
        conax_card_info(reader); break;
      case SC_VIDEOGUARD2:
        videoguard_card_info(reader); break;
      case SC_SECA:
         seca_card_info(reader); break;
      case SC_DRE:
	 dre_card_info(); break;
    }
  }
}

static int reader_get_cardsystem(struct s_reader * reader, ATR atr)
{
  if (nagra2_card_init(reader, atr))		reader->card_system=SC_NAGRA; else
  if (irdeto_card_init(reader, atr))		reader->card_system=SC_IRDETO; else
  if (conax_card_init(reader, atr))		reader->card_system=SC_CONAX; else
  if (cryptoworks_card_init(reader, atr))	reader->card_system=SC_CRYPTOWORKS; else
  if (seca_card_init(reader, atr))	reader->card_system=SC_SECA; else
  if (viaccess_card_init(reader, atr))	reader->card_system=SC_VIACCESS; else
  if (videoguard_card_init(reader, atr))  reader->card_system=SC_VIDEOGUARD2; else
  if (dre_card_init(reader, atr))  reader->card_system=SC_DRE; else
    cs_ri_log(reader, "card system not supported");
  cs_ri_brk(reader, 1);

  return(reader->card_system);
}

static int reader_reset(struct s_reader * reader)
{
	reader_nullcard(reader);
	ATR atr;
	unsigned short int deprecated, ret = ERROR;
	for (deprecated = reader->deprecated; deprecated < 2; deprecated++) {
		if (!reader_activate_card(reader, &atr, deprecated)) return(0);
		ret =reader_get_cardsystem(reader, atr);
		if (ret)
			break;
		if (!deprecated)
			cs_log("Normal mode failed, reverting to Deprecated Mode");
	}
	return(ret);
}

int reader_device_init(struct s_reader * reader)
{
#ifdef HAVE_PCSC
	if (reader->typ == R_PCSC) {
	   return (pcsc_reader_init(reader, reader->device));
	}
#endif
 
  int rc = -1; //FIXME
  cs_ptyp_orig=cs_ptyp;
  cs_ptyp=D_DEVICE;
#ifdef TUXBOX
	reader->typ = reader_device_type(reader);
#endif
	if (ICC_Async_Device_Init(reader))
		cs_log("Cannot open device: %s", reader->device);
	else
		rc = OK;
  cs_debug("ct_init on %s: %d", reader->device, rc);
  cs_ptyp=cs_ptyp_orig;
  return((rc!=OK) ? 2 : 0);
}

int reader_checkhealth(struct s_reader * reader)
{
  if (reader_card_inserted(reader))
  {
    if (reader->card_status == NO_CARD)
    {
      cs_log("card detected");
      reader->card_status  = CARD_NEED_INIT;
      reader->card_status = (reader_reset(reader) ? CARD_INSERTED : CARD_FAILURE);
      if (reader->card_status == CARD_FAILURE)
      {
        cs_log("card initializing error");
      }
      else
      {
        client[cs_idx].au=reader->ridx;
        reader_card_info(reader);
      }

      int i;
      for( i=1; i<CS_MAXPID; i++ ) {
        if( client[i].pid && client[i].typ=='c' && client[i].usr[0] && ph[client[i].ctyp].type & MOD_CONN_NET) {
          kill(client[i].pid, SIGQUIT);
        }
      }
    }
  }
  else
  {
    if (reader->card_status == CARD_INSERTED)
    {
      reader_nullcard(reader);
      client[cs_idx].lastemm=0;
      client[cs_idx].lastecm=0;
      client[cs_idx].au=-1;
      extern int io_serial_need_dummy_char;
      io_serial_need_dummy_char=0;
      cs_log("card ejected slot = %i", reader->slot);
    }
    reader->card_status=NO_CARD;
  }
  return reader->card_status==CARD_INSERTED;
}

void reader_post_process(struct s_reader * reader)
{
  // some systems eg. nagra2/3 needs post process after receiving cw from card
  // To save ECM/CW time we added this function after writing ecm answer
  switch(reader->card_system)
    {
      case SC_NAGRA:
        nagra2_post_process(reader); break;
      default: break;
    }
}

int reader_ecm(struct s_reader * reader, ECM_REQUEST *er)
{
  int rc=-1, r, m=0;
  static int loadbalanced_idx = 1;
  if( (rc=reader_checkhealth(reader)) )
  {
    //cs_log("OUT: ridx = %d (0x%x), client = 0x%x, lb_idx = %d", ridx, &reader[ridx], &client[cs_idx], loadbalanced_idx);
    if(((reader->caid[0]>>8)==((er->caid>>8)&0xFF)) && (((reader->loadbalanced) && (loadbalanced_idx == reader->ridx)) || !reader->loadbalanced))
    {
      //cs_log("IN: ridx = %d (0x%x), client = 0x%x, lb_idx = %d", ridx, &reader[ridx], &client[cs_idx], loadbalanced_idx);
      client[cs_idx].last_srvid=er->srvid;
      client[cs_idx].last_caid=er->caid;
      client[cs_idx].last=time((time_t)0);
      switch(reader->card_system)
      {
        case SC_NAGRA:
          rc=(nagra2_do_ecm(reader, er)) ? 1 : 0; break;
        case SC_IRDETO:
          rc=(irdeto_do_ecm(reader, er)) ? 1 : 0; break;
        case SC_CRYPTOWORKS:
          rc=(cryptoworks_do_ecm(reader, er)) ? 1 : 0; break;
        case SC_VIACCESS:
          rc=(viaccess_do_ecm(reader, er)) ? 1 : 0; break;
        case SC_CONAX:
          rc=(conax_do_ecm(reader, er)) ? 1 : 0; break;
        case SC_SECA:
          rc=(seca_do_ecm(reader, er)) ? 1 : 0; break;
        case SC_VIDEOGUARD2:
          rc=(videoguard_do_ecm(reader, er)) ? 1 : 0; break;
        case SC_DRE:
          rc=(dre_do_ecm(reader, er)) ? 1: 0; break;
        default:
          rc=0;
      }
    }
    else
      rc=0;
  }
  for (r=0;r<CS_MAXREADER;r++)
    if (reader[r].caid[0]) m++;
  if (loadbalanced_idx++ >= m) loadbalanced_idx = 1;
  return(rc);
}

int reader_get_emm_type(EMM_PACKET *ep, struct s_reader * rdr) //rdr differs from calling reader!
{
	cs_debug_mask(D_EMM,"Entered reader_get_emm_type cardsystem %i",rdr->card_system);
	int rc;
	switch(rdr->card_system) {
    case SC_NAGRA:
      rc=nagra2_get_emm_type(ep, rdr); break;
    case SC_IRDETO:
      rc=irdeto_get_emm_type(ep, rdr); break;
    case SC_CRYPTOWORKS:
      rc=cryptoworks_get_emm_type(ep, rdr); break;
    case SC_VIACCESS:
      rc=viaccess_get_emm_type(ep, rdr); break;
    case SC_CONAX:
      rc=conax_get_emm_type(ep, rdr); break;
    case SC_SECA:
      rc=seca_get_emm_type(ep, rdr); break;
    case SC_VIDEOGUARD2:
      rc=videoguard_get_emm_type(ep, rdr); break;
    case SC_DRE:
      rc=dre_get_emm_type(ep, rdr); break;
    default:
      rc=0;
  }
	return rc;
}

int get_cardsystem(ushort caid) {
	switch(caid >> 8) {
		case 0x01:
			return SC_SECA;
		case 0x05:
			return SC_VIACCESS;
		case 0x06:
			return SC_IRDETO;
		case 0x09:
			return SC_VIDEOGUARD2;
		case 0x0B:
			return SC_CONAX;
		case 0x0D:
			return SC_CRYPTOWORKS;
		case 0x17:
			return SC_IRDETO;
		case 0x18:
			return SC_NAGRA;
		case 0x4A:
			return SC_DRE;
		default: 
			return 0;
	}
}

uchar *get_emm_filter(struct s_reader * rdr, int type) {

	static uint8_t filter[32];
	memset(filter, 0xFF, 32); // should deliver a filter which not produce a flood if cardsystem is not yet implemented.

	switch(rdr->card_system) {
		case SC_NAGRA:
			return nagra2_get_emm_filter(rdr, type);
		case SC_IRDETO:
			return irdeto_get_emm_filter(rdr, type);
		case SC_CRYPTOWORKS:
			return cryptoworks_get_emm_filter(rdr, type);
		case SC_VIACCESS:
			break;
		case SC_CONAX:
			return conax_get_emm_filter(rdr, type);
		case SC_SECA:
			return seca_get_emm_filter(rdr, type);
		case SC_VIDEOGUARD2:
			//return videoguard_get_emm_filter(rdr, type);
			break;
		case SC_DRE:
			return dre_get_emm_filter(rdr, type);
			break;
		default:
			break;
	}

	return filter;
}

int reader_emm(struct s_reader * reader, EMM_PACKET *ep)
{
  int rc=-1;

  rc=reader_checkhealth(reader);
  if (rc)
  {
    client[cs_idx].last=time((time_t)0);
    if (reader->b_nano[ep->emm[0]] & 0x02) //should this nano be saved?
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
      {
        cs_log ("ERROR: Cannot open EMM.txt file '%s' (errno=%d)\n", token, errno);
      }
      else
      {
    	cs_log ("Succesfully written text EMM to %s.", token);
    	int emm_length = ((ep->emm[1] & 0x0f) << 8) | ep->emm[2];
    	fprintf (fp, "%s", cs_hexdump (0, ep->emm, emm_length + 3));
    	fclose (fp);
      }

      //sprintf (token, "%s%s.%s", cs_confdir, buf,"emm");
      sprintf (token, "%swrite_%s_%s.%s", cs_confdir, (ep->emm[0] == 0x82) ? "UNIQ" : "SHARED", buf, "emm");
      if (!(fp = fopen (token, "wb")))
      {
    	cs_log ("ERROR: Cannot open EMM.emm file '%s' (errno=%d)\n", token, errno);
      }
      else 
      {
    	if (fwrite(ep, sizeof (*ep), 1, fp) == 1)
        {
        	cs_log ("Succesfully written binary EMM to %s.", token);
        }
        else
        {
        	cs_log ("ERROR: Cannot write binary EMM to %s (errno=%d)\n", token, errno);
        }
    	fclose (fp);
      }
    }

    if (reader->b_nano[ep->emm[0]] & 0x01) //should this nano be blcoked?
      return 3;

    switch(reader->card_system)
    {
      case SC_NAGRA:
        rc=nagra2_do_emm(reader, ep); break;
      case SC_IRDETO:
        rc=irdeto_do_emm(reader, ep); break;
      case SC_CRYPTOWORKS:
        rc=cryptoworks_do_emm(reader, ep); break;
      case SC_VIACCESS:
        rc=viaccess_do_emm(reader, ep); break;
      case SC_CONAX:
        rc=conax_do_emm(reader, ep); break;
      case SC_SECA:
        rc=seca_do_emm(reader, ep); break;
      case SC_VIDEOGUARD2:
        rc=videoguard_do_emm(reader, ep); break;
      case SC_DRE:
	rc=dre_do_emm(reader, ep); break;
      default: rc=0;
    }
  }
  return(rc);
}
