#!/bin/bash

# in case build is executed from outside current dir be a gem and change the dir
CURRENT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd -P)"
cd $CURRENT_DIR

if [ ! -f /.dockerenv ]; then
  echo "ERROR: This script is only supported running in docker"
  exit 1
fi

if [ -d build ]; then
    rm -rf build/
fi
mkdir -p build
cd build

ver=1.29.3
#curl -fLO https://nginx.org/download/nginx-${ver}.tar.gz
tar xzf ../nginx-${ver}.tar.gz

# PCRE2 (used only for regex in config; disable JIT)
#curl -fLO https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.43/pcre2-10.43.tar.gz
tar xzf ../pcre2-10.43.tar.gz

cd nginx-${ver}

export TOOL=/opt/toolchains/mips-gcc720-glibc229
export CROSS=mips-linux-gnu-
export SYSROOT=/opt/k1-sysroot

export CC="$TOOL/bin/${CROSS}gcc"
export AR="$TOOL/bin/${CROSS}ar"
export RANLIB="$TOOL/bin/${CROSS}ranlib"
export STRIP="$TOOL/bin/${CROSS}strip"

export CFLAGS="--sysroot=$SYSROOT -Os -pipe -EL -march=mips32r2 -mhard-float -mfp64 -mnan=2008 -mno-mips16 -mno-micromips -fno-strict-aliasing -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64"
export LDFLAGS="--sysroot=$SYSROOT -Wl,-EL -Wl,-m,elf32ltsmip -Wl,--gc-sections -Wl,-rpath-link,$SYSROOT/lib -Wl,-rpath-link,$SYSROOT/usr/lib -Wl,--dynamic-linker=/lib/ld-linux-mipsn8.so.1"

export QEMU_LD_PREFIX=/opt/k1-sysroot

./configure \
  --with-cc="$CC" \
  --with-cc-opt="$CFLAGS" \
  --with-ld-opt="$LDFLAGS" \
  --crossbuild=Linux::mipsel \
  --prefix=/usr/data/nginx \
  --conf-path=/usr/data/nginx/etc/nginx.conf \
  --sbin-path=/usr/data/nginx/sbin/nginx \
  --pid-path=/var/run/nginx.pid \
  --lock-path=/var/run/lock/nginx.lock \
  --user=www-data --group=www-data \
  --error-log-path=/var/log/nginx/error.log \
  --http-log-path=/var/log/nginx/access.log \
  --http-client-body-temp-path=/var/tmp/nginx/client-body \
  --http-proxy-temp-path=/var/tmp/nginx/proxy \
  --http-fastcgi-temp-path=/var/tmp/nginx/fastcgi \
  --http-scgi-temp-path=/var/tmp/nginx/scgi \
  --http-uwsgi-temp-path=/var/tmp/nginx/uwsgi \
  --with-pcre=../pcre2-10.43 \
  --without-pcre2 \
  --without-http_gzip_module \
  --without-http-cache \
  --without-http_fastcgi_module \
  --without-http_uwsgi_module \
  --without-http_scgi_module

if [ $? -ne 0 ]; then
  cat objs/autoconf.err
  exit 1
fi

make -j"$(nproc)"
make DESTDIR="$PWD/_staging" install
$STRIP _staging/usr/data/nginx/sbin/nginx || true

echo "Creating tarball..."

rm _staging/usr/data/nginx/etc/koi-utf
rm _staging/usr/data/nginx/etc/nginx.conf.default
rm _staging/usr/data/nginx/etc/nginx.conf
rm _staging/usr/data/nginx/etc/uwsgi_params
rm _staging/usr/data/nginx/etc/mime.types.default
rm _staging/usr/data/nginx/etc/scgi_params
rm _staging/usr/data/nginx/etc/fastcgi_params
rm _staging/usr/data/nginx/etc/koi-win
rm _staging/usr/data/nginx/etc/fastcgi_params.default
rm _staging/usr/data/nginx/etc/uwsgi_params.default
rm _staging/usr/data/nginx/etc/fastcgi.conf.default
rm _staging/usr/data/nginx/etc/win-utf
rm _staging/usr/data/nginx/etc/fastcgi.conf
rm _staging/usr/data/nginx/etc/scgi_params.default
mkdir -p _staging/usr/data/nginx/etc/sites/

tar -C _staging -czf $CURRENT_DIR/build/nginx.tar.gz .
