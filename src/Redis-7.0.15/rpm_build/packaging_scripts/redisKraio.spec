Name:           redis-kraio
Version:        7.0.15
Release:        1%{?dist}
Summary:        Redis with io_uring support via kraio
License:        BSD
URL:            https://redis.io
Source0:        redis-7.0.15.tar.gz
Source1:        liburing-2.4.tar.gz
Source2:        libconfig-1.8.1.tar.gz
Source3:        kraio.tar.gz
Patch0:         redis-7.0.15-adapt-iouring.patch

# 禁用 debuginfo，避免 strip
%global debug_package %{nil}
%define __strip /bin/true
%define debuginfo_packages %{nil}

# 构建依赖
BuildRequires:  gcc, make, wget, git, tar, patch, texinfo
BuildRequires:  autoconf, automake, libtool, libuuid-devel
BuildRequires:  liburing-devel, libconfig-devel, systemd-devel

# 运行依赖
Requires:       liburing, libconfig, systemd, glibc >= 2.17

# 自定义平台目录名（如 aarch64-openEuler-linux-gnu）
%define _target_platform %(uname -m)-openEuler-linux-gnu

# 检查是否为 openEuler 22.03 SP4
%define dist_check %(echo %{?dist} | grep -q 'oe2203sp4' && echo 1 || echo 0)

# 获取当前内核版本并提取关键字段
%global kernel_full %(uname -r)
%global kernel_patch_base %(echo %{kernel_full} | awk -F'-' '{print $2}' | awk -F'.' '{print $1}' || echo 0)
%global kernel_update_level %(echo %{kernel_full} | awk -F'[-.]' '{print $7}' | grep -E '^[0-9]+$' || echo 0)

%description
Redis with io_uring acceleration through the kraio library.
This package provides:
- Patched Redis 7.0.15 with io_uring support
- Custom kraio library (libkraio.so) for integration
- Configuration files for io_uring tuning

Note: Depends on system-provided liburing and libconfig.

%prep
echo "DEBUG: Building for dist = %{?dist}"
echo "DEBUG: Kernel version = %{kernel_full}"
echo "DEBUG: Expected base >= 275, got %{kernel_patch_base}"
echo "DEBUG: Expected update >= 178, got %{kernel_update_level}"

# 平台检查
%if %{dist_check} != 1
%fatal This package requires openEuler 22.03 LTS SP4 (detected: %{?dist}). Use --define 'dist .oe2203sp4'.
%endif

# 内核检查
%if %{kernel_patch_base} < 275
%fatal Kernel base %{kernel_patch_base} < 275. Need 275.0.0.178+
%endif

%if %{kernel_patch_base} == 275 && %{kernel_update_level} < 178
%fatal Kernel update %{kernel_update_level} < 178. Need 275.0.0.178+
%endif

echo " All preconditions passed."

# 创建工作目录结构
%setup -T -c -n redis-kraio
mkdir -p %{_target_platform}

# 解压源码
tar -xzf %{SOURCE0} -C %{_target_platform}
tar -xzf %{SOURCE1} -C %{_target_platform}  # 仅用于编译，不安装
tar -xzf %{SOURCE2} -C %{_target_platform}  # 仅用于编译，不安装
tar -xzf %{SOURCE3} -C %{_target_platform}

# 复制补丁
cp %{PATCH0} .

# 打补丁到 Redis
cd %{_target_platform}/redis-%{version}
cp ../../redis-7.0.15-adapt-iouring.patch .
patch -p1 < redis-7.0.15-adapt-iouring.patch

%build
cd %{_target_platform}
TOP_DIR=$(pwd)

# 构建 liburing（仅用于链接，不安装）
cd liburing-2.4
make clean
make -j %{?_smp_mflags} CFLAGS="-g -O2 -fPIC"
cd ..

# 构建 libconfig（仅用于链接，不安装）
cd libconfig-1.8.1
autoreconf --install --force
./configure --prefix=/usr --libdir=%{_libdir} --disable-static CFLAGS="-g -O2" >/dev/null
make -j %{?_smp_mflags} >/dev/null
cd ..

# 构建 kraio
cd kraio
ln -sf $TOP_DIR/liburing-2.4/src/include/liburing.h include/liburing.h
make clean
make CC=gcc \
     LDFLAGS="-shared -fPIC -L../liburing-2.4/src -L../libconfig-1.8.1/lib" \
     LIBS="-luring -lconfig -ldl" \
     CFLAGS="-fPIC -I../libconfig-1.8.1/lib -Iinclude"
cd ..

# 构建 Redis
cd redis-%{version}
make -j %{?_smp_mflags} \
     MALLOC=libc \
     CFLAGS="-I../kraio/include" \
     LDFLAGS="-L../kraio"
%install
# 清理
rm -rf %{buildroot}

# 创建目标目录
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_libdir}
mkdir -p %{buildroot}%{_includedir}
mkdir -p %{buildroot}%{_sysconfdir}/kraio
mkdir -p %{buildroot}%{_sysconfdir}/ld.so.conf.d

# 安装 kraio 库和头文件
install -m 755 %{_target_platform}/kraio/libkraio.so %{buildroot}%{_libdir}/libkraio.so
install -m 644 %{_target_platform}/kraio/include/kraio.h %{buildroot}%{_includedir}/kraio.h
install -m 644 %{_target_platform}/kraio/conf/iouring.conf %{buildroot}%{_sysconfdir}/kraio/iouring.conf

# 安装 ldconfig 配置
echo '/usr/lib64' > %{buildroot}%{_sysconfdir}/ld.so.conf.d/kraio.conf

# 安装 Redis 二进制
cd %{_target_platform}/redis-%{version}
install -m 755 src/redis-server %{buildroot}%{_bindir}/redis-server
install -m 755 src/redis-benchmark     %{buildroot}%{_bindir}/redis-benchmark
install -m 755 src/redis-check-rdb     %{buildroot}%{_bindir}/redis-check-rdb
install -m 755 src/redis-check-aof     %{buildroot}%{_bindir}/redis-check-aof
install -m 755 src/redis-sentinel      %{buildroot}%{_bindir}/redis-sentinel
install -m 755 src/redis-cli           %{buildroot}%{_bindir}/redis-cli
install -m 644 redis.conf              %{buildroot}%{_sysconfdir}/kraio/redis.conf.default

%post
/sbin/ldconfig

# 首次安装提示
if [ "$1" = "1" ]; then
    echo "Redis with kraio installed successfully."
    echo "Please configure /etc/kraio/iouring.conf:"
    echo "  send_callback=1"
    echo "Start Redis with: redis-server /etc/kraio/redis.conf.default"
fi

%postun
if [ "$1" = "0" ]; then
    rm -f %{_sysconfdir}/ld.so.conf.d/kraio.conf
    /sbin/ldconfig
fi

%files
%defattr(-,root,root,-)

# Redis binaries
%{_bindir}/redis-server
%{_bindir}/redis-benchmark
%{_bindir}/redis-check-rdb
%{_bindir}/redis-check-aof
%{_bindir}/redis-sentinel
%{_bindir}/redis-cli

# kraio components
%{_libdir}/libkraio.so
%{_includedir}/kraio.h

# Configuration
%config(noreplace) %{_sysconfdir}/kraio/redis.conf.default
%config(noreplace) %{_sysconfdir}/kraio/iouring.conf
%config %{_sysconfdir}/ld.so.conf.d/kraio.conf

%changelog
* Fri Sep 26 2025  jinzhiyuan <jinzhiyuan2@h-partners.com>- 7.0.15
- Added redis-kraio spec