#include "../globals.h"
#ifdef WITH_CARDREADER
#include "../oscam-time.h"
#include "icc_async.h"
#include "ifd_phoenix.h"
#include "io_serial.h"

#define OK 0
#define ERROR 1

bool detect_db2com_reader(struct s_reader *reader)
{
	struct stat sb;
	if (stat(DEV_MULTICAM, &sb) == -1)
		return false;
	if (stat(reader->device, &sb) == 0) {
		if (S_ISCHR(sb.st_mode)) {
			int32_t dev_major = major(sb.st_rdev);
			int32_t dev_minor = minor(sb.st_rdev);
			if (dev_major == 4 || dev_major == 5) {
				int32_t rc = reader->typ;
				switch (dev_minor & 0x3F) {
					case 0: rc = R_DB2COM1; break;
					case 1: rc = R_DB2COM2; break;
					default: return false;
				}
				reader->typ = rc;
			}
			rdr_debug_mask(reader, D_READER, "device is major: %d, minor: %d, typ=%d", dev_major, dev_minor, reader->typ);
		}
	}
	return true;
}

int32_t db2com_init(struct s_reader *reader)
{
	if (reader->typ != R_DB2COM1 && reader->typ != R_DB2COM2)
		detect_db2com_reader(reader);

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
	if (Phoenix_Init(reader)) {
		rdr_log(reader, "ERROR: Phoenix_Init returns error");
		Phoenix_Close (reader);
		return ERROR;
	}
	return OK;
}

int32_t db2com_get_status(struct s_reader * reader, int32_t *status)
{
	*status = 0;
	uint16_t msr=1;
	IO_Serial_Ioctl_Lock(reader, 1);
	ioctl(reader->fdmc, MULTICAM_GET_PCDAT, &msr);
	if (reader->typ == R_DB2COM2)
		*status = !(msr & 1);
	else
		*status = (msr & 0x0f00) == 0x0f00;
	IO_Serial_Ioctl_Lock(reader, 0);
	return OK;
}

void cardreader_db2com(struct s_cardreader *crdr)
{
	crdr->desc          = "db2com";
	crdr->typ           = R_DB2COM1;
	crdr->flush         = 1;
	crdr->need_inverse  = 1;
	crdr->read_written  = 1;
	crdr->reader_init   = db2com_init;
	crdr->get_status    = db2com_get_status;
	crdr->activate      = Phoenix_Reset;
	crdr->transmit      = Phoenix_Transmit;
	crdr->receive       = Phoenix_Receive;
	crdr->close         = Phoenix_Close;
	crdr->set_parity    = IO_Serial_SetParity;
	crdr->set_baudrate  = Phoenix_SetBaudrate;
	crdr->set_DTS_RTS   = IO_Serial_DTR_RTS_dbox2;
}
#endif
