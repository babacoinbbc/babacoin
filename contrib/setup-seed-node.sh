#!/bin/bash
# BabaCoin v2.0.0 Seed Node Auto-Setup
# Ubuntu 24.04 ARM64 için
# Kullanım: wget -qO- https://raw.githubusercontent.com/... | bash
# Veya: curl -fsSL ... | SEED_NUM=01 bash

set -euo pipefail

SEED_NUM="${SEED_NUM:-01}"
RPC_USER="babacoin"
RPC_PASS="$(openssl rand -hex 16)"

echo "===== BabaCoin v2.0.0 Seed Node Setup ====="
echo "Seed Number: seed${SEED_NUM}"
echo "Date: $(date)"
echo ""

# 1. System update
echo ">>> [1/7] Sistem güncelleniyor..."
sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get upgrade -y -qq

# 2. Gerekli bağımlılıklar
echo ">>> [2/7] Runtime bağımlılıklar kuruluyor..."
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    wget curl tar ufw \
    libboost-filesystem1.83.0 libboost-system1.83.0 libboost-thread1.83.0 \
    libboost-program-options1.83.0 libboost-chrono1.83.0 libboost-date-time1.83.0 \
    libssl3t64 libevent-2.1-7t64 libdb5.3t64 libminiupnpc17 \
    libzmq5 libqrencode4 libgmp10 libprotobuf32t64 libsodium23

# 3. Babacoin v2.0.0 indirme (Ubuntu 24.04 ARM64)
echo ">>> [3/7] Babacoin v2.0.0 indiriliyor..."
cd /tmp
wget -q https://github.com/babacoinbbc/babacoin/releases/download/v2.0.0-test/babacoin-v2.0.0-linux-ubuntu24.04-arm64.tar.gz
tar xzf babacoin-v2.0.0-linux-ubuntu24.04-arm64.tar.gz

# Binary'leri /usr/local/bin'e kopyala
sudo cp babacoind babacoin-cli babacoin-tx /usr/local/bin/
sudo chmod +x /usr/local/bin/babacoin*
rm -rf /tmp/babacoin-v2*

# 4. Konfigürasyon
echo ">>> [4/7] Konfigürasyon oluşturuluyor..."
EXTERNAL_IP=$(curl -s -4 ifconfig.me || curl -s ipv4.icanhazip.com)
echo "External IP: $EXTERNAL_IP"

mkdir -p ~/.babacoin
cat > ~/.babacoin/babacoin.conf << EOF
# BabaCoin Seed Node seed${SEED_NUM}
listen=1
server=1
daemon=1
txindex=1
externalip=${EXTERNAL_IP}
maxconnections=256
dbcache=4096
par=0

# RPC (lokal)
rpcallowip=127.0.0.1
rpcbind=127.0.0.1
rpcuser=${RPC_USER}
rpcpassword=${RPC_PASS}

# Seed node davranışı
discover=1
dnsseed=1

# Diğer seed'lere bağlan
addnode=seed00.babacoin.network
addnode=seed01.babacoin.network
addnode=seed02.babacoin.network
addnode=seed03.babacoin.network
addnode=seed04.babacoin.network
addnode=seed05.babacoin.network
addnode=seed06.babacoin.network
addnode=seed07.babacoin.network
addnode=seed08.babacoin.network
addnode=seed09.babacoin.network
addnode=seed10.babacoin.network
EOF

chmod 600 ~/.babacoin/babacoin.conf

# 5. Firewall
echo ">>> [5/7] UFW firewall yapılandırılıyor..."
sudo ufw allow 22/tcp comment 'SSH' >/dev/null
sudo ufw allow 6678/tcp comment 'Babacoin P2P' >/dev/null
sudo ufw --force enable >/dev/null

# Oracle Cloud'da ayrıca iptables (Oracle'ın default INPUT DROP'u)
sudo iptables -I INPUT 6 -p tcp --dport 6678 -j ACCEPT 2>/dev/null || true
sudo netfilter-persistent save 2>/dev/null || sudo iptables-save | sudo tee /etc/iptables/rules.v4 >/dev/null || true

# 6. systemd service
echo ">>> [6/7] systemd servisi oluşturuluyor..."
sudo tee /etc/systemd/system/babacoind.service > /dev/null << EOF
[Unit]
Description=Babacoin Core Seed Node (seed${SEED_NUM})
After=network-online.target
Wants=network-online.target

[Service]
Type=forking
User=$USER
Group=$USER
WorkingDirectory=$HOME
ExecStart=/usr/local/bin/babacoind -daemon -conf=$HOME/.babacoin/babacoin.conf
ExecStop=/usr/local/bin/babacoin-cli stop
Restart=on-failure
RestartSec=30
TimeoutStopSec=300

# Kaynaklar
LimitNOFILE=65536
MemoryMax=20G

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable babacoind
sudo systemctl start babacoind

# 7. Sonuç
echo ">>> [7/7] Başlatma bekleniyor..."
sleep 20

echo ""
echo "===== KURULUM TAMAMLANDI ====="
echo ""
echo "Public IP: $EXTERNAL_IP"
echo "DNS: seed${SEED_NUM}.babacoin.network → $EXTERNAL_IP"
echo "RPC User: $RPC_USER"
echo "RPC Pass: $RPC_PASS  ← GÜVENLİ YERDE SAKLA"
echo ""
echo "=== Durum Kontrolü ==="
sudo systemctl status babacoind --no-pager | head -10
echo ""
babacoin-cli getblockchaininfo 2>/dev/null | grep -E "blocks|headers" || echo "Daemon henüz hazır değil, 30 saniye sonra tekrar dene"

echo ""
echo "=== Kullanışlı Komutlar ==="
echo "  babacoin-cli getblockchaininfo | grep blocks"
echo "  babacoin-cli getconnectioncount"
echo "  babacoin-cli getpeerinfo | head -50"
echo "  sudo systemctl status babacoind"
echo "  sudo journalctl -u babacoind -f"
echo "  tail -f ~/.babacoin/debug.log"
echo ""
echo "DNS'i Cloudflare/Route53'te güncellemeyi unutma:"
echo "  seed${SEED_NUM}.babacoin.network → $EXTERNAL_IP"
