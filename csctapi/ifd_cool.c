/*
 This module provides IFD handling functions for Coolstream internal reader.
*/

#include"../globals.h"

#ifdef CARDREADER_INTERNAL_COOLAPI
#include "../extapi/coolapi.h"
#include "../oscam-string.h"
#include "../oscam-time.h"
#include "atr.h"

#define OK 0
#define ERROR 1

extern int32_t cool_kal_opened;

#define READ_WRITE_TRANSMIT_TIMEOUT				50

struct s_coolstream_reader {
	void      *handle; //device handle for coolstream
	char      cardbuffer[256];
	uint32_t	cardbuflen;
	int32_t		read_write_transmit_timeout;
};

#define specdev() \
 ((struct s_coolstream_reader *)reader->spec_dev)

static int32_t Cool_Init (struct s_reader *reader)
{
	char *device = reader->device;
	int32_t reader_nb = 0;
	// this is to stay compatible with older config.
	if(strlen(device))
		reader_nb=atoi((const char *)device);
	if(reader_nb>1) {
		// there are only 2 readers in the coolstream : 0 or 1
		rdr_log(reader, "Coolstream reader device can only be 0 or 1");
		return 0;
	}
	if (!cs_malloc(&reader->spec_dev, sizeof(struct s_coolstream_reader)))
		return 0;
	if (cnxt_smc_open (&specdev()->handle, &reader_nb, NULL, NULL))
		return 0;

	int32_t ret = cnxt_smc_enable_flow_control(specdev()->handle, 1);
	coolapi_check_error("cnxt_smc_enable_flow_control", ret);

	specdev()->cardbuflen = 0;
	if (reader->cool_timeout_init > 0) {
		rdr_debug_mask(reader, D_DEVICE, "init timeout set to cool_timeout_init = %i", reader->cool_timeout_init);
		specdev()->read_write_transmit_timeout = reader->cool_timeout_init;
	} else {
		rdr_debug_mask(reader, D_DEVICE, "No init timeout specified - using default init timeout (%i). If you encounter any problems while card init try to use the reader parameter cool_timeout_init = 500",
			READ_WRITE_TRANSMIT_TIMEOUT);
		specdev()->read_write_transmit_timeout = READ_WRITE_TRANSMIT_TIMEOUT;
	}
	return OK;
}

static int32_t Cool_FastReset (struct s_reader *reader)
{
	int32_t n = ATR_MAX_SIZE, ret;
	unsigned char buf[ATR_MAX_SIZE];

	//reset card
	ret = cnxt_smc_reset_card (specdev()->handle, ATR_TIMEOUT, NULL, NULL);
	coolapi_check_error("cnxt_smc_reset_card", ret);
	cs_sleepms(50);
	ret = cnxt_smc_get_atr (specdev()->handle, buf, &n);
	coolapi_check_error("cnxt_smc_get_atr", ret);

	return OK;
}

static int32_t Cool_SetClockrate (struct s_reader *reader, int32_t mhz)
{
        uint32_t clk;
        clk = mhz * 10000;
        int32_t ret = cnxt_smc_set_clock_freq (specdev()->handle, clk);
        coolapi_check_error("cnxt_smc_set_clock_freq", ret);
        call (Cool_FastReset(reader));
        rdr_debug_mask(reader, D_DEVICE, "COOL: clock succesfully set to %i", clk);
        return OK;
}

static int32_t Cool_Set_Transmit_Timeout(struct s_reader *reader, uint32_t set)
{
	//set=0 (init), set=1(after init)
	if (set) {
		if (reader->cool_timeout_after_init > 0) {
			specdev()->read_write_transmit_timeout = reader->cool_timeout_after_init;
			rdr_debug_mask(reader, D_DEVICE, "timeout set to cool_timeout_after_init = %i", reader->cool_timeout_after_init);
		} else {
			if (reader->read_timeout > 50) {
				rdr_log(reader, "ATTENTION: The calculated timeout after init value (%i) is greater than 50 which probably leads to a slow card response. We are going to use the reader parameter cool_timeout_after_init = 50.", reader->read_timeout);
				rdr_log(reader, "If you encounter any problems try a higher value. If you have no problems try a value below to get a faster card response.");
				specdev()->read_write_transmit_timeout = 50;
			} else {
				rdr_debug_mask(reader, D_DEVICE, "no timeout specified - using calculated timeout after init (%i)", reader->read_timeout);
				specdev()->read_write_transmit_timeout = reader->read_timeout;
			}
		}
	} else {
		if (reader->cool_timeout_init > 0) {
			specdev()->read_write_transmit_timeout = reader->cool_timeout_init;
		} else {
			specdev()->read_write_transmit_timeout = READ_WRITE_TRANSMIT_TIMEOUT;
		}
	}
	return OK;
}

static int32_t Cool_GetStatus (struct s_reader *reader, int32_t * in)
{
	if (cool_kal_opened) {
		int32_t state;
		int32_t ret = cnxt_smc_get_state(specdev()->handle, &state);
		if (ret) {
			coolapi_check_error("cnxt_smc_get_state", ret);
			return ERROR;
		}
		//state = 0 no card, 1 = not ready, 2 = ready
		if (state)
			*in = 1; //CARD, even if not ready report card is in, or it will never get activated
		else
			*in = 0; //NOCARD
	} else {
		*in = 0;
	}
	return OK;
}

static int32_t Cool_Reset (struct s_reader *reader, ATR * atr)
{
	int32_t ret;

	if (!reader->ins7e11_fast_reset) {
		//set freq to reader->cardmhz if necessary
		uint32_t clk;

		ret = cnxt_smc_get_clock_freq (specdev()->handle, &clk);
		coolapi_check_error("cnxt_smc_get_clock_freq", ret);
		if (clk/10000 != (uint32_t)reader->cardmhz) {
			rdr_debug_mask(reader, D_DEVICE, "COOL: clock freq: %i, scheduling change to %i for card reset",
					clk, reader->cardmhz*10000);
			call (Cool_SetClockrate(reader, reader->cardmhz));
		}
	}
	else {
		rdr_debug_mask(reader, D_DEVICE, "fast reset needed, restoring transmit parameter for coolstream device %s", reader->device);
		call(Cool_Set_Transmit_Timeout(reader, 0));
		rdr_log(reader, "Doing fast reset");
	}

	//reset card
	ret = cnxt_smc_reset_card (specdev()->handle, ATR_TIMEOUT, NULL, NULL);
	coolapi_check_error("cnxt_smc_reset_card", ret);
	cs_sleepms(50);
	int32_t n = ATR_MAX_SIZE;
	unsigned char buf[ATR_MAX_SIZE];
	ret = cnxt_smc_get_atr (specdev()->handle, buf, &n);
	coolapi_check_error("cnxt_smc_get_atr", ret);

	call (!ATR_InitFromArray (atr, buf, n) != ERROR);
	{
		cs_sleepms(50);
		return OK;
	}
}

static int32_t Cool_Transmit (struct s_reader *reader, unsigned char * sent, uint32_t size, uint32_t UNUSED(delay), uint32_t UNUSED(timeout))
{
	specdev()->cardbuflen = 256;//it needs to know max buffer size to respond?

	int32_t ret = cnxt_smc_read_write(specdev()->handle, 0, sent, size, specdev()->cardbuffer, &specdev()->cardbuflen, specdev()->read_write_transmit_timeout, 0);
	coolapi_check_error("cnxt_smc_read_write", ret);

	rdr_ddump_mask(reader, D_DEVICE, sent, size, "COOL Transmit:");
	return OK;
}

static int32_t Cool_Receive (struct s_reader *reader, unsigned char * data, uint32_t size, uint32_t UNUSED(delay), uint32_t UNUSED(timeout))
{
	if (size > specdev()->cardbuflen)
		size = specdev()->cardbuflen; //never read past end of buffer
	memcpy(data, specdev()->cardbuffer, size);
	specdev()->cardbuflen -= size;
	memmove(specdev()->cardbuffer, specdev()->cardbuffer+size, specdev()->cardbuflen);
	rdr_ddump_mask(reader, D_DEVICE, data, size, "COOL Receive:");
	return OK;
}

static int32_t Cool_WriteSettings (struct s_reader *reader, uint32_t UNUSED(BWT), uint32_t UNUSED(CWT), uint32_t UNUSED(EGT), uint32_t UNUSED(BGT))
{
	//this code worked with old cnxt_lnx.ko, but prevented nagra cards from working with new cnxt_lnx.ko
/*	struct
	{
		unsigned short  CardActTime;   //card activation time (in clock cycles = 1/54Mhz)
		unsigned short  CardDeactTime; //card deactivation time (in clock cycles = 1/54Mhz)
		unsigned short  ATRSTime;			//ATR first char timeout in clock cycles (1/f)
		unsigned short  ATRDTime;			//ATR duration in ETU
		unsigned long	  BWT;
		unsigned long   CWT;
		unsigned char   EGT;
		unsigned char   BGT;
	} params;
	params.BWT = BWT;
	params.CWT = CWT;
	params.EGT = EGT;
	params.BGT = BGT;
	call (cnxt_smc_set_config_timeout(specdev()->handle, params));
	rdr_debug_mask(reader, D_DEVICE, "COOL WriteSettings OK");*/

	//set freq back to reader->mhz if necessary
	uint32_t clk;
	int32_t ret = cnxt_smc_get_clock_freq (specdev()->handle, &clk);
	coolapi_check_error("cnxt_smc_get_clock_freq", ret);
	if (clk/10000 != (uint32_t)reader->mhz) {
		rdr_debug_mask(reader, D_DEVICE, "COOL: clock freq: %i, scheduling change to %i", clk, reader->mhz * 10000);
		call (Cool_SetClockrate(reader, reader->mhz));
	}

	return OK;
}

static int32_t Cool_Close (struct s_reader *reader)
{
	if (cool_kal_opened) {
		int32_t ret = cnxt_smc_close (specdev()->handle);
		coolapi_check_error("cnxt_smc_close", ret);
	}
	NULLFREE(reader->spec_dev);
	return OK;
}

static int32_t cool_write_settings2(struct s_reader *reader, uint32_t EGT, uint32_t BGT)
{
	call (Cool_WriteSettings (reader, reader->BWT, reader->CWT, EGT, BGT));
	return OK;
}

static void cool_set_transmit_timeout(struct s_reader *reader)
{
	rdr_debug_mask(reader, D_DEVICE, "init done - modifying timeout for coolstream internal device %s", reader->device);
	Cool_Set_Transmit_Timeout(reader, 1);
}

void cardreader_internal_cool(struct s_cardreader *crdr)
{
	crdr->desc         = "internal";
	crdr->typ          = R_INTERNAL;
	crdr->max_clock_speed = 1;
	crdr->reader_init  = Cool_Init;
	crdr->get_status   = Cool_GetStatus;
	crdr->activate     = Cool_Reset;
	crdr->transmit     = Cool_Transmit;
	crdr->receive      = Cool_Receive;
	crdr->close        = Cool_Close;
	crdr->write_settings2 = cool_write_settings2;
	crdr->set_transmit_timeout = cool_set_transmit_timeout;
}

#endif
