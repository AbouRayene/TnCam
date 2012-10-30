#include "globals.h"

#ifdef WITH_CARDREADER

#include "module-led.h"
#include "oscam-chk.h"
#include "oscam-client.h"
#include "oscam-net.h"
#include "oscam-time.h"
#include "reader-common.h"
#include "csctapi/atr.h"
#include "csctapi/icc_async.h"
#include "csctapi/ifd_azbox.h"
#include "csctapi/ifd_cool.h"

extern struct s_cardsystem cardsystems[CS_MAX_MOD];

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
          rdr_debug_mask(reader, D_READER, "device is major: %d, minor: %d, typ=%d", dev_major, dev_minor, rc);
        }
      }
  }
	reader->typ = rc;
  return(rc);
}

static void reader_nullcard(struct s_reader * reader)
{
  memset(&reader->csystem , 0   , sizeof(reader->csystem));
  memset(reader->hexserial, 0   , sizeof(reader->hexserial));
  memset(reader->prid     , 0xFF, sizeof(reader->prid     ));
  reader->caid=0;
  memset(reader->availkeys, 0   , sizeof(reader->availkeys));
  reader->acs=0;
  reader->nprov=0;
}

int32_t reader_cmd2icc(struct s_reader * reader, const uchar *buf, const int32_t l, uchar * cta_res, uint16_t * p_cta_lr)
{
	int32_t rc;
	*p_cta_lr=CTA_RES_LEN-1; //FIXME not sure whether this one is necessary
	rdr_ddump_mask(reader, D_READER, buf, l, "write to cardreader");
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

static inline int reader_use_gpio(struct s_reader * reader) {
	return reader->use_gpio && reader->detect > 4;
}

static int32_t reader_card_inserted(struct s_reader * reader)
{
	if (!reader_use_gpio(reader) && (reader->detect & 0x7f) > 3)
		return 1;

	int32_t card;
	if (ICC_Async_GetStatus (reader, &card)) {
		rdr_log(reader, "Error getting card status.");
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
		rdr_log(reader, "Error activating card.");
		led_status_card_activation_error();
		cs_sleepms(500);
	}
  if (ret) return(0);

//  rdr_log("ATR: %s", cs_hexdump(1, atr, atr_size, tmp, sizeof(tmp)));//FIXME
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

   if (reader->emmfile[0] == '/')
      snprintf (token, sizeof(token), "%s", reader->emmfile); //pathname included
   else
      snprintf (token, sizeof(token), "%s%s", cs_confdir, reader->emmfile); //only file specified, look in confdir for this file

   if (!(fp = fopen (token, "rb"))) {
      rdr_log(reader, "ERROR: Cannot open EMM file '%s' (errno=%d %s)\n", token, errno, strerror(errno));
      return;
   }
   EMM_PACKET *eptmp;
   if (!cs_malloc(&eptmp, sizeof(EMM_PACKET))) {
      fclose (fp);
      return;
   }

   size_t ret = fread(eptmp, sizeof(EMM_PACKET), 1, fp);
   if (ret < 1 && ferror(fp)) {
        rdr_log(reader, "ERROR: Can't read EMM from file '%s' (errno=%d %s)", token, errno, strerror(errno));
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
      rdr_debug_mask(reader, D_EMM, "emm skipped, get_emm_type() returns error");
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

   int32_t rc = cardreader_do_emm(reader, eptmp);
   if (rc == OK)
      rdr_log(reader, "EMM from file %s was successful written.", token);
   else
      rdr_log(reader, "ERROR: EMM read from file %s NOT processed correctly! (rc=%d)", token, rc);

   //restore old block/save settings
   reader->s_nano = save_s_nano;
   reader->b_nano = save_b_nano;
   reader->saveemm = save_saveemm;

   free(eptmp);
}

void cardreader_get_card_info(struct s_reader *reader)
{
	if ((reader->card_status == CARD_NEED_INIT) || (reader->card_status == CARD_INSERTED)) {
		struct s_client *cl = reader->client;
		if (cl)
			cl->last=time((time_t*)0);

		if (reader->csystem.active && reader->csystem.card_info) {
			reader->csystem.card_info(reader);
		}
	}
}

static int32_t reader_get_cardsystem(struct s_reader * reader, ATR *atr)
{
	int32_t i;
	for (i=0; i<CS_MAX_MOD; i++) {
		if (cardsystems[i].card_init) {
			if (cardsystems[i].card_init(reader, atr)) {
				rdr_log(reader, "found card system %s", cardsystems[i].desc);
				reader->csystem=cardsystems[i];
				reader->csystem.active=1;
				led_status_found_cardsystem();
				break;
			}
		}
	}

	if (reader->csystem.active==0)
	{
		rdr_log(reader, "card system not supported");
		led_status_unsupported_card_system();
	}

	return(reader->csystem.active);
}

void cardreader_do_reset(struct s_reader *reader)
{
  reader_nullcard(reader);
  ATR atr;
  uint16_t ret = 0;
#ifdef WITH_AZBOX
  int32_t i;
  if (reader->typ == R_INTERNAL) {
    if (reader->azbox_mode != -1) {
      Azbox_SetMode(reader, reader->azbox_mode);
      if (!reader_activate_card(reader, &atr, 0)) return;
      ret = reader_get_cardsystem(reader, &atr);
    } else {
      for (i = 0; i < AZBOX_MODES; i++) {
        Azbox_SetMode(reader, i);
        if (!reader_activate_card(reader, &atr, 0)) return;
        ret = reader_get_cardsystem(reader, &atr);
        if (ret)
          break;
      }
    }
  } else {
#endif
  uint16_t deprecated;
	for (deprecated = reader->deprecated; deprecated < 2; deprecated++) {
		if (!reader_activate_card(reader, &atr, deprecated)) break;
		ret = reader_get_cardsystem(reader, &atr);
		if (ret)
			break;
		if (!deprecated)
			rdr_log(reader, "Normal mode failed, reverting to Deprecated Mode");
	}
#ifdef WITH_AZBOX
  }
#endif

 if (!ret)
      {
        reader->card_status = CARD_FAILURE;
        rdr_log(reader, "card initializing error");
		ICC_Async_DisplayMsg(reader, "AER");
		led_status_card_activation_error();
      }
      else
      {
        cardreader_get_card_info(reader);
        reader->card_status = CARD_INSERTED;
        do_emm_from_file(reader);
		ICC_Async_DisplayMsg(reader, "AOK");

#ifdef WITH_COOLAPI
	if (reader->typ == R_INTERNAL) {
		rdr_debug_mask(reader, D_DEVICE, "init done - modifying timeout for coolstream internal device %s", reader->device);
		Cool_Set_Transmit_Timeout(reader, 1);
	}
#endif
      }

	return;
}

static int32_t cardreader_device_init(struct s_reader *reader)
{
	int32_t rc = -1; //FIXME
	struct stat st;
	if (!stat(DEV_MULTICAM, &st))
		reader->typ = reader_device_type(reader);
	if (ICC_Async_Device_Init(reader))
		rdr_log(reader, "Cannot open device: %s", reader->device);
	else
		rc = OK;
  return((rc!=OK) ? 2 : 0); //exit code 2 means keep retrying, exit code 0 means all OK
}

int32_t cardreader_do_checkhealth(struct s_reader * reader)
{
	struct s_client *cl = reader->client;
	if (reader_card_inserted(reader)) {
		if (reader->card_status == NO_CARD || reader->card_status == UNKNOWN) {
			rdr_log(reader, "card detected");
			led_status_card_detected();
			reader->card_status = CARD_NEED_INIT;
			add_job(cl, ACTION_READER_RESET, NULL, 0);
		}
	} else {
		rdr_debug_mask(reader, D_TRACE, "%s: !reader_card_inserted", __func__);
		if (reader->card_status == CARD_INSERTED || reader->card_status == CARD_NEED_INIT) {
			rdr_log(reader, "card ejected");
			reader_nullcard(reader);
			if (cl) {
				cl->lastemm = 0;
				cl->lastecm = 0;
			}
			led_status_card_ejected();
		}
		reader->card_status = NO_CARD;
	}
	rdr_debug_mask(reader, D_TRACE, "%s: reader->card_status = %d, ret = %d", __func__,
		reader->card_status, reader->card_status == CARD_INSERTED);
	return reader->card_status == CARD_INSERTED;
}

// Check for card inserted or card removed on pysical reader
void cardreader_checkhealth(struct s_client *cl, struct s_reader *rdr) {
	if (!rdr || !rdr->enable)
		return;
	add_job(cl, ACTION_READER_CHECK_HEALTH, NULL, 0);
}

void cardreader_reset(struct s_client *cl) {
	add_job(cl, ACTION_READER_RESET, NULL, 0);
}

void cardreader_init_locks(void) {
	ICC_Async_Init_Locks();
}

bool cardreader_init(struct s_reader *reader) {
	struct s_client *client = reader->client;
	client->typ = 'r';
	set_localhost_ip(&client->ip);
	while (cardreader_device_init(reader) == 2) {
		int8_t i = 0;
		do {
			cs_sleepms(2000);
			if (!ll_contains(configured_readers, reader) || !check_client(client) || reader->enable != 1)
				return false;
			i++;
		} while (i < 30);
	}
	if (reader->mhz > 2000) {
		rdr_log(reader, "Reader initialized (device=%s, detect=%s%s, pll max=%.2f Mhz, wanted cardmhz=%.2f Mhz",
			reader->device,
			reader->detect & 0x80 ? "!" : "",
			RDR_CD_TXT[reader->detect & 0x7f],
			(float)reader->mhz /100,
			(float)reader->cardmhz / 100);
	} else {
		rdr_log(reader, "Reader initialized (device=%s, detect=%s%s, mhz=%d, cardmhz=%d)",
			reader->device,
			reader->detect & 0x80 ? "!" : "",
			RDR_CD_TXT[reader->detect & 0x7f],
			reader->mhz,
			reader->cardmhz);
	}
	return true;
}

void cardreader_close(struct s_reader *reader) {
	ICC_Async_Close(reader);
}

void reader_post_process(struct s_reader * reader)
{
  // some systems eg. nagra2/3 needs post process after receiving cw from card
  // To save ECM/CW time we added this function after writing ecm answer
	if (reader->csystem.active && reader->csystem.post_process) {
		reader->csystem.post_process(reader);
	}
}

int32_t cardreader_do_ecm(struct s_reader *reader, ECM_REQUEST *er, struct s_ecm_answer *ea)
{
  int32_t rc=-1;
	if( (rc=cardreader_do_checkhealth(reader)) ) {
		rdr_debug_mask(reader, D_TRACE, "%s: cardreader_do_checkhealth returned rc=%d", __func__, rc);
		struct s_client *cl = reader->client;
		if (cl) {
			cl->last_srvid=er->srvid;
			cl->last_caid=er->caid;
			cl->last=time((time_t*)0);
		}

		if (reader->csystem.active && reader->csystem.do_ecm) {
			rc=reader->csystem.do_ecm(reader, er, ea);
			rdr_debug_mask(reader, D_TRACE, "%s: after csystem.do_ecm rc=%d", __func__, rc);
		} else
			rc=0;
	}
	rdr_debug_mask(reader, D_TRACE, "%s: ret rc=%d", __func__, rc);
	return(rc);
}

int32_t cardreader_do_emm(struct s_reader *reader, EMM_PACKET *ep)
{
  int32_t rc=-1;

  rc=cardreader_do_checkhealth(reader);
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

void cardreader_process_ecm(struct s_reader *reader, struct s_client *cl, ECM_REQUEST *er) {
	if (ecm_ratelimit_check(reader, er, 1) != OK) {
		rdr_debug_mask(reader, D_TRACE, "%s: ratelimit check failed.", __func__);
		return; // reader_mode = 1: checkout ratelimiter in reader mode so srvid can be replaced
	}
	cs_ddump_mask(D_ATR, er->ecm, er->l, "ecm:");

	struct timeb tps, tpe;
	cs_ftime(&tps);

	struct s_ecm_answer ea;
	memset(&ea, 0, sizeof(struct s_ecm_answer));

	int32_t rc = cardreader_do_ecm(reader, er, &ea);
	rdr_debug_mask(reader, D_TRACE, "%s: cardreader_do_ecm returned rc=%d (ERROR=%d)", __func__, rc, ERROR);

	ea.rc = E_FOUND; //default assume found
	ea.rcEx = 0; //no special flag

	if (rc == ERROR) {
		char buf[32];
		rdr_debug_mask(reader, D_TRACE, "Error processing ecm for caid %04X, srvid %04X, servicename: %s",
			er->caid, er->srvid, get_servicename(cl, er->srvid, er->caid, buf));
		ea.rc = E_NOTFOUND;
		ea.rcEx = 0;
		ICC_Async_DisplayMsg(reader, "Eer");
	}

	if (rc == E_CORRUPT) {
		char buf[32];
		rdr_debug_mask(reader, D_TRACE, "Error processing ecm for caid %04X, srvid %04X, servicename: %s",
			er->caid, er->srvid, get_servicename(cl, er->srvid, er->caid, buf));
		ea.rc = E_NOTFOUND;
		ea.rcEx = E2_WRONG_CHKSUM; //flag it as wrong checksum
		memcpy (ea.msglog,"Invalid ecm type for card",25);
	}
	cs_ftime(&tpe);
	cl->lastecm=time((time_t*)0);

	rdr_debug_mask(reader, D_TRACE, "ecm: %04X real time: %ld ms",
		htons(er->checksum), 1000 * (tpe.time - tps.time) + tpe.millitm - tps.millitm);

	write_ecm_answer(reader, er, ea.rc, ea.rcEx, ea.cw, ea.msglog);

	reader_post_process(reader);
}

#endif
