#ifndef OSCAM_CONFIG_FUNCS_H_
#define OSCAM_CONFIG_FUNCS_H_

#ifdef TUXBOX
#  if defined(__MIPSEL__)
#    define CS_LOGFILE "/dev/null"
#  else
#    define CS_LOGFILE "/dev/tty"
#  endif
#  if !defined(COOL) && !defined(SCI_DEV)
#    define SCI_DEV 1
#  endif
#  ifndef HAVE_DVBAPI
#    define HAVE_DVBAPI
#  endif
#endif

#if defined(WITH_SSL) && !defined(WITH_LIBCRYPTO)
#  define WITH_LIBCRYPTO
#endif

#if defined(__CYGWIN__)
#  define CS_LOGFILE "/dev/tty"
#endif

#if defined(__AIX__) || defined(__SGI__) || defined(__OSF__) || defined(__HPUX__) || defined(__SOLARIS__) || defined(__APPLE__)
#  define NEED_DAEMON
#endif

#if defined(__AIX__) || defined(__SGI__) || defined(__OSF__) || defined(__HPUX__) || defined(__SOLARIS__) || defined(__CYGWIN__)
#  define NO_ENDIAN_H
#endif

#if defined(__AIX__) || defined(__SGI__)
#  define socklen_t unsigned long
#endif

#if defined(__SOLARIS__) || defined(__FreeBSD__)
#  define BSD_COMP
#endif

#if defined(__HPUX__)
#  define _XOPEN_SOURCE_EXTENDED
#endif

/*
 * These functions allow checking of configuration variables in
 * the C code without using #ifdefs's. The dead code elimination
 * of the compiler takes care of removing the code that depends
 * on the disabled config options and we get compilation coverage
 * even when some option is disabled.
 */

static inline int config_WEBIF(void) {
	#ifdef WEBIF
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_WITH_SSL(void) {
	#ifdef WITH_SSL
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_WITH_LIBCRYPTO(void) {
	#ifdef WITH_LIBCRYPTO
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_HAVE_PCSC(void) {
	#ifdef HAVE_PCSC
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_LIBUSB(void) {
	#ifdef LIBUSB
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_HAVE_DVBAPI(void) {
	#ifdef HAVE_DVBAPI
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_WITH_STAPI(void) {
	#ifdef WITH_STAPI
	return config_HAVE_DVBAPI() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_IRDETO_GUESSING(void) {
	#ifdef IRDETO_GUESSING
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_CS_ANTICASC(void) {
	#ifdef CS_ANTICASC
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_WITH_DEBUG(void) {
	#ifdef WITH_DEBUG
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_WITH_LB(void) {
	#ifdef WITH_LB
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_LCDSUPPORT(void) {
	#ifdef LCDSUPPORT
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_IPV6SUPPORT(void) {
	#ifdef IPV6SUPPORT
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_MONITOR(void) {
	#ifdef MODULE_MONITOR
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_CAMD33(void) {
	#ifdef MODULE_CAMD33
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_CAMD35(void) {
	#ifdef MODULE_CAMD35
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_CAMD35_TCP(void) {
	#ifdef MODULE_CAMD35_TCP
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_NEWCAMD(void) {
	#ifdef MODULE_NEWCAMD
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_CCCAM(void) {
	#ifdef MODULE_CCCAM
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_CCCSHARE(void) {
	#ifdef MODULE_CCCSHARE
	return config_MODULE_CCCAM() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_GBOX(void) {
	#ifdef MODULE_GBOX
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_RADEGAST(void) {
	#ifdef MODULE_RADEGAST
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_SERIAL(void) {
	#ifdef MODULE_SERIAL
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_CONSTCW(void) {
	#ifdef MODULE_CONSTCW
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_MODULE_PANDORA(void) {
	#ifdef MODULE_PANDORA
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_WITH_CARDREADER(void) {
	#ifdef WITH_CARDREADER
	return 1;
	#else
	return 0;
	#endif
}

static inline int config_READER_NAGRA(void) {
	#ifdef READER_NAGRA
	return config_WITH_CARDREADER() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_READER_IRDETO(void) {
	#ifdef READER_IRDETO
	return config_WITH_CARDREADER() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_READER_CONAX(void) {
	#ifdef READER_CONAX
	return config_WITH_CARDREADER() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_READER_CRYPTOWORKS(void) {
	#ifdef READER_CRYPTOWORKS
	return config_WITH_CARDREADER() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_READER_SECA(void) {
	#ifdef READER_SECA
	return config_WITH_CARDREADER() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_READER_VIACCESS(void) {
	#ifdef READER_VIACCESS
	return config_WITH_CARDREADER() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_READER_VIDEOGUARD(void) {
	#ifdef READER_VIDEOGUARD
	return config_WITH_CARDREADER() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_READER_DRE(void) {
	#ifdef READER_DRE
	return config_WITH_CARDREADER() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_READER_TONGFANG(void) {
	#ifdef READER_TONGFANG
	return config_WITH_CARDREADER() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_READER_BULCRYPT(void) {
	#ifdef READER_BULCRYPT
	return config_WITH_CARDREADER() ? 1 : 0;
	#else
	return 0;
	#endif
}

static inline int config_CS_CACHEEX(void) {
	#ifdef CS_CACHEEX
	return 1;
	#else
	return 0;
	#endif
}

#endif
