#!/usr/bin/env bash
#
# ============================================================================
# BabaCoin Universal Installer
# ============================================================================
# One-shot installer that handles everything:
#   1. Detects your OS (Ubuntu 20/22/24, Debian, Raspberry Pi OS, other)
#   2. Detects your architecture (x86_64, aarch64/arm64, armv7)
#   3. Downloads the matching binary release
#   4. Installs runtime dependencies automatically
#   5. Creates ~/.babacoin/babacoin.conf with sync-optimized settings
#   6. Configures firewall (UFW + iptables)
#   7. Installs systemd service (auto-start on boot)
#   8. Starts the daemon
#   9. Creates a wallet and generates a receive address
#  10. Shows you the address + private key (save them!)
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/install.sh | bash
#
# Options (env vars):
#   SEED_NUM=XX          Assign seed number (default: auto)
#   AUTO_YES=1           Skip all confirmations (for CI/automation)
#   SKIP_WALLET=1        Don't create wallet (just install node)
#   BBC_VERSION=tag      Override release tag (default: v2.0.0-test)
#
# Requirements:
#   - Linux (Ubuntu/Debian/Raspberry Pi OS)
#   - User with sudo (passwordless ideal, interactive works too)
#   - Port 6678/tcp open
#   - 10+ GB disk, 2+ GB RAM recommended
# ============================================================================

set -euo pipefail

# ============================================================================
# Configuration
# ============================================================================

SEED_NUM="${SEED_NUM:-auto}"
AUTO_YES="${AUTO_YES:-0}"
SKIP_WALLET="${SKIP_WALLET:-0}"
BBC_VERSION="${BBC_VERSION:-v2.0.0-test}"
WORK_DIR="${HOME}/bbc-install-$(date +%s)"
DATA_DIR="${HOME}/.babacoin"
BBC_USER="${USER}"
BBC_HOME="${HOME}"
REPO="babacoinbbc/babacoin"

# Colors
R="\033[1;31m"; G="\033[1;32m"; Y="\033[1;33m"; B="\033[1;34m"; C="\033[1;36m"; N="\033[0m"

log()   { printf "${C}[%s]${N} %s\n" "$(date +%H:%M:%S)" "$*"; }
ok()    { printf "${G}[ OK ]${N} %s\n" "$*"; }
warn()  { printf "${Y}[WARN]${N} %s\n" "$*"; }
err()   { printf "${R}[FAIL]${N} %s\n" "$*" >&2; }
title() { printf "\n${B}══════════════════════════════════════════════════════════════${N}\n${B}  %s${N}\n${B}══════════════════════════════════════════════════════════════${N}\n" "$*"; }
step()  { printf "\n${B}▶ [%s] %s${N}\n" "$1" "$2"; }
die()   { err "$*"; exit 1; }

confirm() {
    [ "$AUTO_YES" = "1" ] && return 0
    local prompt="${1:-Continue?}"
    printf "${Y}%s [y/N]${N} " "$prompt"
    read -r ans < /dev/tty || return 1
    case "$ans" in y|Y|yes|YES) return 0 ;; *) return 1 ;; esac
}

# ============================================================================
# Banner
# ============================================================================

title "BabaCoin Universal Installer"

printf "  Version:     ${G}%s${N}\n" "$BBC_VERSION"
printf "  Runtime:     %s\n" "$(date)"
printf "  User:        %s\n" "$USER"
printf "  Home:        %s\n" "$HOME"
printf "  Repo:        https://github.com/%s\n" "$REPO"

# ============================================================================
# Preflight
# ============================================================================

step "1/10" "Preflight checks"

# Not root
if [ "$EUID" -eq 0 ]; then
    die "Do not run as root. Run as your regular user (e.g. 'ubuntu', 'yunus', 'pi')."
fi

# Must be Linux
[ "$(uname -s)" = "Linux" ] || die "This installer only supports Linux. Detected: $(uname -s)"

# OS detection
[ -f /etc/os-release ] || die "/etc/os-release not found - unsupported OS"
# shellcheck disable=SC1091
source /etc/os-release

OS_ID="${ID:-unknown}"
OS_VER="${VERSION_ID:-unknown}"
OS_MAJOR="${OS_VER%%.*}"

log "OS: $PRETTY_NAME"

# Raspberry Pi detection (even if running Ubuntu)
IS_RASPI=0
if [ -f /proc/device-tree/model ] && grep -qi "raspberry pi" /proc/device-tree/model 2>/dev/null; then
    IS_RASPI=1
    log "Hardware: $(tr -d '\0' < /proc/device-tree/model)"
fi

# Architecture detection
ARCH=$(uname -m)
case "$ARCH" in
    x86_64|amd64)    ARCH_NAME="x86_64" ;;
    aarch64|arm64)   ARCH_NAME="arm64" ;;
    armv7l|armhf)    ARCH_NAME="armhf"; warn "armv7 (32-bit ARM) may not have a prebuilt binary" ;;
    *)               die "Unsupported architecture: $ARCH" ;;
esac
log "Architecture: $ARCH ($ARCH_NAME)"

# Sudo detection
SUDO_CMD=""
if sudo -n true 2>/dev/null; then
    SUDO_CMD="sudo"
    ok "Passwordless sudo available"
elif sudo -v 2>/dev/null; then
    SUDO_CMD="sudo"
    warn "Interactive sudo - keeping credentials cached"
    ( while true; do sudo -v; sleep 50; done ) 2>/dev/null &
    SUDO_PID=$!
    trap "kill $SUDO_PID 2>/dev/null || true" EXIT
else
    die "No sudo access. Fix with:
         echo \"$USER ALL=(ALL) NOPASSWD:ALL\" | sudo tee /etc/sudoers.d/90-$USER"
fi

# Disk
AVAIL_GB=$(df -BG "$HOME" | awk 'NR==2 {gsub("G",""); print $4}')
[ "$AVAIL_GB" -ge 10 ] || die "Need 10+ GB free, have ${AVAIL_GB}GB"
ok "Disk: ${AVAIL_GB}GB free"

# RAM
TOTAL_MB=$(free -m | awk 'NR==2 {print $2}')
if   [ "$TOTAL_MB" -lt 2500 ];  then DBCACHE=512
elif [ "$TOTAL_MB" -lt 5000 ];  then DBCACHE=1024
elif [ "$TOTAL_MB" -lt 10000 ]; then DBCACHE=2048
elif [ "$TOTAL_MB" -lt 20000 ]; then DBCACHE=4096
else                                  DBCACHE=6144
fi
ok "RAM: ${TOTAL_MB}MB → dbcache=${DBCACHE}MB"

# CPU
CPU_COUNT=$(nproc)
PAR=$CPU_COUNT
[ "$PAR" -gt 8 ] && PAR=8
ok "CPUs: ${CPU_COUNT} → par=${PAR}"

# External IP
EXTERNAL_IP="(unknown)"
for svc in https://api.ipify.org https://ifconfig.me https://ipv4.icanhazip.com; do
    RES=$(curl -fsS --max-time 5 "$svc" 2>/dev/null | tr -d '[:space:]') && [ -n "$RES" ] && EXTERNAL_IP="$RES" && break
done
ok "External IP: $EXTERNAL_IP"

# ============================================================================
# Binary selection — pick the right tarball based on OS + arch
# ============================================================================

step "2/10" "Selecting binary for your system"

# Binary naming convention in releases:
#   babacoin-v2.0.0-linux-ubuntu{20,22,24}.04-{x86_64,arm64}.tar.gz
#   babacoin-v2.0.0-raspberry-pi-arm64.tar.gz
#   (others: macos, windows - we don't use them in this Linux installer)

BINARY_FILE=""
EXPECTED_ARCH_MAGIC=""

if [ "$IS_RASPI" = "1" ] && [ "$ARCH_NAME" = "arm64" ]; then
    BINARY_FILE="babacoin-v2.0.0-raspberry-pi-arm64.tar.gz"
    EXPECTED_ARCH_MAGIC="ARM aarch64"
    PLATFORM_LABEL="Raspberry Pi (ARM64)"
elif [ "$OS_ID" = "ubuntu" ] || [ "$OS_ID" = "debian" ] || [ "$ID_LIKE" = "debian" ] 2>/dev/null; then
    # Default to Ubuntu 24.04 if we can't determine, then fall back progressively
    # We try 24.04 first, then 22.04, then 20.04 — matching by Ubuntu major version
    case "$OS_MAJOR" in
        24)  UBU_VER="24.04" ;;
        22)  UBU_VER="22.04" ;;
        20)  UBU_VER="20.04" ;;
        *)
            # Debian or other Ubuntu-like — pick by LTS similarity
            #  Debian 12 ≈ Ubuntu 22.04, Debian 13 ≈ Ubuntu 24.04
            if [ "$OS_ID" = "debian" ]; then
                case "$OS_MAJOR" in
                    13|14) UBU_VER="24.04" ;;
                    12)    UBU_VER="22.04" ;;
                    11)    UBU_VER="20.04" ;;
                    *)     UBU_VER="22.04" ;;
                esac
                warn "Debian detected — using Ubuntu ${UBU_VER} binary (should be ABI-compatible)"
            else
                UBU_VER="24.04"
                warn "Unknown OS version '$OS_VER' — trying Ubuntu 24.04 binary"
            fi
            ;;
    esac

    if [ "$ARCH_NAME" = "x86_64" ]; then
        BINARY_FILE="babacoin-v2.0.0-linux-ubuntu${UBU_VER}-x86_64.tar.gz"
        EXPECTED_ARCH_MAGIC="x86-64"
    elif [ "$ARCH_NAME" = "arm64" ]; then
        BINARY_FILE="babacoin-v2.0.0-linux-ubuntu${UBU_VER}-arm64.tar.gz"
        EXPECTED_ARCH_MAGIC="ARM aarch64"
    else
        die "No binary available for arch: $ARCH_NAME on Ubuntu/Debian"
    fi
    PLATFORM_LABEL="Ubuntu ${UBU_VER} (${ARCH_NAME})"
else
    die "Unsupported OS: $OS_ID. Supported: Ubuntu, Debian, Raspberry Pi OS."
fi

BINARY_URL="https://github.com/${REPO}/releases/download/${BBC_VERSION}/${BINARY_FILE}"
log "Platform: ${PLATFORM_LABEL}"
log "Binary: ${BINARY_FILE}"

# Pre-flight: check the binary actually exists on GitHub
log "Verifying binary exists on GitHub..."
HTTP_CODE=$(curl -o /dev/null -s -w "%{http_code}" -I -L "$BINARY_URL" --max-time 10)
if [ "$HTTP_CODE" != "200" ]; then
    warn "Binary not available yet (HTTP $HTTP_CODE)"
    warn "This usually means the release is still being built by CI."
    warn "Check status: https://github.com/${REPO}/actions"
    warn ""
    warn "Retry in 15-30 minutes, or if you're sure the release is done, check:"
    warn "  $BINARY_URL"
    die "Binary not downloadable yet"
fi
ok "Binary available (HTTP 200)"

# ============================================================================
# Stop any running daemon
# ============================================================================

step "3/10" "Stopping running daemon"

if $SUDO_CMD systemctl is-active --quiet babacoind 2>/dev/null; then
    log "Stopping systemd service..."
    $SUDO_CMD systemctl stop babacoind || true
fi

if pgrep -x babacoind >/dev/null; then
    log "Graceful RPC stop..."
    command -v babacoin-cli >/dev/null && babacoin-cli stop 2>/dev/null || true
    sleep 3
    pgrep -x babacoind >/dev/null && $SUDO_CMD killall babacoind 2>/dev/null || true
    sleep 2
    pgrep -x babacoind >/dev/null && $SUDO_CMD killall -9 babacoind 2>/dev/null || true
fi

ok "No daemon running"

# ============================================================================
# Install runtime dependencies
# ============================================================================

step "4/10" "Installing runtime dependencies"

log "apt update..."

# Wait for any running apt/dpkg processes (Ubuntu's unattended-upgrades
# commonly runs on first boot and holds the dpkg lock for 5-15 minutes).
WAIT_COUNT=0
while $SUDO_CMD fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1 \
   || $SUDO_CMD fuser /var/lib/dpkg/lock >/dev/null 2>&1 \
   || pgrep -x unattended-upgr >/dev/null 2>&1; do
    if [ $WAIT_COUNT -eq 0 ]; then
        warn "Another apt/dpkg process is running (likely unattended-upgrades)"
        warn "Waiting up to 10 minutes for it to finish..."
    fi
    WAIT_COUNT=$((WAIT_COUNT + 1))
    if [ $WAIT_COUNT -gt 60 ]; then
        warn "unattended-upgrades has been running for 10+ minutes"
        warn "Will force-stop it to continue with the install"
        $SUDO_CMD systemctl stop unattended-upgrades 2>/dev/null || true
        $SUDO_CMD killall -9 unattended-upgr 2>/dev/null || true
        sleep 2
        break
    fi
    sleep 10
    [ $((WAIT_COUNT % 3)) -eq 0 ] && log "  still waiting... ($((WAIT_COUNT * 10))s)"
done
[ $WAIT_COUNT -gt 0 ] && ok "apt lock released, continuing"

$SUDO_CMD DEBIAN_FRONTEND=noninteractive apt-get update -qq

# Package names vary by Ubuntu/Debian version. Use a superset and let apt skip unavailable.
if [ "$OS_MAJOR" -ge 24 ] || [ "$OS_ID" = "debian" ] && [ "$OS_MAJOR" -ge 13 ]; then
    # Ubuntu 24.04+ uses t64 suffix, libboost 1.83
    PACKAGES=(
        wget curl tar gzip file ca-certificates
        ufw iptables iptables-persistent netfilter-persistent
        libboost-filesystem1.83.0 libboost-system1.83.0 libboost-thread1.83.0
        libboost-program-options1.83.0 libboost-chrono1.83.0t64 libboost-date-time1.83.0
        libssl3t64 libsodium23 libevent-2.1-7t64 libevent-pthreads-2.1-7t64
        libdb5.3++t64 libminiupnpc17 libzmq5 libqrencode4 libgmp10
        libprotobuf32t64 openssl
    )
elif [ "$OS_MAJOR" = "22" ] || { [ "$OS_ID" = "debian" ] && [ "$OS_MAJOR" = "12" ]; }; then
    # Ubuntu 22.04 / Debian 12 - libboost 1.74, no t64
    PACKAGES=(
        wget curl tar gzip file ca-certificates
        ufw iptables iptables-persistent netfilter-persistent
        libboost-filesystem1.74.0 libboost-system1.74.0 libboost-thread1.74.0
        libboost-program-options1.74.0 libboost-chrono1.74.0 libboost-date-time1.74.0
        libssl3 libsodium23 libevent-2.1-7 libevent-pthreads-2.1-7
        libdb5.3++ libminiupnpc17 libzmq5 libqrencode4 libgmp10
        libprotobuf23 openssl
    )
else
    # Fallback (Ubuntu 20.04, older Debian) - use apt-cache search at runtime
    PACKAGES=(
        wget curl tar gzip file ca-certificates
        ufw iptables iptables-persistent netfilter-persistent
        libboost-filesystem1.71.0 libboost-system1.71.0 libboost-thread1.71.0
        libboost-program-options1.71.0 libboost-chrono1.71.0 libboost-date-time1.71.0
        libssl1.1 libsodium23 libevent-2.1-7 libevent-pthreads-2.1-7
        libdb5.3++ libminiupnpc17 libzmq5 libqrencode4 libgmp10
        libprotobuf17 openssl
    )
fi

# Bulk install (fast path)
if $SUDO_CMD DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${PACKAGES[@]}" 2>/dev/null; then
    ok "All dependencies installed"
else
    warn "Bulk install failed — trying individually"
    for pkg in "${PACKAGES[@]}"; do
        if $SUDO_CMD DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$pkg" 2>/dev/null; then
            :  # ok
        else
            # Try without t64 suffix (or with t64 suffix added)
            if [[ "$pkg" == *"t64"* ]]; then
                alt="${pkg%t64}"
            else
                alt="${pkg}t64"
            fi
            $SUDO_CMD DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$alt" 2>/dev/null \
                || warn "  Skipped: $pkg (may resolve at ldd stage)"
        fi
    done
fi

# ============================================================================
# Download + install binary
# ============================================================================

step "5/10" "Downloading and installing binary"

# Back up existing binaries
for bin in babacoind babacoin-cli babacoin-tx babacoin-qt; do
    for dir in /usr/bin /usr/local/bin; do
        if [ -x "$dir/$bin" ] && [ ! -L "$dir/$bin" ]; then
            BACKUP="${dir}/${bin}.backup-$(date +%s)"
            log "Backup: $dir/$bin → $BACKUP"
            $SUDO_CMD mv "$dir/$bin" "$BACKUP" || true
        fi
    done
done

mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

log "Downloading: $BINARY_URL"
if ! wget -q --show-progress -O babacoin.tar.gz "$BINARY_URL"; then
    die "Download failed — check your internet connection"
fi

log "Extracting..."
tar xzf babacoin.tar.gz

[ -x ./babacoind ] || die "babacoind not found in archive"

# Verify architecture
FILE_INFO=$(file ./babacoind)
log "Binary: $FILE_INFO"
if ! echo "$FILE_INFO" | grep -q "$EXPECTED_ARCH_MAGIC"; then
    die "Binary arch mismatch. Expected '$EXPECTED_ARCH_MAGIC', got: $FILE_INFO"
fi

# Auto-resolve missing .so
MAX_ATTEMPTS=5
ATTEMPT=0
while [ $ATTEMPT -lt $MAX_ATTEMPTS ]; do
    ATTEMPT=$((ATTEMPT + 1))
    MISSING=$(ldd ./babacoind 2>&1 | awk '/not found/ {print $1}')

    if [ -z "$MISSING" ]; then
        if OUT=$(./babacoind -version 2>&1 | head -1) && [ -n "$OUT" ]; then
            ok "Binary runs: $OUT"
            break
        fi
        die "Binary fails with no obvious missing libs"
    fi

    log "Missing libs (attempt $ATTEMPT):"
    echo "$MISSING" | sed 's/^/    /'

    # Install apt-file if needed
    command -v apt-file >/dev/null || {
        $SUDO_CMD DEBIAN_FRONTEND=noninteractive apt-get install -y -qq apt-file 2>/dev/null || true
        $SUDO_CMD apt-file update 2>/dev/null || true
    }

    RESOLVED=0
    for LIB in $MISSING; do
        PKG=""
        command -v apt-file >/dev/null && PKG=$(apt-file search --package-only "/${LIB}$" 2>/dev/null | head -1)

        # Fallback to hardcoded map
        if [ -z "$PKG" ]; then
            case "$LIB" in
                libevent_pthreads*) PKG="libevent-pthreads-2.1-7t64" ;;
                libevent-*)         PKG="libevent-2.1-7t64" ;;
                libminiupnpc*)      PKG="libminiupnpc17" ;;
                libboost_filesystem*) PKG="libboost-filesystem1.83.0" ;;
                libboost_system*)     PKG="libboost-system1.83.0" ;;
                libboost_thread*)     PKG="libboost-thread1.83.0" ;;
                libboost_program*)    PKG="libboost-program-options1.83.0" ;;
                libboost_chrono*)     PKG="libboost-chrono1.83.0t64" ;;
                libboost_date_time*)  PKG="libboost-date-time1.83.0" ;;
                libdb_cxx*)           PKG="libdb5.3++t64" ;;
                libssl.so.3|libcrypto.so.3) PKG="libssl3t64" ;;
                libzmq.so.5)          PKG="libzmq5" ;;
                libprotobuf.so.*)     PKG="libprotobuf32t64" ;;
                libqrencode.so.4)     PKG="libqrencode4" ;;
                libgmp.so.10)         PKG="libgmp10" ;;
                libsodium.so.23)      PKG="libsodium23" ;;
            esac

            # If t64 fails, try without t64
            if [ -n "$PKG" ] && ! apt-cache show "$PKG" >/dev/null 2>&1; then
                PKG="${PKG%t64}"
            fi
        fi

        if [ -n "$PKG" ]; then
            log "  Installing $PKG..."
            $SUDO_CMD DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$PKG" 2>/dev/null && RESOLVED=1
        fi
    done

    [ "$RESOLVED" = "0" ] && die "Cannot resolve missing libs — check manually: ldd $WORK_DIR/babacoind"
done

# Install to /usr/local/bin
log "Installing binaries to /usr/local/bin..."
for bin in babacoind babacoin-cli babacoin-tx; do
    if [ -x "./$bin" ]; then
        $SUDO_CMD install -m 755 "./$bin" "/usr/local/bin/$bin"
        ok "  $bin"
    fi
done

babacoind -version | head -1
cd "$HOME"
rm -rf "$WORK_DIR"

# ============================================================================
# Write optimized babacoin.conf
# ============================================================================

step "6/10" "Writing optimized config"

mkdir -p "$DATA_DIR"

[ "$SEED_NUM" = "auto" ] && SEED_NUM=$(printf "%02d" "$((RANDOM % 20))")

# Preserve / generate RPC credentials
RPC_USER="" RPC_PASS=""
if [ -f "$DATA_DIR/babacoin.conf" ]; then
    RPC_USER=$(grep -E '^rpcuser=' "$DATA_DIR/babacoin.conf" 2>/dev/null | head -1 | cut -d= -f2-)
    RPC_PASS=$(grep -E '^rpcpassword=' "$DATA_DIR/babacoin.conf" 2>/dev/null | head -1 | cut -d= -f2-)
    cp -a "$DATA_DIR/babacoin.conf" "$DATA_DIR/babacoin.conf.backup-$(date +%s)"
fi
[ -z "$RPC_USER" ] && RPC_USER="babacoin"
[ -z "$RPC_PASS" ] && RPC_PASS=$(openssl rand -hex 24 2>/dev/null || head -c 32 /dev/urandom | base64 | tr -d '/+=' | head -c 32)

cat > "$DATA_DIR/babacoin.conf" << EOF
# Babacoin node - auto-generated $(date)
daemon=1
server=1
txindex=1
listen=1
port=6678
maxconnections=125
bind=0.0.0.0

# Pinned seed peers (DNS seeds being rebuilt)
addnode=158.101.169.146:6678
addnode=129.146.161.225:6678
addnode=158.101.207.106:6678
addnode=141.145.197.15:6678
addnode=31.155.99.197:6678

# Sync tuning
dbcache=${DBCACHE}
par=${PAR}
maxmempool=300

# RPC
rpcuser=${RPC_USER}
rpcpassword=${RPC_PASS}
rpcallowip=127.0.0.1
rpcbind=127.0.0.1

logtimestamps=1
logips=1
EOF

chmod 600 "$DATA_DIR/babacoin.conf"
ok "Config written"

# ============================================================================
# Swap file (required for small Oracle/cloud instances with low RAM)
# ============================================================================

step "6.5/10" "Configuring swap file"

CURRENT_SWAP_MB=$(free -m | awk '/^Swap:/ {print $2}')
if [ "$CURRENT_SWAP_MB" -lt 1024 ]; then
    if [ ! -f /swapfile ]; then
        log "No swap file found. Creating /swapfile (2GB)..."
        # Use dd for broad compatibility (fallocate can fail on some filesystems)
        $SUDO_CMD dd if=/dev/zero of=/swapfile bs=1M count=2048 status=none
        $SUDO_CMD chmod 600 /swapfile
        $SUDO_CMD mkswap /swapfile >/dev/null
        $SUDO_CMD swapon /swapfile
        # Persist via fstab (if not already there)
        if ! grep -q '/swapfile' /etc/fstab; then
            echo '/swapfile swap swap auto 0 0' | $SUDO_CMD tee -a /etc/fstab >/dev/null
        fi
        ok "Swap file created and activated (2GB)"
    else
        log "/swapfile exists but not active, activating..."
        $SUDO_CMD swapon /swapfile 2>/dev/null || true
    fi

    # Tune swappiness for VPS (prefer RAM, use swap only when needed)
    $SUDO_CMD sysctl -w vm.swappiness=10 >/dev/null
    if ! grep -q '^vm.swappiness' /etc/sysctl.conf; then
        echo 'vm.swappiness = 10' | $SUDO_CMD tee -a /etc/sysctl.conf >/dev/null
    fi
    ok "vm.swappiness set to 10"
else
    ok "Swap already configured (${CURRENT_SWAP_MB}MB)"
fi

# ============================================================================
# Firewall — Oracle Cloud requires all three layers:
#   1. iptables rules.v4  (Oracle default has INPUT REJECT at position ~6)
#   2. UFW (user-friendly wrapper, auto-persists)
#   3. firewalld (some Oracle images use this instead/additionally)
# ============================================================================

step "7/10" "Configuring firewall (Oracle-compatible, 3 layers)"

# --- Layer 1: iptables rules.v4 (critical on Oracle Cloud) ---
log "Layer 1: iptables..."

# Make sure iptables-persistent directory exists
$SUDO_CMD mkdir -p /etc/iptables

RULES_FILE="/etc/iptables/rules.v4"
RULE_LINE="-A INPUT -p tcp -m state --state NEW -m tcp --dport 6678 -j ACCEPT"

# Create rules.v4 if missing, with a sensible default set
if [ ! -f "$RULES_FILE" ]; then
    $SUDO_CMD tee "$RULES_FILE" >/dev/null << 'DEFAULTS'
*filter
:INPUT ACCEPT [0:0]
:FORWARD ACCEPT [0:0]
:OUTPUT ACCEPT [0:0]
-A INPUT -i lo -j ACCEPT
-A INPUT -m state --state RELATED,ESTABLISHED -j ACCEPT
-A INPUT -p tcp -m state --state NEW -m tcp --dport 22 -j ACCEPT
COMMIT
DEFAULTS
    log "Created new rules.v4 with SSH + lo + established rules"
fi

# Add port 6678 rule if not already present
if ! $SUDO_CMD grep -qF -- "$RULE_LINE" "$RULES_FILE"; then
    # Insert before the COMMIT line of the filter table
    $SUDO_CMD sed -i "/^COMMIT$/i $RULE_LINE" "$RULES_FILE"
    log "Added port 6678 rule to rules.v4"
fi

# Apply immediately (without waiting for reboot)
$SUDO_CMD iptables-restore < "$RULES_FILE" 2>/dev/null || {
    # Fallback if restore fails — add rule directly
    $SUDO_CMD iptables -C INPUT -p tcp --dport 6678 -j ACCEPT 2>/dev/null \
        || $SUDO_CMD iptables -I INPUT -p tcp --dport 6678 -j ACCEPT
}
ok "iptables: 6678/tcp rule active and persisted to $RULES_FILE"

# Also save current state via netfilter-persistent if available
command -v netfilter-persistent >/dev/null && $SUDO_CMD netfilter-persistent save >/dev/null 2>&1 || true

# --- Layer 2: UFW ---
log "Layer 2: UFW..."
if command -v ufw >/dev/null 2>&1; then
    $SUDO_CMD ufw allow 22/tcp comment 'SSH' >/dev/null 2>&1 || true
    $SUDO_CMD ufw allow 6678/tcp comment 'Babacoin P2P' >/dev/null 2>&1 || true
    $SUDO_CMD ufw status | grep -q "Status: active" \
        || $SUDO_CMD ufw --force enable >/dev/null 2>&1 \
        || warn "UFW enable failed (may conflict with firewalld)"
    ok "UFW: 22, 6678/tcp allowed"
else
    warn "UFW not installed — skipping"
fi

# --- Layer 3: firewalld (some Oracle images need this) ---
log "Layer 3: firewalld..."
if ! command -v firewall-cmd >/dev/null 2>&1; then
    log "Installing firewalld..."
    $SUDO_CMD DEBIAN_FRONTEND=noninteractive apt-get install -y -qq firewalld 2>/dev/null || true
fi

if command -v firewall-cmd >/dev/null 2>&1; then
    $SUDO_CMD systemctl start firewalld 2>/dev/null || true
    $SUDO_CMD systemctl enable firewalld >/dev/null 2>&1 || true
    if $SUDO_CMD systemctl is-active --quiet firewalld; then
        $SUDO_CMD firewall-cmd --zone=public --permanent --add-port=6678/tcp >/dev/null 2>&1 || true
        $SUDO_CMD firewall-cmd --zone=public --permanent --add-port=22/tcp >/dev/null 2>&1 || true
        $SUDO_CMD firewall-cmd --reload >/dev/null 2>&1 || true
        ok "firewalld: 6678/tcp allowed in public zone"
    else
        warn "firewalld service could not be started (UFW may be using the port)"
    fi
else
    warn "firewalld not available — skipping"
fi

# Oracle Cloud reminder
warn "REMINDER: Oracle Cloud also requires opening port 6678/tcp in:"
warn "  Cloud Console → Networking → VCN → Security List → Ingress Rules"
warn "  Add: Source 0.0.0.0/0, TCP, Dest port 6678"

# ============================================================================
# systemd service
# ============================================================================

step "8/10" "Creating systemd service"

$SUDO_CMD tee /etc/systemd/system/babacoind.service >/dev/null << EOF
[Unit]
Description=Babacoin Core Node (seed${SEED_NUM})
Documentation=https://github.com/${REPO}
After=network-online.target
Wants=network-online.target

[Service]
Type=forking
User=${BBC_USER}
Group=${BBC_USER}
WorkingDirectory=${BBC_HOME}
ExecStart=/usr/local/bin/babacoind -daemon -conf=${DATA_DIR}/babacoin.conf -datadir=${DATA_DIR}
PIDFile=${DATA_DIR}/babacoind.pid
ExecStop=/usr/local/bin/babacoin-cli -datadir=${DATA_DIR} stop
TimeoutStopSec=300
Restart=on-failure
RestartSec=30
StartLimitInterval=180
StartLimitBurst=4
LimitNOFILE=65536
MemoryMax=90%
PrivateTmp=true
ProtectSystem=full
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
EOF

$SUDO_CMD systemctl daemon-reload
$SUDO_CMD systemctl enable babacoind >/dev/null 2>&1
ok "systemd service installed and enabled"

# ============================================================================
# Start service
# ============================================================================

step "9/10" "Starting service"

$SUDO_CMD systemctl start babacoind
log "Waiting for daemon to initialize (up to 60s)..."

RPC_OK=0
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do
    sleep 5
    if babacoin-cli -datadir="$DATA_DIR" getblockchaininfo >/dev/null 2>&1; then
        RPC_OK=1
        break
    fi
done

if [ "$RPC_OK" = "0" ]; then
    err "Daemon did not start. Recent logs:"
    $SUDO_CMD journalctl -u babacoind --no-pager -n 20 | sed 's/^/    /'
    die "Installation failed"
fi

ok "Daemon running, RPC responsive"

# ============================================================================
# Create wallet
# ============================================================================

step "10/10" "Creating wallet"

WALLET_ADDR=""
WALLET_PRIVKEY=""

if [ "$SKIP_WALLET" = "1" ]; then
    ok "Skipping wallet creation (SKIP_WALLET=1)"
else
    # Check if wallet already exists
    if babacoin-cli getwalletinfo 2>/dev/null | grep -q '"walletname"'; then
        log "Existing wallet detected"
        EXISTING_ADDRS=$(babacoin-cli getaddressesbylabel "seed-receive" 2>/dev/null | grep -oE '"B[A-Za-z0-9]+"' | head -1 | tr -d '"')
        if [ -n "$EXISTING_ADDRS" ]; then
            WALLET_ADDR="$EXISTING_ADDRS"
            log "Reusing existing address: $WALLET_ADDR"
        else
            WALLET_ADDR=$(babacoin-cli getnewaddress "seed-receive" 2>/dev/null)
        fi
    else
        # Create default wallet (auto-creates on first call to some RPC methods on Dash forks)
        babacoin-cli createwallet "wallet.dat" 2>/dev/null || true
        sleep 2
        WALLET_ADDR=$(babacoin-cli getnewaddress "seed-receive" 2>/dev/null) || true
    fi

    if [ -n "$WALLET_ADDR" ]; then
        ok "Wallet address: $WALLET_ADDR"
        # Export privkey
        WALLET_PRIVKEY=$(babacoin-cli dumpprivkey "$WALLET_ADDR" 2>/dev/null || echo "")

        # Backup wallet.dat
        BACKUP_DIR="$HOME/babacoin-backups"
        mkdir -p "$BACKUP_DIR"
        chmod 700 "$BACKUP_DIR"
        BACKUP_FILE="$BACKUP_DIR/wallet-$(hostname)-$(date +%Y%m%d-%H%M%S).dat"
        babacoin-cli backupwallet "$BACKUP_FILE" 2>/dev/null && chmod 400 "$BACKUP_FILE" && ok "Wallet backup: $BACKUP_FILE"
    else
        warn "Could not generate wallet address — run manually later:"
        warn "  babacoin-cli getnewaddress \"seed-receive\""
    fi
fi

# ============================================================================
# Sync monitor helper
# ============================================================================

cat > "$HOME/sync-watch.sh" << 'WATCH_EOF'
#!/bin/bash
while true; do
    clear
    echo "=== Babacoin Sync Monitor — $(date) ==="
    echo ""
    BCI=$(babacoin-cli getblockchaininfo 2>/dev/null)
    if [ -z "$BCI" ]; then
        echo "  [Node not responding]"
    else
        echo "$BCI" | grep -E '"chain"|"blocks"|"headers"|"verificationprogress"|"initialblockdownload"|"size_on_disk"'
        echo ""
        echo "  Peers: $(babacoin-cli getconnectioncount 2>/dev/null)"
    fi
    echo ""
    echo "=== Log ==="
    tail -15 ~/.babacoin/debug.log 2>/dev/null \
        | grep -v "ProcessTick\|RenameThread\|bls-worker\|llmq-\|sigshares" | tail -8
    echo ""
    echo "Ctrl-C to exit · refresh 15s"
    sleep 15
done
WATCH_EOF
chmod +x "$HOME/sync-watch.sh"

# ============================================================================
# Final summary
# ============================================================================

title "✅ INSTALLATION COMPLETE"

printf "\n${C}System${N}\n"
printf "  Platform:      %s\n" "$PLATFORM_LABEL"
printf "  Hostname:      %s\n" "$(hostname)"
printf "  External IP:   ${G}%s${N}\n" "$EXTERNAL_IP"
printf "  RAM:           ${TOTAL_MB}MB (dbcache=${DBCACHE}MB)\n"
printf "  CPUs:          ${CPU_COUNT} (par=${PAR})\n"

printf "\n${C}Babacoin${N}\n"
printf "  Version:       %s\n" "$(babacoind -version 2>/dev/null | head -1)"
printf "  Data dir:      %s\n" "$DATA_DIR"
printf "  P2P port:      6678/tcp\n"
printf "  RPC user:      %s\n" "$RPC_USER"

printf "\n${C}Service${N}\n"
printf "  Status:        ${G}%s${N}\n" "$(systemctl is-active babacoind 2>/dev/null)"
printf "  Enabled:       %s\n" "$(systemctl is-enabled babacoind 2>/dev/null)"

# Sync status
printf "\n${C}Initial sync status${N}\n"
babacoin-cli -datadir="$DATA_DIR" getblockchaininfo 2>/dev/null \
    | grep -E '"blocks"|"headers"|"initialblockdownload"' | sed 's/^/  /'
printf "  Peers:         %s\n" "$(babacoin-cli -datadir="$DATA_DIR" getconnectioncount 2>/dev/null)"

# Wallet
if [ -n "$WALLET_ADDR" ]; then
    printf "\n${G}══════════════════════════════════════════════════════════════${N}\n"
    printf "${G}  YOUR BABACOIN WALLET${N}\n"
    printf "${G}══════════════════════════════════════════════════════════════${N}\n\n"
    printf "  ${C}Receive address:${N}\n"
    printf "  ${G}%s${N}\n\n" "$WALLET_ADDR"
    if [ -n "$WALLET_PRIVKEY" ]; then
        printf "  ${C}Private key (SAVE THIS — shown once):${N}\n"
        printf "  ${Y}%s${N}\n\n" "$WALLET_PRIVKEY"
        printf "  ${R}⚠ Anyone with this key controls the funds. Store it offline.${N}\n"
    fi
    printf "  ${C}Wallet backup:${N}\n"
    printf "  %s\n" "$BACKUP_FILE"
fi

printf "\n${C}Useful commands${N}\n"
printf "  Live monitor:  ${Y}~/sync-watch.sh${N}\n"
printf "  Status:        ${Y}sudo systemctl status babacoind${N}\n"
printf "  Logs:          ${Y}sudo journalctl -u babacoind -f${N}\n"
printf "  Balance:       ${Y}babacoin-cli getbalance${N}\n"
printf "  Sync info:     ${Y}babacoin-cli getblockchaininfo${N}\n"

printf "\n${G}══════════════════════════════════════════════════════════════${N}\n"
printf "${G}  Node is running. Sync will take 2-6 hours.${N}\n"
printf "${G}══════════════════════════════════════════════════════════════${N}\n\n"
