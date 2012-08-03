/*
    icc_async.c
    Asynchronous ICC's handling functions

    This file is part of the Unix driver for Towitoko smartcard readers
    Copyright (C) 2000 2001 Carlos Prados <cprados@yahoo.com>

    This version is modified by doz21 to work in a special manner ;)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "../globals.h"
#ifdef WITH_CARDREADER
#include "defines.h"
#include "icc_async.h"
#include "mc_global.h"
#include "protocol_t0.h"
#include "io_serial.h"
#include "ifd_cool.h"
#include "ifd_mp35.h"
#include "ifd_phoenix.h"
#include "ifd_sc8in1.h"
#include "ifd_sci.h"
#include "ifd_smartreader.h"
#include "ifd_azbox.h"
#include "ifd_pcsc.h"

// Default T0/T14 settings
#define DEFAULT_WI		10
// Default T1 settings
#define DEFAULT_IFSC	32
#define MAX_IFSC		254  /* Cannot send > 255 buffer */
#define DEFAULT_CWI		13
#define DEFAULT_BWI		4
#define EDC_LRC				0

#define PPS_MAX_LENGTH	6
#define PPS_HAS_PPS1(block)       ((block[1] & 0x10) == 0x10)
#define PPS_HAS_PPS2(block)       ((block[1] & 0x20) == 0x20)
#define PPS_HAS_PPS3(block)       ((block[1] & 0x40) == 0x40)

/*
 * Not exported functions declaration
 */

static void ICC_Async_InvertBuffer (uint32_t size, BYTE * buffer);
static int32_t Parse_ATR (struct s_reader * reader, ATR * atr, uint16_t deprecated);
static int32_t PPS_Exchange (struct s_reader * reader, BYTE * params, uint32_t *length);
static uint32_t PPS_GetLength (BYTE * block);
static int32_t InitCard (struct s_reader * reader, ATR * atr, BYTE FI, double d, double n, uint16_t deprecated);
static uint32_t ETU_to_ms(struct s_reader * reader, uint32_t WWT);
static BYTE PPS_GetPCK (BYTE * block, uint32_t length);
static int32_t SetRightParity (struct s_reader * reader);

/*
 * Exported functions definition
 */

int32_t ICC_Async_Device_Init (struct s_reader *reader)
{
	reader->fdmc=-1;
	rdr_debug_mask(reader, D_IFD, "Opening device %s", reader->device);

	reader->written = 0;

	if (reader->crdr.active==1 && reader->crdr.reader_init) {
		return reader->crdr.reader_init(reader);
	}

	switch(reader->typ) {
		case R_SC8in1:
			cs_writelock(&reader->sc8in1_config->sc8in1_lock);
			if (reader->handle != 0) {//this reader is already initialized
				rdr_debug_mask(reader, D_DEVICE, "%s Sc8in1 already open", __func__);
				cs_writeunlock(&reader->sc8in1_config->sc8in1_lock);
				return OK;
			}

			//get physical device name
			int32_t pos = strlen(reader->device)-2; //this is where : should be located; is also valid length of physical device name
			if (pos <= 0 || reader->device[pos] != 0x3a) //0x3a = ":"
				rdr_log(reader, "ERROR: '%c' detected instead of slot separator `:` at second to last position of device %s", reader->device[pos], reader->device);

			// Check if serial port is open already
			reader->handle = Sc8in1_GetActiveHandle(reader, FALSE);
			if ( ! reader->handle ) {
				rdr_debug_mask(reader, D_DEVICE, "%s opening SC8in1", __func__);
				//open physical device
				char deviceName[128];
				strncpy(deviceName, reader->device, 128);
				deviceName[pos] = 0;
				reader->handle = open (deviceName,  O_RDWR | O_NOCTTY| O_NONBLOCK);
				if (reader->handle < 0) {
					rdr_log(reader, "ERROR: Opening device %s with real device %s (errno=%d %s)", reader->device, deviceName, errno, strerror(errno));
					reader->handle = 0;
					cs_writeunlock(&reader->sc8in1_config->sc8in1_lock);
					return ERROR;
				}
			}
			else {
				// serial port already initialized
				rdr_debug_mask(reader, D_DEVICE, "%s another Sc8in1 already open", __func__);
				cs_writeunlock(&reader->sc8in1_config->sc8in1_lock);
				return OK;
			}
			break;
		case R_MP35:
		case R_MOUSE:
			reader->handle = open (reader->device,  O_RDWR | O_NOCTTY| O_NONBLOCK);
			if (reader->handle < 0) {
				rdr_log(reader, "ERROR: Opening device %s (errno=%d %s)", reader->device, errno, strerror(errno));
				return ERROR;
			}
			break;
		case R_DB2COM1:
		case R_DB2COM2:
			reader->handle = open (reader->device,  O_RDWR | O_NOCTTY| O_SYNC);
			if (reader->handle < 0) {
				rdr_log(reader, "ERROR: Opening device %s (errno=%d %s)", reader->device, errno, strerror(errno));
				return ERROR;
			}
			if ((reader->fdmc = open(DEV_MULTICAM, O_RDWR)) < 0) {
				rdr_log(reader, "ERROR: Opening device %s (errno=%d %s)", DEV_MULTICAM, errno, strerror(errno));
				close(reader->handle);
				return ERROR;
			}
			break;
		case R_SMART:
#if defined(WITH_LIBUSB)
			call (SR_Init(reader));
			break;
#else
			rdr_log(reader, "ERROR: You have specified 'protocol = smartreader' in oscam.server, recompile with libusb support");
			return ERROR;
#endif
		case R_INTERNAL:
#if defined(WITH_COOLAPI)
			return Cool_Init(reader);
#elif defined(WITH_AZBOX)
			return Azbox_Init(reader);
#else
	#if defined(__SH4__) || defined(STB04SCI)
			reader->handle = open (reader->device, O_RDWR|O_NONBLOCK|O_NOCTTY);
	#else
			reader->handle = open (reader->device, O_RDWR|O_NONBLOCK|O_NOCTTY);
	#endif
			if (reader->handle < 0) {
				rdr_log(reader, "ERROR: Opening device %s (errno:%d %s)", reader->device, errno, strerror(errno));
				return ERROR;
			}
#endif
			break;
#ifdef WITH_PCSC
		case R_PCSC:
			return (pcsc_reader_init(reader, reader->device));
			break;
#endif
		default:
			rdr_log(reader, "ERROR: %s: Unknown reader type: %d", __func__, reader->typ);
			return ERROR;
	}

	if (reader->typ == R_MP35)
	{
		if (MP35_Init(reader)) {
				rdr_log(reader, "ERROR: MP35_Init returns error");
				MP35_Close (reader);
				return ERROR;
		}
	}
	else if (reader->typ <= R_MOUSE)
		if (Phoenix_Init(reader)) {
			rdr_log(reader, "ERROR: Phoenix_Init returns error");
			Phoenix_Close (reader);
			cs_writeunlock(&reader->sc8in1_config->sc8in1_lock);
			return ERROR;
		}

	if (reader->typ == R_SC8in1) {
		int32_t ret  = Sc8in1_Init(reader);
		cs_writeunlock(&reader->sc8in1_config->sc8in1_lock);
		if (ret) {
			rdr_log(reader, "ERROR: Sc8in1_Init returns error");
			return ERROR;
		}
	}

	rdr_debug_mask(reader, D_IFD, "Device %s succesfully opened", reader->device);
	return OK;
}

int32_t ICC_Async_Init_Locks (void) {
	// Init device specific locks here, called from init thread
	// before reader threads are running
	struct s_reader *rdr;
	LL_ITER itr = ll_iter_create(configured_readers);
	while((rdr = ll_iter_next(&itr))) {
		if (rdr->typ == R_SC8in1) {
			Sc8in1_InitLocks(rdr);
		}
	}
	return OK;
}

int32_t ICC_Async_GetStatus (struct s_reader *reader, int32_t * card)
{
	int32_t in=0;

	if (reader->crdr.active==1 && reader->crdr.get_status) {
		call(reader->crdr.get_status(reader, &in));

		if (in)
			*card = TRUE;
		else
			*card = FALSE;

		return OK;
	}

	switch(reader->typ) {
		case R_DB2COM1:
		case R_DB2COM2:
			{
			uint16_t msr=1;
			IO_Serial_Ioctl_Lock(reader, 1);
			ioctl(reader->fdmc, GET_PCDAT, &msr);
			if (reader->typ == R_DB2COM2)
				in=(!(msr & 1));
			else
				in=((msr & 0x0f00) == 0x0f00);
			IO_Serial_Ioctl_Lock(reader, 0);
			}
			break;
		case R_SC8in1:
			cs_writelock(&reader->sc8in1_config->sc8in1_lock);
			int32_t ret = Sc8in1_GetStatus(reader, &in);
			cs_writeunlock(&reader->sc8in1_config->sc8in1_lock);
			if (ret == ERROR) return ERROR;
			break;
		case R_MP35:
		case R_MOUSE:
			call (Phoenix_GetStatus(reader, &in));
			break;
#if defined(WITH_LIBUSB)
		case R_SMART:
			call (SR_GetStatus(reader, &in));
			break;
#endif
		case R_INTERNAL:
#if defined(WITH_COOLAPI)
			call (Cool_GetStatus(reader, &in));
#elif defined(WITH_AZBOX)
			call(Azbox_GetStatus(reader, &in));
#else
			call(Sci_GetStatus(reader, &in));
#endif
			break;
#ifdef WITH_PCSC
		case R_PCSC:
			in =  pcsc_check_card_inserted(reader);
			break;
#endif
		default:
			rdr_log(reader, "ERROR: %s: Unknown reader type: %d", __func__, reader->typ);
			return ERROR;
	}

  if (in)
		*card = TRUE;
	else
		*card = FALSE;

	return OK;
}

int32_t ICC_Async_Activate (struct s_reader *reader, ATR * atr, uint16_t deprecated)
{
	rdr_debug_mask(reader, D_IFD, "Activating card");

	reader->current_baudrate = DEFAULT_BAUDRATE; //this is needed for all readers to calculate work_etu for timings

	if (reader->atr[0] != 0 && !reader->ins7e11_fast_reset) {
		rdr_log(reader, "Using ATR from reader config");
		ATR_InitFromArray(atr, reader->atr, ATR_MAX_SIZE);
	}
	else {
		if (reader->crdr.active==1 && reader->crdr.activate) {
			call(reader->crdr.activate(reader, atr));
		} else {

		switch(reader->typ) {
			case R_MP35:
			case R_DB2COM1:
			case R_DB2COM2:
			case R_SC8in1:
			case R_MOUSE:
				LOCK_SC8IN1
				int32_t retval = Phoenix_Reset(reader, atr);
				UNLOCK_SC8IN1
				if (retval) {
					rdr_debug_mask(reader, D_TRACE, "ERROR: Phoenix_Reset returns error");
					return ERROR;
				}
				break;
#if defined(WITH_LIBUSB)
			case R_SMART:
				if ( ! reader->ins7e11_fast_reset) {
					call (SR_Reset(reader, atr));
				}
				else {
					rdr_log(reader, "Doing fast reset");
					call (SR_FastReset_With_ATR(reader, atr));
				}
				break;
#endif
			case R_INTERNAL:
#if defined(WITH_COOLAPI)
				if ( ! reader->ins7e11_fast_reset) {
					call (Cool_Reset(reader, atr));
				}
				else {
					rdr_debug_mask(reader, D_DEVICE, "fast reset needed, restoring transmit parameter for coolstream device %s", reader->device);
					call(Cool_Set_Transmit_Timeout(reader, 0));
					rdr_log(reader, "Doing fast reset");
					call (Cool_FastReset_With_ATR(reader, atr));
				}
#elif defined(WITH_AZBOX)
				call (Azbox_Reset(reader, atr));
#else
				call (Sci_Activate(reader));
				call (Sci_Reset(reader, atr));
#endif
				break;
#ifdef WITH_PCSC
			case R_PCSC:
				 {
					unsigned char atrarr[ATR_MAX_SIZE];
					uint16_t atr_size = 0;
					if (pcsc_activate_card(reader, atrarr, &atr_size))
					{
						if (ATR_InitFromArray (atr, atrarr, atr_size) == ATR_OK)
							return OK;
						else
							return ERROR;
					}
					else
						return ERROR;
				 }
				break;
#endif
			default:
				rdr_log(reader, "ERROR: %s: Unknown reader type: %d", __func__, reader->typ);
				return ERROR;
		}
		}
	}

	unsigned char atrarr[ATR_MAX_SIZE];
	uint32_t atr_size;
	ATR_GetRaw(atr, atrarr, &atr_size);
	char tmp[atr_size*3+1];
	rdr_log(reader, "ATR: %s", cs_hexdump(1, atrarr, atr_size, tmp, sizeof(tmp)));
	memcpy(reader->card_atr, atrarr, atr_size);
	reader->card_atr_length = atr_size;

	/* Get ICC reader->convention */
	if (ATR_GetConvention (atr, &(reader->convention)) != ATR_OK) {
		rdr_log(reader, "ERROR: Could not read reader->convention");
		reader->convention = 0;
		reader->protocol_type = 0;
		return ERROR;
	}

	reader->protocol_type = ATR_PROTOCOL_TYPE_T0;

	LOCK_SC8IN1;
	int32_t ret = Parse_ATR(reader, atr, deprecated);
	UNLOCK_SC8IN1; //Parse_ATR and InitCard need to be included in lock because they change parity of serial port
	if (ret)
		rdr_log(reader, "ERROR: Parse_ATR returned error");
	if (ret)
		return ERROR;
	rdr_debug_mask(reader, D_IFD, "Card succesfully activated");

	return OK;
}

int32_t ICC_Async_CardWrite (struct s_reader *reader, unsigned char *command, uint16_t command_len, unsigned char *rsp, uint16_t *lr)
{
#ifdef WITH_PCSC
	if (reader->typ == R_PCSC)
 	  return (pcsc_reader_do_api(reader, command, rsp, lr, command_len));
#endif
	*lr = 0; //will be returned in case of error

	int32_t ret;

	LOCK_SC8IN1;

	int32_t try = 1;
	uint16_t type = 0;
	do {
	 switch (reader->protocol_type) {
		if (try > 1)
			rdr_log(reader, "Warning: needed try nr %i, next ECM has some delay", try);
		case ATR_PROTOCOL_TYPE_T0:
			ret = Protocol_T0_Command (reader, command, command_len, rsp, lr);
			type = 0;
			break;
		case ATR_PROTOCOL_TYPE_T1:
			ret = Protocol_T1_Command (reader, command, command_len, rsp, lr);
			type = 1;
			if (ret != OK) {
				//try to resync
				unsigned char resync[] = { 0x21, 0xC0, 0x00, 0xE1 };
				ret = Protocol_T1_Command (reader, resync, sizeof(resync), rsp, lr);
				if (ret == OK) {
					reader->ifsc = DEFAULT_IFSC;
					rdr_log(reader, "T1 Resync command succesfull new ifsc = %i", reader->ifsc);
				}
				else {
					rdr_log(reader, "T1 Resync command error, trying to reactivate!");
					ATR atr;
					ICC_Async_Activate(reader, &atr, reader->deprecated);
					return ERROR;
				}
			}
			break;
		case ATR_PROTOCOL_TYPE_T14:
			ret = Protocol_T14_ExchangeTPDU (reader, command, command_len, rsp, lr);
			type = 14;
			break;
		default:
			rdr_log(reader, "ERROR: Unknown protocol type %i", reader->protocol_type);
			type = 99; // use 99 for unknown.
			ret = ERROR;
	 }
	try++;
	} while ((try < 3) && (ret != OK)); //always do one retry when failing

	UNLOCK_SC8IN1;

	if (ret) {
		rdr_debug_mask(reader, D_TRACE, "ERROR: Protocol_T%d_Command returns error", type);
		return ERROR;
	}

	rdr_ddump_mask(reader, D_READER, rsp, *lr, "Answer from cardreader:");
	return OK;
}

int32_t ICC_Async_SetTimings (struct s_reader * reader, uint32_t wait_etu)
{
	reader->read_timeout = ETU_to_ms(reader, wait_etu);
	rdr_debug_mask(reader, D_IFD, "Setting timeout to %i ETU (%d ms)", wait_etu, reader->read_timeout);
	return OK;
}

int32_t ICC_Async_Transmit (struct s_reader *reader, uint32_t size, BYTE * data)
{
	int32_t ret;
	rdr_ddump_mask(reader, D_IFD, data, size, "Transmit:");
	BYTE *buffer = NULL, *sent;

	if (reader->crdr.active==1) {
		if (reader->convention == ATR_CONVENTION_INVERSE && reader->crdr.need_inverse==1) {
            if(!cs_malloc(&buffer,sizeof (BYTE)*size,-1)) return -1;
			memcpy (buffer, data, size);
			ICC_Async_InvertBuffer (size, buffer);
			sent = buffer;
		} else
			sent = data;

		call(reader->crdr.transmit(reader, sent, size));
		if (buffer)
			free (buffer);
		rdr_debug_mask(reader, D_IFD, "Transmit succesful");
		return OK;
	}

	if (reader->convention == ATR_CONVENTION_INVERSE && reader->typ <= R_MOUSE) {
		buffer = (BYTE *) calloc(sizeof (BYTE), size);
		memcpy (buffer, data, size);
		ICC_Async_InvertBuffer (size, buffer);
		sent = buffer;
	}
	else
		sent = data;

	switch(reader->typ) {
		case R_MP35:
		case R_DB2COM1:
		case R_DB2COM2:
		case R_SC8in1:
		case R_MOUSE:
			ret = Phoenix_Transmit (reader, sent, size, reader->block_delay, reader->char_delay);
			break;
#if defined(WITH_LIBUSB)
		case R_SMART:
			ret = SR_Transmit(reader, sent, size);
			break;
#endif
		case R_INTERNAL:
#if defined(WITH_COOLAPI)
			ret = Cool_Transmit(reader, sent, size);
#elif defined(WITH_AZBOX)
			ret = Azbox_Transmit(reader, sent, size);
#else
			ret = Phoenix_Transmit (reader, sent, size, 0, 0); //the internal reader will provide the delay
#endif
			break;
		default:
			rdr_log(reader, "ERROR: %s: Unknown reader type: %d", __func__, reader->typ);
			return ERROR;
	}

	if (ret) rdr_debug_mask(reader, D_IFD, "Transmit error!");
	else rdr_debug_mask(reader, D_IFD, "Transmit succesful"); 
	if (buffer)	free (buffer);
	return ret;
}

int32_t ICC_Async_Receive (struct s_reader *reader, uint32_t size, BYTE * data)
{

	int32_t ret;
	if (reader->crdr.active==1) {
		call(reader->crdr.receive(reader, data, size));

		if (reader->convention == ATR_CONVENTION_INVERSE && reader->crdr.need_inverse==1)
			ICC_Async_InvertBuffer (size, data);

		rdr_ddump_mask(reader, D_IFD, data, size, "Received:");
		return OK;
	}

	switch(reader->typ) {
		case R_MP35:
		case R_DB2COM1:
		case R_DB2COM2:
		case R_SC8in1:
		case R_MOUSE:
			ret = Phoenix_Receive (reader, data, size, reader->read_timeout);
			break;
#if defined(WITH_LIBUSB)
		case R_SMART:
			ret = SR_Receive(reader, data, size);
			break;
#endif
		case R_INTERNAL:
#if defined(WITH_COOLAPI)
			ret = Cool_Receive(reader, data, size);
#elif defined(WITH_AZBOX)
			ret = Azbox_Receive(reader, data, size);
#else
			ret = Phoenix_Receive (reader, data, size, reader->read_timeout);
#endif
			break;
		default:
			rdr_log(reader, "ERROR: %s: Unknown reader type: %d", __func__, reader->typ);
			return ERROR;
	}

	if (reader->convention == ATR_CONVENTION_INVERSE && reader->typ <= R_MOUSE)
		ICC_Async_InvertBuffer (size, data);
	
	if (ret) rdr_debug_mask(reader, D_IFD, "Receive error!");
	else rdr_ddump_mask(reader, D_IFD, data, size, "Received:");
	return ret;
}

int32_t ICC_Async_Close (struct s_reader *reader)
{
	rdr_debug_mask(reader, D_IFD, "Closing device %s", reader->device);

	if (reader->crdr.active && reader->crdr.close) {
		call(reader->crdr.close(reader));
		rdr_debug_mask(reader, D_IFD, "Device %s succesfully closed", reader->device);
		return OK;
	}

	switch(reader->typ) {
		case R_MP35:
			call (MP35_Close(reader));
			break;
		case R_DB2COM1:
		case R_DB2COM2:
		case R_SC8in1:
			cs_writelock(&reader->sc8in1_config->sc8in1_lock);
			int ret = Sc8in1_Close(reader);
			cs_writeunlock(&reader->sc8in1_config->sc8in1_lock);
			if (ret) {
				return 1;
			}
			break;
		case R_MOUSE:
			call (Phoenix_Close(reader));
			break;
#if defined(WITH_LIBUSB)
		case R_SMART:
			call (SR_Close(reader));
			break;
#endif
		case R_INTERNAL:
#if defined(WITH_COOLAPI)
			call (Cool_Close(reader));
#elif defined(WITH_AZBOX)
			call (Azbox_Close(reader));
#else
			/* Dectivate ICC */
			Sci_Deactivate(reader);
			call (Phoenix_Close(reader));
#endif
			break;
#ifdef WITH_PCSC
		case R_PCSC:
			pcsc_close(reader);
			break;
#endif
		default:
			rdr_log(reader, "ERROR: %s: Unknown reader type: %d", __func__, reader->typ);
			return ERROR;
	}

	rdr_debug_mask(reader, D_IFD, "Device %s succesfully closed", reader->device);
	return OK;
}

static uint32_t ICC_Async_GetClockRate (int32_t cardmhz)
{
	switch (cardmhz) {
		case 357:
		case 358:
	  	return (372L * 9600L);
		case 368:
	  	return (384L * 9600L);
		default:
 	  	return cardmhz * 10000L;
	}
}

static int32_t ICC_Async_GetPLL_Divider (struct s_reader * reader)
{
	if(reader->divider != 0) return reader->divider;

	if(reader->mhz != 8300) /* Check dreambox is not DM7025 */ {
		float divider;

		divider = ((float) reader->mhz) / ((float) reader->cardmhz);
		reader->divider = (int32_t) divider;
		if(divider > reader->divider) reader->divider++; /* to prevent over clocking, ceil (round up) the divider */

		rdr_debug_mask(reader, D_DEVICE,"PLL maxmhz = %.2f, wanted cardmhz = %.2f, divider used = %d, actualcardclock=%.2f", (float) reader->mhz/100, (float) reader->cardmhz/100,
			reader->divider, (float) reader->mhz/reader->divider/100);
	}
	else /* STB is DM7025 */ {
		int32_t i, dm7025_clock_freq[] = {518, 461, 395, 360, 319, 296, 267, 244, 230, 212, 197},
			dm7025_PLL_setting[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}, t_cardmhz = reader->cardmhz;

		for(i = 0; i < 11; i++)
			if(t_cardmhz >= dm7025_clock_freq[i]) break;

		if(i > 10) i = 10;

		reader->cardmhz = dm7025_clock_freq[i];
		reader->divider = dm7025_PLL_setting[i]; /*Nicer way of codeing is: reader->divider = i + 6;*/

		rdr_debug_mask(reader, D_DEVICE,"DM7025 PLL maxmhz = %.2f, wanted cardmhz = %.2f, PLL setting used = %d, actualcardclock=%.2f", (float) reader->mhz/100, (float) t_cardmhz/100,
			reader->divider, (float) reader->cardmhz/100);
	}

	return reader->divider;
}


static void ICC_Async_InvertBuffer (uint32_t size, BYTE * buffer)
{
	uint32_t i;

	for (i = 0; i < size; i++)
		buffer[i] = ~(INVERT_BYTE (buffer[i]));
}

static int32_t Parse_ATR (struct s_reader * reader, ATR * atr, uint16_t deprecated)
{
	BYTE FI = ATR_DEFAULT_FI;
	//BYTE t = ATR_PROTOCOL_TYPE_T0;
	double d = ATR_DEFAULT_D;
	double n = ATR_DEFAULT_N;
	int32_t ret;
	char tmp[256];

		int32_t numprot = atr->pn;
		//if there is a trailing TD, this number is one too high
		BYTE tx;
		if (ATR_GetInterfaceByte (atr, numprot-1, ATR_INTERFACE_BYTE_TD, &tx) == ATR_OK)
			if ((tx & 0xF0) == 0)
				numprot--;
		int32_t i,point;
		char txt[50];
		bool OffersT[3]; //T14 stored as T2
		for (i = 0; i <= 2; i++)
			OffersT[i] = FALSE;
		for (i=1; i<= numprot; i++) {
			point = 0;
			if (ATR_GetInterfaceByte (atr, i, ATR_INTERFACE_BYTE_TA, &tx) == ATR_OK) {
				snprintf((char *)txt+point,sizeof(txt)-point,"TA%i=%02X ",i,tx);
				point +=7;
			}
			if (ATR_GetInterfaceByte (atr, i, ATR_INTERFACE_BYTE_TB, &tx) == ATR_OK) {
				snprintf((char *)txt+point,sizeof(txt)-point,"TB%i=%02X ",i,tx);
				point +=7;
			}
			if (ATR_GetInterfaceByte (atr, i, ATR_INTERFACE_BYTE_TC, &tx) == ATR_OK) {
				snprintf((char *)txt+point,sizeof(txt)-point,"TC%i=%02X ",i,tx);
				point +=7;
			}
			if (ATR_GetInterfaceByte (atr, i, ATR_INTERFACE_BYTE_TD, &tx) == ATR_OK) {
				snprintf((char *)txt+point,sizeof(txt)-point,"TD%i=%02X ",i,tx);
				point +=7;
				tx &= 0X0F;
				snprintf((char *)txt+point,sizeof(txt)-point,"(T%i)",tx);
				if (tx == 14)
					OffersT[2] = TRUE;
				else
					OffersT[tx] = TRUE;
			}
			else {
				snprintf((char *)txt+point,sizeof(txt)-point,"no TD%i means T0",i);
				OffersT[0] = TRUE;
			}
			rdr_debug_mask(reader, D_ATR, "%s", txt);
		}

		int32_t numprottype = 0;
		for (i = 0; i <= 2; i++)
			if (OffersT[i])
				numprottype ++;
		rdr_debug_mask(reader, D_ATR, "%i protocol types detected. Historical bytes: %s",
			numprottype, cs_hexdump(1,atr->hb,atr->hbn, tmp, sizeof(tmp)));

		ATR_GetParameter (atr, ATR_PARAMETER_N, &(n));
		ATR_GetProtocolType(atr,1,&(reader->protocol_type)); //get protocol from TD1
		BYTE TA2;
		bool SpecificMode = (ATR_GetInterfaceByte (atr, 2, ATR_INTERFACE_BYTE_TA, &TA2) == ATR_OK); //if TA2 present, specific mode, else negotiable mode
		if (SpecificMode) {
			reader->protocol_type = TA2 & 0x0F;
			if ((TA2 & 0x10) != 0x10) { //bit 5 set to 0 means F and D explicitly defined in interface characters
				BYTE TA1;
				if (ATR_GetInterfaceByte (atr, 1 , ATR_INTERFACE_BYTE_TA, &TA1) == ATR_OK) {
					FI = TA1 >> 4;
					ATR_GetParameter (atr, ATR_PARAMETER_D, &(d));
				}
				else {
					FI = ATR_DEFAULT_FI;
					d = ATR_DEFAULT_D;
				}
			}
			else {
				rdr_log(reader, "Specific mode: speed 'implicitly defined', not sure how to proceed, assuming default values");
				FI = ATR_DEFAULT_FI;
				d = ATR_DEFAULT_D;
			}
			rdr_debug_mask(reader, D_ATR, "Specific mode: T%i, F=%.0f, D=%.6f, N=%.0f",
				reader->protocol_type, (double) atr_f_table[FI], d, n);
		}
		else { //negotiable mode

			if (reader->mhz > 2000)  // Initial timeout for pll readers to 10000000 us
					reader->read_timeout = 1000000;
			else
					reader->read_timeout = 1000; // for all other readers set initial timeout to 1000 ms
			bool PPS_success = FALSE;
			bool NeedsPTS = ((reader->protocol_type != ATR_PROTOCOL_TYPE_T14) && (numprottype > 1 || (atr->ib[0][ATR_INTERFACE_BYTE_TA].present == TRUE && atr->ib[0][ATR_INTERFACE_BYTE_TA].value != 0x11) || n == 255)); //needs PTS according to old ISO 7816
			if (NeedsPTS && deprecated == 0) {
				//						 PTSS	PTS0	PTS1	PCK
				BYTE req[6] = { 0xFF, 0x10, 0x00, 0x00 }; //we currently do not support PTS2, standard guardtimes or PTS3,
																									//but spare 2 bytes in arrayif card responds with it
				req[1]=0x10 | reader->protocol_type; //PTS0 always flags PTS1 to be sent always
				if (ATR_GetInterfaceByte (atr, 1, ATR_INTERFACE_BYTE_TA, &req[2]) != ATR_OK)	//PTS1
					req[2] = 0x11; //defaults FI and DI to 1
				uint32_t len = 0;
				call (SetRightParity (reader));
				ret = PPS_Exchange (reader, req, &len);
				if (ret == OK) {
					FI = req[2] >> 4;
					BYTE DI = req[2] & 0x0F;
					d = (double) (atr_d_table[DI]);
					PPS_success = TRUE;
					rdr_debug_mask(reader, D_ATR, "PTS Succesfull, selected protocol: T%i, F=%.0f, D=%.6f, N=%.0f",
						reader->protocol_type, (double) atr_f_table[FI], d, n);
				}
				else
					rdr_ddump_mask(reader, D_ATR, req, len,"PTS Failure, response:");
			}

			//When for SCI, T14 protocol, TA1 is obeyed, this goes OK for mosts devices, but somehow on DM7025 Sky S02 card goes wrong when setting ETU (ok on DM800/DM8000)
			if (!PPS_success) {//last PPS not succesfull
				BYTE TA1;
				if (ATR_GetInterfaceByte (atr, 1 , ATR_INTERFACE_BYTE_TA, &TA1) == ATR_OK) {
					FI = TA1 >> 4;
					ATR_GetParameter (atr, ATR_PARAMETER_D, &(d));
				}
				else { //do not obey TA1
					FI = ATR_DEFAULT_FI;
					d = ATR_DEFAULT_D;
				}
				if (NeedsPTS) {
					if ((d == 32) || (d == 12) || (d == 20)) //those values were RFU in old table
						d = 0; // viaccess cards that fail PTS need this
				}

				rdr_debug_mask(reader, D_ATR, "No PTS %s, selected protocol T%i, F=%.0f, D=%.6f, N=%.0f",
					NeedsPTS ? "happened" : "needed", reader->protocol_type, (double) atr_f_table[FI], d, n);
			}
		}//end negotiable mode

	//make sure no zero values
	double F =	(double) atr_f_table[FI];
	if (!F) {
		FI = ATR_DEFAULT_FI;
		rdr_log(reader, "Warning: F=0 is invalid, forcing FI=%d", FI);
	}
	if (!d) {
		d = ATR_DEFAULT_D;
		rdr_log(reader, "Warning: D=0 is invalid, forcing D=%.0f", d);
	}

	if (deprecated == 0)
		return InitCard (reader, atr, FI, d, n, deprecated);
	else
		return InitCard (reader, atr, ATR_DEFAULT_FI, ATR_DEFAULT_D, n, deprecated);
}

static int32_t PPS_Exchange (struct s_reader * reader, BYTE * params, uint32_t *length)
{
	BYTE confirm[PPS_MAX_LENGTH];
	uint32_t len_request, len_confirm;
	char tmp[128];
	int32_t ret;

	len_request = PPS_GetLength (params);
	params[len_request - 1] = PPS_GetPCK(params, len_request - 1);
	rdr_debug_mask(reader, D_IFD, "PTS: Sending request: %s",
		cs_hexdump(1, params, len_request, tmp, sizeof(tmp)));

	if (reader->crdr.active && reader->crdr.set_protocol) {
		ret = reader->crdr.set_protocol(reader, params, length, len_request);
		return ret;
	}

	/* Send PPS request */
	call (ICC_Async_Transmit (reader, len_request, params));

	/* Get PPS confirm */
	call (ICC_Async_Receive (reader, 2, confirm));
	len_confirm = PPS_GetLength (confirm);
	call (ICC_Async_Receive (reader, len_confirm - 2, confirm + 2));

	rdr_debug_mask(reader, D_IFD, "PTS: Receiving confirm: %s",
		cs_hexdump(1, confirm, len_confirm, tmp, sizeof(tmp)));
	if ((len_request != len_confirm) || (memcmp (params, confirm, len_request)))
		ret = ERROR;
	else
		ret = OK;

	/* Copy PPS handshake */
	memcpy (params, confirm, len_confirm);
	(*length) = len_confirm;
	return ret;
}

static uint32_t PPS_GetLength (BYTE * block)
{
	uint32_t length = 3;

	if (PPS_HAS_PPS1 (block))
	length++;

	if (PPS_HAS_PPS2 (block))
	length++;

	if (PPS_HAS_PPS3 (block))
	length++;

	return length;
}

static uint32_t ETU_to_ms(struct s_reader * reader, uint32_t WWT)
{
#define CHAR_LEN 10L //character length in ETU, perhaps should be 9 when parity = none?
	
	if (reader->mhz>2000){
		double work_etu = 1000L*1000L / (double) reader->current_baudrate; 
		return (uint32_t) ((double) WWT * work_etu); // in us
	}

	if (WWT > CHAR_LEN)
		WWT -= CHAR_LEN;
	else
		WWT = 0;
	double work_etu = 1000 / (double)reader->current_baudrate;
	return (uint32_t) (WWT * work_etu * reader->cardmhz / reader->mhz); // in ms
}

static int32_t ICC_Async_SetParity (struct s_reader * reader, uint16_t parity)
{
	if (reader->crdr.active && reader->crdr.set_parity) {
		call(reader->crdr.set_parity(reader, parity));
		return OK;
	} else if(reader->crdr.active)
		return OK;

	switch(reader->typ) {
		case R_MP35:
		case R_DB2COM1:
		case R_DB2COM2:
		case R_SC8in1:
		case R_MOUSE:
			call (IO_Serial_SetParity (reader, parity));
		break;
#if defined(WITH_LIBUSB)
		case R_SMART:
			call (SR_SetParity(reader, parity));
			break;
#endif
		case R_INTERNAL:
			return OK;
		default:
			rdr_log(reader, "ERROR: %s: Unknown reader type: %d", __func__, reader->typ);
			return ERROR;
	}
	return OK;
}

static int32_t SetRightParity (struct s_reader * reader)
{
	//set right parity
	uint16_t parity = PARITY_EVEN;
	if (reader->convention == ATR_CONVENTION_INVERSE)
		parity = PARITY_ODD;
	else if(reader->protocol_type == ATR_PROTOCOL_TYPE_T14)
		parity = PARITY_NONE;

	call (ICC_Async_SetParity(reader, parity));

	if (reader->crdr.active) {
		if (reader->crdr.flush==1)
			IO_Serial_Flush(reader);
		return OK;
	}

#if defined(WITH_COOLAPI) || defined(WITH_AZBOX)
	if (reader->typ != R_INTERNAL)
#endif
#if defined(WITH_LIBUSB)
  if (reader->typ != R_SMART)
#endif
            IO_Serial_Flush(reader);
	return OK;
}

static int32_t InitCard (struct s_reader * reader, ATR * atr, BYTE FI, double d, double n, uint16_t deprecated)
{
	double I;
	double F;
	uint32_t BGT, edc, EGT = 0, CGT = 0, WWT = 0, GT = 0, gt_ms = 0;

	//set the amps and the volts according to ATR
	if (ATR_GetParameter(atr, ATR_PARAMETER_I, &I) != ATR_OK)
		I = 0;

	//set clock speed to max if internal reader
	if((reader->typ > R_MOUSE && reader->crdr.active == 0) || (reader->crdr.active == 1 && reader->crdr.max_clock_speed==1))
		if (reader->mhz == 357 || reader->mhz == 358) //no overclocking
			reader->mhz = atr_fs_table[FI] / 10000; //we are going to clock the card to this nominal frequency

		if (reader->mhz > 2000 && reader->cardmhz == -1) // -1 is magic number pll internal reader set cardmhz according to optimal atr speed
			reader->cardmhz = atr_fs_table[FI] / 10000 ;

		if (reader->mhz > 2000) {
			reader->divider = 0; //reset pll divider so divider will be set calculated again.
			ICC_Async_GetPLL_Divider(reader); // calculate pll divider for target cardmhz.
		}

	//set clock speed/baudrate must be done before timings
	//because reader->current_baudrate is used in calculation of timings
	F =	(double) atr_f_table[FI];  //Get FI (this is != clockspeed)

	reader->current_baudrate = DEFAULT_BAUDRATE;

	if (deprecated == 0) {
		uint32_t baud_temp;
		if (reader->protocol_type != ATR_PROTOCOL_TYPE_T14) { //dont switch for T14
			if (reader->mhz >2000 && reader->mhz != 8300)
				baud_temp = (uint32_t) (d * (double) reader->mhz / reader->divider *10000L / F);
			else
				baud_temp = d * ICC_Async_GetClockRate (reader->cardmhz) / F;
			if (reader->crdr.active == 1) {
				if (reader->crdr.set_baudrate)
					call (reader->crdr.set_baudrate(reader, baud_temp));
			} else {
				if (reader->typ == R_SC8in1) {
					call (Sc8in1_SetBaudrate(reader, baud_temp, NULL, 0));
				}
				else if (reader->typ <= R_MOUSE)
					call (Phoenix_SetBaudrate(reader, baud_temp));
			}
			reader->current_baudrate = baud_temp; //this is needed for all readers to calculate work_etu for timings
			rdr_debug_mask(reader, D_IFD, "Setting baudrate to %d", reader->current_baudrate);
		}
	}

	//set timings according to ATR
	reader->read_timeout = 0;
	reader->block_delay = 0;
	reader->char_delay = 0;
	if ( reader->mhz < 2000){
		if (n == 255) //Extra Guard Time
			EGT = 0;
		else
			EGT = n;
		GT = EGT + 12; //Guard Time in ETU
		gt_ms = ETU_to_ms(reader, GT);
	}
	switch (reader->protocol_type) {
		case ATR_PROTOCOL_TYPE_T0:
		case ATR_PROTOCOL_TYPE_T14:
			{
			BYTE wi;
			/* Integer value WI	= TC2, by default 10 */
#ifndef PROTOCOL_T0_USE_DEFAULT_TIMINGS
			if (ATR_GetInterfaceByte (atr, 2, ATR_INTERFACE_BYTE_TC, &(wi)) != ATR_OK)
#endif
			wi = DEFAULT_WI;

			// WWT = 960 * d * WI  work etu

			if (reader->mhz > 2000) WWT = (uint32_t) 960 * d * wi; //in work ETU
			else WWT = (uint32_t) 960 * wi; //in ETU

			if (reader->protocol_type == ATR_PROTOCOL_TYPE_T14)
				WWT >>= 1; //is this correct?

			if( reader->mhz > 2000){
				EGT = 2;
				if (n != 255) //Extra Guard Time
					EGT =+ n;  // T0 protocol, if TC1 = 255 then dont add extra guardtime
				GT = 0;
				gt_ms = ETU_to_ms(reader, GT);
				reader->CWT = 0; // T0 protocol doesnt have char_delay, block_delay.
				reader->BWT = 0;
				if (reader->mhz != 8300)
					rdr_debug_mask(reader, D_IFD, "Protocol: T=%i, WWT=%u, Clockrate=%u",
						reader->protocol_type, WWT,
						(reader->mhz / reader->divider * 10000));
				else
					rdr_debug_mask(reader, D_IFD, "Protocol: T=%i, WWT=%u, Clockrate=%u",
						reader->protocol_type, WWT,
						(reader->cardmhz * 10000));
			}
			else
				rdr_debug_mask(reader, D_IFD, "Protocol: T=%i, WWT=%u, Clockrate=%u",
					reader->protocol_type, WWT,
					ICC_Async_GetClockRate(reader->cardmhz));

			reader->read_timeout = ETU_to_ms(reader, WWT);
			reader->block_delay = gt_ms;
			reader->char_delay = gt_ms;
			rdr_debug_mask(reader, D_ATR, "Setting timings: timeout=%u ms, block_delay=%u ms, char_delay=%u ms",
				reader->read_timeout, reader->block_delay, reader->char_delay);
			break;
		}
	 case ATR_PROTOCOL_TYPE_T1:
			{
				BYTE ta, tb, tc, cwi, bwi;

				// Set IFSC
				if (ATR_GetInterfaceByte (atr, 3, ATR_INTERFACE_BYTE_TA, &ta) == ATR_NOT_FOUND)
					reader->ifsc = DEFAULT_IFSC;
				else if ((ta != 0x00) && (ta != 0xFF))
					reader->ifsc = ta;
				else
					reader->ifsc = DEFAULT_IFSC;

				//FIXME workaround for Smargo until native mode works
				if (reader->smargopatch == 1)
					reader->ifsc = MIN (reader->ifsc, 28);
				else
					// Towitoko does not allow IFSC > 251
					//FIXME not sure whether this limitation still exists
					//tryfix HD+ cards setting MAX_IFSC to 254
					reader->ifsc = MIN (reader->ifsc, MAX_IFSC);

			#ifndef PROTOCOL_T1_USE_DEFAULT_TIMINGS
				// Calculate CWI and BWI
				if (ATR_GetInterfaceByte (atr, 3, ATR_INTERFACE_BYTE_TB, &tb) == ATR_NOT_FOUND)
					{
			#endif
						cwi	= DEFAULT_CWI;
						bwi = DEFAULT_BWI;
			#ifndef PROTOCOL_T1_USE_DEFAULT_TIMINGS
					}
				else
					{
						cwi	= tb & 0x0F;
						bwi = tb >> 4;
					}
			#endif

				// Set CWT = (2^CWI + 11) work etu
				if (reader->mhz > 2000)
					reader->CWT = (uint16_t) (((1<<cwi) + 11L)); // in ETU
				else
					reader->CWT = (uint16_t) (((1<<cwi) + 11)); // in ETU

				// Set BWT = (2^BWI * 960 * 372 / clockspeed) seconds + 11 work etu  (in seconds)
				// 1 worketu = 1 / baudrate *1000*1000 us
				if (reader->mhz > 2000 && reader->mhz != 8300)
					reader->BWT = (uint32_t) ((((1<<bwi) * 960L * 372L / ((double)reader->mhz / (double) reader->divider / 100L)) * (double) reader->current_baudrate / 1000L / 1000L)+ 11L); // BWT in ETU
				else
					reader->BWT = (uint32_t)((1<<bwi) * 960 * 372 * 9600 / ICC_Async_GetClockRate(reader->cardmhz)) + 11 ;

				// Set BGT = 22 * work etu
				BGT = 22L; //in ETU

				if (n == 255)
					CGT = 11L; //in ETU
				else
					CGT = GT;

				// Set the error detection code type
				if (ATR_GetInterfaceByte (atr, 3, ATR_INTERFACE_BYTE_TC, &tc) == ATR_NOT_FOUND)
					edc = EDC_LRC;
				else
					edc = tc & 0x01;

				// Set initial send sequence (NS)
				reader->ns = 1;

				rdr_debug_mask(reader, D_ATR, "Protocol: T=%i: IFSC=%d, CWT=%d etu, BWT=%d etu, BGT=%d etu, EDC=%s",
					reader->protocol_type, reader->ifsc,
					reader->CWT, reader->BWT,
					BGT, (edc == EDC_LRC) ? "LRC" : "CRC");

				if( reader->mhz > 2000){
					GT = 12L;
					EGT = 2;
					if (n == 255) //Extra Guard Time T1
						EGT--;  // T1 protocol, if TC1 = 255 then substract 1 ETU from guardtime
					else
						EGT =+n;
				CGT = GT + EGT; // otherwise break T1 timings on MIPS, PPC ok
				}
				reader->read_timeout = ETU_to_ms(reader, reader->BWT);
				reader->block_delay = ETU_to_ms(reader, BGT);
				reader->char_delay = ETU_to_ms(reader, CGT);
				rdr_debug_mask(reader, D_ATR, "Setting timings: timeout=%u ms, block_delay=%u ms, char_delay=%u ms",
					reader->read_timeout, reader->block_delay, reader->char_delay);
			}
			break;
	 default:
			return ERROR;
			break;
	}//switch

	rdr_debug_mask(reader, D_ATR, "Setting right parity");
	call (SetRightParity (reader));
	rdr_debug_mask(reader, D_ATR, "Done!");

	if (reader->crdr.active==1 && reader->crdr.write_settings) {
		uint32_t ETU = 0;
		//for Irdeto T14 cards, do not set ETU
		if (!(atr->hbn >= 6 && !memcmp(atr->hb, "IRDETO", 6) && reader->protocol_type == ATR_PROTOCOL_TYPE_T14))
			ETU = F / d;
		call(reader->crdr.write_settings(reader, ETU, EGT, 5, I, (uint16_t) atr_f_table[FI], (BYTE)d, n));
	}

  //write settings to internal device
	if(reader->typ == R_INTERNAL && reader->crdr.active==0) {
#if defined(WITH_COOLAPI)
		call (Cool_WriteSettings (reader, reader->BWT, reader->CWT, EGT, BGT));
#else
		double F =	(double) atr_f_table[FI];
		uint32_t ETU = 0;
		//for Irdeto T14 cards, do not set ETU
		if (!(atr->hbn >= 6 && !memcmp(atr->hb, "IRDETO", 6) && reader->protocol_type == ATR_PROTOCOL_TYPE_T14))
			ETU = F / d;
		if (reader->mhz > 2000){ // Extra Guardtime is only slowing card ecm responses down. Although its calculated correct its not needed with internal readers!
			if (reader->protocol_type == ATR_PROTOCOL_TYPE_T0) EGT = 0;
			if (reader->protocol_type == ATR_PROTOCOL_TYPE_T1) EGT = 0;
			call (Sci_WriteSettings (reader, reader->protocol_type, reader->divider, ETU, WWT, reader->BWT, reader->CWT, EGT, 5, (unsigned char)I)); //P fixed at 5V since this is default class A card, and TB is deprecated
		}
		else {
			call (Sci_WriteSettings (reader, reader->protocol_type, reader->mhz / 100, ETU, WWT, reader->BWT, reader->CWT, EGT, 5, (unsigned char)I)); //P fixed at 5V since this is default class A card, and TB is deprecated
		}
#endif //WITH_COOLAPI
	}
#if defined(WITH_LIBUSB)
	if (reader->typ == R_SMART)
		SR_WriteSettings(reader, (uint16_t) atr_f_table[FI], (BYTE)d, (BYTE)EGT, (BYTE)reader->protocol_type, reader->convention);
#endif
	if (reader->mhz > 2000)
		if (reader->mhz != 8300)
			rdr_log(reader, "Maximum frequency for this card is formally %i Mhz, clocking it to %.2f Mhz",
				atr_fs_table[FI] / 1000000,
				(float) reader->mhz / reader->divider / 100);
		else
			rdr_log(reader, "Maximum frequency for this card is formally %i Mhz, clocking it to %.2f Mhz",
				atr_fs_table[FI] / 1000000,
				(float) reader->cardmhz / 100);
	else
		rdr_log(reader, "Maximum frequency for this card is formally %i Mhz, clocking it to %.2f Mhz",
			atr_fs_table[FI] / 1000000,
				(float) reader->mhz / 100);

	//IFS setting in case of T1
	if ((reader->protocol_type == ATR_PROTOCOL_TYPE_T1) && (reader->ifsc != DEFAULT_IFSC)) {
		unsigned char rsp[CTA_RES_LEN];
		uint16_t lr=0;
		unsigned char tmp[] = { 0x21, 0xC1, 0x01, 0x00, 0x00 };
		tmp[3] = reader->ifsc; // Information Field size
		tmp[4] = reader->ifsc ^ 0xE1;
		Protocol_T1_Command (reader, tmp, sizeof(tmp), rsp, &lr);
	}
 return OK;
}

static BYTE PPS_GetPCK (BYTE * block, uint32_t length)
{
	BYTE pck;
	uint32_t i;

	pck = block[0];
	for (i = 1; i < length; i++)
		pck ^= block[i];

	return pck;
}
#endif
