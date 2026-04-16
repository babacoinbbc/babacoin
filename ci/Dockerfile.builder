FROM ubuntu:22.04

# Timezone ayarı (apt etkileşimli sormaması için)
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Build ve base paketleri
ENV APT_ARGS="-y --no-install-recommends --no-upgrade"
RUN apt-get update && apt-get install $APT_ARGS \
    git wget unzip curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install $APT_ARGS \
    build-essential g++ g++-12 \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install $APT_ARGS \
    autotools-dev libtool m4 automake autoconf pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 22.04'te libssl-dev = OpenSSL 3.x
RUN apt-get update && apt-get install $APT_ARGS \
    zlib1g-dev libssl-dev ccache bsdmainutils cmake \
    && rm -rf /var/lib/apt/lists/*

# BerkeleyDB 5.3 (Ubuntu 20.04+ için libdb5.3-dev)
RUN apt-get update && apt-get install $APT_ARGS \
    libdb5.3-dev libdb5.3++-dev \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install $APT_ARGS \
    python3 python3-dev python3-pip python3-setuptools python3-zmq \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install $APT_ARGS \
    libevent-dev libboost-all-dev \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install $APT_ARGS \
    libminiupnpc-dev libzmq3-dev \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install $APT_ARGS \
    shellcheck imagemagick libcap-dev librsvg2-bin \
    libz-dev libbz2-dev libtiff-tools \
    && rm -rf /var/lib/apt/lists/*

# Cross-compile araçları
RUN dpkg --add-architecture i386
RUN apt-get update && apt-get install $APT_ARGS \
    g++-arm-linux-gnueabihf \
    g++-mingw-w64-i686 \
    g++-mingw-w64-x86-64 \
    wine-stable wine32 wine64 bc nsis \
    && rm -rf /var/lib/apt/lists/*

# Python paketleri
RUN pip3 install pyzmq jinja2 flake8

# babacoin_hash
RUN git clone https://github.com/babacoin/babacoin_hash && \
    cd babacoin_hash && python3 setup.py install

# GCC sembolik link fix (multilib / arm çakışması)
RUN ln -sf x86_64-linux-gnu/asm /usr/include/asm || true

# MinGW POSIX thread modeli
RUN update-alternatives --set i686-w64-mingw32-gcc   /usr/bin/i686-w64-mingw32-gcc-posix   2>/dev/null || true && \
    update-alternatives --set i686-w64-mingw32-g++   /usr/bin/i686-w64-mingw32-g++-posix   2>/dev/null || true && \
    update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix 2>/dev/null || true && \
    update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix 2>/dev/null || true

ARG USER_ID=1000
ARG GROUP_ID=1000
ENV USER_ID ${USER_ID}
ENV GROUP_ID ${GROUP_ID}

RUN groupadd -g ${GROUP_ID} babacoin && \
    useradd -u ${USER_ID} -g babacoin -s /bin/bash -m -d /babacoin babacoin

RUN mkdir /babacoin-src && \
    mkdir -p /cache/ccache /cache/depends /cache/sdk-sources && \
    chown -R $USER_ID:$GROUP_ID /babacoin-src /cache

WORKDIR /babacoin-src
USER babacoin
