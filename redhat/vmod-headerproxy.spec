Summary: Varnish Header Proxy VMOD
Name: vmod-headerproxy
Version: 0.1.0
Release: 1%{?dist}
License: BSD
Group: System Environment/Daemons
URL: https://github.com/SteveEasley/libvmod-headerproxy
Source: libvmod-headerproxy-0.1.0.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Requires: varnish >= 4.0.2
BuildRequires: make
BuildRequires: python-docutils
BuildRequires: varnish-libs-devel >= 4.0.2

%description
Varnish Header Proxy VMOD

%prep
%setup -n libvmod-headerproxy-%{version}

%build
%configure \
#    --enable-debug
%{__make} %{?_smp_mflags}
%{__make} %{?_smp_mflags} check

%install
[ %{buildroot} != "/" ] && %{__rm} -rf %{buildroot}
%{__make} install DESTDIR=%{buildroot}

%clean
[ %{buildroot} != "/" ] && %{__rm} -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_libdir}/varnis*/vmods/
%doc /usr/share/doc/lib%{name}/*
%{_mandir}/man?/*

