/*
 * Bulcrypt card reader for OSCAM
 * Copyright (C) 2012 Unix Solutions Ltd.
 *
 * Authors: Anton Tinchev (atl@unixsol.org)
 *          Georgi Chorbadzhiyski (gf@unixsol.org)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * =========================================================================
 *
 * For more information read the code and the comments. We have tried to
 * write clear code with lots of comments so it is easy for others to
 * understand what is going on. There are some things marked *FIXME*,
 * that are mostly unknown or not fully understand.
 *
 * WHAT WAS TESTED AND WAS WORKING:
 *   - Cards with bulcrypt v1 ("cherga"/carpet) are working (we have cards
 *     that report CardType: 0x4c and 0x75.
 *   - Cards return valid code words for subscribed channels.
 *      - Tested with channels encrypted with CAID 0x5581 and 0x4aee on
 *         Hellas 39E. Both MPEG2 (SD) and H.264 (SD) channels were decrypted.
 *   - Brand new cards were inited without ever being put into providers STBs.
 *   - AU was working (subscription dates and packages were updated).
 *
 * WHAT WAS NOT TESTED (presumed not working):
 *   - Bulcrypt v2 codeword deobfuscation (we need v2 card).
 *     Bulsatcom do not enable HD packages on v1 cards, v2 cards is rumored
 *     to have different CW obfuscation routine.
 *   - Unfortunately there is no easy to know if you have v1 or v2 card. If
 *     there is a way to detect them please notify us.
 *
 * PERSONAL MESSAGES:
 *   - Many thanks to ilian_71 @ satfriends forum for the protocol info.
 *   - Shouts to yuriks for oscam-ymod, pity it is violating the GPL.
 *
 */

#include "globals.h"

#ifdef READER_BULCRYPT
#include "reader-common.h"

static const uchar atr_carpet[]    = { 0x3b, 0x20, 0x00 };

// *FIXME* We do not know how every 4th byte of the sess_key is calculated.
// Currently they are correct thou and code words checksums are correct are
// the deobfuscation.
static const uchar sess_key[]      = { 0xF2, 0x21, 0xC5, 0x69,
                                       0x28, 0x86, 0xFB, 0x9E,
                                       0xC0, 0x20, 0x28, 0x06,
                                       0xD2, 0x23, 0x72, 0x31 };

static const uchar cmd_set_key[]   = { 0xDE, 0x1C, 0x00, 0x00, 0x0A,
                                       0x12, 0x08,
                                       0x56, 0x47, 0x38, 0x29,
                                       0x10, 0xAF, 0xBE, 0xCD };
// Response: 90 00

static const uchar cmd_cardtype1[] = { 0xDE, 0x16, 0x00, 0x00, 0x00, 0x00 };
static const uchar cmd_cardtype2[] = { 0xDE, 0x1E, 0x00, 0x00, 0x03, 0x00 };
// Response1: 90 03
// Response2: 01 01 4C 90 00 or 01 01 xx 90 00
//   xx - 4C or 75 (Card type)

static const uchar cmd_unkn_0a1[]  = { 0xDE, 0x0A, 0x00, 0x00, 0x00, 0x00 };
static const uchar cmd_unkn_0a2[]  = { 0xDE, 0x1E, 0x00, 0x00, 0x03, 0x00 };
// Response1: 90 03
// Response2: 08 01 00 90 00

static const uchar cmd_cardsn1[]   = { 0xDE, 0x18, 0x00, 0x00, 0x00, 0x00 };
static const uchar cmd_cardsn2[]   = { 0xDE, 0x1E, 0x00, 0x00, 0x06, 0x00 };
// Response1: 90 06
// Response2: 02 04 xx xx xx yy 90 00
//   xx - Card HEX serial
//   yy - Unknown *FIXME*

static const uchar cmd_ascsn1[]    = { 0xDE, 0x1A, 0x00, 0x00, 0x00, 0x00 };
static const uchar cmd_ascsn2[]    = { 0xDE, 0x1E, 0x00, 0x00, 0x0F, 0x00 };
// Response1: 90 0F
// Response2: 05 0D xx xx 20 xx xx xx xx xx xx 20 xx xx xx 90 00
//   xx - Card ASCII serial

static const uchar cmd_ecm_empty[] = { 0xDE, 0x20, 0x00, 0x00, 0x00, 0x00 };
// Response: 90 00

static const uchar cmd_ecm[]       = { 0xDE, 0x20, 0x00, 0x00, 0x4c };
// The last byte is ECM length

static const uchar cmd_ecm_get_cw[]= { 0xDE, 0x1E, 0x00, 0x00, 0x13, 0x00 };
// Response: 0A 11 80 xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx 90 00
//   80 - Returned codeword type? *FIXME*
//   xx - Obfuscated CW

static const uchar cmd_emm_uniq[]  = { 0xDE, 0x02, 0x82, 0x00, 0xb0 };
// Response: 90 00 (EMM written OK) or
// Response: 90 0A (Subscription data was updated)
// The last byte is EMM length (0xb0)

static const uchar cmd_emm[]       = { 0xDE, 0x04, 0x00, 0x00, 0xb0 };
// Response: 90 00 (EMM written OK)
//   cmd_emm[2] = emm_cmd1
//   cmd_emm[3] = emm_cmd2
// The last byte is EMM length (0xb0)

static const uchar cmd_sub_info1[] = { 0xDE, 0x06, 0x00, 0x00, 0x00, 0x00 };
static const uchar cmd_sub_info2[] = { 0xDE, 0x1E, 0x00, 0x00, 0x2B, 0x00 };
// See bulcrypt_card_info() for reponse description

#define LOG_PREFIX "[bulcrypt] "

static int32_t bulcrypt_card_init(struct s_reader *reader, ATR newatr)
{
	int i;
	char tmp[1024];
	char card_serial[16];
	uchar card_type;

	get_atr
	def_resp

	if (memcmp(atr, atr_carpet, MIN(sizeof(atr_carpet), atr_size)) != 0)
	{
		if (atr_size == 3) {
			cs_ri_log(reader, LOG_PREFIX "ATR_len=3 but ATR is unknown: %s",
				cs_hexdump(1, atr, atr_size, tmp, sizeof(tmp)));
		}
		return ERROR;
	}

	cs_ri_log(reader, LOG_PREFIX "Card detected.");

	reader->nprov = 1;
	memset(reader->prid, 0, sizeof(reader->prid));
	memset(reader->hexserial, 0, sizeof(reader->hexserial));
	memset(card_serial, 0, sizeof(card_serial));

	// Set CW obfuscation key
	write_cmd(cmd_set_key, cmd_set_key + 5);
	if (cta_lr < 2 || (cta_res[0] != 0x90 && cta_res[1] != 0x00))
	{
		cs_ri_log(reader, LOG_PREFIX "(cmd_set_key) Unexpected card answer: %s",
			cs_hexdump(1, cta_res, cta_lr, tmp, sizeof(tmp)));
		return ERROR;
	}

	// Read card type
	write_cmd(cmd_cardtype1, NULL);
	write_cmd(cmd_cardtype2, NULL);
	if (cta_lr < 5 || (cta_res[0] != 0x01 && cta_res[1] != 0x01))
	{
		cs_ri_log(reader, LOG_PREFIX "(cmd_cardtype) Unexpected card answer: %s",
			cs_hexdump(1, cta_res, cta_lr, tmp, sizeof(tmp)));
		return ERROR;
	}
	card_type = cta_res[2]; // We have seen 0x4c and 0x75

	// *FIXME* Unknown command
	write_cmd(cmd_unkn_0a1, NULL);
	write_cmd(cmd_unkn_0a2, NULL);

	// Read card HEX serial
	write_cmd(cmd_cardsn1, NULL);
	write_cmd(cmd_cardsn2, NULL);
	if (cta_lr < 6 || (cta_res[0] != 0x02 && cta_res[1] != 0x04))
	{
		cs_ri_log(reader, LOG_PREFIX "(card_sn) Unexpected card answer: %s",
			cs_hexdump(1, cta_res, cta_lr, tmp, sizeof(tmp)));
		return ERROR;
	}
	memcpy(reader->hexserial, cta_res + 2, 3);

	// Read card ASCII serial
	write_cmd(cmd_ascsn1, NULL);
	write_cmd(cmd_ascsn2, NULL);
	if (cta_lr < 15 || (cta_res[0] != 0x05 && cta_res[1] != 0x0d))
	{
		cs_ri_log(reader, LOG_PREFIX "(asc_sn) Unexpected card answer: %s",
			cs_hexdump(1, cta_res, cta_lr, tmp, sizeof(tmp)));
		return ERROR;
	}
	memcpy(card_serial, cta_res + 2, 13);
	cta_lr = strlen(card_serial);
	for (i = 0; i < cta_lr; i++)
	{
		if (card_serial[i] == ' ')
			continue;
		// Sanity check
		if (!isdigit(card_serial[i]))
			card_serial[i] = '*';
	}

	// Write empty ECM, *FIXME* why are we doing this? To prepare the card somehow?
	write_cmd(cmd_ecm_empty, NULL);

	// The HEX serial have nothing to do with Serial (they do not match)
	// *FIXME* is this a problem? Probably not.
	cs_ri_log(reader, LOG_PREFIX "CAID: 0x4AEE|0x5581, CardType: 0x%02x, Serial: %s, HexSerial: %02X %02X %02X",
		card_type,
		card_serial,
		reader->hexserial[0], reader->hexserial[1], reader->hexserial[2]);

	cs_ri_log(reader, LOG_PREFIX "Ready for requests.");

	return OK;
}

static int cw_is_valid(unsigned char *cw)
{
	unsigned int i = 0, cnt = 0;
	do {
		if (cw[i++] == 0)
			cnt++;
	} while(i < 8);

	if (cnt == 8)
	{
		cs_log(LOG_PREFIX "Invalid CW (all zeroes)");
		return ERROR;
	}

	uchar cksum1 = cw[0] + cw[1] + cw[2];
	uchar cksum2 = cw[4] + cw[5] + cw[6];
	if (cksum1 != cw[3] || cksum2 != cw[7])
	{
		if (cksum1 != cw[3])
			cs_log(LOG_PREFIX "Invalid CW (cksum1 mismatch expected 0x%02x got 0x%02x)", cksum1, cw[3]);
		if (cksum2 != cw[7])
			cs_log(LOG_PREFIX "Invalid CW (cksum2 mismatch expected 0x%02x got 0x%02x)", cksum2, cw[7]);
		return ERROR;
	}

	return OK;
}

/*
Bulcrypt ECM structure:

  80 70       - ECM header (80 | 81)
  4c          - ECM length after this field (0x4c == 76 bytes)
  4f 8d 87 0b - unixts == 1334675211 == Tue Apr 17 18:06:51 EEST 2012
  00 66       - *FIXME* Program number?
  00 7d       - *FIXME*
  ce 70       - ECM counter
  0b 88       - ECM type
  xx yy zz .. - Encrypted ECM payload (64 bytes)

*/
static int32_t bulcrypt_do_ecm(struct s_reader * reader, const ECM_REQUEST *er, struct s_ecm_answer *ea)
{
	int i;
	char tmp[512];
	uchar ecm_cmd[256];

	def_resp

	int32_t ecm_len = check_sct_len(er->ecm, 3);
	if (ecm_len < 64 || ecm_len > 188)
	{
		cs_ri_log(reader, LOG_PREFIX "Wrong ECM length: %d", ecm_len);
		return ERROR;
	}

	// CMD: DE 20 00 00 4C
	memcpy(ecm_cmd, cmd_ecm, sizeof(cmd_ecm));
	ecm_cmd[4] = er->ecm[2]; // Set ECM length
	memcpy(ecm_cmd + sizeof(cmd_ecm), er->ecm + 3, ecm_cmd[4]);

	// Send ECM
	write_cmd(ecm_cmd, ecm_cmd + 5);
	if (cta_lr != 2)
	{
		cs_ri_log(reader, LOG_PREFIX "(ecm_cmd) Unexpected card answer: %s",
			cs_hexdump(1, cta_res, cta_lr, tmp, sizeof(tmp)));
		return ERROR;
	}

	if (cta_res[0] == 0x90 && cta_res[1] == 0x03)
	{
		cs_ri_log(reader, LOG_PREFIX "No active subscription.");
		return ERROR;
	}

	if ( !(cta_res[0] == 0x90 && cta_res[1] == 0x13) )
	{
		cs_ri_log(reader, LOG_PREFIX "(ecm_cmd) Unexpected card answer: %s",
			cs_hexdump(1, cta_res, cta_lr, tmp, sizeof(tmp)));
		return ERROR;
	}

	// Call get_cw
	write_cmd(cmd_ecm_get_cw, NULL);

	// cs_ri_log(reader, LOG_PREFIX "CW_LOG: %s", cs_hexdump(1, cta_res, cta_lr, tmp, sizeof(tmp)));
	if (cta_lr < 20 || (cta_res[0] != 0x0a && cta_res[1] != 0x11))
	{
		cs_ri_log(reader, LOG_PREFIX "(get_cw) Unexpected card answer: %s",
			cs_hexdump(1, cta_res, cta_lr, tmp, sizeof(tmp)));
		return ERROR;
	}

	// *FIXME* is the bellow info true?
	//   0x80 (ver 1) is supported
	//   0xc0 (ver 2) is *NOT* supported currently
	if (cta_res[2] == 0xc0)
	{
		cs_ri_log(reader, LOG_PREFIX "Possibly unsupported codeword (bulcrypt v2): %s",
			cs_hexdump(1, cta_res, cta_lr, tmp, sizeof(tmp)));
		// *FIXME* commented for testing, this really should be an error
		//return ERROR;
	}

	// Remove code word obfuscation
	uchar *cw = cta_res + 3;
	for (i = 0 ; i < 16; i++) {
		cw[i] = cw[i] ^ sess_key[i];
	}

	if (er->ecm[0] == 0x81)
	{
		// Even/Odd CWs should be exchanged
		memcpy(ea->cw, cw + 8, 8);
		memcpy(ea->cw + 8, cw, 8);
	} else {
		memcpy(ea->cw, cw, 8);
		memcpy(ea->cw + 8, cw + 8, 8);
	}

	// Check if DCW is valid
	if (!cw_is_valid(ea->cw) || !cw_is_valid(ea->cw + 8))
		return ERROR;

	return OK;
}

#define BULCRYPT_EMM_UNIQUE_82  0x82 // Addressed at single card (updates subscription info)
#define BULCRYPT_EMM_UNIQUE_85  0x85 // Addressed at single card (updates keys?)
#define BULCRYPT_EMM_SHARED_84  0x84 // Addressed to 256 cards
#define BULCRYPT_EMM_8a         0x8a // *FIXME* Another kind of shared EMMs  (they are addressed to 256 cards)
#define BULCRYPT_EMM_8b         0x8b // *FIXME* Possibly GLOBAL EMMs (they are addressed to 65535 cards)
#define BULCRYPT_EMM_FILLER     0x8f // Filler to pad the EMM stream

/*
Bulcrypt EMMs structure

All EMMs are with section length 183 (0xb7)
     3 bytes section header
     7 bytes EMM header
   173 bytes payload

  82 70       - UNUQUE_EMM_82 (updates subscription info)
  b4          - Payload length (0xb4 == 180)
  xx xx xx    - Card HEX SN
  xx          - ???? (0x00, 0x40, 0x80, 0xc0) Not send to the card
  payload

  85 70       - UNIQUE_EMM_85
  b4          - Payload length  (0xb4 == 180)
  xx xx xx    - Card HEX SN
  xx          - EMM Card command
  payload

  84 70       - SHARED_EMM_84
  b4          - Payload length  (0xb4 == 180)
  xx xx       - Card HEX SN Prefix
  xx          - EMM card command
  xx          - EMM card command
  payload

  8a 70       - EMM_8a (we mark these with type = GLOBAL) *FIXME*
  b4          - Payload length  (0xb4 == 180)
  00          - ????
  xx xx       - Card HEX SN Prefix
  xx          - EMM card command
  payload

  8b 70       - EMM_8b (we mark these with type = UNKNOWN) *FIXME*
  b4          - Payload length  (0xb4 == 180)
  00          - ????
  xx          - Card HEX SN Prefix
  xx          - EMM card command
  xx          - EMM card command
  payload

 Padding EMM:
  8f 70 b4 ff ff ff ff ff ff ff ff ff .. .. (ff to the end)

Unique 82 EMMs are used to update subscrition info. Bulsat sends updates once
every hour. All EMMs (except update subscription) are cycling over ~13 minute
cycle.

Stats for EMMs collected for a period of 1 hours and 24 minutes

  2279742 - 82 70 b4 - unique_82
   595309 - 85 70 b4 - unique_85
   199949 - 84 70 b4 - shared_84
    19051 - 8a 70 b4 - emm_8a (global?)
     6417 - 8b 70 b4 - emm_8b (unknown?)
    74850 - 8f 70 b4 - filler

Total EMMs for the period: 3175317
*/

static int32_t bulcrypt_get_emm_type(EMM_PACKET *ep, struct s_reader *reader)
{
	char dump_emm_sn[64], dump_card_sn[64];
	unsigned int emm_len = check_sct_len(ep->emm, 3);
	int32_t ret = FALSE;

	memset(ep->hexserial, 0, 8);

	if (emm_len < 176)
	{
		cs_debug_mask(D_EMM, LOG_PREFIX "(get_emm_type): emm_len < 176 (%u): %s",
			emm_len, cs_hexdump(1, ep->emm, 12, dump_emm_sn, sizeof(dump_emm_sn)));
		ep->type = UNKNOWN;
		return FALSE;
	}

	cs_hexdump(1, reader->hexserial, 3, dump_card_sn, sizeof(dump_card_sn));

#define compare_hex_serial(serial_len) \
	do { \
		cs_hexdump(1, ep->hexserial, serial_len, dump_emm_sn, sizeof(dump_emm_sn)); \
		ret = memcmp(ep->hexserial, reader->hexserial, serial_len) == 0; \
	} while(0)

#define check_serial(serial_len) \
	do { \
		memcpy(ep->hexserial, ep->emm + 3, serial_len); \
		compare_hex_serial(serial_len); \
	} while(0)

#define check_serial_skip_first(serial_len) \
	do { \
		memcpy(ep->hexserial, ep->emm + 4, serial_len); \
		compare_hex_serial(serial_len); \
	} while(0)

	switch (ep->emm[0]) {
	case BULCRYPT_EMM_UNIQUE_82:
	case BULCRYPT_EMM_UNIQUE_85:
		ep->type = UNIQUE;
		check_serial(3);
		if (ret)
			cs_ri_log(reader, LOG_PREFIX "EMM_UNIQUE-%02x-%02x, emm_sn = %s, card_sn = %s",
				ep->emm[0], ep->emm[6], dump_emm_sn, dump_card_sn);
		break;
	case BULCRYPT_EMM_SHARED_84:
		ep->type = SHARED;
		check_serial(2);
		if (ret)
			cs_ri_log(reader, LOG_PREFIX "EMM_SHARED-%02x-%02x-%02x, emm_sn = %s, card_sn = %s",
				ep->emm[0], ep->emm[5], ep->emm[6], dump_emm_sn, dump_card_sn);
		break;
	case BULCRYPT_EMM_8a:
		ep->type = UNKNOWN;
		check_serial_skip_first(2);
		if (ret)
			cs_ri_log(reader, LOG_PREFIX "EMM_UNKNOWN-%02x-%02x-%02x, emm_sn = %s, card_sn = %s",
				ep->emm[0], ep->emm[5], ep->emm[6], dump_emm_sn, dump_card_sn);
		break;
	case BULCRYPT_EMM_8b:
		ep->type = GLOBAL;
		check_serial_skip_first(1);
		if (ret)
			cs_ri_log(reader, LOG_PREFIX "EMM_GLOBAL-%02x-%02x-%02x, emm_sn = %s, card_sn = %s",
				ep->emm[0], ep->emm[5], ep->emm[6], dump_emm_sn, dump_card_sn);
		break;
	case BULCRYPT_EMM_FILLER:
		ep->type = UNKNOWN;
		break;
	default:
		ep->type = UNKNOWN;
		cs_ri_log(reader, LOG_PREFIX "UNKNOWN_EMM len: %u, %s..", emm_len,
			cs_hexdump(1, ep->emm, 12, dump_emm_sn, sizeof(dump_emm_sn)));
		break;
	}

	return ret;
}

static int32_t bulcrypt_do_emm(struct s_reader *reader, EMM_PACKET *ep)
{
	char tmp[512];
	uchar emm_cmd[1024];

	def_resp

	switch (ep->emm[0]) {
	case BULCRYPT_EMM_UNIQUE_82:
		// DE 02 82 00 B0
		memcpy(emm_cmd, cmd_emm_uniq, sizeof(cmd_emm));
		memcpy(emm_cmd + sizeof(cmd_emm), ep->emm + 7, 176);
		break;
	case BULCRYPT_EMM_UNIQUE_85:
	case BULCRYPT_EMM_SHARED_84:
	case BULCRYPT_EMM_8b:
	case BULCRYPT_EMM_8a:
		// DE 04 00 00 B0
		memcpy(emm_cmd, cmd_emm, sizeof(cmd_emm));
		memcpy(emm_cmd + sizeof(cmd_emm), ep->emm + 7, 176);
		emm_cmd[2] = ep->emm[5]; // Emm cmd 1
		emm_cmd[3] = ep->emm[6]; // Emm cmd 2
		break;
	}

	// Write emm
	write_cmd(emm_cmd, emm_cmd + 5);
	if (cta_lr != 2 || cta_res[0] != 0x90 || (cta_res[1] != 0x00 && cta_res[1] != 0x0a))
	{
		cs_ri_log(reader, LOG_PREFIX "(emm_cmd) Unexpected card answer: %s",
			cs_hexdump(1, cta_res, cta_lr, tmp, sizeof(tmp)));
		return ERROR;
	}

	if (ep->emm[0] == BULCRYPT_EMM_UNIQUE_82 && cta_res[0] == 0x90 && cta_res[1] == 0x0a)
		cs_ri_log(reader, LOG_PREFIX "Your subscription data was updated.");

	return OK;
}

static char *dec2bin_str(unsigned int d, char *s)
{
	unsigned int i, r = 8;
	memset(s, 0, 9);
	for (i = 1; i < 256; i <<= 1)
		s[--r] = (d & i) == i ? '+' : '-';
	return s;
}

static int32_t bulcrypt_card_info(struct s_reader *reader)
{
	char tmp[512];
	time_t last_upd_ts, subs_end_ts;
	struct tm tm;
	def_resp

	cs_ri_log(reader, LOG_PREFIX "Reading subscription info.");

	cs_clear_entitlement(reader);

	write_cmd(cmd_sub_info1, NULL);
	write_cmd(cmd_sub_info2, NULL);

	if (cta_lr < 45)
	{
		cs_ri_log(reader, LOG_PREFIX "(info_cmd) Unexpected card answer: %s",
			cs_hexdump(1, cta_res, cta_lr, tmp, sizeof(tmp)));
		return ERROR;
	}

	// Response contains:
	//  13 29 0B
	//  4F 8F 00 E9 - Unix ts set by UNIQUE_EMM_82
	//  3C 65 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 BF
	//  3C 84 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 BF
	//  90 2B

	last_upd_ts = b2i(4, cta_res + 3);
	subs_end_ts = last_upd_ts + (31 * 86400); // *FIXME* this is just a guess

	reader->card_valid_to = subs_end_ts;

	gmtime_r(&last_upd_ts, &tm);
	memset(tmp, 0, sizeof(tmp));
	strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S %Z", &tm);
	cs_ri_log(reader, LOG_PREFIX "Subscription data last update    : %s", tmp);

	gmtime_r(&subs_end_ts, &tm);
	memset(tmp, 0, sizeof(tmp));
	strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S %Z", &tm);
	cs_ri_log(reader, LOG_PREFIX "Subscription should be active to : %s", tmp);

	unsigned int subs1 = b2i(2, cta_res + 3 + 4 + 16);
	unsigned int subs2 = b2i(2, cta_res + 3 + 4 + 16 + 18);

	if (subs1 == 0xffff) {
		cs_ri_log(reader, LOG_PREFIX "No active subscriptions (0x%04x, 0x%04x)", subs1, subs2);
	} else {
		unsigned int i;
		cs_ri_log(reader, LOG_PREFIX "Subscription data 1 (0x%04x): %s",
			subs1, dec2bin_str(subs1, tmp));
		cs_ri_log(reader, LOG_PREFIX "Subscription data 2 (0x%04x): %s",
			subs2, dec2bin_str(subs2, tmp));

		// Configure your tiers to get subscription packets name resolution
// # Example oscam.tiers file
// 5581:0001|Economic
// 5581:0002|Standard
// 5581:0004|Premium
// 5581:0008|HBO
// 5581:0010|Unknown Package 10
// 5581:0020|Unknown Package 20
// 5581:0040|Unknown Package 40
// 5581:0080|Unknown Package 80
		for (i = 1; i < 256; i <<= 1)
		{
			if ((subs1 & i) == i) {
				cs_add_entitlement(reader, 0x4AEE,
					0, /* provid */
					i, /* id  */
					0, /* class */
					last_upd_ts, /* start_ts */
					subs_end_ts, /* end_ts */
					4 /* type: Tier */
				);
				cs_add_entitlement(reader, 0x5581,
					0, /* provid */
					i, /* id  */
					0, /* class */
					last_upd_ts, /* start_ts */
					subs_end_ts, /* end_ts */
					4 /* type: Tier */
				);
				get_tiername(i, 0x4aee, tmp);
				if (tmp[0] == 0x00)
					get_tiername(i, 0x5581, tmp);
				cs_ri_log(reader, LOG_PREFIX "  Package %02x is active: %s", i, tmp);
			}
		}
	}

	return OK;
}

void reader_bulcrypt(struct s_cardsystem *ph)
{
	ph->do_emm			= bulcrypt_do_emm;
	ph->do_ecm			= bulcrypt_do_ecm;
	ph->card_info		= bulcrypt_card_info;
	ph->card_init		= bulcrypt_card_init;
	ph->get_emm_type	= bulcrypt_get_emm_type;
	ph->desc			= "bulcrypt";
	ph->caids[0]		= 0x5581;
	ph->caids[1]		= 0x4aee;
}
#endif
