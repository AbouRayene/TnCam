	SHELL	= /bin/sh

VER	= $(subst ",,$(filter-out \#define CS_VERSION,$(shell grep CS_VERSION globals.h)))
export VER

linux:	i386-pc-linux
freebsd:	i386-pc-freebsd
tuxbox:	cross-powerpc-tuxbox-linux
win:	cross-i386-pc-cygwin
cygwin: i386-pc-cygwin

std:	linux \
	cross-i386-pc-cygwin \
	cross-powerpc-tuxbox-linux \
	cross-i386-pc-freebsd \
	cross-arm-nslu2-linux \
	cross-mipsel-router-linux-uclibc927 \
	cross-mipsel-router-linux-uclibc928 \
	cross-mipsel-tuxbox-linux-glibc \
	cross-sh4-linux

all:	\
	cross-sparc-sun-solaris2.7 \
	cross-rs6000-ibm-aix4.2 \
	cross-mips-sgi-irix6.5


dist:	std
	@cd Distribution && tar cvf "../mpcs$(VER).tar" *
	@bzip2 -9f "mpcs$(VER).tar"

extra:	all
	@cd Distribution && tar cvf "../mpcs$(VER)-extra.tar" *
	@bzip2 -9f "mpcs$(VER)-extra.tar"

clean:
	@-rm -rf mpcs-ostype.h lib Distribution/mpcs-*

tar:	clean
	@tar cvf "mpcs$(VER)-src.tar" Distribution Make* *.c *.h cscrypt csctapi
	@bzip2 -9f "mpcs$(VER)-src.tar"

nptar:	clean
	@tar cvf "mpcs$(VER)-nonpublic-src.tar" Distribution Make* *.c *.np *.h cscrypt csctapi csgbox
	@bzip2 -9f "mpcs$(VER)-nonpublic-src.tar"

######################################################################
#
#	LINUX native
#
######################################################################
i386-pc-linux:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_LINUX" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=gcc \
		DS_AR=ar \
		DS_LD=ld \
		DS_RL=ranlib \
		DS_ST=strip

######################################################################
#
#	FreeBSD native
#
######################################################################
i386-pc-freebsd:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_FREEBSD -DBSD_COMP -static-libgcc" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=gcc \
		DS_AR=ar \
		DS_LD=ld \
		DS_RL=ranlib \
		DS_ST=strip

######################################################################
#
#	FreeBSD 5.4 crosscompiler
#
######################################################################
cross-i386-pc-freebsd:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_FREEBSD -DBSD_COMP -static-libgcc" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=i386-pc-freebsd5.4-gcc \
		DS_AR=i386-pc-freebsd5.4-ar \
		DS_LD=i386-pc-freebsd5.4-ld \
		DS_RL=i386-pc-freebsd5.4-ranlib \
		DS_ST=i386-pc-freebsd5.4-strip

######################################################################
#
#	Tuxbox crosscompiler
#
######################################################################
cross-powerpc-tuxbox-linux:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_LINUX -DTUXBOX -DPPC" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=powerpc-tuxbox-linux-gnu-gcc \
		DS_AR=powerpc-tuxbox-linux-gnu-ar \
		DS_LD=powerpc-tuxbox-linux-gnu-ld \
		DS_RL=powerpc-tuxbox-linux-gnu-ranlib \
		DS_ST=powerpc-tuxbox-linux-gnu-strip


######################################################################
#
#	sh4 crosscompiler
#
######################################################################
cross-sh4-linux:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_LINUX" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=/opt/STM/STLinux-2.0/devkit/sh4/bin/sh4-linux-gcc \
		DS_AR=/opt/STM/STLinux-2.0/devkit/sh4/bin/sh4-linux-ar \
		DS_LD=/opt/STM/STLinux-2.0/devkit/sh4/bin/sh4-linux-ld \
		DS_RL=/opt/STM/STLinux-2.0/devkit/sh4/bin/sh4-linux-ranlib \
		DS_ST=/opt/STM/STLinux-2.0/devkit/sh4/bin/sh4-linux-strip

######################################################################
#
#	Cygwin crosscompiler
#
######################################################################
cross-i386-pc-cygwin:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_CYGWIN32" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=i686-pc-cygwin-gcc \
		DS_AR=i686-pc-cygwin-ar \
		DS_LD=i686-pc-cygwin-ld \
		DS_RL=i686-pc-cygwin-ranlib \
		DS_ST=i686-pc-cygwin-strip

######################################################################
#
#	Cygwin native
#
######################################################################
i386-pc-cygwin:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_CYGWIN32 -I /tmp/include" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=gcc \
		DS_AR=ar \
		DS_LD=ld \
		DS_RL=ranlib \
		DS_ST=strip

######################################################################
#
#	Solaris 7 crosscompiler
#
######################################################################
cross-sparc-sun-solaris2.7:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_SOLARIS -DOS_SOLARIS7 -DBSD_COMP -static-libgcc" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="-lsocket" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=sparc-sun-solaris2.7-gcc \
		DS_AR=sparc-sun-solaris2.7-ar \
		DS_LD=sparc-sun-solaris2.7-ld \
		DS_RL=sparc-sun-solaris2.7-ranlib \
		DS_ST=sparc-sun-solaris2.7-strip

######################################################################
#
#	AIX 4.2 crosscompiler
#
######################################################################
cross-rs6000-ibm-aix4.2:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthreads" \
		DS_OPTS="-O2 -DOS_AIX -DOS_AIX42 -static-libgcc" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=rs6000-ibm-aix4.2-gcc \
		DS_AR=rs6000-ibm-aix4.2-ar \
		DS_LD=rs6000-ibm-aix4.2-ld \
		DS_RL=rs6000-ibm-aix4.2-ranlib \
		DS_ST=rs6000-ibm-aix4.2-strip

######################################################################
#
#	IRIX 6.5 crosscompiler
#
######################################################################
cross-mips-sgi-irix6.5:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_IRIX -DOS_IRIX65 -static-libgcc" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=mips-sgi-irix6.5-gcc \
		DS_AR=mips-sgi-irix6.5-ar \
		DS_LD=mips-sgi-irix6.5-ld \
		DS_RL=mips-sgi-irix6.5-ranlib \
		DS_ST=mips-sgi-irix6.5-strip

cross-mipsel-router-linux-uclibc: cross-mipsel-router-linux-uclibc928
######################################################################
#
#	Linux MIPS(LE) crosscompiler with ucLibc 0.9.27
#
######################################################################
cross-mipsel-router-linux-uclibc927:
	@-mipsel-linux-uclibc-setlib 0.9.27
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_LINUX -DMIPSEL -DUCLIBC -DUSE_GPIO -static-libgcc" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=mipsel-linux-uclibc-gcc \
		DS_AR=mipsel-linux-uclibc-ar \
		DS_LD=mipsel-linux-uclibc-ld \
		DS_RL=mipsel-linux-uclibc-ranlib \
		DS_ST=mipsel-linux-uclibc-strip

######################################################################
#
#	Linux MIPS(LE) crosscompiler with ucLibc 0.9.28
#
######################################################################
cross-mipsel-router-linux-uclibc928:
	@-mipsel-linux-uclibc-setlib 0.9.28
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_LINUX -DMIPSEL -DUCLIBC -DUSE_GPIO -static-libgcc" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=mipsel-linux-uclibc-gcc \
		DS_AR=mipsel-linux-uclibc-ar \
		DS_LD=mipsel-linux-uclibc-ld \
		DS_RL=mipsel-linux-uclibc-ranlib \
		DS_ST=mipsel-linux-uclibc-strip

######################################################################
#
#	Linux MIPS(LE) crosscompiler with glibc (DM7025)
#
######################################################################
cross-mipsel-tuxbox-linux-glibc:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_LINUX -DTUXBOX -DMIPSEL -static-libgcc" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=mipsel-linux-glibc-gcc \
		DS_AR=mipsel-linux-glibc-ar \
		DS_LD=mipsel-linux-glibc-ld \
		DS_RL=mipsel-linux-glibc-ranlib \
		DS_ST=mipsel-linux-glibc-strip

######################################################################
#
#	HP/UX 10.20 native
#
######################################################################
hppa1.1-hp-hpux10.20:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_HPUX -DOS_HPUX10 -D_XOPEN_SOURCE_EXTENDED" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=gcc \
		DS_AR=ar \
		DS_LD=ld \
		DS_RL=ranlib \
		DS_ST=strip

######################################################################
#
#	OSF5.1 native
#
######################################################################
alpha-dec-osf5.1:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP=$(subst cross-,,$@) \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-O2 -DOS_OSF -DOS_OSF5" \
		XDS_CFLAGS="-I/usr/include -c" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_CC=cc \
		DS_AR=ar \
		DS_LD=ld \
		DS_RL=ranlib \
		DS_ST=strip

######################################################################
#
#	ARM crosscompiler (big-endian)
#
######################################################################
cross-arm-nslu2-linux:
	@-$(MAKE) --no-print-directory \
		-f Maketype TYP="$(subst cross-,,$@)" \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-DOS_LINUX -O2 -DARM -DALIGNMENT" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_AWK="awk" \
		DS_CC="armv5b-softfloat-linux-gcc" \
		DS_AR="armv5b-softfloat-linux-ar" \
		DS_LD="armv5b-softfloat-linux-ld" \
		DS_RL="armv5b-softfloat-linux-ranlib" \
		DS_ST="armv5b-softfloat-linux-strip"

######################################################################
#
#	ARM crosscompiler (big-endian)
#
######################################################################
cross-armBE-unkown-linux:
	-$(MAKE) --no-print-directory \
		-f Maketype TYP="$(subst cross-,,$@)" \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-DOS_LINUX -O2 -DARM -DALIGNMENT" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_AWK="awk" \
		DS_CC="arm-linux-gcc -mbig-endian" \
		DS_AR="arm-linux-ar" \
		DS_LD="arm-linux-ld -EB" \
		DS_RL="arm-linux-ranlib" \
		DS_ST="arm-linux-strip"

######################################################################
#
#	ARM crosscompiler (little-endian)
#
######################################################################
cross-armLE-unkown-linux:
	-$(MAKE) --no-print-directory \
		-f Maketype TYP="$(subst cross-,,$@)" \
		OS_LIBS="" \
		OS_CULI="-lncurses" \
		OS_PTLI="-lpthread" \
		DS_OPTS="-DOS_LINUX -O2 -DARM -DALIGNMENT" \
		DS_CFLAGS="-c" \
		DS_LDFLAGS="" \
		DS_ARFLAGS="-rvsl" \
		DS_AWK="awk" \
		DS_CC="arm-linux-gcc -mlittle-endian" \
		DS_AR="arm-linux-ar" \
		DS_LD="arm-linux-ld -EL" \
		DS_RL="arm-linux-ranlib" \
		DS_ST="arm-linux-strip"
