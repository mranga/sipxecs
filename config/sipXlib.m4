##
## Libs from SipFoundry
##

## Common C and C++ flags for pingtel related source
AC_DEFUN([SFAC_INIT_FLAGS],
[
    AC_REQUIRE([SFAC_SIPX_GLOBAL_OPTS])

    AC_SUBST(CPPUNIT_CFLAGS,  [])
    AC_SUBST(CPPUNIT_LDFLAGS, [])

    ## TODO Remove cpu specifics and use make variables setup for this
    ##
    ## NOTES:
    ##   -D__pingtel_on_posix__   - really used for linux v.s. other
    ##   -D_REENTRANT             - 
    ##   -fmessage-length=0       - ?
    ##

    CFLAGS="-I${prefix}/include $CFLAGS"
    CXXFLAGS="-I${prefix}/include $CXXFLAGS"
    LDFLAGS="-L${prefix}/lib ${LDFLAGS}"

    if test x_"${ax_cv_c_compiler_vendor}" = x_gnu
    then
    	SF_CXX_C_FLAGS="-D__pingtel_on_posix__ -D_linux_ -D_REENTRANT -D_FILE_OFFSET_BITS=64 -fmessage-length=0"
    	SF_CXX_WARNINGS="-Wall -Wformat -Wwrite-strings -Wpointer-arith"
    	CXXFLAGS="$CXXFLAGS $SF_CXX_C_FLAGS $SF_CXX_WARNINGS"
    	CFLAGS="$CFLAGS $SF_CXX_C_FLAGS $SF_CXX_WARNINGS -Wnested-externs -Wmissing-declarations -Wmissing-prototypes"

      # the sfac_strict_compile flag is set by SFAC_STRICT_COMPILE_NO_WARNINGS_ALLOWED
      AC_MSG_CHECKING(how to treat compilation warnings)
      if test x$sfac_strict_compile = xstrictmode 
      then 
         AC_MSG_RESULT([strict mode - treat as errors])
         CXXFLAGS="$CXXFLAGS -Wno-strict-aliasing -fno-strict-aliasing -Werror"
         CFLAGS="$CXXFLAGS -Wno-strict-aliasing -fno-strict-aliasing -Werror"
      else 
         AC_MSG_RESULT([normal mode - allow warnings])
      fi

    elif test x_"${ax_cv_c_compiler_vendor}" = x_sun
    then
        SF_CXX_C_FLAGS="-D__pingtel_on_posix__ -D_REENTRANT -D_FILE_OFFSET_BITS=64 -mt -fast -v"
        SF_CXX_FLAGS="-D__pingtel_on_posix__ -D_REENTRANT -D_FILE_OFFSET_BITS=64 -mt -xlang=c99 -fast -v"
        SF_CXX_WARNINGS=""
        CXXFLAGS="$CXXFLAGS $SF_CXX_FLAGS $SF_CXX_WARNINGS"
        CFLAGS="$CFLAGS $SF_CXX_C_FLAGS $SF_CXX_WARNINGS"
    else
        SF_CXX_C_FLAGS="-D__pingtel_on_posix__ -D_linux_ -D_REENTRANT -D_FILE_OFFSET_BITS=64"
        SF_CXX_WARNINGS=""
        CXXFLAGS="$CXXFLAGS $SF_CXX_C_FLAGS $SF_CXX_WARNINGS"
        CFLAGS="$CFLAGS $SF_CXX_C_FLAGS $SF_CXX_WARNINGS"
    fi

    ## set flag for gcc
    AM_CONDITIONAL(ISGCC, [test  x_"${GCC}" != x_])
])

# sipX-specific options that affect everything and so should be visible at the top level
AC_DEFUN([SFAC_SIPX_GLOBAL_OPTS],
[
    AC_REQUIRE([SFAC_SIPX_INSTALL_PREFIX])

    AC_SUBST(SIPX_INCDIR, [${includedir}])
    AC_SUBST(SIPX_LIBDIR, [${libdir}])
    AC_SUBST(SIPX_LIBEXECDIR, [${libexecdir}/sipXecs])

    ## NOTE: These are not expanded (e.g. contain $(prefix)) and are only
    ## fit for Makefiles. You can however write a Makefile that transforms
    ## *.in to * with the concrete values. 
    ##
    ##  See sipXconfig/Makefile.am for an example.   
    ##  See autoconf manual 4.7.2 Installation Directory Variables for why it's restricted
    ##
    AC_SUBST(SIPX_BINDIR,  [${bindir}])
    AC_SUBST(SIPX_CONFDIR, [${sysconfdir}/sipxpbx])
    AC_SUBST(SIPX_DATADIR, [${datadir}/sipxecs])
    AC_SUBST(SIPX_DOCDIR,  [${datadir}/doc/sipxecs])
    AC_SUBST(SIPX_JAVADIR, [${datadir}/java/sipXecs])
    AC_SUBST(SIPX_VARDIR,  [${localstatedir}/sipxdata])
    AC_SUBST(SIPX_TMPDIR,  [${localstatedir}/sipxdata/tmp])
    AC_SUBST(SIPX_DBDIR,   [${localstatedir}/sipxdata/sipdb])
    AC_SUBST(SIPX_LOGDIR,  [${localstatedir}/log/sipxpbx])
    AC_SUBST(SIPX_RUNDIR,  [${localstatedir}/run/sipxpbx])
    AC_SUBST(SIPX_VARLIB,  [${localstatedir}/lib/sipxpbx])
    AC_SUBST(SIPX_VXMLDATADIR,[${localstatedir}/sipxdata/mediaserver/data])

    # Freeswitch prefix directory
    AC_SUBST(FREESWITCH_PREFIX,  [/usr/local/freeswitch])

    ## Used in a number of different project and subjective where this should really go
    ## INSTALL instruction assume default, otherwise safe to change/override
    AC_ARG_VAR(wwwdir, [Web root for web content, default is ${datadir}/www. \
                        WARNING: Adjust accordingly when following INSTALL instructions])
    test -z $wwwdir && wwwdir='${datadir}/www'

    # Get the user to run sipX under.
    AC_ARG_VAR(SIPXPBXUSER, [The sipX service daemon user name, default is 'sipxchange'])
    test -z "$SIPXPBXUSER" && SIPXPBXUSER=sipxchange

    # Get the group to run sipX under.
    AC_ARG_VAR(SIPXPBXGROUP, [The sipX service daemon group name, default is SIPXPBXUSER])
    test -z "$SIPXPBXGROUP" && SIPXPBXGROUP=$SIPXPBXUSER

    # Test the consistency of SIPXPBXUSER and SIPXPBXGROUP with the host's
    # configuration.  Note that if they don't match, it is not an error,
    # as these are not needed during build.  But developers building and
    # running on the same host should be warned.
    default_group_number=`grep "^$SIPXPBXUSER:" /etc/passwd |
			  head -1 |
			  cut -d: -f4`
    if test -z "$default_group_number"
    then
        AC_MSG_NOTICE([No /etc/passwd entry for SIPXPBXUSER $SIPXPBXUSER (or it has empty default group)])
    elif group_number=`grep "^$SIPXPBXGROUP:" /etc/group |
		       head -1 |
		       cut -d: -f3` ; \
	 test -z "$group_number"
    then
        AC_MSG_NOTICE([No /etc/group entry for SIPXPBXGROUP $SIPXPBXGROUP (or it has empty group number)])
    elif test "$default_group_number" != "$group_number"
    then
        AC_MSG_NOTICE([SIPXPBXGROUP $SIPXPBXGROUP is not the default group for SIPXPBXUSER $SIPXPBXUSER])
    fi

    AC_ARG_ENABLE(rpmbuild, 
      AC_HELP_STRING([--enable-rpmbuild], [Build an rpm]),
      enable_rpmbuild=yes)

    AC_ARG_ENABLE(buildnumber,
                 AC_HELP_STRING([--enable-buildnumber],
                                [Enable build number as part of RPM name]),
                 enable_buildnumber=yes)

    AC_ARG_VAR(USE_IBM_JVM)
    AC_ARG_ENABLE([ibm-jvm],
      AC_HELP_STRING([--enable-ibm-jvm],
        [When enabled, RPM's requiring a JVM will specify the IBM JVM.  Othewise the Sun JVM will be specified.]),
      [USE_IBM_JVM=1],
      [USE_IBM_JVM=0]
    )

    ## --enable-slow-tests enables executing unit tests that take a long time.
    ## "long time" should be approximately "10 seconds or longer".

    AC_ARG_ENABLE(slow-tests, 
      AC_HELP_STRING([--enable-slow-tests], 
        [Enable execution of slow unit tests]),
      AC_DEFINE(EXECUTE_SLOW_TESTS, 1, [Define to run "slow" unit tests.]))

    AC_ARG_VAR(SIPXECS_NAME, [Label for sipXecs, default is 'sipXecs'])
    test -z "$SIPXECS_NAME" && SIPXECS_NAME='sipXecs'

    # Enable profiling via gprof
    ENABLE_PROFILE

    SFAC_DIST_DIR

    SFAC_CONFIGURE_OPTIONS
])

AC_DEFUN([SFAC_CONFIGURE_OPTIONS],
[
  ConfigureArgs=`sed \
    -e '/^ *\$ .*\/configure/!d' \
    -e 's/^ *\$ .*\/configure *//' \
    config.log`

  ## Strip out configure switched that cause issue in RPM spec file
  ## configure switch. Does not support spaces in paths
  for a in $ConfigureArgs; do
    case ${a} in
      --srcdir=*|--cache-file=*|--prefix=*)
        ;;
      *)
        CleanedArgs="$CleanedArgs $a"
        ;;
    esac 
  done

  AC_SUBST(CONFIGURE_OPTIONS, $CleanedArgs)
])

AC_DEFUN([SFAC_SIPX_INSTALL_PREFIX],[
   # set the install prefix
   AC_PREFIX_DEFAULT(${SIPX_INSTALLDIR:-/usr/local/sipx})
])

dnl If SFAC_STRICT_COMPILE_NO_WARNINGS_ALLOWED is included in configure.ac,
dnl   then the default behavior is to treat all gcc warnings as errors.
dnl This can be temporarily disabled by using --disable-compile-strict
dnl If used, this macro must be before SFAC_INIT_FLAGS in configure.ac
AC_DEFUN([SFAC_STRICT_COMPILE_NO_WARNINGS_ALLOWED],[
   AC_BEFORE([$0], [SFAC_INIT_FLAGS])dnl 

   AC_ARG_ENABLE([compile-strict],
     AC_HELP_STRING([--enable-compile-strict],
       [When enabled, treat all compiler warnings as errors - default is to disallow warnings]),
     [if test x$enable_compile_strict = xyes 
      then sfac_strict_compile=strictmode
      else sfac_strict_compile=allowmode
      fi
     ], 
     [sfac_strict_compile=strictmode]
   )
])

## Check to see that we are using the minimum required version of automake
AC_DEFUN([SFAC_AUTOMAKE_VERSION],[
   AC_MSG_CHECKING(for automake version >= $1)
   sf_am_version=`automake --version | head -n 1 | awk '/^automake/ {print $NF}'`
   AX_COMPARE_VERSION( [$1], [le], [$sf_am_version], AC_MSG_RESULT($sf_am_version is ok), AC_MSG_ERROR(found $sf_am_version - you must upgrade automake))
])

AC_DEFUN([SFAC_REQUIRE_LIBWWWSSL],
[
   AC_MSG_CHECKING(W3C libwww ssl requirement)
   AC_ARG_ENABLE(sipx-w3c-libwww-rpm,
     AC_HELP_STRING([--enable-sipx-w3c-libwww-rpm],
       [Forces RPM to require sipx-w3c-libwww, only required on RHE3 or RHE4]),
       LIBWWW_RPM=sipx-w3c-libwww, 
       LIBWWW_RPM=w3c-libwww)
   AC_MSG_RESULT(${LIBWWW_RPM})
   AC_SUBST(LIBWWW_RPM)
])

## sipXportLib 
# SFAC_LIB_PORT attempts to find the sf portability library and include
# files by looking in /usr/[lib|include], /usr/local/[lib|include], and
# relative paths.
#
# If not found, the configure is aborted.  Otherwise, variables are defined
# for both the INC and LIB paths 
# AND the paths are added to the CFLAGS and CXXFLAGS
AC_DEFUN([SFAC_LIB_PORT],
[
    AC_REQUIRE([SFAC_INIT_FLAGS])
    AC_REQUIRE([CHECK_PCRE])
    AC_REQUIRE([CHECK_SSL])

    SFAC_ARG_WITH_INCLUDE([os/OsDefs.h],
            [sipxportinc],
            [ --with-sipxportinc=<dir> portability include path ],
            [sipXportLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN('assuming it will be in '$prefix/include')
        foundpath=$prefix/include
    fi
    SIPXPORTINC=$foundpath
    AC_SUBST(SIPXPORTINC)

    CFLAGS="-I$SIPXPORTINC $PCRE_CFLAGS $CFLAGS"
    CXXFLAGS="-I$SIPXPORTINC $PCRE_CXXFLAGS $CXXFLAGS"

    foundpath=""

    SFAC_ARG_WITH_INCLUDE([sipxunit/TestUtilities.h],
            [sipxportinc],
            [ --with-sipxportinc=<dir> portability include path ],
            [sipXportLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN('sipxunit/TestUtilities.h' not found; assuming it will be in '$prefix/include')
        foundpath=$prefix/include
    fi
    SIPXUNITINC=$foundpath
    AC_SUBST(SIPXUNITINC)

    CFLAGS="-I$SIPXUNITINC $CFLAGS"
    CXXFLAGS="-I$SIPXUNITINC $CXXFLAGS"

    foundpath=""

    SFAC_ARG_WITH_LIB([libsipXport.la],
            [sipxportlib],
            [ --with-sipxportlib=<dir> portability library path ],
            [sipXportLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN(assuming it will be in '$prefix/lib')
        foundpath=$prefix/lib
    fi
    AC_SUBST(SIPXPORT_LIBS, "$foundpath/libsipXport.la")
 
    foundpath=""

    SFAC_ARG_WITH_LIB([libsipXunit.la],
            [sipxportlib],
            [ --with-sipxportlib=<dir> portability library path ],
            [sipXportLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
        # sipXunit unitesting support
    else
        AC_MSG_WARN(assuming it will be in '$prefix/lib')
        foundpath=$prefix/lib
    fi
    AC_SUBST(SIPXUNIT_LIBS,    "$foundpath/libsipXunit.la")

]) # SFAC_LIB_PORT


## sipXtackLib 
# SFAC_LIB_STACK attempts to find the sf networking library and include
# files by looking in /usr/[lib|include], /usr/local/[lib|include], and
# relative paths.
#
# If not found, the configure is aborted.  Otherwise, variables are defined
# for both the INC and LIB paths AND the paths are added to the CFLAGS, 
# CXXFLAGS, LDFLAGS, and LIBS.
AC_DEFUN([SFAC_LIB_STACK],
[
    AC_REQUIRE([SFAC_LIB_PORT])

    SFAC_ARG_WITH_INCLUDE([net/SipUserAgent.h],
            [sipxtackinc],
            [ --with-sipxtackinc=<dir> sip stack include path ],
            [sipXtackLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN(assuming it will be in '$prefix/include')
        foundpath=$prefix/include
    fi
    SIPXTACKINC=$foundpath
    AC_SUBST(SIPXTACKINC)

    if test "$SIPXTACKINC" != "$SIPXPORTINC"
    then
        CFLAGS="-I$SIPXTACKINC $CFLAGS"
        CXXFLAGS="-I$SIPXTACKINC $CXXFLAGS"
    fi

    SFAC_ARG_WITH_LIB([libsipXtack.la],
            [sipxtacklib],
            [ --with-sipxtacklib=<dir> sip stack library path ],
            [sipXtackLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN(assuming it will be in '$prefix/lib')
        foundpath=$prefix/lib
    fi 

    SIPXTACKLIB=$foundpath

    AC_SUBST(SIPXTACK_LIBS,["$SIPXTACKLIB/libsipXtack.la"])
]) # SFAC_LIB_STACK


## sipXmediaLib 
# SFAC_LIB_MEDIA attempts to find the sf media library and include
# files by looking in /usr/[lib|include], /usr/local/[lib|include], and
# relative paths.
#
# If not found, the configure is aborted.  Otherwise, variables are defined
# for both the INC and LIB paths AND the paths are added to the CFLAGS, 
# CXXFLAGS, LDFLAGS, and LIBS.
AC_DEFUN([SFAC_LIB_MEDIA],
[
    AC_REQUIRE([SFAC_LIB_STACK])

    SFAC_ARG_WITH_INCLUDE([mp/MpMediaTask.h],
            [sipxmediainc],
            [ --with-sipxmediainc=<dir> media library include path ],
            [sipXmediaLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN(assuming it will be in '$prefix/include')
        foundpath=$prefix/include
    fi
    SIPXMEDIAINC=$foundpath
    AC_SUBST(SIPXMEDIAINC)

    if test "$SIPXMEDIAINC" != "$SIPXTACKINC"
    then
        CFLAGS="-I$SIPXMEDIAINC $CFLAGS"
        CXXFLAGS="-I$SIPXMEDIAINC $CXXFLAGS"
    fi
    
    SFAC_ARG_WITH_LIB([libsipXmedia.la],
            [sipxmedialib],
            [ --with-sipxmedialib=<dir> media library path ],
            [sipXmediaLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN(assuming it will be in '$prefix/lib')
        foundpath=$prefix/lib
    fi
    SIPXMEDIALIB=$foundpath

    AC_SUBST(SIPXMEDIA_LIBS, ["$SIPXMEDIALIB/libsipXmedia.la"])
]) # SFAC_LIB_MEDIA


## sipXmediaAdapterLib 
# SFAC_LIB_MEDIAADAPTER attempts to find the sf media adapter library and include
# files by looking in /usr/[lib|include], /usr/local/[lib|include], and
# relative paths.
#
# If not found, the configure is aborted.  Otherwise, variables are defined
# for both the INC and LIB paths AND the paths are added to the CFLAGS, 
# CXXFLAGS, LDFLAGS, and LIBS.
AC_DEFUN([SFAC_LIB_MEDIAADAPTER],
[
    AC_REQUIRE([SFAC_LIB_MEDIA])

    SFAC_ARG_WITH_INCLUDE([mi/CpMediaInterface.h],
            [sipxmediainterfaceinc],
            [ --with-sipxmediainterfaceinc=<dir> media interface library include path ],
            [sipXmediaAdapterLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN(assuming it will be in '$prefix/include')
        foundpath=$prefix/include
    fi
    SIPXMEDIAINTERFACEINC=$foundpath
    AC_SUBST(SIPXMEDIAINTERFACEINC)

    if test "$SIPXMEDIAINTERFACEINC" != "$SIPXMEDIAINC"
    then
        CFLAGS="-I$SIPXMEDIAINTERFACEINC $CFLAGS"
        CXXFLAGS="-I$SIPXMEDIAINTERFACEINC $CXXFLAGS"
    fi
    
    SFAC_ARG_WITH_LIB([libsipXmediaProcessing.la],
            [sipxmediaprocessinglib],
            [ --with-sipxmediaprocessinglib=<dir> media library path ],
            [sipXmediaAdapterLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN(assuming it will be in '$prefix/lib')
        foundpath=$prefix/lib
    fi
    SIPXMEDIAADAPTERLIB=$foundpath

    AC_SUBST(SIPXMEDIAADAPTER_LIBS, ["$SIPXMEDIAADAPTERLIB/libsipXmediaProcessing.la"])
]) # SFAC_LIB_MEDIAADAPTER


## Optionally compile in the GIPS library in the media subsystem
# (sipXmediaLib project) and executables that link it in
# Conditionally use the GIPS audio libraries
AC_DEFUN([CHECK_GIPSNEQ],
[
   AC_ARG_WITH(gipsneq,
      [  --with-gipsneq       Compile the media subsystem with the GIPS audio libraries
],
      compile_with_gips=yes)

   gips_file_check=$withval/include/GIPS/Vendor_gips_typedefs.h

   AC_REQUIRE([SFAC_LIB_MEDIAADAPTER])

   AC_MSG_CHECKING(if link in with gips NetEQ)

   if test x$compile_with_gips = xyes
   then
      AC_MSG_RESULT(yes)
      
      SFAC_SRCDIR_EXPAND

      AC_MSG_CHECKING(for gips includes)
      # Define HAVE_GIPS for c pre-processor
      GIPS_CPPFLAGS=-DHAVE_GIPS
      if test -e $gips_file_check
      then
         gips_dir=$withval
      else
         AC_MSG_ERROR($gips_file_check not found)
      fi

      # Cascade flags into RPM build
      DIST_FLAGS="$DIST_FLAGS --with-gipsneq=$gips_dir"

      AC_MSG_RESULT($gips_dir)

      # Add GIPS include path
      GIPSINC=$gips_dir/include
      # Add GIPS objects to be linked in
      GIPS_NEQ_OBJS=$gips_dir/GIPS_SuSE_i386.a
      CPPFLAGS="$CPPFLAGS $GIPS_CPPFLAGS -I$GIPSINC"
      # GIPS needs to be at the end of the list
      #LIBS="$LIBS $GIPS_OBJS"

   else
      AC_MSG_RESULT(no)
   fi

   AC_SUBST(GIPSINC)
   AC_SUBST(GIPS_NEQ_OBJS)
   AC_SUBST(GIPS_CPPFLAGS)
]) # CHECK_GIPSNEQ


AC_DEFUN([CHECK_GIPSVE],
[
   AC_ARG_WITH(gipsve,
      [  --with-gipsve       Link to GIPS voice engine if --with-gipsve is set],
      link_with_gipsve=yes)

   AC_MSG_CHECKING(if linking to gips voice engine)

   if test x$link_with_gipsve = xyes
   then
      AC_MSG_RESULT(yes)
      
      SFAC_SRCDIR_EXPAND

      AC_MSG_CHECKING(for gips includes)

      if test -e $abs_srcdir/../sipXbuild/vendors/gips/VoiceEngine/interface/GipsVoiceEngineLib.h
      then
         gips_dir=$abs_srcdir/../sipXbuild/vendors/gips
      else
         AC_MSG_ERROR(sipXbuild/vendors/gips/VoiceEngine/interface/GipsVoiceEngineLib.h not found)
      fi

      AC_MSG_RESULT($gips_dir)

      # Add GIPS include path
      GIPSINC=$gips_dir/VoiceEngine/interface
      CPPFLAGS="$CPPFLAGS -I$gips_dir/include -I$GIPSINC"
      # Add GIPS objects to be linked in
      GIPS_VE_OBJS="$gips_dir/VoiceEngine/libraries/VoiceEngine_Linux_gcc.a"

   else
      AC_MSG_RESULT(no)
   fi

   AC_SUBST(GIPSINC)
   AC_SUBST(GIPS_VE_OBJS)

   AC_SUBST(SIPXMEDIA_VE_LIBS, ["$SIPXMEDIALIB/libsipXvoiceEngine.la"])

   AM_CONDITIONAL(BUILDVE, test x$link_with_gipsve = xyes)

]) # CHECK_GIPSVE

AC_DEFUN([CHECK_GIPSCE],
[
   AC_ARG_WITH(gipsce,
      [  --with-gipsce       Link to GIPS conference engine if --with-gipsce is set],
      link_with_gipsce=yes)

   AC_MSG_CHECKING(if linking to gips conference engine)

   if test x$link_with_gipsce = xyes
   then
      AC_MSG_RESULT(yes)
      
      SFAC_SRCDIR_EXPAND

      AC_MSG_CHECKING(for gips includes)

      if test -e $abs_srcdir/../sipXbuild/vendors/gips/ConferenceEngine/interface/ConferenceEngine.h
      then
         gips_dir=$abs_srcdir/../sipXbuild/vendors/gips
      else
         AC_MSG_ERROR(sipXbuild/vendors/gips/ConferenceEngine/interface/ConferenceEngine.h not found)
      fi

      AC_MSG_RESULT($gips_dir)

      # Add GIPS include path
      GIPSINC=$gips_dir/ConferenceEngine/interface
      CPPFLAGS="$CPPFLAGS -I$gips_dir/include -I$GIPSINC"
      # Add GIPS objects to be linked in
      GIPS_CE_OBJS="$gips_dir/ConferenceEngine/libraries/ConferenceEngine_Linux_gcc.a"

   else
      AC_MSG_RESULT(no)
   fi

   AC_SUBST(GIPSINC)
   AC_SUBST(GIPS_CE_OBJS)

   AC_SUBST(SIPXMEDIA_CE_LIBS, ["$SIPXMEDIALIB/libsipXconferenceEngine.la"])
   AM_CONDITIONAL(BUILDCE, test x$link_with_gipsce = xyes)

]) # CHECK_GIPSCE


## sipXcallLib
# SFAC_LIB_CALL attempts to find the sf call processing library and include
# files by looking in /usr/[lib|include], /usr/local/[lib|include], and
# relative paths.
#
# If not found, the configure is aborted.  Otherwise, variables are defined
# for both the INC and LIB paths AND the paths are added to the CFLAGS,
# CXXFLAGS, LDFLAGS, and LIBS.
AC_DEFUN([SFAC_LIB_CALL],
[
    AC_REQUIRE([SFAC_LIB_MEDIAADAPTER])

    SFAC_ARG_WITH_INCLUDE([cp/CallManager.h],
            [sipxcallinc],
            [ --with-sipxcallinc=<dir> call processing library include path ],
            [sipXcallLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN(assuming it will be in '$prefix/include')
        foundpath=$prefix/include
    fi
    SIPXCALLINC=$foundpath
    AC_SUBST(SIPXCALLINC)

    if test "$SIPXCALLINC" != "$SIPXPORTINC"
    then
        CFLAGS="-I$SIPXCALLINC $CFLAGS"
        CXXFLAGS="-I$SIPXCALLINC $CXXFLAGS"
    fi

    SFAC_ARG_WITH_LIB([libsipXcall.la],
            [sipxcalllib],
            [ --with-sipxcalllib=<dir> call processing library path ],
            [sipXcallLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN(assuming it will be in '$prefix/lib')
        foundpath=$prefix/lib
    fi
    SIPXCALLLIB=$foundpath

    AC_SUBST(SIPXCALL_LIBS,   ["$SIPXCALLLIB/libsipXcall.la"])
]) # SFAC_LIB_CALL


## sipXcommserverLib

# SFAC_LIB_CALL attempts to find the sf communication server library and 
# include files by looking in /usr/[lib|include], /usr/local/[lib|include], 
# and relative paths.
#
# If not found, the configure is aborted.  Otherwise, variables are defined
# for both the INC and LIB paths AND the paths are added to the CFLAGS,
# CXXFLAGS, LDFLAGS, and LIBS.
AC_DEFUN([SFAC_LIB_COMMSERVER],
[
    AC_REQUIRE([SFAC_LIB_STACK])

    SFAC_ARG_WITH_INCLUDE([sipdb/SIPDBManager.h],
            [sipxcommserverinc],
            [ --with-sipxcommserverinc=<dir> call processing library include path ],
            [sipXcommserverLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN(assuming it will be in '$prefix/include')
        foundpath=$prefix/include
    fi
    SIPXCOMMSERVERINC=$foundpath
    if test "$SIPXCOMMSERVERINC" != "$SIPXPORTINC"
    then
        CFLAGS="-I$SIPXCOMMSERVERINC $CFLAGS"
        CXXFLAGS="-I$SIPXCOMMSERVERINC $CXXFLAGS"
    fi
    AC_SUBST(SIPXCOMMSERVERINC)

    SFAC_ARG_WITH_LIB([libsipXcommserver.la],
            [sipxcommserverlib],
            [ --with-sipxcommserverlib=<dir> call processing library path ],
            [sipXcommserverLib])

    if test x_$foundpath != x_; then
        AC_MSG_RESULT($foundpath)
    else
        AC_MSG_WARN(assuming it will be in '$prefix/lib')
        foundpath=$prefix/lib
    fi
    SIPXCOMMSERVERLIB=$foundpath

    AC_SUBST(SIPXCOMMSERVER_LIBS,   ["$SIPXCOMMSERVERLIB/libsipXcommserver.la"])
    # just assume that the test lib is there too
    AC_SUBST(SIPXCOMMSERVERTEST_LIBS,   ["$SIPXCOMMSERVERLIB/libsipXcommserverTest.la"])
]) # SFAC_LIB_COMMSERVER


##  Generic find of an include
#   Fed from AC_DEFUN([SFAC_INCLUDE_{module name here}],
#
# $1 - sample include file
# $2 - variable name (for overridding with --with-$2
# $3 - help text
# $4 - directory name (assumed parallel with this script)
AC_DEFUN([SFAC_ARG_WITH_INCLUDE],
[
    SFAC_SRCDIR_EXPAND()

    AC_MSG_CHECKING(for [$4] includes)
    AC_ARG_WITH( [$2],
        [ [$3] ],
        [ include_path=$withval ],
        [ include_path="$includedir $prefix/include /usr/include /usr/local/include" ]
    )
    foundpath=""
    for dir in $include_path ; do
        if test -f "$dir/[$1]";
        then
            foundpath=$dir;
            break;
        fi;
    done
    if test x_$foundpath = x_; then
       AC_MSG_WARN("'$1' not found)
       AC_MSG_WARN("    searched $include_path")
    fi
        

]) # SFAC_ARG_WITH_INCLUDE


##  Generic find of a library
#   Fed from AC_DEFUN([SFAC_LIB_{module name here}],
#
# $1 - sample lib file
# $2 - variable name (for overridding with --with-$2
# $3 - help text
# $4 - directory name (assumed parallel with this script)
AC_DEFUN([SFAC_ARG_WITH_LIB],
[
    SFAC_SRCDIR_EXPAND()

    AC_MSG_CHECKING(for [$4] libraries)
    AC_ARG_WITH( [$2],
        [ [$3] ],
        [ lib_path=$withval ],
        [ lib_path="$libdir $prefix/lib /usr/lib /usr/local/lib"]
    )
    foundpath=""
    for dir in $lib_path ; do
        if test -f "$dir/[$1]";
        then
            foundpath=$dir;
            break;
        fi;
    done
    if test x_$foundpath = x_; then
       AC_MSG_WARN("'$1' not found)
       AC_MSG_WARN("    searched $lib_path")
    fi
]) # SFAC_ARG_WITH_LIB


AC_DEFUN([SFAC_SRCDIR_EXPAND], 
[
    abs_srcdir=`cd $srcdir && pwd`
    AC_SUBST(TOP_ABS_SRCDIR, $abs_srcdir)
    AC_SUBST(TOP_SRCDIR, $srcdir)
])


AC_DEFUN([SFAC_FEATURE_SIP_TLS],
[
   AC_ARG_ENABLE(sip-tls, 
                 [  --enable-sip-tls        enable support for sips: and transport=tls (no)],
                 [], [enable_sip_tls=no])
   AC_MSG_CHECKING([Support for SIP over TLS])
   AC_MSG_RESULT(${enable_sip_tls})

   if test "${enable_sip_tls}" = "yes"
   then
      CFLAGS="-DSIP_TLS $CFLAGS"
      CXXFLAGS="-DSIP_TLS $CXXFLAGS"
   fi
])


# Place to store RPM output files
AC_DEFUN([SFAC_DIST_DIR],
[
  AC_ARG_WITH([distdir],
    AC_HELP_STRING([--with-distdir=directory], 
      [Directory to output distribution output files like tarballs, srpms and rpms, default is $(top_builddir)/dist]),
    [DIST_DIR=${withval}],[DIST_DIR=dist])

  mkdir -p "$DIST_DIR" 2>/dev/null
  DIST_DIR=`cd "$DIST_DIR"; pwd`

  # all distro tarballs
  DEST_SRC="${DIST_DIR}/SRC"
  mkdir "${DEST_SRC}"  2>/dev/null
  AC_SUBST([DEST_SRC])

  AC_ARG_VAR([LIBSRC], [Where downloaded files are kept between builds, default ~/libsrc])
  test -z $LIBSRC && LIBSRC=~/libsrc

  # RPM based distros
  AC_PATH_PROG(RPM, rpm)
  AM_CONDITIONAL(RPM_CAPABLE, [test "x$RPM" != "x"])
  if test "x$RPM" != "x"
  then
    DEST_RPM="${DIST_DIR}/RPM"
    mkdir "${DEST_RPM}" 2>/dev/null
    AC_SUBST([DEST_RPM])

    DEST_SRPM="${DIST_DIR}/SRPM"
    mkdir "${DEST_SRPM}"  2>/dev/null
    AC_SUBST([DEST_SRPM])

    DEST_ISO="${DIST_DIR}/ISO"
    mkdir "${DEST_ISO}"  2>/dev/null
    AC_SUBST([DEST_ISO])
    RPMBUILD_TOPDIR="\$(shell rpm --eval '%{_topdir}')"
    AC_SUBST(RPMBUILD_TOPDIR)
    RPM_TARGET_ARCH="\$(shell rpm --eval '%{_target_cpu}')"
    AC_SUBST(RPM_TARGET_ARCH)
  fi

])

AC_DEFUN([SFAC_DOWNLOAD_DEPENDENCIES],
[  
  # URLs to files pulled down files
  AC_SUBST(RUBY_AUX_RPMS_URL, http://people.redhat.com/dlutter/yum)
  AC_SUBST(MOD_CPLUSPLUS_URL, http://umn.dl.sourceforge.net/sourceforge/modcplusplus)
  AC_SUBST(JPKG_FREE_URL, http://mirrors.dotsrc.org/jpackage/1.7/generic/free)
  AC_SUBST(JPKG_NONFREE_URL, http://mirrors.dotsrc.org/jpackage/1.7/generic/non-free)
  AC_SUBST(CGICC_URL, http://ftp.gnu.org/gnu/cgicc)
  AC_SUBST(XERCES_C_URL, http://www.apache.org/dist/xerces/c/sources)
  AC_SUBST(RUBY_RPM_URL, http://dev.centos.org/centos/4/testing)
  AC_SUBST(FC4_RUBY_RPM_URL, http://download.fedora.redhat.com/pub/fedora/linux/core/updates/4)
  AC_SUBST(W3C_URL, http://ftp.redhat.com/pub/redhat/linux/enterprise/4/en/os/i386)
  AC_SUBST(W3C_SRC_URL, http://www.w3.org/Library/Distribution)
  AC_SUBST(PCRE_URL, http://umn.dl.sourceforge.net/sourceforge/pcre)
  #AC_SUBST(CPPUNIT_URL, ftp://download.fedora.redhat.com/pub/fedora/linux/extras/3/SRPMS)
  AC_SUBST(CPPUNIT_URL, http://umn.dl.sourceforge.net/sourceforge/cppunit)
  AC_SUBST(GRAPHVIZ_URL, ftp://194.199.20.114/linux/SuSE-Linux/i386/9.3/suse/src)
  AC_SUBST(CENTOS_URL, http://mirrors.easynews.com/linux/centos)
  AC_SUBST(FC5_URL, http://redhat.download.fedoraproject.org/pub/fedora/linux/core)
  AC_SUBST(FC5_EXTRAS_URL, http://download.fedora.redhat.com/pub/fedora/linux/extras)
  AC_SUBST(FC5_UPDATES_URL, http://download.fedora.redhat.com/pub/fedora/linux/core/updates)
  AC_SUBST(RHEL5_URL, http://mirrors.kernel.org/centos)
  AC_SUBST(RHEL5_UPDATES_URL, http://mirrors.kernel.org/centos/5/updates)
  AC_SUBST(FC6_URL, http://mirrors.kernel.org/fedora/core)
  AC_SUBST(FC6_UPDATES_URL, http://mirrors.kernel.org/fedora/core/updates)
  AC_SUBST(FC6_EXTRAS_URL, http://mirrors.kernel.org/fedora/extras)
  AC_SUBST(RRDTOOL_URL, http://oss.oetiker.ch/rrdtool/pub)
  AC_SUBST(JAIN_SIP_URL, http://download.java.net/communications/jain-sip/nightly/jain-sip-src)
  AC_SUBST(DNSJAVA_URL, http://www.dnsjava.org/download/)

  SFAC_SRCDIR_EXPAND
  download_file="$abs_srcdir/config/download-file"
  AC_SUBST(DOWNLOAD_FILE, $download_file)
])


AC_DEFUN([CHECK_POSTGRES],
[
  AC_ARG_WITH([postgresql_user],
    AC_HELP_STRING([--with-postgresql_user=username],
      [The PostgreSQL user, default is postgres]),
    [POSTGRESQL_USER=${withval}],[POSTGRESQL_USER=postgres])
  AC_SUBST([POSTGRESQL_USER])
  if test -d /usr/include/pgsql ; then
    AC_SUBST(POSTGRESQL_INLCLUDE,--with-pgsql-include-dir=/usr/include/pgsql)
  fi
  AC_MSG_NOTICE([POSTGRESQL_USER = $POSTGRESQL_USER])
  AC_MSG_NOTICE([POSTGRESQL_INCLUDE = $POSTGRESQL_INCLUDE])
])

# This allows configuring where the script that starts sipXecs gets installed
AC_DEFUN([CHECK_SERVICEDIR],
[
  AC_ARG_WITH([servicedir],
    AC_HELP_STRING([--with-servicedir=directory],
      [The directory containing scripts that start services]),
    [SERVICEDIR=${withval}],[SERVICEDIR='$(sysconfdir)/init.d'])
  AC_SUBST([SERVICEDIR])
])
