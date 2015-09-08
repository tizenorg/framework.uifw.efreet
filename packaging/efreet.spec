Name:       efreet
Summary:    FreeDesktop.Org Compatibility Library
Version:    1.7.1+svn.77412+build14
Release:    1
Group:      System/Libraries
License:    BSD 2-Clause
URL:        http://www.enlightenment.org/
Source0:    %{name}-%{version}.tar.gz
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  gettext
BuildRequires:  eina-devel, eet-devel, ecore-devel


%description
Library that implements freedesktop.org specs for use with E17/EFL An implementation of several specifications from freedesktop.org intended for
 use in Enlightenment DR17 (e17) and other applications using the Enlightenment
 Foundation Libraries (EFL). Currently, the following specifications are
 included:
  - Base Directory
  - Desktop Entry
  - Icon Theme
  - Menu
 .
 This package provides the libefreet0 and libefreet0-mime libraries, which
 contains efreet-based functions for dealing with mime.


%package devel
Summary:    FreeDesktop.Org Compatibility Library (devel)
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}


%description devel
Library that implements freedesktop.org specs (devel)


%prep
%setup -q

%build
export CFLAGS+=" -fvisibility=hidden -fPIC -Wall"
export LDFLAGS+=" -fvisibility=hidden -Wl,--hash-style=both -Wl,--as-needed"

%autogen --disable-static
make %{?jobs:-j%jobs}


%install
%make_install
mkdir -p %{buildroot}/%{_datadir}/license
cp %{_builddir}/%{buildsubdir}/COPYING %{buildroot}/%{_datadir}/license/%{name}


%post -p /sbin/ldconfig


%postun -p /sbin/ldconfig


%files
%defattr(-,root,root,-)
%{_libdir}/*.so.*
%{_libdir}/efreet/*
%{_datadir}/license/%{name}
%manifest %{name}.manifest


%files devel
%defattr(-,root,root,-)
%{_includedir}/*
%{_libdir}/*.so
%{_bindir}/*
%{_datadir}/locale
%{_datadir}/efreet
%{_libdir}/pkgconfig/*.pc
