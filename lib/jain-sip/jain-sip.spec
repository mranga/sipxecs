# Copyright (c) 2000-2007, JPackage Project
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of the JPackage Project nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

%define javadir         %{_datadir}/java
%define javadocdir      %{_datadir}/javadoc
%define section         free

# -----------------------------------------------------------------------------

Summary:        JAIN-SIP reference implementation
Name:           jain-sip
Version:        1.2
Release:        1
Group:          Development/Java
License:        Apache Software License 2.0
URL:            https://jain-sip.dev.java.net/
BuildArch:      noarch
Source0:        jain-sip-1.2.tar.gz
Patch0:			build-fix.patch
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root

BuildRequires:  jpackage-utils >= 0:1.7.3
BuildRequires:  java-devel >= 0:1.5.0
BuildRequires:  ant >= 0:1.6.5
BuildRequires:  junit >= 0:3.8.2
BuildRequires:  log4j >= 0:1.2
Requires:       log4j >= 0:1.2

%description
Reference implementation of a SIP protocol stack/library that for building
and testing SIP applications and servers.

# -----------------------------------------------------------------------------

%package javadoc
Group:          Documentation
Summary:        Javadoc for %{name}

%description javadoc
Javadoc for %{name}.

# -----------------------------------------------------------------------------

%prep
rm -rf $RPM_BUILD_ROOT
%setup -q
%patch -p1

# -----------------------------------------------------------------------------

%build
export CLASSPATH=$(build-classpath \
junit \
log4j \
)
CLASSPATH=$CLASSPATH:classes

ant -Dbuild.sysclasspath=only compileapi compileri compilesdp sip-sdp-jar javadoc javadoc-jain
# -----------------------------------------------------------------------------

%install
rm -rf $RPM_BUILD_ROOT

# jar
install -d $RPM_BUILD_ROOT%{javadir}/%{name}
# install jars to $RPM_BUILD_ROOT%{javadir}/%{name} (as %{name}-%{version}.jar)
install -pm 644 %{name}-api-%{version}.jar \
  $RPM_BUILD_ROOT%{_javadir}/%{name}/%{name}-api-%{version}.jar
install -pm 644 %{name}-ri-%{version}.jar \
  $RPM_BUILD_ROOT%{_javadir}/%{name}/%{name}-ri-%{version}.jar
install -pm 644 sip-sdp-%{version}.jar \
  $RPM_BUILD_ROOT%{_javadir}/%{name}/sip-sdp-%{version}.jar
(cd $RPM_BUILD_ROOT%{javadir}/%{name} && for jar in *-%{version}.jar; do ln -sf ${jar} `echo $jar| sed  "s|-%{version}||g"`; done)

# javadoc
install -d $RPM_BUILD_ROOT%{javadocdir}/%{name}-%{version}/
install -d $RPM_BUILD_ROOT%{javadocdir}/%{name}-%{version}/jain
install -d $RPM_BUILD_ROOT%{javadocdir}/%{name}-%{version}/ri
cp -pr javadoc-jain/* $RPM_BUILD_ROOT%{javadocdir}/%{name}-%{version}/jain
cp -pr javadoc/* $RPM_BUILD_ROOT%{javadocdir}/%{name}-%{version}/ri

# -----------------------------------------------------------------------------

%clean
rm -rf $RPM_BUILD_ROOT

# -----------------------------------------------------------------------------

%files
%defattr(0644,root,root,0755)
%{javadir}/*

%files javadoc
%defattr(0644,root,root,0755)
%{javadocdir}/%{name}-%{version}

# -----------------------------------------------------------------------------

%changelog
* Thu May 14 2008 Damian Krzeminski <damian at pingtel.com> 0:1.0-1jpp
- Initial build