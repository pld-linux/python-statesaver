
%define	module	statesaver

Summary:	Python object duplicator working on generators and iterators
Summary(pl.UTF-8):   Duplikator obiektów Pythona działający na generatorach i iteratorach
Name:		python-%{module}
Version:	0.20051220.1
Release:	1
License:	GPL
Group:		Development/Languages/Python
Source0:	statesaver.c
# NoSource0-md5:	0a43470f63f7151df8d00f8c9942ab21
Source1:	statesaver-setup.py
# NoSource1-md5:	e62b320692c79bdaba519c977bbd6b33
BuildRequires:	python-devel
BuildRoot:	%{tmpdir}/%{name}-%{version}-root-%(id -u -n)

%description
Object duplicator working on generators and iterators.

%description -l pl.UTF-8
Duplikator obiektów działający na generatorach i iteratorach.

%prep
%setup -q -c -T
install %{SOURCE0} .
install %{SOURCE1} setup.py

%build
python setup.py build

%install
rm -rf $RPM_BUILD_ROOT

python setup.py install \
	--root=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(644,root,root,755)
%attr(755,root,root) %{py_sitedir}/*.so
