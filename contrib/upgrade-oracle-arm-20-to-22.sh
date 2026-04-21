#!/usr/bin/env bash
#
# ============================================================================
# Oracle ARM Ubuntu 20.04 → 22.04 Otomatik Upgrade Script
# ============================================================================
# Kullanım:
#   curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/upgrade-oracle-arm-20-to-22.sh | bash
#
# UYARI: BU SCRIPT CİDDİ DEĞİŞİKLİKLER YAPAR!
#   - Sistem paketlerini günceller
#   - /etc/apt/sources.list değiştirir (focal → jammy)
#   - Sistemi reboot eder
#   - SSH erişimini geçici olarak kaybedebilirsin
#
# KULLANMADAN ÖNCE ORACLE CLOUD CONSOLE'DAN SNAPSHOT AL!
# ============================================================================

set -euo pipefail

# ===== Renkler =====
R="\033[1;31m"; G="\033[1;32m"; Y="\033[1;33m"; B="\033[1;34m"; C="\033[1;36m"; N="\033[0m"

# ===== Fonksiyonlar =====
log()   { printf "${C}[%s]${N} %s\n" "$(date +%H:%M:%S)" "$*"; }
ok()    { printf "${G}✓${N} %s\n" "$*"; }
warn()  { printf "${Y}⚠${N} %s\n" "$*"; }
err()   { printf "${R}✗${N} %s\n" "$*" >&2; }
title() { printf "\n${B}╔══════════════════════════════════════════════════════════════╗${N}\n${B}║${N} %-60s ${B}║${N}\n${B}╚══════════════════════════════════════════════════════════════╝${N}\n" "$*"; }

die() { err "$*"; exit 1; }

# ===== Başlangıç =====
title "Oracle ARM Ubuntu 20.04 → 22.04 Upgrade"

# Root kontrolü (olmamalı)
[ "$EUID" -eq 0 ] && die "Root olarak çalıştırma. Normal kullanıcı (ubuntu) olarak çalıştır."

# sudo kontrolü
sudo -v || die "sudo yetkisi gerekli"

# OS kontrolü
source /etc/os-release
[ "${ID}" != "ubuntu" ] && die "Ubuntu değil: ${ID}"

if [ "${VERSION_ID%%.*}" != "20" ]; then
    if [ "${VERSION_ID%%.*}" = "22" ]; then
        ok "Zaten Ubuntu 22.04! Bu script gereksiz."
        echo ""
        echo "Artık seed node kurulum script'ini çalıştırabilirsin:"
        echo "  curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | bash"
        exit 0
    fi
    die "Beklenen: Ubuntu 20.04. Mevcut: ${VERSION_ID}"
fi

# Mimari kontrolü
ARCH=$(uname -m)
if [ "$ARCH" != "aarch64" ] && [ "$ARCH" != "arm64" ]; then
    warn "Bu script Oracle ARM için. Mevcut: $ARCH"
fi

# Disk kontrolü
AVAIL_GB=$(df -BG / | awk 'NR==2 {gsub("G",""); print $4}')
[ "$AVAIL_GB" -lt 5 ] && die "Yeterli disk yok: ${AVAIL_GB}GB (minimum 5GB)"

ok "Ubuntu 20.04 $ARCH | Disk: ${AVAIL_GB}GB boş"

# ===== Kritik Onay =====
echo ""
printf "${R}╔══════════════════════════════════════════════════════════════╗${N}\n"
printf "${R}║                          UYARI                                ║${N}\n"
printf "${R}╚══════════════════════════════════════════════════════════════╝${N}\n"
cat << 'EOF'

Bu script:
  1. Sistem paketlerini Ubuntu 20.04 (focal) → 22.04 (jammy)'ye yükseltir
  2. ~30-45 dakika sürer
  3. SSH oturumunu koruyabilmek için screen içinde çalışmalı
  4. Sonunda REBOOT yapar

ÖNERİLER:
  ✓ Snapshot aldığından emin ol (Oracle Console)
  ✓ screen/tmux içinde çalıştır: screen -S upgrade
  ✓ Stabil network bağlantısından çalıştır
  ✓ İşlem sırasında laptop'u kapatma

EOF

if [ ! -t 0 ] && [ "${AUTO_YES:-}" != "1" ]; then
    warn "Terminal interaktif değil. AUTO_YES=1 ile override edebilirsin."
    warn "Güvenlik için script duraklatılıyor..."
    die "Onay olmadan devam edilemez"
fi

if [ "${AUTO_YES:-}" != "1" ]; then
    read -rp "$(printf "${Y}Snapshot aldığını onaylıyor musun? [yes/NO]: ${N}")" CONFIRM
    [ "$CONFIRM" != "yes" ] && die "İptal edildi. Önce snapshot al."

    echo ""
    read -rp "$(printf "${Y}screen/tmux içinde misin? [yes/NO]: ${N}")" CONFIRM
    if [ "$CONFIRM" != "yes" ]; then
        echo ""
        echo "Önce şunu çalıştır:"
        echo "  screen -S upgrade"
        echo "Sonra bu script'i tekrar çalıştır."
        exit 1
    fi
fi

log "Onay alındı, başlıyoruz..."
START_TIME=$(date +%s)

# ===== AŞAMA 1: Backup =====
title "Aşama 1/6: Kritik dosyalar yedekleniyor"

BACKUP_DIR="${HOME}/pre-upgrade-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP_DIR"

log "SSH config yedekleniyor..."
sudo cp /etc/ssh/sshd_config "$BACKUP_DIR/sshd_config" 2>/dev/null || true

log "Netplan config yedekleniyor..."
sudo cp -r /etc/netplan "$BACKUP_DIR/netplan" 2>/dev/null || true

log "Cloud-init config yedekleniyor..."
sudo cp -r /etc/cloud "$BACKUP_DIR/cloud" 2>/dev/null || true

log "iptables yedekleniyor..."
sudo iptables-save > "$BACKUP_DIR/iptables.rules" 2>/dev/null || true

log "APT sources yedekleniyor..."
sudo cp /etc/apt/sources.list "$BACKUP_DIR/sources.list.focal" 2>/dev/null || true
sudo cp -r /etc/apt/sources.list.d "$BACKUP_DIR/sources.list.d.focal" 2>/dev/null || true

log "Hostname ve network state..."
hostname > "$BACKUP_DIR/hostname" 2>/dev/null || true
ip -4 addr > "$BACKUP_DIR/ip.txt" 2>/dev/null || true

# BabaCoin config'i yedekle (varsa)
if [ -f "${HOME}/.babacoin/babacoin.conf" ]; then
    cp "${HOME}/.babacoin/babacoin.conf" "$BACKUP_DIR/" 2>/dev/null || true
    log "BabaCoin config yedeklendi"
fi

ok "Yedekler: $BACKUP_DIR"

# ===== AŞAMA 2: BabaCoin durdur =====
title "Aşama 2/6: Çalışan servisler durduruluyor"

if systemctl is-active --quiet babacoind 2>/dev/null; then
    log "babacoind servisi durduruluyor..."
    sudo systemctl stop babacoind || true
fi

if pgrep -x babacoind >/dev/null; then
    log "babacoind process durduruluyor..."
    command -v babacoin-cli >/dev/null && babacoin-cli stop 2>/dev/null || true
    sleep 5
    sudo killall babacoind 2>/dev/null || true
    sleep 2
fi

ok "babacoind durdu"

# ===== AŞAMA 3: 20.04'ü son kez güncelle =====
title "Aşama 3/6: Mevcut 20.04 tam güncelleniyor"

log "apt update..."
sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq

log "apt upgrade (tüm paketler)..."
sudo DEBIAN_FRONTEND=noninteractive apt-get upgrade -y -qq \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold"

log "apt dist-upgrade..."
sudo DEBIAN_FRONTEND=noninteractive apt-get dist-upgrade -y -qq \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold"

log "Gereksiz paketler temizleniyor..."
sudo apt-get autoremove --purge -y -qq

log "Araçlar kuruluyor..."
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    update-manager-core screen curl wget

# Release upgrade policy
sudo sed -i 's/^Prompt=.*/Prompt=lts/' /etc/update-manager/release-upgrades

ok "20.04 fully updated"

# ===== AŞAMA 4: Jammy'e yükselt (manuel sed) =====
title "Aşama 4/6: Ubuntu 22.04'e yükseltiliyor"

log "focal → jammy değişimi..."

# Ana sources.list
if grep -q "focal" /etc/apt/sources.list 2>/dev/null; then
    sudo sed -i 's/focal-security/jammy-security/g' /etc/apt/sources.list
    sudo sed -i 's/focal-updates/jammy-updates/g' /etc/apt/sources.list
    sudo sed -i 's/focal-backports/jammy-backports/g' /etc/apt/sources.list
    sudo sed -i 's/focal/jammy/g' /etc/apt/sources.list
    ok "/etc/apt/sources.list güncellendi"
fi

# sources.list.d dizini
if [ -d /etc/apt/sources.list.d ]; then
    for f in /etc/apt/sources.list.d/*.list; do
        [ -f "$f" ] || continue
        if grep -q "focal" "$f" 2>/dev/null; then
            sudo sed -i 's/focal-security/jammy-security/g' "$f"
            sudo sed -i 's/focal-updates/jammy-updates/g' "$f"
            sudo sed -i 's/focal-backports/jammy-backports/g' "$f"
            sudo sed -i 's/focal/jammy/g' "$f"
            log "Güncellendi: $f"
        fi
    done
fi

log "APT cache yeniden oluşturuluyor..."
sudo rm -rf /var/lib/apt/lists/*
sudo apt-get clean

log "Jammy repolarından update (bu 3-5 dk sürebilir)..."
sudo DEBIAN_FRONTEND=noninteractive apt-get update

log "dist-upgrade başlıyor (20-40 dk, sabırlı ol)..."
log "İlerlemeyi izlemek için başka bir terminal'de: tail -f /var/log/apt/term.log"

# dist-upgrade — config dosyalarını koru
sudo DEBIAN_FRONTEND=noninteractive \
    APT_LISTCHANGES_FRONTEND=none \
    apt-get dist-upgrade -y \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold" \
    -o APT::Get::Fix-Missing=true

ok "dist-upgrade tamamlandı"

# ===== AŞAMA 5: Cleanup =====
title "Aşama 5/6: Temizlik"

log "Gereksiz paketler kaldırılıyor..."
sudo DEBIAN_FRONTEND=noninteractive apt-get autoremove --purge -y \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold"

log "Apt cache temizleniyor..."
sudo apt-get autoclean

log "Broken paket kontrolü..."
sudo DEBIAN_FRONTEND=noninteractive apt-get -f install -y \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold"

# Versiyon doğrulama
NEW_VERSION=$(lsb_release -rs 2>/dev/null || grep "^VERSION_ID" /etc/os-release | cut -d'"' -f2)
log "Kurulu versiyon: $NEW_VERSION"

if [ "${NEW_VERSION%%.*}" != "22" ]; then
    err "Upgrade beklendiği gibi gerçekleşmedi. Mevcut: $NEW_VERSION"
    die "Snapshot'tan restore etmen gerekebilir"
fi

ok "Ubuntu 22.04 yüklendi"

# ===== AŞAMA 6: Reboot =====
title "Aşama 6/6: Reboot zamanı"

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))
log "Geçen süre: $((ELAPSED / 60)) dk $((ELAPSED % 60)) sn"

echo ""
cat << EOF
${G}╔══════════════════════════════════════════════════════════════╗${N}
${G}║                  UPGRADE BAŞARIYLA TAMAMLANDI                  ║${N}
${G}╚══════════════════════════════════════════════════════════════╝${N}

Mevcut sürüm:  Ubuntu $NEW_VERSION
Backup dizin:  $BACKUP_DIR

${Y}⚠ ŞİMDİ REBOOT GEREKLİ${N}

Reboot sonrası yeniden bağlan (1-3 dakika) ve şunu çalıştır:

${C}  curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | bash${N}

Bu komut eski v1 binary'yi yedekler ve yeni v2.0.0'ı kurar.

EOF

if [ "${AUTO_REBOOT:-}" = "1" ]; then
    log "AUTO_REBOOT=1 ayarlı, 10 saniye içinde reboot..."
    for i in 10 9 8 7 6 5 4 3 2 1; do
        printf "\r  Reboot %s saniye içinde...  " "$i"
        sleep 1
    done
    echo ""
    sudo reboot
else
    read -rp "$(printf "${Y}Şimdi reboot edilsin mi? [yes/NO]: ${N}")" REBOOT_CONFIRM
    if [ "$REBOOT_CONFIRM" = "yes" ]; then
        log "Reboot başlatılıyor..."
        sudo reboot
    else
        echo ""
        ok "Reboot erteledin. Hazır olunca şunu çalıştır: sudo reboot"
    fi
fi
