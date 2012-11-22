#ifndef OSCAM_CONFIG_H_
#define OSCAM_CONFIG_H_

//
// ADDONS
//

#ifndef WEBIF
#define WEBIF
#endif

#ifndef TOUCH
#define TOUCH
#endif

#ifndef WITH_SSL
//#define WITH_SSL
#endif

#ifndef HAVE_DVBAPI
#if defined(__linux__)
#define HAVE_DVBAPI
#endif
#endif

#ifndef IRDETO_GUESSING
#define IRDETO_GUESSING
#endif

#ifndef CS_ANTICASC
#define CS_ANTICASC
#endif

#ifndef WITH_DEBUG
#define WITH_DEBUG
#endif

#ifndef WITH_LB
#define WITH_LB
#endif

#ifndef LCDSUPPORT
//#define LCDSUPPORT
#endif

#ifndef LEDSUPPORT
//#define LEDSUPPORT
#endif

#ifndef IPV6SUPPORT
//#define IPV6SUPPORT
#endif

//
// MODULES
//

#ifndef MODULE_MONITOR
#define MODULE_MONITOR
#endif

#ifndef MODULE_CAMD33
//#define MODULE_CAMD33
#endif

#ifndef MODULE_CAMD35
#define MODULE_CAMD35
#endif

#ifndef MODULE_CAMD35_TCP
#define MODULE_CAMD35_TCP
#endif

#ifndef MODULE_NEWCAMD
#define MODULE_NEWCAMD
#endif

#ifndef MODULE_CCCAM
#define MODULE_CCCAM
#endif

#ifdef MODULE_CCCAM
#ifndef MODULE_CCCSHARE
#define MODULE_CCCSHARE
#endif
#endif


#ifndef MODULE_GBOX
#define MODULE_GBOX
#endif

#ifndef MODULE_RADEGAST
#define MODULE_RADEGAST
#endif

#ifndef MODULE_SERIAL
#define MODULE_SERIAL
#endif

#ifndef MODULE_CONSTCW
#define MODULE_CONSTCW
#endif

#ifndef MODULE_PANDORA
#define MODULE_PANDORA
#endif

//
// CARDREADER
//

#ifndef WITH_CARDREADER
#define WITH_CARDREADER
#endif

#ifdef WITH_CARDREADER

#ifndef READER_NAGRA
#define READER_NAGRA
#endif

#ifndef READER_IRDETO
#define READER_IRDETO
#endif

#ifndef READER_CONAX
#define READER_CONAX
#endif

#ifndef READER_CRYPTOWORKS
#define READER_CRYPTOWORKS
#endif

#ifndef READER_SECA
#define READER_SECA
#endif

#ifndef READER_VIACCESS
#define READER_VIACCESS
#endif

#ifndef READER_VIDEOGUARD
#define READER_VIDEOGUARD
#endif

#ifndef READER_DRE
#define READER_DRE
#endif

#ifndef READER_TONGFANG
#define READER_TONGFANG
#endif

#ifndef READER_BULCRYPT
#define READER_BULCRYPT
#endif

#ifndef CARDREADER_PHOENIX
#define CARDREADER_PHOENIX
#endif

#ifndef CARDREADER_INTERNAL
#define CARDREADER_INTERNAL
#endif

#ifndef CARDREADER_MP35
#define CARDREADER_MP35
#endif

#ifndef CARDREADER_SC8IN1
#define CARDREADER_SC8IN1
#endif

#ifndef CARDREADER_SMARGO
#define CARDREADER_SMARGO
#endif

#ifndef CARDREADER_DB2COM
#define CARDREADER_DB2COM
#endif

#ifdef WITH_PCSC
#ifndef CARDREADER_PCSC
#define CARDREADER_PCSC
#endif
#endif

#ifdef WITH_LIBUSB
#ifndef CARDREADER_SMART
#define CARDREADER_SMART
#endif
#endif

// CARDREADER_INTERNAL_{AZBOX,COOLAPI,SCI} are internal variables
// do not touch them
#if   defined(CARDREADER_INTERNAL) && defined(WITH_AZBOX)
#define CARDREADER_INTERNAL_AZBOX
#elif defined(CARDREADER_INTERNAL) && defined(WITH_COOLAPI)
#define CARDREADER_INTERNAL_COOLAPI
#elif defined(CARDREADER_INTERNAL)
#define CARDREADER_INTERNAL_SCI
#endif

#ifdef WITH_STAPI
#ifndef CARDREADER_STAPI
#define CARDREADER_STAPI
#endif
#endif

#endif // WITH_CARDREADER

#ifndef CS_CACHEEX
#define CS_CACHEEX
#endif

#endif //OSCAM_CONFIG_H_
