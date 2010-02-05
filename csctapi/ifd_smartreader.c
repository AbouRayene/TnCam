#if defined(LIBUSB)
/*
		ifd_smartreader.c
		This module provides IFD handling functions for for Argolis smartreader+.
*/

#include <stdio.h>
#include <time.h>
#include <string.h>
#include"ifd_smartreader.h"

#define OK 0
#define ERROR 1
#define LOBYTE(w) ((BYTE)((w) & 0xff))
#define HIBYTE(w) ((BYTE)((w) >> 8))


typedef struct s_reader S_READER;


static struct libusb_device* find_smartreader(const char*busname,const char *devname);
static void smartreader_init(S_READER *reader);
static unsigned int smartreader_determine_max_packet_size(S_READER *reader);
static int smartreader_usb_close_internal (S_READER *reader);
static int smartreader_usb_reset(S_READER *reader);
static int smartreader_usb_open_dev(S_READER *reader);
static int smartreader_usb_purge_rx_buffer(S_READER *reader);
static int smartreader_usb_purge_tx_buffer(S_READER *reader);
static int smartreader_usb_purge_buffers(S_READER *reader);
static int smartreader_convert_baudrate(int baudrate, S_READER *reader, unsigned short *value, unsigned short *index);
static int smartreader_set_baudrate(S_READER *reader, int baudrate);
static int smartreader_setdtr_rts(S_READER *reader, int dtr, int rts);
static int smartreader_setflowctrl(S_READER *reader, int flowctrl);
static int smartreader_set_line_property2(S_READER *reader, enum smartreader_bits_type bits,
                            enum smartreader_stopbits_type sbit, enum smartreader_parity_type parity,
                            enum smartreader_break_type break_type);
static int smartreader_set_line_property(S_READER *reader, enum smartreader_bits_type bits,
                           enum smartreader_stopbits_type sbit, enum smartreader_parity_type parity);
static void smart_flush(S_READER *reader);
static int smart_read(S_READER *reader, unsigned char* buff, unsigned int size, int timeout_sec);
static int smartreader_write_data(S_READER *reader, unsigned char *buf, unsigned int size);
static int smart_write(S_READER *reader, unsigned char* buff, unsigned int size, int udelay);
static int smartreader_set_latency_timer(S_READER *reader, unsigned short latency);
static void EnableSmartReader(S_READER *reader, int clock, unsigned short Fi, unsigned char Di, unsigned char Ni, unsigned char T,unsigned char inv, int parity);
static void ResetSmartReader(S_READER *reader);
static void* ReaderThread(void *p);

#ifdef DEBUG_USB_IO
static void sr_hexdump(const unsigned char* data, unsigned int size, bool single);
#endif

int SR_Init (struct s_reader *reader)
{
    
    int ret;
    char device[128];
    char *busname;
    char *devname;
    char *search = ":";
    // split the device name from the reader conf into devname and busname
    memcpy(device,reader->device,128);
    busname=strtok(device,search);
    devname=strtok(NULL,search);
    if(!busname || !devname) {
        cs_log("Wrong device format (%s), it should be Device=bus:dev",reader->device);
        return ERROR;
    }
#ifdef DEBUG_USB_IO
    cs_log("looking for device %s on bus %s",devname,busname);
#endif

   	ret = libusb_init(NULL);
	if (ret < 0) {
        cs_log("Libusb init error : %d",ret);
        return ret;
    }

    smartreader_init(reader);

    reader->sr_config.usb_dev=find_smartreader(busname,devname);
    if(!reader->sr_config.usb_dev)
        return EXIT_FAILURE;
    
    //The smartreader has different endpoint addresses
    //compared to a real FT232 device, so change them here,
    //also a good way to compare a real FT232 with a smartreader
    //if you enumarate usb devices
    reader->sr_config.in_ep = 0x1;
    reader->sr_config.out_ep = 0x82;

    //open the first smartreader found in the system, this
    //would need to be changed to an enumerate function
    //of some sort using the ftdi library. /dev/ttyUSB0 wont exist
    //as we are accessing the device directly so you will have
    //to have some other way of addressing the smartreader within
    //OSCam config files etc...
    if ((ret=smartreader_usb_open_dev(reader))) {
        cs_log("unable to open smartreader device %s in bus %s (ret=%d)\n", devname,busname,ret);
        return EXIT_FAILURE;
    }

#ifdef DEBUG_USB_IO
    cs_log("IO:SR: Setting smartreader latency timer to 1ms");
#endif
    //Set the FTDI latency timer to 1ms
    ret = smartreader_set_latency_timer(reader, 1);

    //Set databits to 8o2
    ret = smartreader_set_line_property(reader, BITS_8, STOP_BIT_2, ODD);

    //Set the DTR HIGH and RTS LOW
    ret=smartreader_setdtr_rts(reader, 0, 0);

    //Disable flow control
    ret=smartreader_setflowctrl(reader, 0);

    // start the reading thread
    reader->g_read_buffer_size = 0;
    reader->modem_status = 0 ;
    pthread_mutex_init(&reader->g_read_mutex,NULL);
    pthread_mutex_init(&reader->g_usb_mutex,NULL);
    ret = pthread_create(&reader->rt, NULL, ReaderThread, (void *)(reader));
    if (ret) {
        cs_log("ERROR; return code from pthread_create() is %d", ret);
        return ERROR;
    }

	return OK;
}


int SR_GetStatus (struct s_reader *reader, int * in)
{
	int state;

    pthread_mutex_lock(&reader->g_read_mutex);
    state =(reader->modem_status & 0x80) == 0x80 ? 0 : 2;
    pthread_mutex_unlock(&reader->g_read_mutex);

    
	//state = 0 no card, 1 = not ready, 2 = ready
	if (state)
		*in = 1; //CARD, even if not ready report card is in, or it will never get activated
	else
		*in = 0; //NOCARD

	return OK;
}

int SR_Reset (struct s_reader *reader, ATR *atr)
{
    unsigned char data[40];
    int ret;
    int atr_ok;
    unsigned int i;
    int parity[4] = {EVEN, ODD, NONE, EVEN};    // the last EVEN is to try with different F, D values for irdeto card.
#ifdef DEBUG_USB_IO
    char *parity_str[5]={"NONE", "ODD", "EVEN", "MARK", "SPACE"};
#endif
    
    if(reader->mhz==reader->cardmhz && reader->cardmhz*10000 > 3690000)
        reader->sr_config.fs=reader->cardmhz*10000; 
    else    
        reader->sr_config.fs=3690000; 

    ResetSmartReader(reader);
    
    for(i=0 ; i<sizeof(parity) ;i++) {
        reader->sr_config.irdeto=FALSE;
        atr_ok=ERROR;
        memset(data,0,sizeof(data));
        reader->sr_config.parity=parity[i];
#ifdef DEBUG_USB_IO
        cs_log("IO:SR: Trying with parity %s",parity_str[reader->sr_config.parity]);
#endif    

        // special irdeto case
        if(i==3) {
#ifdef DEBUG_USB_IO
            cs_log("IO:SR: Trying irdeto");
#endif
            reader->sr_config.F=618; /// magic smartreader value
            reader->sr_config.D=1;
            // reader->sr_config.T=2; // will be set to T=1 in EnableSmartReader
            reader->sr_config.T=1;
            reader->sr_config.irdeto=TRUE;
        }
        
        smart_flush(reader);
        EnableSmartReader(reader, reader->sr_config.fs, reader->sr_config.F, (BYTE)reader->sr_config.D, reader->sr_config.N, reader->sr_config.T, reader->sr_config.inv,reader->sr_config.parity);
        sched_yield();
        
        //Reset smartcard
    
        //Set the DTR HIGH and RTS HIGH
        smartreader_setdtr_rts(reader, 1, 1);
        // A card with an active low reset is reset by maintaining RST in state L for at least 40 000 clock cycles
        // so if we have a base freq of 3.5712MHz : 40000/3690000 = .0112007168458781 seconds, aka 11ms
        // so if we have a base freq of 6.00MHz : 40000/6000000 = .0066666666666666 seconds, aka 6ms
        // here were doing 200ms .. is it too much ?
        usleep(200000);
        
        //Set the DTR HIGH and RTS LOW
        smartreader_setdtr_rts(reader, 1, 0);
    
        usleep(200000);
        sched_yield();
    
        //Read the ATR
        ret = smart_read(reader,data, 40,1);
#ifdef DEBUG_USB_IO
        cs_log("IO:SR: get ATR ret = %d" , ret);
        if(ret)
            sr_hexdump(data,ATR_MAX_SIZE*2,FALSE);
#endif
        if(data[0]!=0x3B && data[0]!=0x03 && data[0]!=0x3F)
            continue; // this is not a valid ATR.
            
        if(data[0]==0x03) {
#ifdef DEBUG_USB_IO
            cs_log("IO:SR: Inverse convention detected, setting smartreader inv to 1");
#endif
            reader->sr_config.inv=1;
            EnableSmartReader(reader, reader->sr_config.fs, reader->sr_config.F, (BYTE)reader->sr_config.D, reader->sr_config.N, reader->sr_config.T, reader->sr_config.inv,reader->sr_config.parity);
        }
        // parse atr
        if(ATR_InitFromArray (atr, data, ret) == ATR_OK) {
#ifdef DEBUG_USB_IO
            cs_log("IO:SR: ATR parsing OK");
#endif
            atr_ok=OK;
            if(i==3) {
#ifdef DEBUG_USB_IO
                cs_log("IO:SR: Locking F and D for Irdeto mode");
#endif
                reader->sr_config.irdeto=TRUE;
            }
        }

        if(atr_ok == OK)
            break;
     }


    return atr_ok;
}

int SR_Transmit (struct s_reader *reader, BYTE * buffer, unsigned size)

{ 
    unsigned int ret;
    ret = smart_write(reader, buffer, size, 0);
    if (ret!=size)
        return ERROR;
        
	return OK;
}

int SR_Receive (struct s_reader *reader, BYTE * buffer, unsigned size)
{ 
    unsigned int ret;
    ret = smart_read(reader, buffer, size, 1);
    if (ret!=size)
        return ERROR;

	return OK;
}	

int SR_SetBaudrate (struct s_reader *reader)
{
    reader->sr_config.fs=reader->mhz*10000; //freq in KHz
    EnableSmartReader(reader, reader->sr_config.fs, reader->sr_config.F, (BYTE)reader->sr_config.D, reader->sr_config.N, reader->sr_config.T, reader->sr_config.inv,reader->sr_config.parity);
    //baud rate not really used in native mode since
    //it's handled by the card, so just set to maximum 3Mb/s
    smartreader_set_baudrate(reader, 3000000);
    sched_yield();

	return OK;
}

int SR_SetParity (struct s_reader *reader)
{
    int ret;
#ifdef DEBUG_USB_IO
    char *parity_str[5]={"NONE", "ODD", "EVEN", "MARK", "SPACE"};
    cs_log("IO:SR: Setting parity to %s",parity_str[reader->sr_config.parity]);
#endif    
    ret = smartreader_set_line_property(reader, (enum smartreader_bits_type) 8, STOP_BIT_2, reader->sr_config.parity);
    if(ret)
        return ERROR;
        
    sched_yield();

	return OK;
}

int SR_Close (struct s_reader *reader)
{
#ifdef DEBUG_USB_IO
	cs_log ("IO:SR: Closing smarteader\n");
#endif

    reader->sr_config.running=FALSE;
    pthread_join(reader->rt,NULL);
    libusb_close(reader->sr_config.usb_dev_handle);
    reader[ridx].status = 0;
    return OK;

}


static void EnableSmartReader(S_READER *reader, int clock, unsigned short Fi, unsigned char Di, unsigned char Ni, unsigned char T, unsigned char inv,int parity) {

    int ret = 0;
    unsigned char FiDi[4];
    unsigned short freqk;
    unsigned char Freq[3];
    unsigned char N[2];
    unsigned char Prot[2];
    unsigned char Invert[2];
    
        
    ret = smartreader_set_baudrate(reader, 9600);
    smartreader_setflowctrl(reader, 0);
    ret = smartreader_set_line_property(reader, (enum smartreader_bits_type) 5, STOP_BIT_2, NONE);

    // command 1, set F and D parameter
    if(!reader->sr_config.irdeto) {
#ifdef DEBUG_USB_IO
    cs_log("IO:SR: sending F=%04X (%d) to smartreader",Fi,Fi);
    cs_log("IO:SR: sending D=%02X (%d) to smartreader",Di,Di);
#endif
        FiDi[0]=0x01;
        FiDi[1]=HIBYTE(Fi);
        FiDi[2]=LOBYTE(Fi);
        FiDi[3]=Di;
        ret = smart_write(reader,FiDi, sizeof (FiDi),0);
    }
    else {
        cs_log("Not setting F and D as we're in Irdeto mode");
    }

    // command 2, set the frequency in KHz
    // direct from the source .. 4MHz is the best init frequency for T=0 card, but looks like it's causing issue with some nagra card, reveting to 3.69MHz
    // if (clock<3690000 && T==0)
    if (clock<3690000)
        clock=3690000;
    freqk = (unsigned short) (clock / 1000);
#ifdef DEBUG_USB_IO
    cs_log("IO:SR: sending Freq=%04X (%d) to smartreader",freqk,freqk);
#endif
    Freq[0]=0x02;
    Freq[1]=HIBYTE(freqk);
    Freq[2]=LOBYTE(freqk);
    ret = smart_write(reader, Freq, sizeof (Freq),0);

    // command 3, set paramter N
#ifdef DEBUG_USB_IO
    cs_log("IO:SR: sending N=%02X (%d) to smartreader",Ni,Ni);
#endif
    N[0]=0x03;
    N[1]=Ni;
    ret = smart_write(reader, N, sizeof (N),0);

    // command 4 , set parameter T
    // if(T==2) // special trick to get ATR for Irdeto card, we need T=1 at reset, after that oscam takes care of T1 protocol, so we need T=0
    if(reader->sr_config.irdeto) // special trick to get ATR for Irdeto card, we need T=1 at reset, after that oscam takes care of T1 protocol, so we need T=0
        {
        T=1;
        reader->sr_config.T=1;
        }
    else if (T==1)
        T=0; // T=1 protocol is handled by oscam
        
#ifdef DEBUG_USB_IO
    cs_log("IO:SR: sending T=%02X (%d) to smartreader",T,T);
#endif
    // 
    Prot[0]=0x04;
    Prot[1]=T;
    ret = smart_write(reader, Prot, sizeof (Prot),0);

    // command 5, set invert y/n
#ifdef DEBUG_USB_IO
    cs_log("IO:SR: sending inv=%02X to smartreader",inv);
#endif
    Invert[0]=0x05;
    Invert[1]=inv;
    ret = smart_write(reader, Invert, sizeof (Invert),0);

    ret = smartreader_set_line_property2(reader, BITS_8, STOP_BIT_2, parity, BREAK_ON);
    //  send break for 350ms, also comes from JoePub debugging.
    usleep(350000);
    ret = smartreader_set_line_property2(reader, BITS_8, STOP_BIT_2, parity, BREAK_OFF);

    smart_flush(reader);
}

static void ResetSmartReader(S_READER *reader) 
{

    smart_flush(reader);
    // set smartreader+ default values 
    reader->sr_config.F=372; 
    reader->sr_config.D=1.0; 
    if(reader->mhz==reader->cardmhz && reader->cardmhz*10000 > 3690000)
        reader->sr_config.fs=reader->cardmhz*10000; 
    else    
        reader->sr_config.fs=3690000; 
    reader->sr_config.N=0; 
    reader->sr_config.T=0; 
    reader->sr_config.inv=0; 
    
    EnableSmartReader(reader, reader->sr_config.fs, reader->sr_config.F, (BYTE)reader->sr_config.D, reader->sr_config.N, reader->sr_config.T, reader->sr_config.inv,reader->sr_config.parity);
    sched_yield();

}
/*

static bool smartreader_check_endpoint(struct usb_device *dev)
{
    int nb_interfaces;
    int i,j,k,l;
    u_int8_t tmpEndpointAddress;
    int nb_endpoint_ok;

    if (!dev->config) {
#ifdef DEBUG_USB_IO
        cs_log("IO:SR:  Couldn't retrieve descriptors");
#endif
        return FALSE;
    }
        
    nb_interfaces=dev->config->bNumInterfaces;
    // smartreader only has 1 interface
    if(nb_interfaces!=1) {
#ifdef DEBUG_USB_IO
        cs_log("IO:SR:  Couldn't retrieve interfaces");
#endif
        return FALSE;
    }

    nb_endpoint_ok=0;
    for (i = 0; i < dev->descriptor.bNumConfigurations; i++)
        for (j = 0; j < dev->config[i].bNumInterfaces; j++)
            for (k = 0; k < dev->config[i].interface[j].num_altsetting; k++)
                for (l = 0; l < dev->config[i].interface[j].altsetting[k].bNumEndpoints; l++) {
                    tmpEndpointAddress=dev->config[i].interface[j].altsetting[k].endpoint[l].bEndpointAddress;
#ifdef DEBUG_USB_IO
                    // cs_log("IO:SR:  checking endpoint address %02X on bus %03X of device %03x",tmpEndpointAddress,dev->);
                    cs_log("IO:SR:  checking endpoint address %02X",tmpEndpointAddress);
#endif
                    if((tmpEndpointAddress== 0x1) || (tmpEndpointAddress== 0x82))
                        nb_endpoint_ok++;
                }

    if(nb_endpoint_ok!=2)
        return FALSE;
    return TRUE;
}

*/

#ifdef DEBUG_USB_IO
static void sr_hexdump(const unsigned char* data, unsigned int size, bool single)
{
    unsigned int idx;
    unsigned int i;
    char buffer[512];

    memset(buffer,0,512);
    i=0;
    for (idx = 0; idx < size; idx++) {
        if(!single && idx % 16 == 0 && idx != 0){
            cs_log("IO:SR: %s",buffer);
            memset(buffer,0,512);
            i=0;
        }
        if((i+1)*3 >= 509) {
            cs_log("IO:SR: %s",buffer);
            memset(buffer,0,512);
            i=0;
        }

        sprintf(buffer+i*3,"%02X ", data[idx]);
        i++;
    }
}
#endif


///////////////////////
static struct libusb_device* find_smartreader(const char *busname,const char *devname)
{
    int dev_found;
	libusb_device *dev;
	libusb_device_handle *usb_dev_handle;
	libusb_device **devs;
    ssize_t cnt;
	int i = 0;
	int ret;
    struct libusb_device_descriptor desc;

	cnt = libusb_get_device_list(NULL, &devs);
	if (cnt < 0)
		return NULL;
    
	while ((dev = devs[i++]) != NULL) {
        dev_found=FALSE;
		ret = libusb_get_device_descriptor(dev, &desc);
		if (ret < 0) {
			cs_log("failed to get device descriptor for device %s on bus %s\n",devname,busname);
			return NULL;
		}

		if (desc.idVendor==0x0403 && desc.idProduct==0x6001) {
            ret=libusb_open(dev,&usb_dev_handle);
            if (ret) {
                cs_log ("coulnd't open device %03d:%03d\n", libusb_get_bus_number(dev), libusb_get_device_address(dev));
                continue;
            }

            if(libusb_get_bus_number(dev)==atoi(busname) && libusb_get_device_address(dev)==atoi(devname)) {
                cs_log("IO:SR: Checking FTDI device: %03d on bus %03d",libusb_get_device_address(dev),libusb_get_bus_number(dev));
                // check for smargo endpoints.
                
                dev_found=TRUE;
            }
            
            
            libusb_close(usb_dev_handle);
        }

    if (dev_found)
        break;    
	}
	
	if(!dev_found) {
        cs_log("Smartreader device %s:%s not found",busname,devname);
	   return NULL;
	}
    else
        cs_log("Found smartreader device %s:%s",busname,devname);

    return dev;
}

void smartreader_init(S_READER *reader)
{
    reader->sr_config.usb_dev = NULL;
    reader->sr_config.usb_dev_handle=NULL;
    reader->sr_config.usb_read_timeout = 5000;
    reader->sr_config.usb_write_timeout = 5000;

    reader->sr_config.type = TYPE_BM;    /* chip type */
    reader->sr_config.baudrate = -1;
    reader->sr_config.bitbang_enabled = 0;  /* 0: normal mode 1: any of the bitbang modes enabled */

    reader->sr_config.writebuffer_chunksize = 64;
    reader->sr_config.max_packet_size = 0;

    reader->sr_config.interface = 0;
    reader->sr_config.index = 0;
    reader->sr_config.in_ep = 0x02;
    reader->sr_config.out_ep = 0x82;
//    reader->sr_config.bitbang_mode = 1; /* when bitbang is enabled this holds the number of the mode  */

//    reader->sr_config.async_usb_buffer_size=0;
//    reader->sr_config.async_usb_buffer = NULL;
}


static unsigned int smartreader_determine_max_packet_size(S_READER *reader)
{
    unsigned int packet_size;
    struct libusb_device_descriptor desc;
    struct libusb_config_descriptor *configDesc;
    struct libusb_interface interface;
    struct libusb_interface_descriptor intDesc;
    int config;
    int ret;
    // Determine maximum packet size. Init with default value.
    // New hi-speed devices from FTDI use a packet size of 512 bytes
    // but could be connected to a normal speed USB hub -> 64 bytes packet size.
    if (reader->sr_config.type == TYPE_2232H || reader->sr_config.type == TYPE_4232H)
        packet_size = 512;
    else
        packet_size = 64;

    ret = libusb_get_device_descriptor(reader->sr_config.usb_dev, &desc);
    if (ret < 0) {
        cs_log("Smartreader : failed to get device descriptor");
        return (-1);
    }
    
    if (desc.bNumConfigurations)
    {
        ret=libusb_get_configuration(reader->sr_config.usb_dev_handle,&config);
        ret=libusb_get_config_descriptor(reader->sr_config.usb_dev,config,&configDesc);

        if (reader->sr_config.interface < configDesc->bNumInterfaces)
        {
            interface=configDesc->interface[reader->sr_config.interface];
            if (interface.num_altsetting > 0)
            {
                intDesc = interface.altsetting[0];
                if (intDesc.bNumEndpoints > 0)
                {
                    packet_size = intDesc.endpoint[0].wMaxPacketSize;
                }
            }
        }
    }

    return packet_size;
}


static int smartreader_usb_close_internal (S_READER *reader)
{
    int ret = 0;

    if (reader->sr_config.usb_dev_handle)
    {
       libusb_close (reader->sr_config.usb_dev_handle);
       reader->sr_config.usb_dev_handle=NULL;
    }

    return ret;
}


int smartreader_usb_reset(S_READER *reader)
{
    if (libusb_control_transfer(reader->sr_config.usb_dev_handle,
                                FTDI_DEVICE_OUT_REQTYPE,
                                SIO_RESET_REQUEST,
                                SIO_RESET_SIO,
                                reader->sr_config.index,
                                NULL,
                                0,
                                reader->sr_config.usb_write_timeout) != 0) {
        cs_log("Smartreader reset failed");
        return (-1);
    }

    // Invalidate data in the readbuffer
    // ftdi->readbuffer_offset = 0;
    // ftdi->readbuffer_remaining = 0;

    return 0;
}


int smartreader_usb_open_dev(S_READER *reader)
{
    int detach_errno = 0;
    struct libusb_device_descriptor desc;
    int ret;

#ifdef __WIN32__
    int config;
    int config_val = 1;
#endif

    ret=libusb_open(reader->sr_config.usb_dev,&reader->sr_config.usb_dev_handle);
    if (ret) {
        cs_log("Smartreader usb_open() failed");
        return (-4);
    }
#if defined(OS_LINUX)
    // Try to detach smartreader_sio kernel module.
    // Returns ENODATA if driver is not loaded.
    //
    // The return code is kept in a separate variable and only parsed
    // if usb_set_configuration() or usb_claim_interface() fails as the
    // detach operation might be denied and everything still works fine.
    // Likely scenario is a static smartreader_sio kernel module.
    if (libusb_detach_kernel_driver(reader->sr_config.usb_dev_handle, reader->sr_config.interface) != 0 && errno != ENODATA) {
        detach_errno = errno;
        cs_log("Couldn't detach interface from kernel. Please unload the FTDI drivers");
        return(LIBUSB_ERROR_NOT_SUPPORTED);
    }
#endif
    ret = libusb_get_device_descriptor(reader->sr_config.usb_dev, &desc);

#ifdef __WIN32__
    // set configuration (needed especially for windows)
    // tolerate EBUSY: one device with one configuration, but two interfaces
    //    and libftdi sessions to both interfaces (e.g. FT2232)

    if (desc.bNumConfigurations > 0)
    {
        ret=libusb_get_configuration(&reader->sr_config.usb_dev_handle,&config);
        
        // libusb-win32 on Windows 64 can return a null pointer for a valid device
        if (libusb_set_configuration(&reader->sr_config.usb_dev_handle, config) &&
            errno != EBUSY)
        {
            smartreader_usb_close_internal (reader);
            if (detach_errno == EPERM) {
                cs_log("inappropriate permissions on device!");
                return(-8);
            }
            else {
                cs_log("unable to set usb configuration. Make sure smartreader_sio is unloaded!");
                return (-3);
            }
        }
    }
#endif

    ret=libusb_claim_interface(reader->sr_config.usb_dev_handle, reader->sr_config.interface) ;
    if (ret!= 0)
    {
        cs_log("WTF !!! ret=%d",ret);
        smartreader_usb_close_internal (reader);
        if (detach_errno == EPERM) {
            cs_log("inappropriate permissions on device!");
            return (-8);
        }
        else {
            cs_log("unable to claim usb device. Make sure smartreader_sio is unloaded!");
            return (-5);
        }
    }

    if (smartreader_usb_reset (reader) != 0) {
        smartreader_usb_close_internal (reader);
        cs_log("smartreader_usb_reset failed");
        return (-6);
    }

    // Try to guess chip type
    // Bug in the BM type chips: bcdDevice is 0x200 for serial == 0
    if (desc.bcdDevice == 0x400 || (desc.bcdDevice == 0x200
            && desc.iSerialNumber == 0))
        reader->sr_config.type = TYPE_BM;
    else if (desc.bcdDevice == 0x200)
        reader->sr_config.type = TYPE_AM;
    else if (desc.bcdDevice == 0x500)
        reader->sr_config.type = TYPE_2232C;
    else if (desc.bcdDevice == 0x600)
        reader->sr_config.type = TYPE_R;
    else if (desc.bcdDevice == 0x700)
        reader->sr_config.type = TYPE_2232H;
    else if (desc.bcdDevice == 0x800)
        reader->sr_config.type = TYPE_4232H;

    // Set default interface on dual/quad type chips
    switch(reader->sr_config.type) {
        case TYPE_2232C:
        case TYPE_2232H:
        case TYPE_4232H:
            if (!reader->sr_config.index)
                reader->sr_config.index = INTERFACE_A;
            break;
        default:
            break;
    }

    // Determine maximum packet size
    reader->sr_config.max_packet_size = smartreader_determine_max_packet_size(reader);

    if (smartreader_set_baudrate (reader, 9600) != 0) {
        smartreader_usb_close_internal (reader);
        cs_log("set baudrate failed");
        return (-7);
    }

    return (0);
}


int smartreader_usb_purge_rx_buffer(S_READER *reader)
{
    if (libusb_control_transfer(reader->sr_config.usb_dev_handle,
                                FTDI_DEVICE_OUT_REQTYPE,
                                SIO_RESET_REQUEST,
                                SIO_RESET_PURGE_RX,
                                reader->sr_config.index,
                                NULL,
                                0,
                                reader->sr_config.usb_write_timeout) != 0) {
        cs_log("FTDI purge of RX buffer failed");
        return (-1);
    }


    return 0;
}

int smartreader_usb_purge_tx_buffer(S_READER *reader)
{
    if (libusb_control_transfer(reader->sr_config.usb_dev_handle,
                                FTDI_DEVICE_OUT_REQTYPE,
                                SIO_RESET_REQUEST,
                                SIO_RESET_PURGE_TX,
                                reader->sr_config.index,
                                NULL,
                                0,
                                reader->sr_config.usb_write_timeout) != 0) {
        cs_log("FTDI purge of TX buffer failed");
        return (-1);
    }

    return 0;
}

int smartreader_usb_purge_buffers(S_READER *reader)
{
    int result;

    result = smartreader_usb_purge_rx_buffer(reader);
    if (result < 0)
        return -1;

    result = smartreader_usb_purge_tx_buffer(reader);
    if (result < 0)
        return -2;

    return 0;
}

static int smartreader_convert_baudrate(int baudrate, S_READER *reader, unsigned short *value, unsigned short *index)
{
    static const char am_adjust_up[8] = {0, 0, 0, 1, 0, 3, 2, 1};
    static const char am_adjust_dn[8] = {0, 0, 0, 1, 0, 1, 2, 3};
    static const char frac_code[8] = {0, 3, 2, 4, 1, 5, 6, 7};
    int divisor, best_divisor, best_baud, best_baud_diff;
    unsigned long encoded_divisor;
    int i;

    if (baudrate <= 0)
    {
        // Return error
        return -1;
    }

    divisor = 24000000 / baudrate;

    if (reader->sr_config.type == TYPE_AM)
    {
        // Round down to supported fraction (AM only)
        divisor -= am_adjust_dn[divisor & 7];
    }

    // Try this divisor and the one above it (because division rounds down)
    best_divisor = 0;
    best_baud = 0;
    best_baud_diff = 0;
    for (i = 0; i < 2; i++)
    {
        int try_divisor = divisor + i;
        int baud_estimate;
        int baud_diff;

        // Round up to supported divisor value
        if (try_divisor <= 8)
        {
            // Round up to minimum supported divisor
            try_divisor = 8;
        }
        else if (reader->sr_config.type != TYPE_AM && try_divisor < 12)
        {
            // BM doesn't support divisors 9 through 11 inclusive
            try_divisor = 12;
        }
        else if (divisor < 16)
        {
            // AM doesn't support divisors 9 through 15 inclusive
            try_divisor = 16;
        }
        else
        {
            if (reader->sr_config.type == TYPE_AM)
            {
                // Round up to supported fraction (AM only)
                try_divisor += am_adjust_up[try_divisor & 7];
                if (try_divisor > 0x1FFF8)
                {
                    // Round down to maximum supported divisor value (for AM)
                    try_divisor = 0x1FFF8;
                }
            }
            else
            {
                if (try_divisor > 0x1FFFF)
                {
                    // Round down to maximum supported divisor value (for BM)
                    try_divisor = 0x1FFFF;
                }
            }
        }
        // Get estimated baud rate (to nearest integer)
        baud_estimate = (24000000 + (try_divisor / 2)) / try_divisor;
        // Get absolute difference from requested baud rate
        if (baud_estimate < baudrate)
        {
            baud_diff = baudrate - baud_estimate;
        }
        else
        {
            baud_diff = baud_estimate - baudrate;
        }
        if (i == 0 || baud_diff < best_baud_diff)
        {
            // Closest to requested baud rate so far
            best_divisor = try_divisor;
            best_baud = baud_estimate;
            best_baud_diff = baud_diff;
            if (baud_diff == 0)
            {
                // Spot on! No point trying
                break;
            }
        }
    }
    // Encode the best divisor value
    encoded_divisor = (best_divisor >> 3) | (frac_code[best_divisor & 7] << 14);
    // Deal with special cases for encoded value
    if (encoded_divisor == 1)
    {
        encoded_divisor = 0;    // 3000000 baud
    }
    else if (encoded_divisor == 0x4001)
    {
        encoded_divisor = 1;    // 2000000 baud (BM only)
    }
    // Split into "value" and "index" values
    *value = (unsigned short)(encoded_divisor & 0xFFFF);
    if (reader->sr_config.type == TYPE_2232C || reader->sr_config.type == TYPE_2232H || reader->sr_config.type == TYPE_4232H)
    {
        *index = (unsigned short)(encoded_divisor >> 8);
        *index &= 0xFF00;
        *index |= reader->sr_config.index;
    }
    else
        *index = (unsigned short)(encoded_divisor >> 16);

    // Return the nearest baud rate
    return best_baud;
}

int smartreader_set_baudrate(S_READER *reader, int baudrate)
{
    unsigned short value, index;
    int actual_baudrate;

    if (reader->sr_config.bitbang_enabled)
    {
        baudrate = baudrate*4;
    }

    actual_baudrate = smartreader_convert_baudrate(baudrate, reader, &value, &index);
    if (actual_baudrate <= 0) {
        cs_log("Silly baudrate <= 0.");
        return (-1);
    }

    // Check within tolerance (about 5%)
    if ((actual_baudrate * 2 < baudrate /* Catch overflows */ )
            || ((actual_baudrate < baudrate)
                ? (actual_baudrate * 21 < baudrate * 20)
                : (baudrate * 21 < actual_baudrate * 20))) {
        cs_log("Unsupported baudrate. Note: bitbang baudrates are automatically multiplied by 4");
        return (-1);
    }

    if (libusb_control_transfer(reader->sr_config.usb_dev_handle,
                                FTDI_DEVICE_OUT_REQTYPE,
                                SIO_SET_BAUDRATE_REQUEST,
                                value,
                                index,
                                NULL,
                                0,
                                reader->sr_config.usb_write_timeout) != 0) {
        cs_log("Setting new baudrate failed");
        return (-2);
    }

    reader->sr_config.baudrate = baudrate;
    return 0;
}

int smartreader_setdtr_rts(S_READER *reader, int dtr, int rts)
{
    unsigned short usb_val;

    if (dtr)
        usb_val = SIO_SET_DTR_HIGH;
    else
        usb_val = SIO_SET_DTR_LOW;

    if (rts)
        usb_val |= SIO_SET_RTS_HIGH;
    else
        usb_val |= SIO_SET_RTS_LOW;

    if (libusb_control_transfer(reader->sr_config.usb_dev_handle,
                                FTDI_DEVICE_OUT_REQTYPE,
                                SIO_SET_MODEM_CTRL_REQUEST,
                                usb_val,
                                reader->sr_config.index,
                                NULL,
                                0,
                                reader->sr_config.usb_write_timeout) != 0) {
        cs_log("set of rts/dtr failed");
        return (-1);
    }

    return 0;
}

int smartreader_setflowctrl(S_READER *reader, int flowctrl)
{
    if (libusb_control_transfer(reader->sr_config.usb_dev_handle,
                                FTDI_DEVICE_OUT_REQTYPE,
                                SIO_SET_FLOW_CTRL_REQUEST,
                                0,
                                (flowctrl | reader->sr_config.index),
                                NULL,
                                0,
                                reader->sr_config.usb_write_timeout) != 0) {
        cs_log("set flow control failed");
        return (-1);
    }

    return 0;
}

int smartreader_set_line_property2(S_READER *reader, enum smartreader_bits_type bits,
                            enum smartreader_stopbits_type sbit, enum smartreader_parity_type parity,
                            enum smartreader_break_type break_type)
{
    unsigned short value = bits;

    switch (parity)
    {
        case NONE:
            value |= (0x00 << 8);
            break;
        case ODD:
            value |= (0x01 << 8);
            break;
        case EVEN:
            value |= (0x02 << 8);
            break;
        case MARK:
            value |= (0x03 << 8);
            break;
        case SPACE:
            value |= (0x04 << 8);
            break;
    }

    switch (sbit)
    {
        case STOP_BIT_1:
            value |= (0x00 << 11);
            break;
        case STOP_BIT_15:
            value |= (0x01 << 11);
            break;
        case STOP_BIT_2:
            value |= (0x02 << 11);
            break;
    }

    switch (break_type)
    {
        case BREAK_OFF:
            value |= (0x00 << 14);
            break;
        case BREAK_ON:
            value |= (0x01 << 14);
            break;
    }

    if (libusb_control_transfer(reader->sr_config.usb_dev_handle,
                                FTDI_DEVICE_OUT_REQTYPE,
                                SIO_SET_DATA_REQUEST,
                                value,
                                reader->sr_config.index,
                                NULL,
                                0,
                                reader->sr_config.usb_write_timeout) != 0) {
        cs_log("Setting new line property failed");
        return (-1);
    }

    return 0;
}


int smartreader_set_line_property(S_READER *reader, enum smartreader_bits_type bits,
                           enum smartreader_stopbits_type sbit, enum smartreader_parity_type parity)
{
    return smartreader_set_line_property2(reader, bits, sbit, parity, BREAK_OFF);
}



void smart_flush(S_READER *reader)
{

    smartreader_usb_purge_buffers(reader);

    pthread_mutex_lock(&reader->g_read_mutex);
    reader->g_read_buffer_size = 0;
    pthread_mutex_unlock(&reader->g_read_mutex);
    sched_yield();
}

static int smart_read(S_READER *reader, unsigned char* buff, unsigned int size, int timeout_sec)
{

    int ret = 0;
    unsigned int total_read = 0;
    struct timeval start, now, dif = {0,0};
    gettimeofday(&start,NULL);
    
    while(total_read < size && dif.tv_sec < timeout_sec) {
        pthread_mutex_lock(&reader->g_read_mutex);
        
        if(reader->g_read_buffer_size > 0) {
        
            ret = reader->g_read_buffer_size > size-total_read ? size-total_read : reader->g_read_buffer_size;
            memcpy(buff+total_read,reader->g_read_buffer,ret);
            reader->g_read_buffer_size -= ret;
            total_read+=ret;
        }
        pthread_mutex_unlock(&reader->g_read_mutex);
       
        gettimeofday(&now,NULL);
        timersub(&now, &start, &dif);
        usleep(50);
        sched_yield();
    }

    
    return total_read;
}

int smartreader_write_data(S_READER *reader, unsigned char *buf, unsigned int size)
{
    int ret;
    int write_size;
    unsigned int offset = 0;
    int total_written = 0;
    int written;
     
    if(size<reader->sr_config.writebuffer_chunksize)
        write_size=size;
    else
        write_size = reader->sr_config.writebuffer_chunksize;

    while (offset < size)
    {

        if (offset+write_size > size)
            write_size = size-offset;

        ret = libusb_bulk_transfer(reader->sr_config.usb_dev_handle,
                                    reader->sr_config.in_ep, 
                                    buf+offset, 
                                    write_size,
                                    &written,
                                    reader->sr_config.usb_write_timeout);
        if (ret < 0) {
            cs_log("usb bulk write failed : ret = %d",ret);
            return(ret);
        }
        
        total_written += written;
        offset += write_size;
    }

    return total_written;
}

static int smartreader_set_latency_timer(S_READER *reader, unsigned short latency)
{
    unsigned short usb_val;

    if (latency < 1) {
        cs_log("latency out of range. Only valid for 1-255");
        return (-1);
    }

    usb_val = latency;
    if (libusb_control_transfer(reader->sr_config.usb_dev_handle, 
                        FTDI_DEVICE_OUT_REQTYPE,
                        SIO_SET_LATENCY_TIMER_REQUEST,
                        usb_val,
                        reader->sr_config.index,
                        NULL,
                        0,
                        reader->sr_config.usb_write_timeout) != 0) {
        cs_log("unable to set latency timer");
        return (-2);
    }

    return 0;
}

static int smart_write(S_READER *reader, unsigned char* buff, unsigned int size, int udelay)
{

    int ret = 0;
    unsigned int idx;

    if (udelay == 0) {
        ret = smartreader_write_data(reader, buff, size);
        if(ret<0) {
#ifdef DEBUG_USB_IO
            cs_log("IO:SR: USB write error : %d",ret);
#endif
        }
    }
    else {
        for (idx = 0; idx < size; idx++) {
            if ((ret = smartreader_write_data(reader, &buff[idx], 1)) < 0){
                break;
            }
            usleep(udelay);
        }
    }
    sched_yield();
    return ret;
}


static void* ReaderThread(void *p)
{

    struct s_reader *reader;
    int ret;
    int read;
    unsigned int copy_size;
    unsigned char local_buffer[64];  //64 is max transfer size of FTDI bulk pipe

    reader = (struct s_reader *)p;
    reader->sr_config.running=TRUE;
    
    
        
    while(reader->sr_config.running){
        if(reader->g_read_buffer_size == sizeof(reader->g_read_buffer)){
            cs_log("buffer full");
            //if out read buffer is full then delay
            //slightly and go around again
            usleep(20000);
            continue;
        }

        ret = libusb_bulk_transfer(reader->sr_config.usb_dev_handle,
                                    reader->sr_config.out_ep,
                                    local_buffer,
                                    4,
                                    &read,
                                    reader->sr_config.usb_read_timeout);
        if(ret<0) {
#ifdef DEBUG_USB_IO
            cs_log("IO:SR: usb_bulk_read read error %d",ret);
#endif
        }
        sched_yield();
        if(read>2) {  //FTDI always sends modem status bytes as first 2 chars with the 232BM
            pthread_mutex_lock(&reader->g_read_mutex);
            reader->modem_status= local_buffer[0];
            copy_size = sizeof(reader->g_read_buffer) - reader->g_read_buffer_size > (unsigned int)read-2 ? (unsigned int)read-2: sizeof(reader->g_read_buffer) - reader->g_read_buffer_size;
            memcpy(reader->g_read_buffer+reader->g_read_buffer_size,local_buffer+2,copy_size);
            reader->g_read_buffer_size += copy_size;            
            pthread_mutex_unlock(&reader->g_read_mutex);
        } 
        else {
            if(read==2) {
                pthread_mutex_lock(&reader->g_read_mutex);
                reader->modem_status=local_buffer[0];
                pthread_mutex_unlock(&reader->g_read_mutex);
            }
        }
    }

    pthread_exit(NULL);
}

#endif // HAVE_LIBUSB && USE_PTHREAD
