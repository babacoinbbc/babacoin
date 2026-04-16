# Babacoin Build Instructions

## Ubuntu 20.04 / 22.04 / 24.04 LTS

### Gerekli paketler

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential libtool autotools-dev automake pkg-config \
    bsdmainutils python3 libssl-dev libevent-dev \
    libboost-all-dev libminiupnpc-dev libzmq3-dev \
    libdb5.3-dev libdb5.3++-dev
```

### Derleme

```bash
./autogen.sh
./configure --with-incompatible-bdb \
    BDB_LIBS="-ldb_cxx-5.3" \
    BDB_CFLAGS="-I/usr/include"
make -j$(nproc)
```

### Daemon çalıştırma

```bash
./src/babacoind -daemon
./src/babacoin-cli getblockcount
```

---

## Ubuntu 18.04 (BDB 4.8 ile)

```bash
sudo apt-get install libdb4.8-dev libdb4.8++-dev
./autogen.sh
./configure
make -j$(nproc)
```

---

## Docker ile derleme

```bash
cd ci
docker build -f Dockerfile.builder -t babacoin-builder .
docker run --rm -v $(pwd)/..:/babacoin-src babacoin-builder bash -c \
  "cd /babacoin-src && ./autogen.sh && ./configure --with-incompatible-bdb BDB_LIBS='-ldb_cxx-5.3' && make -j$(nproc)"
```
