#!/usr/bin/env bash
#
# ============================================================================
# Ubuntu 20.04 -> 22.04 Upgrade Script for Oracle ARM Instances
# ============================================================================
# Usage:
#   # Interactive (recommended):
#   screen -S upgrade
#   curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/upgrade-oracle-arm-20-to-22.sh -o upgrade.sh
#   chmod +x upgrade.sh
#   ./upgrade.sh
#
#   # Unattended (DANGEROUS - only after testing):
#   curl -fsSL ... | AUTO_YES=1 AUTO_REBOOT=1 bash
#
# WARNING: This script makes serious system changes!
#   - Upgrades system packages from Ubuntu 20.04 (focal) to 22.04 (jammy)
#   - Modifies /etc/apt/sources.list
#   - Reboots the system at the end
#   - You may briefly lose SSH access
#
# BEFORE RUNNING: TAKE A BOOT VOLUME SNAPSHOT IN ORACLE CLOUD CONSOLE!
# ============================================================================

set -euo pipefail

# ===== Colors =====
R="\033[1;31m"; G="\033[1;32m"; Y="\033[1;33m"; B="\033[1;34m"; C="\033[1;36m"; N="\033[0m"

# ===== Helpers =====
log()   { printf "${C}[%s]${N} %s\n" "$(date +%H:%M:%S)" "$*"; }
ok()    { printf "${G}[ OK ]${N} %s\n" "$*"; }
warn()  { printf "${Y}[WARN]${N} %s\n" "$*"; }
err()   { printf "${R}[FAIL]${N} %s\n" "$*" >&2; }
title() { printf "\n${B}==============================================================${N}\n${B}  %s${N}\n${B}==============================================================${N}\n" "$*"; }
die()   { err "$*"; exit 1; }

# ===== Start =====
title "Oracle ARM Ubuntu 20.04 -> 22.04 Upgrade"

# Not root
[ "$EUID" -eq 0 ] && die "Do not run as root. Run as 'ubuntu' user."

# Passwordless sudo (Oracle default)
if ! sudo -n true 2>/dev/null; then
    die "Passwordless sudo required. Oracle's 'ubuntu' user has it by default."
fi
ok "Passwordless sudo available"

# OS check
[ -f /etc/os-release ] || die "/etc/os-release not found"
# shellcheck disable=SC1091
source /etc/os-release
[ "${ID}" = "ubuntu" ] || die "Not Ubuntu: ${ID}"

if [ "${VERSION_ID%%.*}" != "20" ]; then
    if [ "${VERSION_ID%%.*}" = "22" ]; then
        ok "Already on Ubuntu 22.04! This script is not needed."
        echo ""
        echo "You can now run the seed node installer:"
        echo "  curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | bash"
        exit 0
    fi
    die "Expected Ubuntu 20.04. Found: ${VERSION_ID}"
fi

# Architecture
ARCH=$(uname -m)
if [ "$ARCH" != "aarch64" ] && [ "$ARCH" != "arm64" ]; then
    warn "This script is designed for Oracle ARM. Found: $ARCH"
fi

# Disk check
AVAIL_GB=$(df -BG / | awk 'NR==2 {gsub("G",""); print $4}')
[ "$AVAIL_GB" -ge 5 ] || die "Insufficient disk space: ${AVAIL_GB}GB (minimum 5GB)"

ok "Ubuntu 20.04 $ARCH | Disk: ${AVAIL_GB}GB free"

# ===== Critical Confirmation =====
echo ""
printf "${R}==============================================================${N}\n"
printf "${R}                         WARNING                                ${N}\n"
printf "${R}==============================================================${N}\n"
cat << 'EOF'

This script will:
  1. Update system packages from Ubuntu 20.04 (focal) to 22.04 (jammy)
  2. Take approximately 30-45 minutes
  3. Should run inside screen/tmux to survive SSH disconnects
  4. Reboot the system at the end

REQUIREMENTS:
  * Boot volume snapshot taken in Oracle Cloud Console
  * Running inside screen or tmux: screen -S upgrade
  * Stable network connection
  * Keep your laptop/terminal open during the process

EOF

if [ ! -t 0 ] && [ "${AUTO_YES:-}" != "1" ]; then
    warn "Terminal is not interactive (piped input)."
    warn "For safety, AUTO_YES=1 is required to proceed non-interactively."
    die "Cannot continue without confirmation"
fi

if [ "${AUTO_YES:-}" != "1" ]; then
    read -rp "$(printf "${Y}Have you taken a boot volume snapshot? [yes/NO]: ${N}")" CONFIRM
    [ "$CONFIRM" = "yes" ] || die "Aborted. Take a snapshot first."

    echo ""
    # Check screen/tmux
    if [ -z "${STY:-}" ] && [ -z "${TMUX:-}" ]; then
        warn "You don't appear to be inside screen or tmux."
        echo ""
        echo "Recommended: Exit, then run:"
        echo "  screen -S upgrade"
        echo "  ./upgrade.sh"
        echo ""
        read -rp "$(printf "${Y}Proceed anyway? [yes/NO]: ${N}")" CONFIRM
        [ "$CONFIRM" = "yes" ] || die "Aborted. Please run inside screen."
    else
        ok "Running inside screen/tmux - safe for SSH disconnects"
    fi
fi

log "Confirmed, starting..."
START_TIME=$(date +%s)

# ===== STAGE 1: Backup =====
title "Stage 1/6: Backing up critical files"

BACKUP_DIR="${HOME}/pre-upgrade-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP_DIR"

log "Backing up SSH config..."
sudo cp /etc/ssh/sshd_config "$BACKUP_DIR/sshd_config" 2>/dev/null || true

log "Backing up Netplan config..."
sudo cp -r /etc/netplan "$BACKUP_DIR/netplan" 2>/dev/null || true

log "Backing up cloud-init config..."
sudo cp -r /etc/cloud "$BACKUP_DIR/cloud" 2>/dev/null || true

log "Backing up iptables rules..."
sudo iptables-save > "$BACKUP_DIR/iptables.rules" 2>/dev/null || true

log "Backing up APT sources..."
sudo cp /etc/apt/sources.list "$BACKUP_DIR/sources.list.focal" 2>/dev/null || true
sudo cp -r /etc/apt/sources.list.d "$BACKUP_DIR/sources.list.d.focal" 2>/dev/null || true

log "Saving hostname and IP info..."
hostname > "$BACKUP_DIR/hostname" 2>/dev/null || true
ip -4 addr > "$BACKUP_DIR/ip.txt" 2>/dev/null || true

# BabaCoin config if present
if [ -f "${HOME}/.babacoin/babacoin.conf" ]; then
    cp "${HOME}/.babacoin/babacoin.conf" "$BACKUP_DIR/" 2>/dev/null || true
    log "Backed up BabaCoin config"
fi

ok "Backups stored in: $BACKUP_DIR"

# ===== STAGE 2: Stop services =====
title "Stage 2/6: Stopping running services"

if systemctl is-active --quiet babacoind 2>/dev/null; then
    log "Stopping babacoind service..."
    sudo systemctl stop babacoind || true
fi

if pgrep -x babacoind >/dev/null; then
    log "Stopping babacoind process..."
    command -v babacoin-cli >/dev/null && babacoin-cli stop 2>/dev/null || true
    sleep 5
    sudo killall babacoind 2>/dev/null || true
    sleep 2
fi

ok "babacoind stopped"

# ===== STAGE 3: Fully update 20.04 first =====
title "Stage 3/6: Fully updating current 20.04"

log "apt update..."
sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq

log "apt upgrade (all packages)..."
sudo DEBIAN_FRONTEND=noninteractive apt-get upgrade -y -qq \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold"

log "apt dist-upgrade..."
sudo DEBIAN_FRONTEND=noninteractive apt-get dist-upgrade -y -qq \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold"

log "Removing obsolete packages..."
sudo apt-get autoremove --purge -y -qq

log "Installing upgrade tools..."
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    update-manager-core screen curl wget

# Release upgrade policy
sudo sed -i 's/^Prompt=.*/Prompt=lts/' /etc/update-manager/release-upgrades

ok "20.04 fully up to date"

# ===== STAGE 4: Upgrade to Jammy (manual sed) =====
title "Stage 4/6: Upgrading to Ubuntu 22.04 (Jammy)"

log "Replacing 'focal' with 'jammy' in sources..."

# Main sources.list
if grep -q "focal" /etc/apt/sources.list 2>/dev/null; then
    sudo sed -i 's/focal-security/jammy-security/g' /etc/apt/sources.list
    sudo sed -i 's/focal-updates/jammy-updates/g' /etc/apt/sources.list
    sudo sed -i 's/focal-backports/jammy-backports/g' /etc/apt/sources.list
    sudo sed -i 's/focal/jammy/g' /etc/apt/sources.list
    ok "/etc/apt/sources.list updated"
fi

# sources.list.d
if [ -d /etc/apt/sources.list.d ]; then
    for f in /etc/apt/sources.list.d/*.list; do
        [ -f "$f" ] || continue
        if grep -q "focal" "$f" 2>/dev/null; then
            sudo sed -i 's/focal-security/jammy-security/g' "$f"
            sudo sed -i 's/focal-updates/jammy-updates/g' "$f"
            sudo sed -i 's/focal-backports/jammy-backports/g' "$f"
            sudo sed -i 's/focal/jammy/g' "$f"
            log "Updated: $f"
        fi
    done
fi

log "Rebuilding APT cache..."
sudo rm -rf /var/lib/apt/lists/*
sudo apt-get clean

log "Refreshing from Jammy repos (may take 3-5 min)..."
sudo DEBIAN_FRONTEND=noninteractive apt-get update

log "Starting dist-upgrade (20-40 min, be patient)..."
log "Monitor progress from another terminal: tail -f /var/log/apt/term.log"

sudo DEBIAN_FRONTEND=noninteractive \
    APT_LISTCHANGES_FRONTEND=none \
    apt-get dist-upgrade -y \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold" \
    -o APT::Get::Fix-Missing=true

ok "dist-upgrade completed"

# ===== STAGE 5: Cleanup =====
title "Stage 5/6: Cleanup"

log "Removing obsolete packages..."
sudo DEBIAN_FRONTEND=noninteractive apt-get autoremove --purge -y \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold"

log "Cleaning apt cache..."
sudo apt-get autoclean

log "Checking for broken packages..."
sudo DEBIAN_FRONTEND=noninteractive apt-get -f install -y \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold"

# Verify version
NEW_VERSION=$(lsb_release -rs 2>/dev/null || grep "^VERSION_ID" /etc/os-release | cut -d'"' -f2)
log "Installed version: $NEW_VERSION"

if [ "${NEW_VERSION%%.*}" != "22" ]; then
    err "Upgrade did not complete as expected. Current: $NEW_VERSION"
    die "You may need to restore from snapshot"
fi

ok "Ubuntu 22.04 installed"

# ===== STAGE 6: Reboot =====
title "Stage 6/6: Reboot required"

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))
log "Elapsed time: $((ELAPSED / 60))m $((ELAPSED % 60))s"

echo ""
cat << EOF
${G}==============================================================${N}
${G}                  UPGRADE COMPLETED SUCCESSFULLY                 ${N}
${G}==============================================================${N}

Current version: Ubuntu $NEW_VERSION
Backup location: $BACKUP_DIR

${Y}REBOOT IS REQUIRED NOW${N}

After reboot (wait 1-3 minutes), reconnect via SSH and run:

${C}  curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | bash${N}

This will backup any old v1 binary and install BabaCoin v2.0.0.

EOF

if [ "${AUTO_REBOOT:-}" = "1" ]; then
    log "AUTO_REBOOT=1 set, rebooting in 10 seconds..."
    for i in 10 9 8 7 6 5 4 3 2 1; do
        printf "\r  Rebooting in %s seconds...  " "$i"
        sleep 1
    done
    echo ""
    sudo reboot
else
    read -rp "$(printf "${Y}Reboot now? [yes/NO]: ${N}")" REBOOT_CONFIRM
    if [ "$REBOOT_CONFIRM" = "yes" ]; then
        log "Rebooting..."
        sudo reboot
    else
        echo ""
        ok "Reboot deferred. When ready, run: sudo reboot"
    fi
fi
