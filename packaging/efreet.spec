#sbs-git:slp/pkgs/e/efreet efreet 1.1.0+svn.68229slp2+build01 96633d447858c306751083627e503e1e3b2bb0e1
Name:       efreet
Summary:    FreeDesktop.Org Compatibility Library
Version:    1.7.1+svn.77412slp2+build02
Release:    1
Group:      System/Libraries
License:    BSD
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
export CFLAGS+=" -fvisibility=hidden -fPIC"
export LDFLAGS+=" -fvisibility=hidden -Wl,--hash-style=both -Wl,--as-needed"

%autogen --disable-static
%configure --disable-static
make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install
mkdir -p %{buildroot}/usr/share/license
cp %{_builddir}/%{buildsubdir}/COPYING %{buildroot}/usr/share/license/%{name}

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
/usr/lib/*.so.*
/usr/bin/*
/usr/lib/efreet/*
%manifest %{name}.manifest
/usr/share/license/%{name}


%files devel
%defattr(-,root,root,-)
/usr/include/*
/usr/lib/*.so
/usr/share/*
/usr/lib/pkgconfig/efreet-mime.pc
/usr/lib/pkgconfig/efreet-trash.pc
/usr/lib/pkgconfig/efreet.pc

