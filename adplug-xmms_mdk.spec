%define name adplug-xmms
%define version 1.0
%define release 1mdk

Summary: AdLib player plugin for XMMS
Name: %{name}
Version: %{version}
Release: %{release}
Source0: %{name}-%{version}.tar.bz2
Patch: adplug-xmms-1.0-gcc3.2.patch.bz2
URL: http://adplug.sourceforge.net/
License: LGPL
Group: Sound
BuildRoot: %{_tmppath}/%{name}-buildroot
Prefix: %{_prefix}
Requires: xmms
BuildRequires: libxmms1-devel
BuildRequires: libgtk+1.2-devel
BuildRequires: libadplug-devel

%description
AdPlug/XMMS is an XMMS input plugin. XMMS is a cross-platform multimedia
player. AdPlug/XMMS uses the AdPlug AdLib sound player library to play back
a wide range of AdLib (OPL2) music file formats on top of an OPL2 emulator.
No OPL2 chip is required for playback.


%prep
%setup -q
%patch

%build
%configure2_5x
%make

%install
rm -rf $RPM_BUILD_ROOT
%makeinstall_std
rm -f %buildroot/%_libdir/xmms/Input/*.la

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc README ChangeLog NEWS TODO AUTHORS
%_libdir/xmms/Input/*so

%changelog
* Tue Nov 26 2002 Götz Waschk <waschk@linux-mandrake.com> 1.0-1mdk
- initial package
