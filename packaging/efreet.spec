Name:       efreet
Summary:    FreeDesktop.Org Compatibility Library
Version:    1.0.0.001+svn.62616slp2
Release:    1
Group:      TO_BE/FILLED_IN
License:    BSD
URL:        http://www.enlightenment.org/
Source0:    http://download.enlightenment.org/releases/efreet-%{version}.tar.bz2
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(eina)


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

%autogen --disable-static
%configure --disable-static
make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
/usr/lib/*.so.*
/usr/bin/*
/usr/lib/efreet/*


%files devel
%defattr(-,root,root,-)
/usr/include/*
/usr/lib/*.so
/usr/share/*
/usr/lib/pkgconfig/efreet-mime.pc
/usr/lib/pkgconfig/efreet-trash.pc
/usr/lib/pkgconfig/efreet.pc
