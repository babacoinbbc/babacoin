#!/usr/bin/env bash
#
# ============================================================================
# BabaCoin v2.0.0 - Seed Node Installer for Ubuntu 24.04 (x86_64 & ARM64)
# ============================================================================
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-ubuntu-24.04.sh | bash
#   curl -fsSL ... | SEED_NUM=03 bash
#   curl -fsSL ... | SEED_NUM=03 AUTO_YES=1 bash
#
# Or direct:
#   wget https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-ubuntu-24.04.sh
#   chmod +x setup-seed-ubuntu-24.04.sh
#   ./setup-seed-ubuntu-24.04.sh
#
# Requirements:
#   - Ubuntu 24.04 LTS (x86_64 or aarch64/arm64)
#   - User with passwordless sudo (Oracle/AWS/GCP defaults have this)
#   - Port 6678/tcp open in cloud firewall Security List / Security Group
#   - 10+ GB disk free, 2+ GB RAM (4+ recommended)
#
# What this does:
#   1. Detects architecture (x86_64 or aarch64), downloads matching binary
#   2. Installs runtime dependencies for Ubuntu 24.04 (boost 1.83, openssl 3, etc.)
#   3. Auto-resolves any missing .so via apt-file + heuristics
#   4. Writes sync-optimized babacoin.conf with RAM-aware dbcache tuning
#   5. Configures firewall (UFW + iptables, handles Oracle REJECT default)
#   6. Installs systemd service (auto-restart, auto-start on boot)
#   7. Verifies startup, shows next-step commands
#
# Safe to re-run (idempotent).
# ============================================================================

set -euo pipefail

# ============================================================================
# Configuration
# ============================================================================

SEED_NUM="${SEED_NUM:-auto}"
AUTO_YES="${AUTO_YES:-0}"
BBC_VERSION="v2.0.0-test"
WORK_DIR="${HOME}/bbc-install-$(date +%s)"
DATA_DIR="${HOME}/.babacoin"
BBC_USER="${USER}"
BBC_HOME="${HOME}"

# Colors
R="\033[1;31m"; G="\033[1;32m"; Y="\033[1;33m"; B="\033[1;34m"; C="\033[1;36m"; N="\033[0m"

# ============================================================================
# Helpers
# ============================================================================

log()   { printf "${C}[%s]${N} %s\n" "$(date +%H:%M:%S)" "$*"; }
ok()    { printf "${G}[ OK ]${N} %s\n" "$*"; }
warn()  { printf "${Y}[WARN]${N} %s\n" "$*"; }
err()   { printf "${R}[FAIL]${N} %s\n" "$*" >&2; }
title() { printf "\n${B}==============================================================${N}\n${B}  %s${N}\n${B}==============================================================${N}\n" "$*"; }
step()  { printf "\n${B}>> [%s] %s${N}\n" "$1" "$2"; }
die()   { err "$*"; exit 1; }

sudo_run() { sudo -n "$@"; }

confirm() {
    if [ "$AUTO_YES" = "1" ]; then return 0; fi
    local prompt="${1:-Continue?}"
    printf "${Y}%s [y/N]${N} " "$prompt"
    read -r ans < /dev/tty || return 1
    case "$ans" in y|Y|yes|YES) return 0 ;; *) return 1 ;; esac
}

# ============================================================================
# Preflight
# ============================================================================

title "BabaCoin Seed Node Installer - Ubuntu 24.04"
log "Runtime: $(date)"
log "User:    ${USER}"
log "Home:    ${HOME}"
log "Seed:    ${SEED_NUM}"
log "Version: ${BBC_VERSION}"

# Must not run as root
if [ "$EUID" -eq 0 ]; then
    die "Do not run as root. Run as the default user (e.g. 'ubuntu', 'opc')."
fi

# Passwordless sudo
if ! sudo -n true 2>/dev/null; then
    die "Passwordless sudo required. Oracle/AWS/GCP defaults have this. Check /etc/sudoers.d/."
fi
ok "Passwordless sudo available"

# OS check
[ -f /etc/os-release ] || die "/etc/os-release not found"
# shellcheck disable=SC1091
source /etc/os-release
[ "${ID}" = "ubuntu" ] || die "This script only supports Ubuntu. Found: ${ID}"
MAJOR="${VERSION_ID%%.*}"
if [ "$MAJOR" -lt 24 ]; then
    die "Ubuntu 24.04 or later required. Found: ${VERSION_ID}. Use setup-seed-oracle-22.04-arm.sh for 22.04."
fi
ok "System: Ubuntu ${VERSION_ID}"

# Architecture detection
ARCH=$(uname -m)
case "$ARCH" in
    x86_64|amd64)
        BINARY_ARCH="x86_64"
        BINARY_URL="https://github.com/babacoinbbc/babacoin/releases/download/${BBC_VERSION}/babacoin-v2.0.0-linux-ubuntu24.04-x86_64.tar.gz"
        EXPECTED_FILE_MAGIC="x86-64"
        ok "Architecture: x86_64 (Intel/AMD 64-bit)"
        ;;
    aarch64|arm64)
        BINARY_ARCH="arm64"
        BINARY_URL="https://github.com/babacoinbbc/babacoin/releases/download/${BBC_VERSION}/babacoin-v2.0.0-linux-ubuntu24.04-arm64.tar.gz"
        EXPECTED_FILE_MAGIC="ARM aarch64"
        ok "Architecture: aarch64 (ARM 64-bit)"
        ;;
    *)
        die "Unsupported architecture: $ARCH. Only x86_64 and aarch64 are supported."
        ;;
esac

# Disk space
AVAIL_GB=$(df -BG "$HOME" | awk 'NR==2 {gsub("G",""); print $4}')
[ "$AVAIL_GB" -ge 10 ] || die "Insufficient disk space (${AVAIL_GB}GB). Minimum 10GB required."
ok "Disk: ${AVAIL_GB}GB available"

# RAM detection (for dbcache tuning)
TOTAL_MB=$(free -m | awk 'NR==2 {print $2}')
if [ "$TOTAL_MB" -lt 1500 ]; then
    warn "Low RAM (${TOTAL_MB}MB). Sync will be slow. Consider larger instance."
fi
# RAM-aware dbcache
if [ "$TOTAL_MB" -lt 2500 ]; then
    DBCACHE=512
elif [ "$TOTAL_MB" -lt 5000 ]; then
    DBCACHE=1024
elif [ "$TOTAL_MB" -lt 10000 ]; then
    DBCACHE=2048
elif [ "$TOTAL_MB" -lt 20000 ]; then
    DBCACHE=4096
else
    DBCACHE=6144
fi
log "RAM: ${TOTAL_MB}MB → dbcache=${DBCACHE}MB"

# CPU for par=
CPU_COUNT=$(nproc)
PAR=$CPU_COUNT
[ "$PAR" -gt 8 ] && PAR=8
log "CPUs: ${CPU_COUNT} → par=${PAR}"

# External IP (for summary)
EXTERNAL_IP=""
for svc in \
    "https://api.ipify.org" \
    "https://ifconfig.me" \
    "https://ipv4.icanhazip.com" \
    "https://checkip.amazonaws.com"
do
    EXTERNAL_IP=$(curl -fsS --max-time 5 "$svc" 2>/dev/null | tr -d '[:space:]') && break
done
[ -n "$EXTERNAL_IP" ] || EXTERNAL_IP="(could not detect)"
log "External IP: $EXTERNAL_IP"

# ============================================================================
# STEP 1: Check existing install
# ============================================================================

step "1/10" "Checking existing installation"

EXISTING_VERSION=""
for path in /usr/local/bin/babacoind /usr/bin/babacoind; do
    if [ -x "$path" ]; then
        EXISTING_VERSION=$("$path" -version 2>/dev/null | head -1 || echo "unknown")
        log "Existing: $path - $EXISTING_VERSION"
        break
    fi
done

SKIP_BINARY_INSTALL=0
if echo "$EXISTING_VERSION" | grep -q "v2.0.0"; then
    ok "v2.0.0 already installed. Skipping binary install."
    SKIP_BINARY_INSTALL=1
fi

# ============================================================================
# STEP 2: Stop running daemon
# ============================================================================

step "2/10" "Stopping running daemon"

if systemctl is-active --quiet babacoind 2>/dev/null; then
    log "Stopping systemd service..."
    sudo systemctl stop babacoind || true
fi

if pgrep -x babacoind >/dev/null; then
    log "Graceful stop..."
    command -v babacoin-cli >/dev/null && babacoin-cli stop 2>/dev/null || true
    sleep 3
    pgrep -x babacoind >/dev/null && sudo killall babacoind 2>/dev/null || true
    sleep 2
    pgrep -x babacoind >/dev/null && sudo killall -9 babacoind 2>/dev/null || true
fi

ok "No babacoind processes running"

# ============================================================================
# STEP 3: System dependencies (Ubuntu 24.04 package names)
# ============================================================================

step "3/10" "Installing system dependencies"

log "apt update..."
sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq

log "Installing runtime libraries for Ubuntu 24.04..."
# Ubuntu 24.04 (Noble Numbat) package names - verified against apt-cache:
#   - boost 1.83 (chrono is t64, rest are plain)
#   - openssl 3 (libssl3t64)
#   - libevent 2.1-7 (t64)
#   - libdb 5.3 (t64)
#   - libminiupnpc 17 (NOT 18 on 24.04 - stayed at 17)
#   - protobuf 32 (t64)
#   - others: libsodium23, libzmq5, libqrencode4, libgmp10 (no t64)
PACKAGES=(
    wget curl tar gzip file ca-certificates
    ufw iptables iptables-persistent netfilter-persistent
    libboost-filesystem1.83.0 libboost-system1.83.0 libboost-thread1.83.0
    libboost-program-options1.83.0 libboost-chrono1.83.0t64 libboost-date-time1.83.0
    libssl3t64 libsodium23 libevent-2.1-7t64 libevent-pthreads-2.1-7t64
    libdb5.3++t64 libminiupnpc17 libzmq5 libqrencode4 libgmp10
    libprotobuf32t64
    openssl
)

# Some packages may have both t64 and non-t64 variants across releases.
# If one fails, try alternate names.
FALLBACK_ALIASES=(
    "libssl3t64:libssl3"
    "libevent-2.1-7t64:libevent-2.1-7"
    "libevent-pthreads-2.1-7t64:libevent-pthreads-2.1-7"
    "libprotobuf32t64:libprotobuf32"
    "libprotobuf32t64:libprotobuf23"
    "libdb5.3++t64:libdb5.3++"
    "libboost-chrono1.83.0t64:libboost-chrono1.83.0"
    "libminiupnpc17:libminiupnpc18"
)

# Bulk install first (fastest when it works)
if sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${PACKAGES[@]}" 2>/dev/null; then
    ok "Bulk install succeeded"
else
    warn "Bulk install failed - installing individually with fallbacks..."
    for pkg in "${PACKAGES[@]}"; do
        if sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$pkg" 2>/dev/null; then
            continue
        fi
        # Try fallbacks
        FOUND_FALLBACK=0
        for alias in "${FALLBACK_ALIASES[@]}"; do
            orig="${alias%%:*}"
            alt="${alias##*:}"
            if [ "$orig" = "$pkg" ]; then
                if sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$alt" 2>/dev/null; then
                    log "  Fallback: $pkg → $alt"
                    FOUND_FALLBACK=1
                    break
                fi
            fi
        done
        [ "$FOUND_FALLBACK" = "0" ] && warn "  Skipped: $pkg (not available, may resolve at runtime)"
    done
fi

ok "Dependencies installed"

# ============================================================================
# STEP 4: Download and install binary
# ============================================================================

if [ "$SKIP_BINARY_INSTALL" = "0" ]; then
    step "4/10" "Downloading BabaCoin ${BBC_VERSION} (${BINARY_ARCH})"

    # Back up old binaries (v1.x or other)
    for bin in babacoind babacoin-cli babacoin-tx babacoin-qt; do
        for dir in /usr/bin /usr/local/bin; do
            if [ -x "$dir/$bin" ] && [ ! -L "$dir/$bin" ]; then
                BACKUP_NAME="${dir}/${bin}.backup-$(date +%Y%m%d-%H%M%S)"
                log "Backup: $dir/$bin → $BACKUP_NAME"
                sudo mv "$dir/$bin" "$BACKUP_NAME" || true
            fi
        done
    done

    mkdir -p "$WORK_DIR"
    cd "$WORK_DIR"

    log "Downloading: $BINARY_URL"
    if ! wget -q --show-progress -O babacoin.tar.gz "$BINARY_URL"; then
        die "Download failed. Check https://github.com/babacoinbbc/babacoin/releases for available binaries."
    fi

    log "Verifying download..."
    [ -s babacoin.tar.gz ] || die "Downloaded file is empty"
    file babacoin.tar.gz | grep -q "gzip" || die "Not a valid gzip archive"

    log "Extracting..."
    tar xzf babacoin.tar.gz

    [ -x ./babacoind ] || die "babacoind not found inside archive"

    FILE_INFO=$(file ./babacoind)
    log "Binary: $FILE_INFO"
    if ! echo "$FILE_INFO" | grep -q "$EXPECTED_FILE_MAGIC"; then
        die "Binary architecture mismatch. Expected '$EXPECTED_FILE_MAGIC', got: $FILE_INFO"
    fi

    # Test & auto-resolve missing .so
    MAX_LDD_ATTEMPTS=5
    LDD_ATTEMPT=0
    while [ $LDD_ATTEMPT -lt $MAX_LDD_ATTEMPTS ]; do
        LDD_ATTEMPT=$((LDD_ATTEMPT + 1))
        MISSING_LIBS=$(ldd ./babacoind 2>&1 | awk '/not found/ {print $1}')

        if [ -z "$MISSING_LIBS" ]; then
            if VERSION_OUTPUT=$(./babacoind -version 2>&1 | head -1) && [ -n "$VERSION_OUTPUT" ]; then
                ok "Binary runs: $VERSION_OUTPUT"
                break
            else
                warn "Binary still fails despite libs resolved:"
                ./babacoind -version 2>&1 | head -5 || true
                die "Unknown binary failure - see errors above"
            fi
        fi

        log "Missing libraries (attempt $LDD_ATTEMPT/$MAX_LDD_ATTEMPTS):"
        echo "$MISSING_LIBS" | sed 's/^/    /'

        # Install apt-file if needed
        if ! command -v apt-file >/dev/null 2>&1; then
            log "Installing apt-file for library resolution..."
            sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq apt-file 2>/dev/null || true
            sudo apt-file update 2>/dev/null || true
        fi

        RESOLVED_ANY=0
        for LIB in $MISSING_LIBS; do
            log "Resolving $LIB..."
            PKG=""

            # Strategy 1: apt-file search
            if command -v apt-file >/dev/null 2>&1; then
                PKG=$(apt-file search --package-only "/${LIB}$" 2>/dev/null | head -1)
            fi

            # Strategy 2: hardcoded known libs for Ubuntu 24.04
            if [ -z "$PKG" ]; then
                case "$LIB" in
                    libevent_pthreads*)   PKG="libevent-pthreads-2.1-7t64" ;;
                    libevent-*)           PKG="libevent-2.1-7t64" ;;
                    libminiupnpc.so.17)   PKG="libminiupnpc17" ;;
                    libminiupnpc.so.18)   PKG="libminiupnpc17" ;;  # 24.04 still uses 17
                    libboost_filesystem*) PKG="libboost-filesystem1.83.0" ;;
                    libboost_system*)     PKG="libboost-system1.83.0" ;;
                    libboost_thread*)     PKG="libboost-thread1.83.0" ;;
                    libboost_program*)    PKG="libboost-program-options1.83.0" ;;
                    libboost_chrono*)     PKG="libboost-chrono1.83.0t64" ;;
                    libboost_date_time*)  PKG="libboost-date-time1.83.0" ;;
                    libdb_cxx*)           PKG="libdb5.3++t64" ;;
                    libssl.so.3)          PKG="libssl3t64" ;;
                    libcrypto.so.3)       PKG="libssl3t64" ;;
                    libzmq.so.5)          PKG="libzmq5" ;;
                    libprotobuf.so.*)     PKG="libprotobuf32t64" ;;
                    libqrencode.so.4)     PKG="libqrencode4" ;;
                    libgmp.so.10)         PKG="libgmp10" ;;
                    libsodium.so.23)      PKG="libsodium23" ;;
                esac
            fi

            # Strategy 3: guess from library name
            if [ -z "$PKG" ]; then
                BASE=$(echo "$LIB" | sed -E 's/\.so\.[0-9]+.*//; s/^lib//; s/_/-/g')
                for PKG_GUESS in "lib${BASE}" "lib${BASE}0" "lib${BASE}1" "lib${BASE}t64"; do
                    if apt-cache show "$PKG_GUESS" >/dev/null 2>&1; then
                        PKG="$PKG_GUESS"
                        break
                    fi
                done
            fi

            if [ -n "$PKG" ]; then
                log "  Installing $PKG..."
                if sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$PKG" 2>/dev/null; then
                    RESOLVED_ANY=1
                else
                    warn "  Failed to install $PKG"
                fi
            else
                warn "  Could not determine package for $LIB"
            fi
        done

        [ "$RESOLVED_ANY" = "0" ] && die "No progress resolving libs. Check manually: ldd ./babacoind"
    done

    # Install to /usr/local/bin
    log "Installing binaries to /usr/local/bin..."
    for bin in babacoind babacoin-cli babacoin-tx; do
        if [ -x "./$bin" ]; then
            sudo install -m 755 "./$bin" "/usr/local/bin/$bin"
            ok "Installed: /usr/local/bin/$bin"
        fi
    done

    # Verify final install
    babacoind -version | head -1
    ok "BabaCoin v2.0.0 binaries installed"

    # Cleanup work dir
    cd "$HOME"
    rm -rf "$WORK_DIR"
fi

# ============================================================================
# STEP 5: Prepare data directory
# ============================================================================

step "5/10" "Preparing data directory"

mkdir -p "$DATA_DIR"
ok "Data dir: $DATA_DIR"

# Detect seed number if auto
if [ "$SEED_NUM" = "auto" ]; then
    HOST_LAST4=$(hostname | tail -c 4)
    SEED_NUM=$(printf "%02d" "$((RANDOM % 20))")
    log "Auto-assigned seed number: $SEED_NUM (override with SEED_NUM=XX)"
fi

# ============================================================================
# STEP 6: babacoin.conf (sync-optimized)
# ============================================================================

step "6/10" "Writing optimized babacoin.conf"

# Preserve existing RPC credentials if present
RPC_USER=""
RPC_PASS=""
if [ -f "$DATA_DIR/babacoin.conf" ]; then
    RPC_USER=$(grep -E '^rpcuser=' "$DATA_DIR/babacoin.conf" 2>/dev/null | head -1 | cut -d= -f2-)
    RPC_PASS=$(grep -E '^rpcpassword=' "$DATA_DIR/babacoin.conf" 2>/dev/null | head -1 | cut -d= -f2-)
    # Backup old config
    cp -a "$DATA_DIR/babacoin.conf" "$DATA_DIR/babacoin.conf.backup-$(date +%Y%m%d-%H%M%S)"
    log "Backed up existing config"
fi

[ -z "$RPC_USER" ] && RPC_USER="babacoin"
if [ -z "$RPC_PASS" ]; then
    RPC_PASS=$(openssl rand -hex 24 2>/dev/null || head -c 32 /dev/urandom | base64 | tr -d '/+=' | head -c 32)
    log "Generated random RPC password"
fi

cat > "$DATA_DIR/babacoin.conf" << EOF
# ============================================================================
# Babacoin node - sync-optimized configuration
# Generated by setup-seed-ubuntu-24.04.sh on $(date)
# Seed number: ${SEED_NUM}
# ============================================================================

# === Core ===
daemon=1
server=1
txindex=1

# === Network ===
listen=1
port=6678
maxconnections=125
bind=0.0.0.0

# === Persistent seed peers (known-live Babacoin nodes) ===
# DNS seeds (seed00-10.babacoin.network) are currently unresolvable.
# Pinning these ensures new nodes can bootstrap reliably.
addnode=158.101.169.146:6678
addnode=129.146.161.225:6678
addnode=158.101.207.106:6678
addnode=141.145.197.15:6678
addnode=31.155.99.197:6678

# === Sync performance tuning ===
dbcache=${DBCACHE}
par=${PAR}
maxmempool=300

# === RPC ===
rpcuser=${RPC_USER}
rpcpassword=${RPC_PASS}
rpcallowip=127.0.0.1
rpcbind=127.0.0.1

# === Logging ===
logtimestamps=1
logips=1
EOF

chmod 600 "$DATA_DIR/babacoin.conf"
ok "Config written: $DATA_DIR/babacoin.conf"

# ============================================================================
# STEP 7: Firewall
# ============================================================================

step "7/10" "Configuring firewall"

log "UFW rules..."
sudo ufw allow 22/tcp comment 'SSH' >/dev/null 2>&1 || true
sudo ufw allow 6678/tcp comment 'Babacoin P2P' >/dev/null 2>&1 || true
if ! sudo ufw status | grep -q "Status: active"; then
    sudo ufw --force enable >/dev/null 2>&1 || warn "UFW enable failed"
fi
ok "UFW: 22/tcp, 6678/tcp allowed"

# Oracle/Cloud iptables fix (Oracle ARM defaults to INPUT REJECT)
log "iptables rule for port 6678..."
if ! sudo iptables -C INPUT -p tcp --dport 6678 -j ACCEPT 2>/dev/null; then
    sudo iptables -I INPUT -p tcp --dport 6678 -j ACCEPT
    ok "iptables rule added"
else
    ok "iptables rule already present"
fi

# Persist iptables
if command -v netfilter-persistent >/dev/null; then
    sudo netfilter-persistent save >/dev/null 2>&1 || true
    ok "iptables saved (netfilter-persistent)"
elif [ -d /etc/iptables ]; then
    sudo iptables-save | sudo tee /etc/iptables/rules.v4 >/dev/null
    ok "iptables saved to /etc/iptables/rules.v4"
else
    warn "No iptables persistence tool - rules may be lost on reboot"
fi

# ============================================================================
# STEP 8: systemd service
# ============================================================================

step "8/10" "Creating systemd service"

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

sudo systemctl daemon-reload
sudo systemctl enable babacoind >/dev/null 2>&1
ok "systemd service enabled"

# ============================================================================
# STEP 9: Start service
# ============================================================================

step "9/10" "Starting service"

sudo systemctl start babacoind
log "Waiting 20s for daemon to initialize..."
sleep 20

if systemctl is-active --quiet babacoind; then
    ok "Service is active"
else
    err "Service failed to start"
    echo ""
    echo "Recent logs:"
    sudo journalctl -u babacoind --no-pager -n 30 || true
    die "Installation failed - see logs above"
fi

# RPC test with retry
log "Testing RPC..."
RPC_OK=0
for i in 1 2 3 4 5 6; do
    if babacoin-cli -datadir="$DATA_DIR" getblockchaininfo >/dev/null 2>&1; then
        RPC_OK=1
        break
    fi
    log "  Attempt $i/6..."
    sleep 10
done

if [ "$RPC_OK" = "1" ]; then
    ok "RPC is live"
else
    warn "RPC not responding yet - may need another minute"
fi

# ============================================================================
# STEP 10: Summary
# ============================================================================

step "10/10" "Installation summary"

# Quick sync monitor helper
cat > "$HOME/sync-watch.sh" << 'WATCH_EOF'
#!/bin/bash
# Live sync monitor - Ctrl-C to exit
while true; do
    clear
    echo "=== Babacoin Sync Monitor - $(date) ==="
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
    echo "=== Recent log ==="
    tail -15 ~/.babacoin/debug.log 2>/dev/null \
        | grep -v "ProcessTick\|RenameThread\|bls-worker\|llmq-\|sigshares" \
        | tail -8
    echo ""
    echo "Refreshing in 15s... (Ctrl-C to exit)"
    sleep 15
done
WATCH_EOF
chmod +x "$HOME/sync-watch.sh"

title "INSTALLATION COMPLETE"

printf "\n${C}--- System Info ---${N}\n"
printf "  Hostname:      %s\n" "$(hostname)"
printf "  External IP:   ${G}%s${N}\n" "$EXTERNAL_IP"
printf "  Seed Number:   ${G}seed%s${N}\n" "$SEED_NUM"
printf "  Ubuntu:        %s\n" "${VERSION}"
printf "  Architecture:  %s (%s binary)\n" "$ARCH" "$BINARY_ARCH"
printf "  RAM:           %sMB → dbcache=%sMB\n" "$TOTAL_MB" "$DBCACHE"
printf "  CPUs:          %s → par=%s\n" "$CPU_COUNT" "$PAR"

printf "\n${C}--- Babacoin Info ---${N}\n"
printf "  Binary:        %s\n" "$(which babacoind)"
printf "  Version:       %s\n" "$(babacoind -version 2>/dev/null | head -1)"
printf "  Data dir:      %s\n" "$DATA_DIR"
printf "  Config:        %s\n" "$DATA_DIR/babacoin.conf"
printf "  P2P port:      6678/tcp\n"
printf "  RPC port:      9998 (localhost only)\n"
printf "  RPC user:      %s\n" "$RPC_USER"

printf "\n${C}--- Service ---${N}\n"
printf "  Status:        ${G}%s${N}\n" "$(systemctl is-active babacoind 2>/dev/null)"
printf "  Enabled:       %s\n" "$(systemctl is-enabled babacoind 2>/dev/null)"

# Initial blockchain info if RPC is responding
if [ "$RPC_OK" = "1" ]; then
    printf "\n${C}--- Initial Sync Status ---${N}\n"
    babacoin-cli -datadir="$DATA_DIR" getblockchaininfo 2>/dev/null \
        | grep -E '"blocks"|"headers"|"verificationprogress"|"initialblockdownload"' \
        | sed 's/^/  /'
    printf "  Peers:         %s\n" "$(babacoin-cli -datadir="$DATA_DIR" getconnectioncount 2>/dev/null)"
fi

printf "\n${C}--- Useful Commands ---${N}\n"
printf "  Status:         ${Y}sudo systemctl status babacoind${N}\n"
printf "  Logs:           ${Y}sudo journalctl -u babacoind -f${N}\n"
printf "  Live sync:      ${Y}~/sync-watch.sh${N}\n"
printf "  Quick check:    ${Y}babacoin-cli getblockchaininfo | grep blocks${N}\n"
printf "  Peer count:     ${Y}babacoin-cli getconnectioncount${N}\n"
printf "  Stop:           ${Y}sudo systemctl stop babacoind${N}\n"
printf "  Restart:        ${Y}sudo systemctl restart babacoind${N}\n"

printf "\n${C}--- Next Steps ---${N}\n"
printf "  1. Wait 3-6 hours for full sync to block ~926,000\n"
printf "  2. Monitor progress: ${Y}~/sync-watch.sh${N}\n"
printf "  3. If this is a new public seed, add DNS record:\n"
printf "     ${Y}seed${SEED_NUM}.babacoin.network  A  %s${N}\n" "$EXTERNAL_IP"
printf "  4. Report issues: https://github.com/babacoinbbc/babacoin/issues\n"

printf "\n${G}==============================================================${N}\n"
printf "${G}  Seed node seed%s is running. Sync will take 3-6 hours.${N}\n" "$SEED_NUM"
printf "${G}==============================================================${N}\n\n"
