FROM --platform=linux/amd64 ubuntu:22.04

ENV PS3DEV=/ps3dev
ENV PSL1GHT=/ps3dev
ENV PATH="${PS3DEV}/bin:${PS3DEV}/ppu/bin:${PS3DEV}/spu/bin:${PATH}"

RUN apt-get update && apt-get install -y \
    curl \
    make \
    git \
    python2.7 \
    python3 \
    python3-dev \
    libelf1 \
    libelf-dev \
    elfutils \
    gettext \
    autoconf \
    automake \
    libtool \
    pkg-config \
    wget \
    cmake \
    ninja-build \
    libgmp-dev \
    libssl-dev \
    zlib1g-dev \
    bison \
    flex \
    gcc \
    g++ \
    texinfo \
    libncurses5-dev \
    patch \
    bzip2 \
    subversion \
    && rm -rf /var/lib/apt/lists/*

RUN curl -sL http://archive.ubuntu.com/ubuntu/pool/main/o/openssl/libssl1.1_1.1.1f-1ubuntu2_amd64.deb -o /tmp/libssl.deb && \
    dpkg -i /tmp/libssl.deb && \
    rm /tmp/libssl.deb

RUN mkdir -p /ps3dev && \
    curl -sL https://github.com/bucanero/ps3toolchain/releases/download/ubuntu-latest-fad3b5fb/ps3dev-ubuntu-latest-2020-08-31.tar.gz | tar xz -C / && \
    curl -sL https://github.com/ps3dev/PSL1GHT/raw/master/ppu/include/sysutil/sysutil.h -o /ps3dev/ppu/include/sysutil/sysutil.h

RUN git clone --depth 1 https://github.com/bucanero/ya2d_ps3.git /tmp/ya2d_ps3 && \
    cd /tmp/ya2d_ps3/libya2d && \
    make install && \
    rm -rf /tmp/ya2d_ps3

RUN git clone --depth 1 https://github.com/bucanero/mini18n.git /tmp/mini18n && \
    cd /tmp/mini18n && \
    make install && \
    rm -rf /tmp/mini18n

RUN git clone --depth 1 https://github.com/bucanero/dbglogger.git /tmp/dbglogger && \
    cd /tmp/dbglogger && \
    make install && \
    rm -rf /tmp/dbglogger

# Build mbedTLS for PS3
RUN cd /tmp && git clone --depth 1 --branch v3.6.0 https://github.com/ARMmbed/mbedtls.git && \
    cd mbedtls && \
    git submodule update --init --recursive && \
    python3 scripts/config.py unset MBEDTLS_PLATFORM_ENTROPY && \
    python3 scripts/config.py unset MBEDTLS_HAVE_TIME && \
    python3 scripts/config.py unset MBEDTLS_HAVE_TIME_DATE && \
    python3 scripts/config.py unset MBEDTLS_TIMING_C && \
    python3 scripts/config.py unset MBEDTLS_NET_C && \
    python3 scripts/config.py set MBEDTLS_NO_PLATFORM_ENTROPY && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_SYSTEM_NAME=Generic \
        -DCMAKE_SYSTEM_PROCESSOR=powerpc64 \
        -DCMAKE_C_COMPILER=powerpc64-ps3-elf-gcc \
        -DCMAKE_C_FLAGS="-mcpu=cell" \
        -DCMAKE_INSTALL_PREFIX=/opt/mbedtls \
        -DENABLE_TESTING=OFF \
        -DENABLE_PROGRAMS=OFF \
        -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
        -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) && make install && \
    rm -rf /tmp/mbedtls

# Install PSL1GHT libraries for PS3 networking (libnet, libsysutil, libsysmodule)
RUN cd /tmp && \
    git clone --depth 1 https://github.com/ps3dev/PSL1GHT.git && \
    cd PSL1GHT && \
    make install-ctrl && make && make install && \
    rm -rf /tmp/PSL1GHT

# Build nghttp2 for HTTP/2 support
RUN cd /tmp && wget https://github.com/nghttp2/nghttp2/releases/download/v1.57.0/nghttp2-1.57.0.tar.gz && \
    tar xzf nghttp2-1.57.0.tar.gz && cd nghttp2-1.57.0 && \
    ./configure \
        --host=powerpc64-ps3-elf \
        --prefix=/opt/nghttp2 \
        --disable-shared \
        --enable-static \
        --disable-examples \
        --disable-app \
        --enable-lib-only && \
    make -j$(nproc) && make install && \
    rm -rf /tmp/nghttp2-1.57.0

# Build libcurl 7.88.1 for PS3 with mbedTLS 3.6.0 (TLS 1.3) + nghttp2 (HTTP/2)
RUN cd /tmp && wget https://curl.se/download/curl-7.88.1.tar.gz && \
    tar xzf curl-7.88.1.tar.gz && cd curl-7.88.1 && \
    PKG_CONFIG_PATH="/opt/nghttp2/lib/pkgconfig" \
    LDFLAGS="-L/ps3dev/ppu/lib -L/opt/mbedtls/lib -L/opt/nghttp2/lib" \
    CPPFLAGS="-I/ps3dev/ppu/include -I/opt/mbedtls/include -I/opt/nghttp2/include" \
    LIBS="-lnet -lsysutil -lsysmodule -lmbedtls -lmbedx509 -lmbedcrypto -lnghttp2" \
    ./configure \
        --host=powerpc64-ps3-elf \
        --build=x86_64-linux-gnu \
        --prefix=/opt/libcurl \
        --disable-shared \
        --enable-static \
        --with-mbedtls=/opt/mbedtls \
        --with-nghttp2=/opt/nghttp2 \
        --enable-http2 \
        --disable-ipv6 \
        --disable-threaded-resolver \
        --disable-ntlm \
        --disable-ntlm-wb \
        --without-zlib \
        --without-brotli \
        --without-zstd \
        --without-libpsl \
        --disable-ldap \
        --disable-ldaps \
        --disable-rtsp \
        --disable-dict \
        --disable-telnet \
        --disable-tftp \
        --disable-pop3 \
        --disable-imap \
        --disable-smb \
        --disable-smtp \
        --disable-gopher \
        --disable-manual \
        --enable-hidden-symbols \
        --with-ca-bundle=none && \
    make -j$(nproc) && make install && \
    rm -rf /tmp/curl-7.88.1

WORKDIR /src
