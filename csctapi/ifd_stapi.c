#include "../globals.h"

#ifdef CARDREADER_STAPI
#include "atr.h"

/* These functions are implemented in liboscam_stapi.a */
extern int32_t STReader_Open(char *device, uint32_t *stsmart_handle);
extern int32_t STReader_GetStatus(uint32_t stsmart_handle, int32_t *in);
extern int32_t STReader_Reset(uint32_t stsmart_handle, ATR *atr);
extern int32_t STReader_Transmit(uint32_t stsmart_handle, unsigned char *sent, uint32_t size);
extern int32_t STReader_Receive(uint32_t stsmart_handle, unsigned char *data, uint32_t size);
extern int32_t STReader_Close(uint32_t stsmart_handle);
extern int32_t STReader_SetProtocol(uint32_t stsmart_handle, unsigned char *params, unsigned *length, uint32_t len_request);
extern int32_t STReader_SetClockrate(uint32_t stsmart_handle);

static int32_t stapi_init(struct s_reader *reader) {
	return STReader_Open(reader->device, &reader->stsmart_handle);
}

static int32_t stapi_getstatus(struct s_reader *reader, int32_t *in) {
	return STReader_GetStatus(reader->stsmart_handle, in);
}

static int32_t stapi_reset(struct s_reader *reader, ATR *atr) {
	return STReader_Reset(reader->stsmart_handle, atr);
}

static int32_t stapi_transmit(struct s_reader *reader, unsigned char *sent, uint32_t size, uint32_t delay, uint32_t timeout) { // delay + timeout not in use (yet)!
	(void) delay; // delay not in use (yet)!
	(void) timeout; // timeout not in use (yet)!
	return STReader_Transmit(reader->stsmart_handle, sent, size);
}

static int32_t stapi_receive(struct s_reader *reader, unsigned char *data, uint32_t size, uint32_t delay, uint32_t timeout) { // delay + timeout not in use (yet)!
	(void) delay; // delay not in use (yet)!
	(void) timeout; // timeout not in use (yet)!
	return STReader_Receive(reader->stsmart_handle, data, size);
}

static int32_t stapi_close(struct s_reader *reader) {
	return STReader_Close(reader->stsmart_handle);
}

static int32_t stapi_setprotocol(struct s_reader *reader, unsigned char *params, unsigned *length, uint32_t len_request) {
	return STReader_SetProtocol(reader->stsmart_handle, params, length, len_request);
}

static int32_t stapi_writesettings(struct s_reader *reader, uint32_t ETU, uint32_t EGT, unsigned char P, unsigned char I, uint16_t Fi, unsigned char Di, unsigned char Ni) {
	(void)ETU; (void)EGT; (void)P; (void)I; (void)Fi; (void)Di; (void)Ni;
	return STReader_SetClockrate(reader->stsmart_handle);
}

void cardreader_stapi(struct s_cardreader *crdr)
{
	crdr->desc		= "stapi";
	crdr->reader_init	= stapi_init;
	crdr->get_status	= stapi_getstatus;
	crdr->activate	= stapi_reset;
	crdr->transmit	= stapi_transmit;
	crdr->receive		= stapi_receive;
	crdr->close		= stapi_close;
	crdr->set_protocol	= stapi_setprotocol;
	crdr->write_settings = stapi_writesettings;
	crdr->typ		= R_INTERNAL;
}
#endif
