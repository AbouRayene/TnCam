#!/bin/sh

WD=$(dirname $0)

addons="WEBIF HAVE_DVBAPI IRDETO_GUESSING CS_ANTICASC WITH_DEBUG MODULE_MONITOR WITH_SSL WITH_LB CS_CACHEEX LCDSUPPORT IPV6SUPPORT"
protocols="MODULE_CAMD33 MODULE_CAMD35 MODULE_CAMD35_TCP MODULE_NEWCAMD MODULE_CCCAM MODULE_GBOX MODULE_RADEGAST MODULE_SERIAL MODULE_CONSTCW MODULE_PANDORA"
readers="WITH_CARDREADER READER_NAGRA READER_IRDETO READER_CONAX READER_CRYPTOWORKS READER_SECA READER_VIACCESS READER_VIDEOGUARD READER_DRE READER_TONGFANG READER_BULCRYPT"

defconfig="
CONFIG_WEBIF=y
CONFIG_HAVE_DVBAPI=y
CONFIG_IRDETO_GUESSING=y
CONFIG_CS_ANTICASC=y
CONFIG_WITH_DEBUG=y
CONFIG_MODULE_MONITOR=y
# CONFIG_WITH_SSL=n
CONFIG_WITH_LB=y
CONFIG_CS_CACHEEX=y
# CONFIG_LCDSUPPORT=n
# CONFIG_IPV6SUPPORT=n
# CONFIG_MODULE_CAMD33=n
CONFIG_MODULE_CAMD35=y
CONFIG_MODULE_CAMD35_TCP=y
CONFIG_MODULE_NEWCAMD=y
CONFIG_MODULE_CCCAM=y
CONFIG_MODULE_GBOX=y
CONFIG_MODULE_RADEGAST=y
CONFIG_MODULE_SERIAL=y
CONFIG_MODULE_CONSTCW=y
CONFIG_MODULE_PANDORA=y
CONFIG_WITH_CARDREADER=y
CONFIG_READER_NAGRA=y
CONFIG_READER_IRDETO=y
CONFIG_READER_CONAX=y
CONFIG_READER_CRYPTOWORKS=y
CONFIG_READER_SECA=y
CONFIG_READER_VIACCESS=y
CONFIG_READER_VIDEOGUARD=y
CONFIG_READER_DRE=y
CONFIG_READER_TONGFANG=y
CONFIG_READER_BULCRYPT=y
"

list_options() {
	PREFIX="$1"
	shift
	for OPT in $@
	do
		grep "^\#define $OPT$" oscam-config.h >/dev/null 2>/dev/null
		[ $? = 0 ] && echo "${OPT#$PREFIX}"
	done
}

valid_opt() {
	[ "$1" = "" ] && return 0
	echo $addons $protocols $readers | grep -w "$1" >/dev/null
	[ $? = 0 ] && return 1
	return 0
}

enable_opt() {
	OPT="$1"
	valid_opt $OPT
	if [ $? ]
	then
		grep "^\//#define $OPT$" oscam-config.h >/dev/null 2>/dev/null
		if [ $? = 0 ]
		then
			sed -i.bak -e "s|//#define $OPT$|#define $OPT|g" oscam-config.h && rm oscam-config.h.bak
			echo "Enable $OPT"
		fi
	fi
}

disable_opt() {
	OPT="$1"
	valid_opt "$OPT"
	if [ $? ]
	then
		grep "^\#define $OPT$" oscam-config.h >/dev/null 2>/dev/null
		if [ $? = 0 ]
		then
			sed -i.bak -e "s|#define $OPT$|//#define $OPT|g" oscam-config.h && rm oscam-config.h.bak
			echo "Disable $OPT"
		fi
	fi
}

check_test() {
	if [ "$(cat $tempfileconfig | grep "^#define $1$")" != "" ]; then
		echo "on"
	else
		echo "off"
	fi
}

disable_all() {
	for i in $1; do
		sed -i.bak -e "s/^#define ${i}$/\/\/#define ${i}/g" $tempfileconfig
	done
}

enable_package() {
	for i in $(cat $tempfile); do
		strip=$(echo $i | sed "s/\"//g")
		sed -i.bak -e "s/\/\/#define ${strip}$/#define ${strip}/g" $tempfileconfig
	done
}

print_components() {
	clear
	echo "You have selected the following components:"
	echo -e "\nAdd-ons:"
	for i in $addons; do
		printf "\t%-20s: %s\n" $i $(check_test "$i")
	done

	echo -e "\nProtocols:"
	for i in $protocols; do
		printf "\t%-20s: %s\n" $i $(check_test "$i")
	done

	echo -e "\nReaders:"
	for i in $readers; do
		printf "\t%-20s: %s\n" $i $(check_test "$i")
	done
	cp -f $tempfileconfig $configfile
}

menu_addons() {
	${DIALOG} --checklist "\nChoose add-ons:\n " $height $width $listheight \
		WEBIF				"Web Interface"				$(check_test "WEBIF") \
		HAVE_DVBAPI			"DVB API"					$(check_test "HAVE_DVBAPI") \
		IRDETO_GUESSING		"Irdeto guessing"			$(check_test "IRDETO_GUESSING") \
		CS_ANTICASC			"Anti cascading"			$(check_test "CS_ANTICASC") \
		WITH_DEBUG			"Debug messages"			$(check_test "WITH_DEBUG") \
		MODULE_MONITOR		"Monitor"					$(check_test "MODULE_MONITOR") \
		WITH_SSL			"OpenSSL support"			$(check_test "WITH_SSL") \
		WITH_LB				"Loadbalancing"				$(check_test "WITH_LB") \
		CS_CACHEEX			"Cache exchange"			$(check_test "CS_CACHEEX") \
		LCDSUPPORT			"LCD support"				$(check_test "LCDSUPPORT") \
		IPV6SUPPORT			"IPv6 support (experimental)"		$(check_test "IPV6SUPPORT") \
		2> ${tempfile}

	opt=${?}
	if [ $opt != 0 ]; then return; fi

	disable_all "$addons"
	enable_package
}

menu_protocols() {
	${DIALOG} --checklist "\nChoose protocols:\n " $height $width $listheight \
		MODULE_CAMD33		"camd 3.3"		$(check_test "MODULE_CAMD33") \
		MODULE_CAMD35		"camd 3.5 UDP"	        $(check_test "MODULE_CAMD35") \
		MODULE_CAMD35_TCP	"camd 3.5 TCP"	        $(check_test "MODULE_CAMD35_TCP") \
		MODULE_NEWCAMD		"newcamd"		$(check_test "MODULE_NEWCAMD") \
		MODULE_CCCAM		"CCcam"			$(check_test "MODULE_CCCAM") \
		MODULE_GBOX		"gbox"  		$(check_test "MODULE_GBOX") \
		MODULE_RADEGAST		"radegast"		$(check_test "MODULE_RADEGAST") \
		MODULE_SERIAL		"Serial"		$(check_test "MODULE_SERIAL") \
		MODULE_CONSTCW		"constant CW"	        $(check_test "MODULE_CONSTCW") \
		MODULE_PANDORA		"Pandora"		$(check_test "MODULE_PANDORA") \
		2> ${tempfile}

	opt=${?}
	if [ $opt != 0 ]; then return; fi

	disable_all "$protocols"
	enable_package
}

menu_reader() {
	${DIALOG} --checklist "\nChoose reader:\n " $height $width $listheight \
		READER_NAGRA		"Nagravision"		$(check_test "READER_NAGRA") \
		READER_IRDETO		"Irdeto"			$(check_test "READER_IRDETO") \
		READER_CONAX		"Conax"				$(check_test "READER_CONAX") \
		READER_CRYPTOWORKS	"Cryptoworks"		$(check_test "READER_CRYPTOWORKS") \
		READER_SECA			"Seca"				$(check_test "READER_SECA") \
		READER_VIACCESS		"Viaccess"			$(check_test "READER_VIACCESS") \
		READER_VIDEOGUARD	"NDS Videoguard"	$(check_test "READER_VIDEOGUARD") \
		READER_DRE			"DRE Crypt"			$(check_test "READER_DRE") \
		READER_TONGFANG		"Tongfang"			$(check_test "READER_TONGFANG") \
		READER_BULCRYPT		"Bulcrypt"			$(check_test "READER_BULCRYPT") \
		2> ${tempfile}

	opt=${?}
	if [ $opt != 0 ]; then return; fi

	menuitem=`cat $tempfile`
	if [ "$menuitem" != "" ]; then
		echo -n " \"WITH_CARDREADER\"" >> ${tempfile}
	fi
	disable_all "$readers"
	enable_package
}

config_dialog() {
	tempfile=/tmp/test$$
	tempfileconfig=/tmp/oscam-config.h
	configfile=oscam-config.h
	DIALOG=${DIALOG:-`which dialog`}

	height=30
	width=65
	listheight=16

	if [ -z "${DIALOG}" ]; then
		echo "Please install dialog package." 1>&2
		exit 1
	fi

	cp -f $configfile $tempfileconfig

	while true; do
		${DIALOG} --menu "\nSelect category:\n " $height $width $listheight \
			Add-ons		"Add-ons" \
			Protocols	"Network protocols" \
			Reader		"Reader" \
			Save		"Save" \
			2> ${tempfile}

		opt=${?}
		if [ $opt != 0 ]; then clear; rm $tempfile; rm $tempfileconfig; exit; fi

		menuitem=`cat $tempfile`
		case $menuitem in
			Add-ons) menu_addons ;;
			Protocols) menu_protocols ;;
			Reader) menu_reader ;;
			Save)
				print_components
				rm $tempfile
				rm $tempfileconfig
				$0 --make-config.mak
				exit
			;;
		esac
	done
}

case "$1" in
	'-g'|'--gui'|'--config'|'--menuconfig')
		config_dialog
	;;
	'-s'|'--show')
		shift
		case "$1" in
			'addons')
				list_options "" $addons
			;;
			'protocols')
				list_options "MODULE_" $protocols
			;;
			'readers')
				list_options "READER_" $readers
			;;
			*)
				list_options "" $addons $protocols $readers
			;;
		esac
		;;
	'-E'|'--enable')
		shift
		while [ "$1" != "" ]
		do
			enable_opt "$1"
			shift
		done
		$0 --make-config.mak
		;;
	'-D'|'--disable')
		shift
		while [ "$1" != "" ]
		do
			disable_opt "$1"
			shift
		done
		$0 --make-config.mak
		;;
	'-R'|'--restore')
		echo $defconfig | sed -e 's|# ||g' | xargs printf "%s\n" | grep "=y$" | sed -e 's|^CONFIG_||g;s|=.*||g' |
		while read OPT
		do
			enable_opt "$OPT"
		done
		echo $defconfig | sed -e 's|# ||g' | xargs printf "%s\n" | grep "=n$" | sed -e 's|^CONFIG_||g;s|=.*||g' |
		while read OPT
		do
			disable_opt "$OPT"
		done
		$0 --make-config.mak
		;;
	'-e'|'--enabled')
		grep "^\#define $2$" oscam-config.h >/dev/null 2>/dev/null
		if [ $? = 0 ]; then
			echo "Y" && exit 0
		else
			echo "N" && exit 1
		fi
	;;
	'-d'|'--disabled')
		grep "^\#define $2$" oscam-config.h >/dev/null 2>/dev/null
		if [ $? = 1 ]; then
			echo "Y" && exit 0
		else
			echo "N" && exit 1
		fi
	;;
	'-v'|'--oscam-version')
		grep CS_VERSION $WD/globals.h | cut -d\" -f2
	;;
	'-r'|'--oscam-revision')
		(svnversion -n $WD 2>/dev/null || echo -n 0) | sed 's/.*://; s/[^0-9]*$//; s/^$/0/'
	;;
	'--detect-osx-sdk-version')
		shift
		OSX_VER=${1:-10.8}
		for DIR in /Developer/SDKs/MacOSX{$OSX_VER,10.7,10.6,10.5}.sdk
		do
			if test -d $DIR
			then
				echo $DIR
				exit 0
			fi
		done
		echo /Developer/SDKs/MacOSX$(OSX_VER).sdk
	;;
	'-l'|'--list-config')
		for OPT in $addons $protocols $readers
		do
			grep "^\#define $OPT$" oscam-config.h >/dev/null 2>/dev/null
			[ $? = 0 ] && echo "CONFIG_$OPT=y" || echo "# CONFIG_$OPT=n"
		done
		echo "CONFIG_INCLUDED=Yes"
		exit 0
	;;
	'-m'|'--make-config.mak')
		$0 --list-config > config.mak.tmp
		cmp config.mak.tmp config.mak >/dev/null 2>/dev/null
		if [ $? != 0 ]
		then
			mv config.mak.tmp config.mak
		else
			rm config.mak.tmp
		fi
		exit 0
	;;
	*)
		echo \
"OSCam config
Usage: `basename $0` [parameters]

 -g, --gui                 Start interactive configuration

 -s, --show [param]        Show enabled configuration options.
                           Possible params: all, addons, protocols, readers

 -l, --list-config         List active configuration variables.
 -e, --enabled [option]    Check if certain option is enabled.
 -d, --disabled [option]   Check if certain option is disabled.

 -E, --enable [option]     Enable config option.
 -D, --disable [option]    Disable config option.

 -R, --restore             Restore default config.

 -v, --oscam-version       Display OSCam version.
 -r, --oscam-revision      Display OSCam SVN revision.

 -m, --make-config.mak     Create or update config.mak

 -h, --help                Display this help text.

Examples:
  # Enable WEBIF and SSL
  ./config.sh --enable WEBIF WITH_SSL

  # Disable SSL
  ./config.sh --disable WITH_SSL

  # Disable some readers
  ./config.sh --disable MODULE_GBOX MODULE_RADEGAST

Available options:
    addons: $addons
 protocols: $protocols
   readers: $readers
"
		exit 1
	;;
esac
