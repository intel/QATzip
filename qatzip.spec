# SPDX-License-Identifier: MIT

%global githubname QATzip
%global libqatzip_soversion 1

Name:           qatzip
Version:        1.0.3
Release:        1%{?dist}
Summary:        Intel QuickAssist Technology (QAT) QATzip Library
License:        BSD
URL:            https://github.com/intel/%{githubname}
Source0:        https://github.com/intel/%{githubname}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc >= 4.8.5
BuildRequires:  zlib-devel >= 1.2.7
BuildRequires:  qatlib-devel >= 20.10.0
# Placeholder comment, will be replaced with bugzilla reporting unsupported architectures
ExcludeArch:    %{arm} aarch64 %{power64} s390x i686

%description
QATzip is a user space library which builds on top of the Intel
QuickAssist Technology user space library, to provide extended
accelerated compression and decompression services by offloading the
actual compression and decompression request(s) to the Intel Chipset
Series. QATzip produces data using the standard gzip* format
(RFC1952) with extended headers. The data can be decompressed with a
compliant gzip* implementation. QATzip is designed to take full
advantage of the performance provided by Intel QuickAssist
Technology.

%package        devel
Summary:        Development components for the libqatzip package
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description    devel
This package contains headers and libraries required to build
applications that use the QATzip APIs.

%prep
%autosetup -n %{githubname}-%{version}

%build
./configure --prefix=%{_prefix} --enable-symbol
%make_build

%install
%make_install

%files
%license LICENSE*
%{_mandir}/man1/qzip.1*
%{_bindir}/qzip
%{_libdir}/libqatzip.so.%{libqatzip_soversion}
%{_libdir}/libqatzip.so.%{version}

%files devel
%doc %{_mandir}/QATzip-man.pdf
%{_includedir}/qatzip.h
%{_libdir}/libqatzip.so

%changelog
* Tue Mar 23 2021 Ma Zheng <zheng.ma@intel.com> - 1.0.3-1
- Initial version of RPM Package
