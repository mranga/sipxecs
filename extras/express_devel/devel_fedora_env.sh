#!/bin/bash
#
# Copyright (C) 2008 Nortel, certain elements licensed under a Contributor Agreement.  
# Contributors retain copyright to elements licensed under a Contributor Agreement.
# Licensed to the User under the LGPL license.
#
###################################################

# See http://sipx-wiki.calivia.com/index.php/Express_Development_Environment_Setup for instructions.

if [ "`whoami`" != root ]
then
  echo "You must be root in order to run this script."
  exit 1
fi

REBOOT_FLAG="--reboot"
if [ $# -gt 2 ]
then
  echo "Usage: ${0} [${REBOOT_FLAG}] <Username>"
  exit 2
fi

DEVEL_USER=sipx
DEFAULT_PASSWORD=PingMe

function parse_argument {
   if [ $1 == $REBOOT_FLAG ] 
   then
      AutoReboot="yup"
   else
      DEVEL_USER=$1
   fi
}
if [ $# -gt 0 ]
then
  parse_argument $1 
fi
if [ $# -gt 1 ]
then
  parse_argument $2
fi

function wget_retry {
   P_OPT=""
   if [ $# == 2 ]
   then
      P_OPT="-P $2 "
   fi
   echo "  wget_retry for $P_OPT$1..." 
   wget $P_OPT$1
   if [ $? != 0 ]
   then
      wget $P_OPT$1
      if [ $? != 0 ]
      then
         wget $P_OPT$1
         if [ $? != 0 ]
         then
            wget $P_OPT$1
            if [ $? != 0 ]
            then
               echo "    FAILED!" 
               exit 3
            fi               
         fi               
      fi      
   fi
   echo "    SUCCESS." 
}

function rpm_file_install_and_check {
   FILENAME=`ruby -e 'puts File.basename(ARGV[0])' $1`
   BASENAME=`ruby -e 'puts ARGV[0].split(/-[0-9]/)[0]' $FILENAME`

   echo -n "  Checking for (rpm package) $BASENAME..." 
   rpm -q $BASENAME > /dev/null
   if [ $? == 0 ]
   then
      echo " FOUND."
   else
      echo " NOT FOUND, installing..."
      rm -rf {$FILENAME}*
      wget_retry $1
      rpm -i $FILENAME
      if [ $? != 0 ]
      then
         echo "    FAILED to install - $BASENAME!" 
         exit 4
      fi
      rpm -q $BASENAME > /dev/null
      if [ $? = 1 ]
      then
         echo "    no error, but then FAILED to find installed - $BASENAME!" 
         exit 4
      fi
   fi
}

function yum_install_and_check {
   echo -n "  Checking for (yum package) $1..." 
   rpm -q $1 > /dev/null
   if [ $? == 0 ]
   then
      echo " FOUND."
   else
      echo " NOT FOUND, installing..."
      yum -y install $1 
      rpm -q $1 > /dev/null
      if [ $? != 0 ]
      then
         yum -y install $1 
         rpm -q $1 > /dev/null
         if [ $? != 0 ]
         then
            yum -y install $1 
            rpm -q $1 > /dev/null
            if [ $? != 0 ]
            then
               echo "    FAILED to install - $1!" 
               exit 5
            fi
         fi         
      fi
      echo "    DONE."
   fi
}

function gem_install_and_check {
   echo -n "  Checking for (gem package) $1..." 
   if [ gem list | grep $1 > /dev/null ]
   then
      echo " FOUND."
   else
      echo " NOT FOUND, installing..."
      gem install $1 
      if [ $? != 0 ]
      then
         gem install $1 
         if [ $? != 0 ]
         then
            gem install $1 --no-rdoc 
            if [ $? != 0 ]
            then
               echo "    FAILED to install - $1!" 
               exit 6
            fi
         fi         
      fi
      echo "    DONE."
   fi
}

function accept_unsigned_yum_packages {
   sed -i -e "s/gpgcheck=1/gpgcheck=0/g" $1
}

#* Disable SELinux (could be done from a GUI install, but just to be sure....)
if [ ! -e /etc/selinux/config_ORIG ] 
then
  cp /etc/selinux/config /etc/selinux/config_ORIG
fi
echo SELINUX=disabled > /etc/selinux/config

#* Disable the Firewall (could be done from a GUI install, but just to be sure....)
/sbin/service iptables stop
/sbin/chkconfig iptables off

#* General update.
yum -y update

#* The command so nice it's best run twice.
yum -y update

#* Uninstall the incompatible JVM that has recently appeared.
rpm -e java-1.7.0-icedtea-devel 2> /dev/null
rpm -e java-1.7.0-icedtea 2> /dev/null

#* For packages from SIPfoundry.
if [ ! -f /etc/yum.repos.d/sipxecs-unstable-fc.repo ] 
then
   wget_retry http://sipxecs.sipfoundry.org/temp/sipXecs/sipxecs-unstable-fc.repo /etc/yum.repos.d
fi
   # The Fedora 10 experiment.
   uname -a | cut -d" " -f3 | grep fc10 > /dev/null
   if [ $? == 0 ]; then
      # This is a Fedora 10 system.  We don't have a sipXecs repo for 10, but
      # the 8 RPMs work well, at least for now.
      sed -i -e "s/\$releasever/8/g" /etc/yum.repos.d/sipxecs-unstable-fc.repo
   fi

#* Tell yum to accept unsigned packages. 
accept_unsigned_yum_packages /etc/yum.conf 
accept_unsigned_yum_packages /etc/yum.repos.d/sipxecs-unstable-fc.repo

#* Install the required packages.
YUM_PACKAGES="gcc gcc-c++ autoconf automake libtool subversion rpm-build httpd httpd-devel openssl-devel jpackage-utils pcre-devel expat-devel unixODBC-devel jakarta-commons-beanutils jakarta-commons-collections jakarta-commons-net ant log4j junit ant-commons-logging ant-trax ant-nodeps postgresql-server zlib-devel postgresql-devel cppunit cppunit-devel redhat-rpm-config alsa-lib-devel curl gnutls-devel lzo-devel mysql-devel ncurses-devel python-devel ruby ruby-devel ruby-postgres rubygems rubygem-rake ruby-dbi bind cgicc-devel java-1.6.0-sun-devel w3c-libwww-devel xerces-c-devel git tftp-server doxygen rpm-build zip which unzip createrepo ant-junit mod_ssl libXp libpng-devel libart_lgpl-devel freetype freetype-devel rpmdevtools alsa-lib-devel gnutls-devel lzo-devel gdb gdbm-devel mysql-devel ncurses-devel python-devel nsis vsftpd sipx-jasperreports-deps rpmdevtools"
   # The Fedora 10 experiment.
   uname -a | cut -d" " -f3 | grep fc10 > /dev/null
   if [ $? == 0 ]; then
      # This is a Fedora 10 system.
      YUM_PACKAGES="$YUM_PACKAGES libcurl-devel scons"
   else
      # Assume Fedora 8.
      YUM_PACKAGES="$YUM_PACKAGES curl-devel termcap"
   fi
for package in $YUM_PACKAGES;
do
   yum_install_and_check $package
done

#* RPMs you can't get via yum.  But you can with Fedora 10!
RPM_PACKAGES=""
uname -a | cut -d" " -f3 | grep fc10 > /dev/null
if [ 0 != $? ]; then
   RPM_PACKAGES="$RPM_PACKAGES ftp://mirror.switch.ch/pool/1/mirror/epel/5/i386/scons-0.98.1-1.el5.noarch.rpm"
fi
for package in $RPM_PACKAGES;
do
   rpm_file_install_and_check $package
done

#* Ruby gems.
gem update --system
GEM_PACKAGES="file-tail"
for package in $GEM_PACKAGES;
do
   gem_install_and_check $package
done

#* See if the development user already exists.
id $DEVEL_USER 2> /dev/null
if [ $? != 0 ]
then
   # No, so create it and change the password.
   /usr/sbin/useradd $DEVEL_USER
   echo $DEFAULT_PASSWORD | passwd $DEVEL_USER --stdin
fi

#* Give the wheel group password-less sudo privileges.
sed -i -e "s/# %wheel[\t]ALL=(ALL)[\t]NOPASSWD/%wheel\tALL=(ALL)\tNOPASSWD/g" /etc/sudoers 

#* Add the development user to the wheel group.
ETC_GROUP_FILE="/etc/group"
WHEEL_GROUP_ORIG=`grep wheel $ETC_GROUP_FILE`
TMP=`echo $WHEEL_GROUP_ORIG | grep $DEVEL_USER | cut -d: -f4`
MISSING=`ruby -e 'ARGV[0].split(",").each {|x| if x == ARGV[1] then exit end}; puts "missing"' $TMP $DEVEL_USER`
if [ $MISSING ]
then
   sed -i -e "s/$WHEEL_GROUP_ORIG/$WHEEL_GROUP_ORIG,$DEVEL_USER/g" $ETC_GROUP_FILE
fi

# Enable TFTP.  Don't bother chaging the /tftpboot, it will later be replaced with a symbolic link
# by the $DEVEL_USER.
sed -i -e "s/[\t]disable[\t][\t][\t]= yes/\tdisable\t\t\t= no/g" /etc/xinetd.d/tftp
/sbin/service xinetd restart

# Enable FTP with a Polycom user, also using the /tftpboot directory.
/usr/sbin/useradd -d /tftpboot -G $DEVEL_USER -s /sbin/nologin -M PlcmSpIp
echo -e "PlcmSpIp" | sudo passwd --stdin PlcmSpIp
echo "dirlist_enable=NO" >> /etc/vsftpd/vsftpd.conf
/sbin/chkconfig vsftpd on
/sbin/service vsftpd restart

# Enable postgresql.
/sbin/service postgresql initdb
/sbin/chkconfig postgresql on
/sbin/service postgresql start

# Get rid of the svn certificate prompt for $DEVEL_USER, which may be useful.
sudo su - $DEVEL_USER -c "echo p | svn co https://sipxecs.sipfoundry.org/rep/sipXecs/main/sipXcallLib/include/tapi/ /tmp/del_me"
rm -rf /tmp/del_me

# Reboot.
echo -e '\a' ; sleep 1 ; echo -e '\a' ; sleep 1 ; echo -e '\a'
echo ""
echo -n "Script complete, "
if [ $AutoReboot ]
then
   echo -n "rebooting now"
   ruby -e '(1..10).each {print "."; $stdout.flush; sleep(1) }'
   echo ""
   reboot
else
   echo "please reboot now."
   echo ""
fi

