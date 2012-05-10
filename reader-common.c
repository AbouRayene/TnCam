#include "globals.h"
#include "reader-common.h"
#include "csctapi/defines.h"
#include "csctapi/atr.h" 
#include "csctapi/icc_async_exports.h" 
#ifdef AZBOX
#include "csctapi/ifd_azbox.h"
#endif
#ifdef COOL
#include "csctapi/ifd_cool.h"
#endif

#include "csctapi/ifd_sc8in1.h"

#if defined(TUXBOX) && defined(__powerpc__) //dbox2 only
#include "csctapi/mc_global.h"
static int32_t reader_device_type(struct s_reader * reader)
{
  int32_t rc=reader->typ;
  struct stat sb;
  if (reader->typ == R_MOUSE)
  {
      if (!stat(reader->device, &sb))
      {
        if (S_ISCHR(sb.st_mode))
        {
          int32_t dev_major, dev_minor;
          dev_major=major(sb.st_rdev);
          dev_minor=minor(sb.st_rdev);
          if (((dev_major==4) || (dev_major==5)))
            switch(dev_minor & 0x3F)
            {
              case 0: rc=R_DB2COM1; break;
              case 1: rc=R_DB2COM2; break;
            }
          cs_debug_mask(D_READER, "device is major: %d, minor: %d, typ=%d", dev_major, dev_minor, rc);
        }
      }
  }
	reader->typ = rc;
  return(rc);
}
#endif

void reader_nullcard(struct s_reader * reader)
{
  reader->csystem.active=0;
  memset(reader->hexserial, 0   , sizeof(reader->hexserial));
  memset(reader->prid     , 0xFF, sizeof(reader->prid     ));
  reader->caid=0;
  memset(reader->availkeys, 0   , sizeof(reader->availkeys));
  reader->acs=0;
  reader->nprov=0;
}

#ifdef WITH_CARDREADER
int32_t reader_cmd2icc(struct s_reader * reader, const uchar *buf, const int32_t l, uchar * cta_res, uint16_t * p_cta_lr)
{
	int32_t rc;
	*p_cta_lr=CTA_RES_LEN-1; //FIXME not sure whether this one is necessary 
	cs_ddump_mask(D_READER, buf, l, "write to cardreader %s:",reader->label);
	rc=ICC_Async_CardWrite(reader, (uchar *)buf, (uint16_t)l, cta_res, p_cta_lr);
	return rc;
}

#define CMD_LEN 5

int32_t card_write(struct s_reader * reader, const uchar *cmd, const uchar *data, uchar *response, uint16_t * response_length)
{
  uchar buf[260];
  // always copy to be able to be able to use const buffer without changing all code  
  memcpy(buf, cmd, CMD_LEN); 

  if (data) {
    if (cmd[4]) memcpy(buf+CMD_LEN, data, cmd[4]);
    return(reader_cmd2icc(reader, buf, CMD_LEN+cmd[4], response, response_length));
  }
  else
    return(reader_cmd2icc(reader, buf, CMD_LEN, response, response_length));
}
#endif

int32_t check_sct_len(const uchar *data, int32_t off)
{
	int32_t l = SCT_LEN(data);
	if (l+off > MAX_LEN) {
		cs_debug_mask(D_READER, "check_sct_len(): smartcard section too long %d > %d", l, MAX_LEN-off);
		l = -1;
	}
	return(l);
}

#ifdef WITH_CARDREADER
static int32_t reader_card_inserted(struct s_reader * reader)
{
#ifndef USE_GPIO
	if ((reader->detect&0x7f) > 3)
		return 1;
#endif
	int32_t card;
	if (ICC_Async_GetStatus (reader, &card)) {
		cs_log("Error getting status of terminal.");

		reader->fd_error++;
		struct s_client *cl = reader->client;
		if (reader->fd_error>5 && cl) {
			cl->init_done = 0;
			cs_log("WARNING: reader %s was disabled because of too many errors", reader->label);
		}
 
		return 0; //corresponds with no card inside!!
	}
	return (card);
}

static int32_t reader_activate_card(struct s_reader * reader, ATR * atr, uint16_t deprecated)
{
	int32_t i,ret;

	if (reader->card_status != CARD_NEED_INIT)
		return 0;

  /* Activate card */
  for (i=0; i<3; i++) {
		ret = ICC_Async_Activate(reader, atr, deprecated);
		if (!ret)
			break;
		cs_log("Error activating card.");
#ifdef QBOXHD
		if(cfg.enableled == 2) qboxhd_led_blink(QBOXHD_LED_COLOR_MAGENTA,QBOXHD_LED_BLINK_MEDIUM);
#endif
  	cs_sleepms(500);
	}
  if (ret) return(0);

  reader->init_history_pos=0;

//  cs_ri_log("ATR: %s", cs_hexdump(1, atr, atr_size, tmp, sizeof(tmp)));//FIXME
  cs_sleepms(1000);
  return(1);
}

static void do_emm_from_file(struct s_reader * reader)
{
  //now here check whether we have EMM's on file to load and write to card:
  if (reader->emmfile == NULL)
     return;

   //handling emmfile
   char token[256];
   FILE *fp;
	
   if ((reader->emmfile[0] == '/'))
      snprintf (token, sizeof(token), "%s", reader->emmfile); //pathname included
   else
      snprintf (token, sizeof(token), "%s%s", cs_confdir, reader->emmfile); //only file specified, look in confdir for this file

   if (!(fp = fopen (token, "rb"))) {
      cs_log ("ERROR: Cannot open EMM file '%s' (errno=%d %s)\n", token, errno, strerror(errno));
      return;
   }
   EMM_PACKET *eptmp;
   if(!cs_malloc(&eptmp,sizeof(EMM_PACKET), -1)) {
      	fclose (fp);
      	return;
   }

   fread(eptmp, sizeof(EMM_PACKET), 1, fp);
   if (ferror(fp)) {
        cs_log("ERROR: Can't read EMM from file '%s' (errno=%d %s)", token, errno, strerror(errno));
        free(eptmp);
        fclose(fp);
        return;
   }
   fclose (fp);

   eptmp->caid[0] = (reader->caid >> 8) & 0xFF;
   eptmp->caid[1] = reader->caid & 0xFF;
   if (reader->nprov > 0)
      memcpy(eptmp->provid, reader->prid[0], sizeof(eptmp->provid));
   eptmp->l = eptmp->emm[2] + 3;

   struct s_cardsystem *cs = get_cardsystem_by_caid(reader->caid);
   if (cs && cs->get_emm_type && !cs->get_emm_type(eptmp, reader)) {
      cs_debug_mask(D_EMM, "emm skipped, get_emm_type() returns error, reader %s", reader->label);
      free(eptmp);
      return;
   }
   //save old b_nano value
   //clear lsb and lsb+1, so no blocking, and no saving for this nano  
   uint16_t save_s_nano = reader->s_nano;
   uint16_t save_b_nano = reader->b_nano;
   uint32_t save_saveemm = reader->saveemm;

   reader->s_nano = reader->b_nano = 0;
   reader->saveemm = 0;

   int32_t rc = reader_emm(reader, eptmp);
   if (rc == OK)
      cs_log ("EMM from file %s was successful written.", token);
   else
      cs_log ("ERROR: EMM read from file %s NOT processed correctly! (rc=%d)", token, rc);

   //restore old block/save settings
   reader->s_nano = save_s_nano; 
   reader->b_nano = save_b_nano;
   reader->saveemm = save_saveemm;
               
   free(eptmp);
}
#endif

void reader_card_info(struct s_reader * reader)
{
	if ((reader->card_status == CARD_NEED_INIT) || (reader->card_status == CARD_INSERTED)) {
		struct s_client *cl = reader->client;
		if (cl)
			cl->last=time((time_t*)0);

		cs_ri_brk(reader, 0);

		if (reader->csystem.active && reader->csystem.card_info) {
			reader->csystem.card_info(reader);
		}
	}
}

#ifdef WITH_CARDREADER
static int32_t reader_get_cardsystem(struct s_reader * reader, ATR atr)
{
	int32_t i;
	for (i=0; i<CS_MAX_MOD; i++) {
		if (cardsystem[i].card_init) {
			if (cardsystem[i].card_init(reader, atr)) {
				cs_log("found cardsystem %s", (cardsystem[i].desc) ? cardsystem[i].desc : "");
				reader->csystem=cardsystem[i];
				reader->csystem.active=1;
#ifdef QBOXHD
				if(cfg.enableled == 2){
					qboxhd_led_blink(QBOXHD_LED_COLOR_YELLOW,QBOXHD_LED_BLINK_MEDIUM);
					qboxhd_led_blink(QBOXHD_LED_COLOR_GREEN,QBOXHD_LED_BLINK_MEDIUM);
					qboxhd_led_blink(QBOXHD_LED_COLOR_YELLOW,QBOXHD_LED_BLINK_MEDIUM);
					qboxhd_led_blink(QBOXHD_LED_COLOR_GREEN,QBOXHD_LED_BLINK_MEDIUM);
				}
#endif
				break;
			}
		}
	}

	if (reader->csystem.active==0) 
	{
		cs_ri_log(reader, "card system not supported");
#ifdef QBOXHD		
		if(cfg.enableled == 2) qboxhd_led_blink(QBOXHD_LED_COLOR_MAGENTA,QBOXHD_LED_BLINK_MEDIUM);
#endif
	}
	cs_ri_brk(reader, 1);

	return(reader->csystem.active);
}

int32_t reader_reset(struct s_reader * reader)
{
  reader_nullcard(reader);
  ATR atr;
  uint16_t ret = 0;
#ifdef AZBOX
  int32_t i;
  if (reader->typ == R_INTERNAL) {
    if (reader->mode != -1) {
      Azbox_SetMode(reader->mode);
      if (!reader_activate_card(reader, &atr, 0)) return(0);
      ret = reader_get_cardsystem(reader, atr);
    } else {
      for (i = 0; i < AZBOX_MODES; i++) {
        Azbox_SetMode(i);
        if (!reader_activate_card(reader, &atr, 0)) return(0);
        ret = reader_get_cardsystem(reader, atr);
        if (ret)
          break;
      }
    }
  } else {
#endif
  uint16_t deprecated;
	for (deprecated = reader->deprecated; deprecated < 2; deprecated++) {
		if (!reader_activate_card(reader, &atr, deprecated)) break;
		ret = reader_get_cardsystem(reader, atr);
		if (ret)
			break;
		if (!deprecated)
			cs_log("Normal mode failed, reverting to Deprecated Mode");
	}
#ifdef AZBOX
  }
#endif

 if (!ret) 
      {
        reader->card_status = CARD_FAILURE;
        cs_log("card initializing error");
        if (reader->typ == R_SC8in1 && reader->sc8in1_config->mcr_type) {
        	char text[] = {'S', (char)reader->slot+0x30, 'A', 'E', 'R'};
        	MCR_DisplayText(reader, text, 5, 400, 0);
        }
#ifdef QBOXHD 
        if(cfg.enableled == 2) qboxhd_led_blink(QBOXHD_LED_COLOR_MAGENTA,QBOXHD_LED_BLINK_MEDIUM);
#endif
      }
      else
      {
        reader_card_info(reader);
        reader->card_status = CARD_INSERTED;
        do_emm_from_file(reader);
        if (reader->typ == R_SC8in1 && reader->sc8in1_config->mcr_type) {
			char text[] = {'S', (char)reader->slot+0x30, 'A', 'O', 'K'};
			MCR_DisplayText(reader, text, 5, 400, 0);
		}

#ifdef COOL
	if (reader->typ == R_INTERNAL) {
		cs_debug_mask(D_DEVICE,"%s init done - modifying timeout for coolstream internal device %s", reader->label, reader->device);
		call(Cool_Set_Transmit_Timeout(reader, 1));
	}
#endif
      }

	return(ret);
}

int32_t reader_device_init(struct s_reader * reader)
{
	int32_t rc = -1; //FIXME
#if defined(TUXBOX) && defined(__powerpc__)
	struct stat st;
	if (!stat(DEV_MULTICAM, &st))
		reader->typ = reader_device_type(reader);
#endif
	if (ICC_Async_Device_Init(reader))
		cs_log("Cannot open device: %s", reader->device);
	else
		rc = OK;
  return((rc!=OK) ? 2 : 0); //exit code 2 means keep retrying, exit code 0 means all OK
}

int32_t reader_checkhealth(struct s_reader * reader)
{
	struct s_client *cl = reader->client;
	if (reader_card_inserted(reader)) {
		if (reader->card_status == NO_CARD || reader->card_status == UNKNOWN) {
			cs_log("%s card detected", reader->label);
#ifdef QBOXHD
			if(cfg.enableled == 2) qboxhd_led_blink(QBOXHD_LED_COLOR_YELLOW,QBOXHD_LED_BLINK_SLOW);
#endif
			reader->card_status = CARD_NEED_INIT;
			//reader_reset(reader);
			add_job(cl, ACTION_READER_RESET, NULL, 0);
		}
	} else {
		if (reader->card_status == CARD_INSERTED || reader->card_status == CARD_NEED_INIT) {
			reader_nullcard(reader);
			if (cl) {
				cl->lastemm = 0;
				cl->lastecm = 0;
			}
			cs_log("card ejected");
#ifdef QBOXHD 
 			if(cfg.enableled == 2) qboxhd_led_blink(QBOXHD_LED_COLOR_YELLOW,QBOXHD_LED_BLINK_SLOW);
#endif
		}
		reader->card_status = NO_CARD;
	}
	return reader->card_status == CARD_INSERTED;
}
#endif

void reader_post_process(struct s_reader * reader)
{
  // some systems eg. nagra2/3 needs post process after receiving cw from card
  // To save ECM/CW time we added this function after writing ecm answer
	if (reader->csystem.active && reader->csystem.post_process) {
		reader->csystem.post_process(reader);
	}
}

#ifdef WITH_CARDREADER
int32_t reader_ecm(struct s_reader * reader, ECM_REQUEST *er, struct s_ecm_answer *ea)
{
  int32_t rc=-1;
	if( (rc=reader_checkhealth(reader)) ) {
		struct s_client *cl = reader->client;
		if (cl) {
			cl->last_srvid=er->srvid;
			cl->last_caid=er->caid;
			cl->last=time((time_t*)0);
		}

		if (reader->csystem.active && reader->csystem.do_ecm) 
			rc=reader->csystem.do_ecm(reader, er, ea);
		else
			rc=0;
	}
	return(rc);
}
#endif

int32_t reader_get_emm_type(EMM_PACKET *ep, struct s_reader * rdr) //rdr differs from calling reader!
{
	cs_debug_mask(D_EMM, "Entered reader_get_emm_type cardsystem %s", rdr->csystem.desc ? rdr->csystem.desc : "");
	int32_t rc;

	if (rdr->csystem.active && rdr->csystem.get_emm_type) 
		rc=rdr->csystem.get_emm_type(ep, rdr);
	else
		rc=0;

	return rc;
}

struct s_cardsystem *get_cardsystem_by_caid(uint16_t caid) {
	int32_t i,j; 
	for (i=0; i<CS_MAX_MOD; i++) { 
		if (cardsystem[i].caids) { 
			for (j=0;j<2;j++) { 
				if (cardsystem[i].caids[j] == caid)
					return &cardsystem[i];
				if ((cardsystem[i].caids[j]==caid >> 8)) { 
					return &cardsystem[i];
				} 
			} 
		} 
	} 
	return NULL;
} 

#ifdef WITH_CARDREADER
int32_t reader_emm(struct s_reader * reader, EMM_PACKET *ep)
{
  int32_t rc=-1;

  rc=reader_checkhealth(reader);
  if (rc) {
	if ((1<<(ep->emm[0] % 0x80)) & reader->b_nano)
		return 3;

	if (reader->csystem.active && reader->csystem.do_emm) 
		rc=reader->csystem.do_emm(reader, ep);
	else
		rc=0;
  }
  return(rc);
}
#endif

int8_t cs_emmlen_is_blocked(struct s_reader *rdr, int16_t len)
{
	int8_t i;

	for( i = 0; i < CS_MAXEMMBLOCKBYLEN; i++ )
		if(rdr->blockemmbylen[i] == len)
			return 1;

	return 0;
}
