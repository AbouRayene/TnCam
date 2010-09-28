#include "globals.h"
#include "reader-common.h"
#include "reader-videoguard-common.h"


static int vg1_do_cmd(struct s_reader *reader, const unsigned char *ins, const unsigned char *txbuff, unsigned char *rxbuff, unsigned char *cta_res)
{
  ushort cta_lr;
  unsigned char ins2[5];
  memcpy(ins2, ins, 5);
  unsigned char len = 0;
  len = ins2[4];

  unsigned char tmp[264];
  if (!rxbuff) {
    rxbuff = tmp;
  }

  if (txbuff == NULL) {
    if (!write_cmd_vg(ins2, NULL) || !status_ok(cta_res + len)) {
      return -1;
    }
    memcpy(rxbuff, ins2, 5);
    memcpy(rxbuff + 5, cta_res, len);
    memcpy(rxbuff + 5 + len, cta_res + len, 2);
  } else {
    if (!write_cmd_vg(ins2, (uchar *) txbuff) || !status_ok(cta_res)) {
      return -2;
    }
    memcpy(rxbuff, ins2, 5);
    memcpy(rxbuff + 5, txbuff, len);
    memcpy(rxbuff + 5 + len, cta_res, 2);
  }

  return len;
}

static void read_tiers(struct s_reader *reader)
{
  def_resp;
//  const unsigned char ins2a[5] = {  0x48, 0x2a, 0x00, 0x00, 0x00  };
  int l;

//  return; // Not working at present so just do nothing

//  l = vg1_do_cmd(reader, ins2a, NULL, NULL, cta_res);
//  if (l < 0 || !status_ok(cta_res + l))
//  {
//    return;
//  }
  unsigned char ins76[5] = { 0x48, 0x76, 0x00, 0x00, 0x00 };
  ins76[3] = 0x7f;
  ins76[4] = 2;
  if (!write_cmd_vg(ins76, NULL) || !status_ok(cta_res + 2)) {
    return;
  }
  ins76[3] = 0;
  ins76[4] = 0x0a;
  int num = cta_res[1];
  int i;
#ifdef CS_RDR_INIT_HIST
  reader->init_history_pos = 0;	//reset for re-read
  memset(reader->init_history, 0, sizeof(reader->init_history));
#endif
  for (i = 0; i < num; i++) {
    ins76[2] = i;
    l = vg1_do_cmd(reader, ins76, NULL, NULL, cta_res);
    if (l < 0 || !status_ok(cta_res + l)) {
      return;
    }
    if (cta_res[2] == 0 && cta_res[3] == 0) {
      break;
    }
    int y, m, d, H, M, S;
    rev_date_calc(&cta_res[4], &y, &m, &d, &H, &M, &S, reader->card_baseyear);
    unsigned short tier_id = (cta_res[2] << 8) | cta_res[3];
    char *tier_name = get_tiername(tier_id, reader->caid[0]);
    cs_ri_log(reader, "[videoguard1-reader] tier: %04x, expiry date: %04d/%02d/%02d-%02d:%02d:%02d %s", tier_id, y, m, d, H, M, S, tier_name);
  }
}

int videoguard1_card_init(struct s_reader *reader, ATR newatr)
{

  get_hist;
  /* 40 B0 09 4A 50 01 4E 5A */
  if ((hist_size < 7) || (hist[1] != 0xB0) || (hist[3] != 0x4A) || (hist[4] != 0x50)) {
    return ERROR;
  }

  get_atr;
  def_resp;

  /* set information on the card stored in reader-videoguard-common.c */
  set_known_card_info(reader,atr,&atr_size);

  if((reader->ndsversion != NDS1) && ((reader->card_system_version != NDS1) || (reader->ndsversion != NDSAUTO))) {
    /* known ATR and not NDS1
       or unknown ATR and not forced to NDS1
       or known NDS1 ATR and forced to another NDS version
       ... probably not NDS1 */
    return ERROR;
  }

  cs_ri_log(reader, "[videoguard1-reader] type: %s, baseyear: %i", reader->card_desc, reader->card_baseyear);
  if(reader->ndsversion == NDS1){
    cs_log("[videoguard1-reader] forced to NDS1+");
  }

  /* NDS1 Class 48 only cards only need a very basic initialisation
     NDS1 Class 48 only cards do not respond to vg1_do_cmd(ins7416)
     nor do they return list of valid command therefore do not even try
     NDS1 Class 48 only cards need to be told the length as (48, ins, 00, 80, 01) 
     does not return the length */

  int l = 0;
  unsigned char buff[256];

  /* Try to get the boxid from the card, even if BoxID specified in the config file
     also used to check if it is an NDS1 card as the returned information will
     not be encrypted if it is an NDS1 card */

  static const unsigned char ins36[5] = { 0x48, 0x36, 0x00, 0x00, 0x90 };
  unsigned char boxID[4];
  int boxidOK = 0;
  l = vg1_do_cmd(reader, ins36, NULL, buff, cta_res);
  if (buff[7] > 0x0F) {
    cs_log("[videoguard1-reader] class48 ins36: encrypted - therefore not an NDS1 card");
    return ERROR;
  } else {
    /* skipping the initial fixed fields: cmdecho (4) + length (1) + encr/rev++ (4) */
    int i = 9;
    int gotUA = 0;
    while (i < l) {
      if (!gotUA && buff[i] < 0xF0) {	/* then we guess that the next 4 bytes is the UA */
	gotUA = 1;
	i += 4;
      } else {
	switch (buff[i]) {	/* object length vary depending on type */
	case 0x00:		/* padding */
	  {
	    i += 1;
	    break;
	  }
	case 0xEF:		/* card status */
	  {
	    i += 3;
	    break;
	  }
	case 0xD1:
	  {
	    i += 4;
	    break;
	  }
	case 0xDF:		/* next server contact */
	  {
	    i += 5;
	    break;
	  }
	case 0xF3:		/* boxID */
	  {
	    memcpy(&boxID, &buff[i + 1], sizeof(boxID));
	    boxidOK = 1;
	    i += 5;
	    break;
	  }
	case 0xF6:
	  {
	    i += 6;
	    break;
	  }
	case 0xFC:		/* No idea seems to with with NDS1 */
	  {
	    i += 14;
	    break;
	  }
	case 0x01:		/* date & time */
	  {
	    i += 7;
	    break;
	  }
	case 0xFA:
	  {
	    i += 9;
	    break;
	  }
	case 0x5E:
	case 0x67:		/* signature */
	case 0xDE:
	case 0xE2:
	case 0xE9:		/* tier dates */
	case 0xF8:		/* Old PPV Event Record */
	case 0xFD:
	  {
	    i += buff[i + 1] + 2;	/* skip length + 2 bytes (type and length) */
	    break;
	  }
	default:		/* default to assume a length byte */
	  {
	    cs_log("[videoguard1-reader] class48 ins36: returned unknown type=0x%02X - parsing may fail", buff[i]);
	    i += buff[i + 1] + 2;
	  }
	}
      }
    }
  }

  // cs_log("[videguard1nz-reader] calculated BoxID: %02X%02X%02X%02X", boxID[0], boxID[1], boxID[2], boxID[3]);

  /* the boxid is specified in the config */
  if (reader->boxid > 0) {
    int i;
    for (i = 0; i < 4; i++) {
      boxID[i] = (reader->boxid >> (8 * (3 - i))) % 0x100;
    }
    // cs_log("[videguard1nz-reader] config BoxID: %02X%02X%02X%02X", boxID[0], boxID[1], boxID[2], boxID[3]);
  }

  if (!boxidOK) {
    cs_log("[videoguard1-reader] no boxID available");
    return ERROR;
  } 

  // Send BoxID
  static const unsigned char ins4C[5] = { 0x48, 0x4C, 0x00, 0x00, 0x09 };
  unsigned char payload4C[9] = { 0, 0, 0, 0, 3, 0, 0, 0, 4 };
  memcpy(payload4C, boxID, 4);
  if (!write_cmd_vg(ins4C, payload4C) || !status_ok(cta_res + l)) {
    cs_log("[videoguard1-reader] class48 ins4C: sending boxid failed");
    return ERROR;
  }

  static const unsigned char ins58[5] = { 0x48, 0x58, 0x00, 0x00, 0x17 };
  l = vg1_do_cmd(reader, ins58, NULL, buff, cta_res);
  if (l < 0) {
    cs_log("[videoguard1-reader] class48 ins58: failed");
    return ERROR;
  }

  memset(reader->hexserial, 0, 8);
  memcpy(reader->hexserial + 2, cta_res + 1, 4);
  memcpy(reader->sa, cta_res + 1, 3);
  //  reader->caid[0] = cta_res[24] * 0x100 + cta_res[25];
  /* Force caid until can figure out how to get it */
  reader->caid[0] = 0x9 * 0x100 + 0x69;

  /* we have one provider, 0x0000 */
  reader->nprov = 1;
  memset(reader->prid, 0x00, sizeof(reader->prid));

  cs_ri_log(reader,
	    "[videoguard1-reader] type: VideoGuard, caid: %04X, serial: %02X%02X%02X%02X, BoxID: %02X%02X%02X%02X",
	    reader->caid[0], reader->hexserial[2], reader->hexserial[3], reader->hexserial[4], reader->hexserial[5], boxID[0], boxID[1], boxID[2], boxID[3]);
  cs_log("[videoguard1-reader] ready for requests - this is in testing please send -d 255 logs");

  return OK;
}

int videoguard1_do_ecm(struct s_reader *reader, ECM_REQUEST * er)
{
  unsigned char cta_res[CTA_RES_LEN];
  unsigned char ins40[5] = { 0x48, 0x40, 0x00, 0x80, 0xFF };
  static const unsigned char ins54[5] = { 0x48, 0x54, 0x00, 0x00, 0x0D };
  int posECMpart2 = er->ecm[6] + 7;
  int lenECMpart2 = er->ecm[posECMpart2];
  unsigned char tbuff[264];
  unsigned char rbuff[264];
  memcpy(&tbuff[0], &(er->ecm[posECMpart2 + 1]), lenECMpart2 - 1);
  ins40[4] = lenECMpart2;
  int l;
  l = vg1_do_cmd(reader, ins40, tbuff, NULL, cta_res);
  if (l > 0 && status_ok(cta_res)) {
    l = vg1_do_cmd(reader, ins54, NULL, rbuff, cta_res);
    if (l > 0 && status_ok(cta_res + l)) {
      if (!cw_is_valid(rbuff+5,0))    //sky cards report 90 00 = ok but send cw = 00 when channel not subscribed
      {
        cs_log("[videoguard1-reader] class48 ins54 status 90 00 but cw=00 -> channel not subscribed");
        return ERROR;
      }

      if(er->ecm[0]&1) {
        memset(er->cw+0, 0, 8);
        memcpy(er->cw+8, rbuff + 5, 8);
      } else {
        memcpy(er->cw+0, rbuff + 5, 8);
        memset(er->cw+8, 0, 8);
      }
      return OK;
    }
  }
  cs_log("[videoguard1-reader] class48 ins54 (%d) status not ok %02x %02x", l, cta_res[0], cta_res[1]);
  return ERROR;
}

static int num_addr(const unsigned char *data)
{
  return ((data[3] & 0x30) >> 4) + 1;
}

/*
Example of GLOBAL EMM's
This one has IRD-EMM + Card-EMM
82 70 20 00 02 06 02 7D 0E 89 53 71 16 90 14 40
01 ED 17 7D 9E 1F 28 CF 09 97 54 F1 8E 72 06 E7
51 AF F5
This one has only IRD-EMM
82 70 6D 00 07 69 01 30 07 14 5E 0F FF FF 00 06 
00 0D 01 00 03 01 00 00 00 0F 00 00 00 5E 01 00 
01 0C 2E 70 E4 55 B6 D2 34 F7 44 86 9E 5C 91 14
81 FC DF CB D0 86 65 77 DF A9 E1 6B A8 9F 9B DE
90 92 B9 AA 6C B3 4E 87 D2 EC 92 DA FC 71 EF 27 
B3 C3 D0 17 CF 0B D6 5E 8C DB EB B3 37 55 6E 09 
7F 27 3C F1 85 29 C9 4E 0B EE DF 68 BE 00 C9 00
*/
static const unsigned char *payload_addr(uchar emmtype, const unsigned char *data, const unsigned char *a)
{
  int s;
  int l;
  const unsigned char *ptr = NULL;
  int position = -1;
  int numAddrs = 0;
  switch (emmtype) {
  case SHARED:
    {
      s = 3;
      break;
    }
  case UNIQUE:
    {
      s = 4;
      break;
    }
  default:
    {
      s = 0;
    }
  }

  numAddrs = num_addr(data);
  if (s > 0) {
    for (l = 0; l < numAddrs; l++) {
      if (!memcmp(&data[l * 4 + 4], a + 2, s)) {
	position = l;
	break;
      }
    }
  }

  int num_filter = (position == -1) ? 0 : numAddrs;
  /* skip header and the filter list */
  ptr = data + 4 + 4 * num_filter;
  if (*ptr != 0x02 && *ptr != 0x07)	// some clients omit 00 00 separator */
  {
    ptr += 2;			// skip 00 00 separator
    if (*ptr == 0x00)
      ptr++;			// skip optional 00
    ptr++;			// skip the 1st bitmap len
  }

  /* check for IRD-EMM */
  if (*ptr != 0x02 && *ptr != 0x07) {
    return NULL;
  }

  /* skip IRD-EMM part, 02 00 or 02 06 xx aabbccdd yy */
  ptr += 2 + ptr[1];
  /* check for EMM boundaries - ptr should not exceed EMM length */
  if ((int) (ptr - (data + 3)) >= data[2]) {
    return NULL;
  }

  for (l = 0; l < position; l++) {
    /* skip the payload of the previous sub-EMM */
    ptr += 1 + ptr[0];
    /* check for EMM boundaries - ptr should not exceed EMM length */
    if ((int) (ptr - (data + 3)) >= data[2]) {
      return NULL;
    }

    /* skip optional 00 */
    if (*ptr == 0x00) {
      ptr++;
    }

    /* skip the bitmap len */
    ptr++;
    /* check for IRD-EMM */
    if (*ptr != 0x02 && *ptr != 0x07) {
      return NULL;
    }

    /* skip IRD-EMM part, 02 00 or 02 06 xx aabbccdd yy */
    ptr += 2 + ptr[1];
  }

  return ptr;
}

int videoguard1_get_emm_type(EMM_PACKET * ep, struct s_reader *rdr)
{

/*
82 30 ad 70 00 XX XX XX 00 XX XX XX 00 XX XX XX 00 XX XX XX 00 00 
d3 02 00 22 90 20 44 02 4a 50 1d 88 ab 02 ac 79 16 6c df a1 b1 b7 77 00 ba eb 63 b5 c9 a9 30 2b 43 e9 16 a9 d5 14 00 
d3 02 00 22 90 20 44 02 13 e3 40 bd 29 e4 90 97 c3 aa 93 db 8d f5 6b e4 92 dd 00 9b 51 03 c9 3d d0 e2 37 44 d3 bf 00
d3 02 00 22 90 20 44 02 97 79 5d 18 96 5f 3a 67 70 55 bb b9 d2 49 31 bd 18 17 2a e9 6f eb d8 76 ec c3 c9 cc 53 39 00 
d2 02 00 21 90 1f 44 02 99 6d df 36 54 9c 7c 78 1b 21 54 d9 d4 9f c1 80 3c 46 10 76 aa 75 ef d6 82 27 2e 44 7b 00
*/

  int i, pos;
  int serial_count = ((ep->emm[3] >> 4) & 3) + 1;
  int serial_len = (ep->emm[3] & 0x80) ? 3 : 4;
  uchar emmtype = (ep->emm[3] & VG_EMMTYPE_MASK) >> 6;
  pos = 4 + (serial_len * serial_count) + 2;
  switch (emmtype) {
  case VG_EMMTYPE_G:
    {
      ep->type = GLOBAL;
      cs_debug_mask(D_EMM, "VIDEOGUARD1 EMM: GLOBAL");
      return TRUE;
    }
  case VG_EMMTYPE_U:
    {
      cs_debug_mask(D_EMM, "VIDEOGUARD1 EMM: UNIQUE");
      ep->type = UNIQUE;
      if (ep->emm[1] == 0)	// detected UNIQUE EMM from cccam (there is no serial)
      {
	return TRUE;
      }

      for (i = 1; i <= serial_count; i++) {
	if (!memcmp(rdr->hexserial + 2, ep->emm + (serial_len * i), serial_len)) {
	  memcpy(ep->hexserial, ep->emm + (serial_len * i), serial_len);
	  return TRUE;
	}

	pos = pos + ep->emm[pos + 5] + 5;
      }
      return FALSE;		// if UNIQUE but no serial match return FALSE
    }
  case VG_EMMTYPE_S:
    {
      ep->type = SHARED;
      cs_debug_mask(D_EMM, "VIDEOGUARD1 EMM: SHARED");
      return TRUE;		// FIXME: no check for SA
    }
  default:
    {
      if (ep->emm[pos - 2] != 0x00 && ep->emm[pos - 1] != 0x00 && ep->emm[pos - 1] != 0x01) {
	//remote emm without serial
	ep->type = UNKNOWN;
	return TRUE;
      }
      return FALSE;
    }
  }
}

void videoguard1_get_emm_filter(struct s_reader *rdr, uchar * filter)
{
  filter[0] = 0xFF;
  filter[1] = 3;
  //ToDo videoguard1_get_emm_filter basic construction
  filter[2] = UNIQUE;
  filter[3] = 0;
  filter[4 + 0] = 0x82;
  filter[4 + 0 + 16] = 0xFF;
  memcpy(filter + 4 + 2, rdr->hexserial + 2, 4);
  memset(filter + 4 + 2 + 16, 0xFF, 4);
  filter[36] = UNIQUE;
  filter[37] = 0;
  filter[38 + 0] = 0x82;
  filter[38 + 0 + 16] = 0xFF;
  memcpy(filter + 38 + 6, rdr->hexserial + 2, 4);
  memset(filter + 38 + 6 + 16, 0xFF, 4);
  filter[70] = UNIQUE;
  filter[71] = 0;
  filter[72 + 0] = 0x82;
  filter[72 + 0 + 16] = 0xFF;
  memcpy(filter + 72 + 10, rdr->hexserial + 2, 4);
  memset(filter + 72 + 10 + 16, 0xFF, 4);
  /* filter[104]=UNIQUE;
     filter[105]=0;

     filter[106+0]    = 0x82;
     filter[106+0+16] = 0xFF;

     memcpy(filter+106+14, rdr->hexserial+2, 2);
     memset(filter+106+14+16, 0xFF, 2); */
  return;
}

int videoguard1_do_emm(struct s_reader *reader, EMM_PACKET * ep)
{
  unsigned char cta_res[CTA_RES_LEN];
  unsigned char ins42[5] = { 0x48, 0x42, 0x00, 0x00, 0xFF  };
  int rc = ERROR;
  const unsigned char *payload = payload_addr(ep->type, ep->emm, reader->hexserial);
  while (payload) {
    ins42[4] = *payload;
    int l = vg1_do_cmd(reader, ins42, payload + 1, NULL, cta_res);
    if (l > 0 && status_ok(cta_res)) {
      rc = OK;
    }

    cs_debug_mask(D_EMM, "[videoguard1-reader] EMM request return code : %02X%02X", cta_res[0], cta_res[1]);
    //cs_dump(ep->emm, 64, "EMM:");
    if (status_ok(cta_res) && (cta_res[1] & 0x01)) {
      read_tiers(reader);
    }

    if (num_addr(ep->emm) == 1 && (int) (&payload[1] - &ep->emm[0]) + *payload + 1 < ep->l) {
      payload += *payload + 1;
      if (*payload == 0x00) {
	++payload;
      }
      ++payload;
      if (*payload != 0x02) {
	break;
      }
      payload += 2 + payload[1];
    } else {
      payload = 0;
    }

  }

  return (rc);
}

int videoguard1_card_info(struct s_reader *reader)
{
  /* info is displayed in init, or when processing info */
  cs_log("[videoguard1-reader] card detected");
  cs_log("[videoguard1-reader] type: %s", reader->card_desc);
  read_tiers(reader);
  return OK;
}

void reader_videoguard1(struct s_cardsystem *ph) 
{
	ph->do_emm=videoguard1_do_emm;
	ph->do_ecm=videoguard1_do_ecm;
	ph->card_info=videoguard1_card_info;
	ph->card_init=videoguard1_card_init;
	ph->get_emm_type=videoguard1_get_emm_type;
	ph->caids[0]=0x09;
}
