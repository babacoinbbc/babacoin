#!/usr/bin/env bash
#
# ============================================================================
# BabaCoin v2.0.0 — Oracle ARM Ubuntu 22.04 Seed Node Kurulum Script
# ============================================================================
# Kullanım:
#   curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | bash
#   curl -fsSL ... | SEED_NUM=03 bash
#
# Gereksinimler:
#   - Oracle Cloud ARM instance (VM.Standard.A1.Flex)
#   - Ubuntu 22.04 LTS aarch64
#   - sudo yetkisi
#   - Port 6678 TCP açık (Security List + UFW)
#
# Özellikler:
#   ✓ Idempotent (tekrar çalıştırılabilir)
#   ✓ Eski v1 binary'yi otomatik yedekler
#   ✓ Oracle Cloud iptables DROP policy'sini bypass eder
#   ✓ systemd servisi kurar (reboot'ta otomatik başlar)
#   ✓ Güçlü rastgele RPC şifresi üretir
#   ✓ Comprehensive error handling
#   ✓ Renkli ve detaylı log çıktısı
# ============================================================================

set -euo pipefail

# ===== Konfigürasyon =====
SEED_NUM="${SEED_NUM:-auto}"
BBC_VERSION="v2.0.0-test"
BINARY_URL="https://github.com/babacoinbbc/babacoin/releases/download/${BBC_VERSION}/babacoin-v2.0.0-linux-ubuntu22.04-arm64.tar.gz"
WORK_DIR="${HOME}/bbc-install-$(date +%s)"
DATA_DIR="${HOME}/.babacoin"
SERVICE_NAME="babacoind"
BBC_USER="${USER}"
BBC_HOME="${HOME}"

# ===== Renkler =====
R="\033[1;31m"; G="\033[1;32m"; Y="\033[1;33m"; B="\033[1;34m"; C="\033[1;36m"; N="\033[0m"

# ===== Yardımcı Fonksiyonlar =====
log()     { printf "${C}[%s]${N} %s\n" "$(date +%H:%M:%S)" "$*"; }
ok()      { printf "${G}✓${N} %s\n" "$*"; }
warn()    { printf "${Y}⚠${N} %s\n" "$*"; }
err()     { printf "${R}✗${N} %s\n" "$*" >&2; }
title()   { printf "\n${B}╔══════════════════════════════════════════════════════════════╗${N}\n${B}║${N} %-60s ${B}║${N}\n${B}╚══════════════════════════════════════════════════════════════╝${N}\n" "$*"; }

die() { err "$*"; exit 1; }

step() {
    local num="$1" desc="$2"
    printf "\n${B}▶ [${num}] ${desc}${N}\n"
}

confirm_or_auto() {
    local prompt="$1"
    if [ "${AUTO_YES:-}" = "1" ] || [ ! -t 0 ]; then
        return 0
    fi
    read -rp "$prompt [Y/n] " answer
    [[ ! "$answer" =~ ^[Nn]$ ]]
}

# ===== Başlangıç Kontrolleri =====
title "BabaCoin Seed Node Kurulum — Oracle ARM Ubuntu 22.04"

log "Çalışma zamanı: $(date)"
log "Kullanıcı: ${USER}"
log "Home: ${HOME}"
log "Seed Number: ${SEED_NUM}"
log "BBC Version: ${BBC_VERSION}"

# Root olarak çalıştırma kontrolü
if [ "$EUID" -eq 0 ]; then
    die "Bu script root olarak çalıştırılmamalı. Normal kullanıcı (ubuntu) olarak çalıştır, sudo otomatik kullanılacak."
fi

# sudo var mı?
if ! sudo -n true 2>/dev/null && ! sudo -v; then
    die "sudo yetkisi gerekli. 'sudo -v' ile doğrula ve tekrar dene."
fi

# OS kontrolü
if [ ! -f /etc/os-release ]; then
    die "/etc/os-release bulunamadı — desteklenmeyen sistem"
fi
source /etc/os-release
if [ "${ID}" != "ubuntu" ]; then
    die "Bu script sadece Ubuntu için. Mevcut: ${ID}"
fi
if [ "${VERSION_ID%%.*}" -lt 22 ]; then
    die "Ubuntu 22.04 veya üstü gerekli. Mevcut: ${VERSION_ID}"
fi

# Mimari kontrolü
ARCH=$(uname -m)
if [ "$ARCH" != "aarch64" ] && [ "$ARCH" != "arm64" ]; then
    warn "Bu script ARM64 için tasarlandı. Mevcut: $ARCH"
    confirm_or_auto "Yine de devam?" || die "İptal edildi"
fi

ok "Sistem kontrolü tamam: Ubuntu ${VERSION_ID} ${ARCH}"

# Disk alanı kontrolü
AVAIL_GB=$(df -BG "$HOME" | awk 'NR==2 {gsub("G",""); print $4}')
if [ "$AVAIL_GB" -lt 10 ]; then
    die "Yeterli disk alanı yok (${AVAIL_GB}GB). Minimum 10GB gerekli."
fi
ok "Disk alanı: ${AVAIL_GB}GB"

# RAM kontrolü
TOTAL_MB=$(free -m | awk 'NR==2 {print $2}')
if [ "$TOTAL_MB" -lt 1500 ]; then
    warn "RAM düşük (${TOTAL_MB}MB). Sync yavaş olabilir."
fi

# ========================================================================
# ADIM 1: Mevcut babacoind durumunu kontrol et
# ========================================================================
step "1/10" "Mevcut kurulum kontrolü"

EXISTING_BINARY=""
EXISTING_VERSION=""

for path in /usr/local/bin/babacoind /usr/bin/babacoind; do
    if [ -x "$path" ]; then
        EXISTING_BINARY="$path"
        EXISTING_VERSION=$("$path" -version 2>/dev/null | head -1 || echo "unknown")
        log "Mevcut binary: $path"
        log "Mevcut versiyon: $EXISTING_VERSION"
        break
    fi
done

if echo "$EXISTING_VERSION" | grep -q "v2.0.0"; then
    ok "v2.0.0 zaten kurulu. Sadece config kontrolü yapılacak."
    SKIP_BINARY_INSTALL=1
else
    SKIP_BINARY_INSTALL=0
fi

# ========================================================================
# ADIM 2: Çalışan daemon'ı durdur
# ========================================================================
step "2/10" "Mevcut daemon kapatılıyor (varsa)"

if systemctl is-active --quiet babacoind 2>/dev/null; then
    log "systemd servisi aktif, durduruluyor..."
    sudo systemctl stop babacoind || true
fi

if pgrep -x babacoind >/dev/null; then
    log "Çalışan babacoind process'i bulundu, durduruluyor..."
    if command -v babacoin-cli >/dev/null; then
        babacoin-cli stop 2>/dev/null || true
    fi
    sleep 3
    # Hâlâ çalışıyorsa zorla
    if pgrep -x babacoind >/dev/null; then
        sudo killall babacoind 2>/dev/null || true
        sleep 2
    fi
    if pgrep -x babacoind >/dev/null; then
        sudo killall -9 babacoind 2>/dev/null || true
        sleep 1
    fi
fi

ok "Tüm babacoind process'leri kapalı"

# ========================================================================
# ADIM 3: Sistem paketleri ve bağımlılıklar
# ========================================================================
step "3/10" "Sistem bağımlılıkları kuruluyor"

log "apt update çalıştırılıyor..."
sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq

log "Runtime kütüphaneler kuruluyor (Ubuntu 22.04 ARM)..."
PACKAGES=(
    wget curl tar gzip
    ufw iptables-persistent netfilter-persistent
    # Boost 1.74 (Ubuntu 22.04)
    libboost-filesystem1.74.0
    libboost-system1.74.0
    libboost-thread1.74.0
    libboost-program-options1.74.0
    libboost-chrono1.74.0
    libboost-date-time1.74.0
    # SSL/Crypto
    libssl3
    libsodium23
    # Event loop
    libevent-2.1-7
    # Berkeley DB
    libdb5.3++
    # UPnP
    libminiupnpc17
    # ZeroMQ
    libzmq5
    # QR Code
    libqrencode4
    # GMP (big integers)
    libgmp10
    # Protobuf
    libprotobuf23
    # Extra
    openssl
)

sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${PACKAGES[@]}" || {
    warn "Bazı paketler kurulamadı. Tek tek deneyeceğiz..."
    for pkg in "${PACKAGES[@]}"; do
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$pkg" 2>/dev/null || {
            warn "Atlanan paket: $pkg"
        }
    done
}

ok "Bağımlılıklar kuruldu"

# ========================================================================
# ADIM 4: Binary indir ve kur
# ========================================================================
if [ "$SKIP_BINARY_INSTALL" = "0" ]; then
    step "4/10" "BabaCoin v2.0.0 binary indiriliyor ve kuruluyor"

    # Eski binary'leri yedekle
    for bin in babacoind babacoin-cli babacoin-tx babacoin-qt; do
        for dir in /usr/bin /usr/local/bin; do
            if [ -x "$dir/$bin" ] && [ ! -L "$dir/$bin" ]; then
                BACKUP_NAME="${dir}/${bin}.backup-$(date +%Y%m%d-%H%M%S)"
                log "Yedekleniyor: $dir/$bin → $BACKUP_NAME"
                sudo mv "$dir/$bin" "$BACKUP_NAME" || true
            fi
        done
    done

    # İndir
    mkdir -p "$WORK_DIR"
    cd "$WORK_DIR"

    log "İndiriliyor: $BINARY_URL"
    wget -q --show-progress -O babacoin.tar.gz "$BINARY_URL" || die "İndirme başarısız"

    log "Archive açılıyor..."
    tar xzf babacoin.tar.gz
    ls -la

    # Binary kontrolü
    if [ ! -x ./babacoind ]; then
        die "babacoind binary'si archive içinde bulunamadı"
    fi

    FILE_INFO=$(file ./babacoind)
    log "Binary info: $FILE_INFO"

    if ! echo "$FILE_INFO" | grep -q "ARM aarch64"; then
        die "Binary ARM64 değil! İndirme hatalı olabilir."
    fi

    # Versiyon kontrolü (bağımlılıklar eksikse burada fail eder, bilgilendirici)
    log "Binary versiyon testi..."
    if ! ./babacoind -version 2>&1 | head -1; then
        warn "Binary çalıştırılamadı — bağımlılık eksik olabilir. Devam ediliyor..."
        ldd ./babacoind | grep "not found" && warn "Yukarıdaki kütüphaneler eksik!" || true
    fi

    # /usr/local/bin'e kur
    log "/usr/local/bin/'e kopyalanıyor..."
    sudo cp ./babacoind /usr/local/bin/babacoind
    sudo cp ./babacoin-cli /usr/local/bin/babacoin-cli
    sudo cp ./babacoin-tx /usr/local/bin/babacoin-tx
    sudo chmod +x /usr/local/bin/babacoind /usr/local/bin/babacoin-cli /usr/local/bin/babacoin-tx

    # PATH hash temizle
    hash -r

    # Teyit
    INSTALLED_VER=$(/usr/local/bin/babacoind -version 2>&1 | head -1)
    if [[ "$INSTALLED_VER" != *"v2.0.0"* ]]; then
        die "Kurulum başarısız: $INSTALLED_VER"
    fi
    ok "Kurulu: $INSTALLED_VER"
    ok "Path: $(which babacoind)"
else
    step "4/10" "Binary zaten v2.0.0 — atlanıyor"
fi

# ========================================================================
# ADIM 5: External IP tespit ve Seed Number
# ========================================================================
step "5/10" "External IP tespit ediliyor"

EXTERNAL_IP=""
for service in "ifconfig.me" "ipv4.icanhazip.com" "api.ipify.org" "checkip.amazonaws.com"; do
    if EXTERNAL_IP=$(curl -s -4 --max-time 5 "https://${service}" 2>/dev/null) && [ -n "$EXTERNAL_IP" ]; then
        break
    fi
done

if [ -z "$EXTERNAL_IP" ]; then
    die "External IP tespit edilemedi. Manuel ayarla: export EXTERNAL_IP=x.x.x.x"
fi

ok "External IP: $EXTERNAL_IP"

# Seed num otomatik ataması (manuel verilmemişse hostname'den çıkar)
if [ "$SEED_NUM" = "auto" ]; then
    if hostname | grep -qE "node-?([0-9]+)"; then
        SEED_NUM=$(hostname | grep -oE "[0-9]+" | head -1 | xargs printf "%02d")
    else
        SEED_NUM=$(printf "%02d" $((RANDOM % 90 + 10)))
    fi
    ok "Otomatik seed number: $SEED_NUM (env SEED_NUM ile override edilebilir)"
fi

# ========================================================================
# ADIM 6: Konfigürasyon oluştur
# ========================================================================
step "6/10" "babacoin.conf oluşturuluyor"

mkdir -p "$DATA_DIR"

# RPC şifresi — mevcut config'i koru
if [ -f "$DATA_DIR/babacoin.conf" ] && grep -q "^rpcpassword=" "$DATA_DIR/babacoin.conf"; then
    RPC_PASS=$(grep "^rpcpassword=" "$DATA_DIR/babacoin.conf" | cut -d= -f2)
    RPC_USER=$(grep "^rpcuser=" "$DATA_DIR/babacoin.conf" | cut -d= -f2 || echo "babacoin")
    log "Mevcut RPC credential'ları koruyoruz"
else
    RPC_USER="babacoin"
    RPC_PASS=$(openssl rand -hex 24)
    log "Yeni RPC credential'ları üretildi"
fi

# Config dosyası
cat > "$DATA_DIR/babacoin.conf" << EOF
# ============================================================================
# BabaCoin Seed Node — seed${SEED_NUM}
# Oluşturulma: $(date)
# ============================================================================

# Network
listen=1
server=1
daemon=1
txindex=1
externalip=${EXTERNAL_IP}
port=6678
maxconnections=256

# Performance
dbcache=4096
par=0

# RPC (lokal)
rpcallowip=127.0.0.1
rpcbind=127.0.0.1
rpcuser=${RPC_USER}
rpcpassword=${RPC_PASS}

# Seed davranışı
discover=1
dnsseed=1

# Peer listesi
addnode=31.155.99.197
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

# Güvenlik
upnp=0
natpmp=0
EOF

chmod 600 "$DATA_DIR/babacoin.conf"
ok "Config yazıldı: $DATA_DIR/babacoin.conf"

# ========================================================================
# ADIM 7: Firewall (UFW + Oracle iptables)
# ========================================================================
step "7/10" "Firewall ayarlanıyor"

# UFW
log "UFW kuralları..."
sudo ufw allow 22/tcp comment 'SSH' >/dev/null 2>&1 || true
sudo ufw allow 6678/tcp comment 'Babacoin P2P' >/dev/null 2>&1 || true
if ! sudo ufw status | grep -q "Status: active"; then
    sudo ufw --force enable >/dev/null 2>&1 || warn "UFW enable başarısız, manuel kontrol et"
fi
ok "UFW: 22/tcp, 6678/tcp açık"

# Oracle iptables fix — Oracle ARM instance'ları default INPUT REJECT policy'si kullanır
log "Oracle iptables kuralı..."
if ! sudo iptables -C INPUT -p tcp --dport 6678 -j ACCEPT 2>/dev/null; then
    # INPUT chain'ine 6678 ACCEPT kuralı ekle (REJECT'ten önce olacak şekilde)
    sudo iptables -I INPUT -p tcp --dport 6678 -j ACCEPT
    ok "iptables kuralı eklendi"
else
    ok "iptables kuralı zaten mevcut"
fi

# Kalıcı yap
if command -v netfilter-persistent >/dev/null; then
    sudo netfilter-persistent save >/dev/null 2>&1 || true
    ok "iptables kuralları kalıcı kaydedildi (netfilter-persistent)"
elif [ -d /etc/iptables ]; then
    sudo iptables-save | sudo tee /etc/iptables/rules.v4 >/dev/null
    ok "iptables kuralları kaydedildi: /etc/iptables/rules.v4"
else
    warn "iptables kalıcılık aracı yok, reboot'ta kaybolabilir"
fi

# Port test
log "Port 6678 local listen testi..."
if sudo ss -tlnp 2>/dev/null | grep -q ":6678 " || sudo netstat -tlnp 2>/dev/null | grep -q ":6678 "; then
    warn "Port 6678 zaten dinleniyor (başka bir process?)"
fi

# ========================================================================
# ADIM 8: systemd servisi
# ========================================================================
step "8/10" "systemd servisi oluşturuluyor"

sudo tee /etc/systemd/system/babacoind.service > /dev/null << EOF
[Unit]
Description=Babacoin Core Seed Node (seed${SEED_NUM})
Documentation=https://github.com/babacoinbbc/babacoin
After=network-online.target
Wants=network-online.target

[Service]
Type=forking
User=${BBC_USER}
Group=${BBC_USER}
WorkingDirectory=${BBC_HOME}

# Daemon başlat
ExecStart=/usr/local/bin/babacoind -daemon -conf=${DATA_DIR}/babacoin.conf

# PID dosyasını sistem doğru beklesin
PIDFile=${DATA_DIR}/babacoind.pid

# Grace shutdown
ExecStop=/usr/local/bin/babacoin-cli stop
TimeoutStopSec=300

# Yeniden başlatma
Restart=on-failure
RestartSec=30
StartLimitInterval=180
StartLimitBurst=4

# Kaynak sınırları
LimitNOFILE=65536
MemoryMax=90%

# Güvenlik
PrivateTmp=true
ProtectSystem=full
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable babacoind >/dev/null 2>&1
ok "systemd servisi: babacoind.service (enabled)"

# ========================================================================
# ADIM 9: Servisi başlat
# ========================================================================
step "9/10" "Servis başlatılıyor"

sudo systemctl start babacoind

log "20 saniye bekliyoruz (daemon yükleniyor)..."
sleep 20

# Durum kontrolü
if systemctl is-active --quiet babacoind; then
    ok "Servis aktif"
else
    err "Servis başlamadı. journalctl -u babacoind --no-pager -n 30:"
    sudo journalctl -u babacoind --no-pager -n 30
    die "Kurulum başarısız — yukarıdaki log'u incele"
fi

# RPC bağlantı testi
log "RPC bağlantısı test ediliyor..."
RETRY=0
while [ $RETRY -lt 6 ]; do
    if babacoin-cli -datadir="$DATA_DIR" getblockchaininfo >/dev/null 2>&1; then
        break
    fi
    RETRY=$((RETRY + 1))
    log "  Deneme $RETRY/6 — bekliyor..."
    sleep 10
done

if ! babacoin-cli -datadir="$DATA_DIR" getblockchaininfo >/dev/null 2>&1; then
    warn "RPC henüz cevap vermiyor — daemon daha yüklenmiş olabilir"
fi

# ========================================================================
# ADIM 10: Özet ve kontroller
# ========================================================================
step "10/10" "Kurulum raporu"

title "✅ KURULUM TAMAMLANDI"

printf "\n${C}═══ Sistem Bilgisi ═══${N}\n"
printf "  Hostname:      %s\n" "$(hostname)"
printf "  External IP:   ${G}%s${N}\n" "$EXTERNAL_IP"
printf "  Seed Number:   ${G}seed%s${N}\n" "$SEED_NUM"
printf "  Ubuntu:        %s\n" "${VERSION}"
printf "  Architecture:  %s\n" "$ARCH"

printf "\n${C}═══ BabaCoin Bilgisi ═══${N}\n"
printf "  Binary:        %s\n" "$(which babacoind)"
printf "  Versiyon:      %s\n" "$(babacoind -version 2>&1 | head -1)"
printf "  Config:        %s\n" "$DATA_DIR/babacoin.conf"
printf "  Data dir:      %s\n" "$DATA_DIR"
printf "  Service:       %s\n" "$(systemctl is-active babacoind)"

printf "\n${C}═══ RPC Credentials (GÜVENLİ SAKLA!) ═══${N}\n"
printf "  User: ${Y}%s${N}\n" "$RPC_USER"
printf "  Pass: ${Y}%s${N}\n" "$RPC_PASS"

# Sync durumu
printf "\n${C}═══ Sync Durumu ═══${N}\n"
if CHAIN_INFO=$(babacoin-cli -datadir="$DATA_DIR" getblockchaininfo 2>/dev/null); then
    BLOCKS=$(echo "$CHAIN_INFO" | grep -Po '"blocks":\s*\K\d+' | head -1)
    HEADERS=$(echo "$CHAIN_INFO" | grep -Po '"headers":\s*\K\d+' | head -1)
    PROGRESS=$(echo "$CHAIN_INFO" | grep -Po '"verificationprogress":\s*\K[\d.]+' | head -1)
    printf "  Blocks:    %s\n" "${BLOCKS:-?}"
    printf "  Headers:   %s\n" "${HEADERS:-?}"
    printf "  Progress:  %s\n" "${PROGRESS:-?}"

    CONNS=$(babacoin-cli -datadir="$DATA_DIR" getconnectioncount 2>/dev/null || echo "?")
    printf "  Peers:     %s\n" "$CONNS"
else
    printf "  ${Y}RPC henüz hazır değil (daemon yükleniyor olabilir)${N}\n"
    printf "  30 saniye sonra tekrar kontrol et:\n"
    printf "  ${C}babacoin-cli getblockchaininfo | grep -E 'blocks|headers'${N}\n"
fi

# DNS hatırlatıcısı
printf "\n${C}═══ YAPACAKLAR ═══${N}\n"
printf "  1. DNS kaydı ekle:\n"
printf "     ${Y}seed%s.babacoin.network${N}  →  ${G}%s${N}\n" "$SEED_NUM" "$EXTERNAL_IP"
printf "  2. Oracle Cloud Security List'te port 6678 TCP'yi aç\n"
printf "     (Virtual Cloud Networks → Security Lists → Ingress Rules)\n"
printf "  3. Sync'in tamamlanmasını bekle (~4-8 saat)\n"

# Kullanışlı komutlar
printf "\n${C}═══ Kullanışlı Komutlar ═══${N}\n"
cat << 'EOF'
  # Durum
  sudo systemctl status babacoind
  babacoin-cli getblockchaininfo | grep -E "blocks|headers"
  babacoin-cli getconnectioncount
  babacoin-cli getpeerinfo | grep -E "addr|subver" | head -20

  # Loglar
  sudo journalctl -u babacoind -f
  tail -f ~/.babacoin/debug.log

  # Yeniden başlat
  sudo systemctl restart babacoind

  # Durdur
  sudo systemctl stop babacoind

  # Config değiştir
  nano ~/.babacoin/babacoin.conf && sudo systemctl restart babacoind
EOF

printf "\n${G}Kurulum başarıyla tamamlandı!${N}\n\n"
