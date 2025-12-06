FROM ubuntu:22.04

ENV PS3DEV=/ps3dev
ENV PSL1GHT=/ps3dev
ENV PATH="${PS3DEV}/bin:${PS3DEV}/ppu/bin:${PS3DEV}/spu/bin:${PATH}"

RUN apt-get update && apt-get install -y \
    curl \
    make \
    git \
    python2.7 \
    python3 \
    libelf1 \
    gettext \
    autoconf \
    automake \
    libtool \
    pkg-config \
    wget \
    cmake \
    ninja-build \
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

# Create compatibility symlinks for PS3 socket headers (net/socket.h -> sys/socket.h)
RUN mkdir -p /ps3dev/ppu/include/sys && \
    ln -sf ../net/socket.h /ps3dev/ppu/include/sys/socket.h && \
    ln -sf ../netinet/in.h /ps3dev/ppu/include/sys/netinet_in.h

# Copy CMake toolchain file for PS3 cross-compilation
RUN mkdir -p /tmp/cmake
COPY docker/ps3-toolchain.cmake /tmp/cmake/

# Build libcurl 7.79.1 for PS3 with mbedTLS 3.6.0 (older, more stable)
RUN cd /tmp && wget https://curl.se/download/curl-7.79.1.tar.gz && \
    tar xzf curl-7.79.1.tar.gz && cd curl-7.79.1 && \
    ./configure \
        --host=powerpc64-ps3-elf \
        --build=x86_64-linux-gnu \
        --prefix=/opt/libcurl \
        --disable-shared \
        --enable-static \
        --with-mbedtls=/opt/mbedtls \
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
        --disable-ftp \
        --disable-file \
        --disable-manual \
        --disable-threaded-resolver \
        --disable-curldebug \
        --with-ca-bundle=none && \
    make -j$(nproc) && make install && \
    rm -rf /tmp/curl-7.79.1

WORKDIR /src