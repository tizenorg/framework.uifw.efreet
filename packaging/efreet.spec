#sbs-git:slp/pkgs/e/efreet efreet 1.1.0+svn.68229slp2+build01 96633d447858c306751083627e503e1e3b2bb0e1
Name:           efreet
Version:        1.7.1+svn.77412slp2+build02
Release:        1
License:        BSD
Summary:        FreeDesktop
Url:            http://www.enlightenment.org/
Group:          System/Libraries
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  pkgconfig(eet)
BuildRequires:  pkgconfig(eina)
BuildRequires:  gettext
BuildRequires:  pkgconfig(ecore)

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
Summary:        FreeDesktop
Group:          Development/Libraries
Requires:       %{name} = %{version}

%description devel
Library that implements freedesktop.org specs (devel)

%prep
%setup -q

%build
export CFLAGS+=" -fvisibility=hidden -fPIC"
export LDFLAGS+=" -fvisibility=hidden -Wl,--hash-style=both -Wl,--as-needed"

%autogen --disable-static
%configure --disable-static
make %{?_smp_mflags}

%install
%make_install
mkdir -p %{buildroot}%{_datadir}/license
cp %{_builddir}/%{buildsubdir}/COPYING %{buildroot}%{_datadir}/license/%{name}

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/*.so.*
%{_bindir}/*
%{_libdir}/efreet/*
%manifest %{name}.manifest
%{_datadir}/license/%{name}


%files devel
%defattr(-,root,root,-)
%{_includedir}/*
%{_libdir}/*.so
%{_datadir}/*
%{_libdir}/pkgconfig/efreet-mime.pc
%{_libdir}/pkgconfig/efreet-trash.pc
%{_libdir}/pkgconfig/efreet.pc

