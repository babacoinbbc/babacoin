#!/usr/bin/env bash
#
# ============================================================================
# BabaCoin v2.0.0 - Seed Node Installer for Oracle ARM Ubuntu 22.04
# ============================================================================
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | bash
#   curl -fsSL ... | SEED_NUM=03 bash
#   curl -fsSL ... | SEED_NUM=03 AUTO_YES=1 bash
#
# Requirements:
#   - Oracle Cloud ARM instance (VM.Standard.A1.Flex)
#   - Ubuntu 22.04 LTS aarch64
#   - Default 'ubuntu' user with passwordless sudo (Oracle default)
#   - Port 6678/tcp allowed in Security List
#
# Features:
#   * Idempotent (safe to re-run)
#   * Backs up existing v1 binaries automatically
#   * Bypasses Oracle Cloud default iptables REJECT policy
#   * Installs systemd service (auto-start on reboot)
#   * Generates strong random RPC password
#   * Preserves existing RPC credentials if present
# ============================================================================

set -euo pipefail

# ===== Configuration =====
SEED_NUM="${SEED_NUM:-auto}"
BBC_VERSION="v2.0.0-test"
BINARY_URL="https://github.com/babacoinbbc/babacoin/releases/download/${BBC_VERSION}/babacoin-v2.0.0-linux-ubuntu22.04-arm64.tar.gz"
WORK_DIR="${HOME}/bbc-install-$(date +%s)"
DATA_DIR="${HOME}/.babacoin"
BBC_USER="${USER}"
BBC_HOME="${HOME}"

# ===== Colors =====
R="\033[1;31m"; G="\033[1;32m"; Y="\033[1;33m"; B="\033[1;34m"; C="\033[1;36m"; N="\033[0m"

# ===== Helpers =====
log()   { printf "${C}[%s]${N} %s\n" "$(date +%H:%M:%S)" "$*"; }
ok()    { printf "${G}[ OK ]${N} %s\n" "$*"; }
warn()  { printf "${Y}[WARN]${N} %s\n" "$*"; }
err()   { printf "${R}[FAIL]${N} %s\n" "$*" >&2; }
title() { printf "\n${B}==============================================================${N}\n${B}  %s${N}\n${B}==============================================================${N}\n" "$*"; }
step()  { printf "\n${B}>> [%s] %s${N}\n" "$1" "$2"; }
die()   { err "$*"; exit 1; }

# ===== Preflight =====
title "BabaCoin Seed Node Installer - Oracle ARM Ubuntu 22.04"
log "Runtime: $(date)"
log "User:    ${USER}"
log "Home:    ${HOME}"
log "Seed:    ${SEED_NUM}"
log "Version: ${BBC_VERSION}"

# Must not run as root
if [ "$EUID" -eq 0 ]; then
    die "Do not run as root. Run as the default user (e.g. 'ubuntu')."
fi

# Check passwordless sudo (Oracle default has this)
if ! sudo -n true 2>/dev/null; then
    die "Passwordless sudo required. Oracle's 'ubuntu' user has it by default. Check /etc/sudoers.d/."
fi
ok "Passwordless sudo available"

# OS check
[ -f /etc/os-release ] || die "/etc/os-release not found"
# shellcheck disable=SC1091
source /etc/os-release
[ "${ID}" = "ubuntu" ] || die "This script only supports Ubuntu. Found: ${ID}"
[ "${VERSION_ID%%.*}" -ge 22 ] || die "Ubuntu 22.04 or later required. Found: ${VERSION_ID}"

# Architecture
ARCH=$(uname -m)
if [ "$ARCH" != "aarch64" ] && [ "$ARCH" != "arm64" ]; then
    warn "Designed for ARM64. Found: $ARCH - proceeding anyway"
fi

ok "System: Ubuntu ${VERSION_ID} ${ARCH}"

# Disk
AVAIL_GB=$(df -BG "$HOME" | awk 'NR==2 {gsub("G",""); print $4}')
[ "$AVAIL_GB" -ge 10 ] || die "Insufficient disk space (${AVAIL_GB}GB). Minimum 10GB required."
ok "Disk: ${AVAIL_GB}GB available"

# RAM
TOTAL_MB=$(free -m | awk 'NR==2 {print $2}')
if [ "$TOTAL_MB" -lt 1500 ]; then
    warn "Low RAM (${TOTAL_MB}MB). Initial sync may be slow."
fi

# ========================================================================
# STEP 1: Check existing install
# ========================================================================
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

# ========================================================================
# STEP 2: Stop running daemon
# ========================================================================
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

# ========================================================================
# STEP 3: System dependencies
# ========================================================================
step "3/10" "Installing system dependencies"

log "apt update..."
sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq

log "Installing runtime libraries..."
PACKAGES=(
    wget curl tar gzip file
    ufw iptables-persistent netfilter-persistent
    libboost-filesystem1.74.0 libboost-system1.74.0 libboost-thread1.74.0
    libboost-program-options1.74.0 libboost-chrono1.74.0 libboost-date-time1.74.0
    libssl3 libsodium23 libevent-2.1-7 libevent-pthreads-2.1-7 libdb5.3++
    libminiupnpc17 libzmq5 libqrencode4 libgmp10 libprotobuf23
    openssl
)

if ! sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${PACKAGES[@]}" 2>/dev/null; then
    warn "Bulk install failed, trying individually..."
    for pkg in "${PACKAGES[@]}"; do
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$pkg" 2>/dev/null \
            || warn "Skipped: $pkg"
    done
fi

ok "Dependencies installed"

# ========================================================================
# STEP 4: Download and install binary
# ========================================================================
if [ "$SKIP_BINARY_INSTALL" = "0" ]; then
    step "4/10" "Downloading BabaCoin v2.0.0 binary"

    # Back up old binaries
    for bin in babacoind babacoin-cli babacoin-tx babacoin-qt; do
        for dir in /usr/bin /usr/local/bin; do
            if [ -x "$dir/$bin" ] && [ ! -L "$dir/$bin" ]; then
                BACKUP_NAME="${dir}/${bin}.backup-$(date +%Y%m%d-%H%M%S)"
                log "Backup: $dir/$bin -> $BACKUP_NAME"
                sudo mv "$dir/$bin" "$BACKUP_NAME" || true
            fi
        done
    done

    mkdir -p "$WORK_DIR"
    cd "$WORK_DIR"

    log "Downloading: $BINARY_URL"
    wget -q --show-progress -O babacoin.tar.gz "$BINARY_URL" || die "Download failed"

    log "Extracting archive..."
    tar xzf babacoin.tar.gz

    [ -x ./babacoind ] || die "babacoind not found inside archive"

    FILE_INFO=$(file ./babacoind)
    log "Binary: $FILE_INFO"
    echo "$FILE_INFO" | grep -q "ARM aarch64" || die "Not ARM64 - wrong architecture"

    # Test & auto-resolve missing dependencies
    MAX_LDD_ATTEMPTS=5
    LDD_ATTEMPT=0
    while [ $LDD_ATTEMPT -lt $MAX_LDD_ATTEMPTS ]; do
        LDD_ATTEMPT=$((LDD_ATTEMPT + 1))
        MISSING_LIBS=$(ldd ./babacoind 2>&1 | awk '/not found/ {print $1}')

        if [ -z "$MISSING_LIBS" ]; then
            # All libraries resolved - try running the binary
            if VERSION_OUTPUT=$(./babacoind -version 2>&1 | head -1) && [ -n "$VERSION_OUTPUT" ]; then
                ok "Binary runs: $VERSION_OUTPUT"
                break
            else
                warn "Binary still fails despite all libs resolved:"
                ./babacoind -version 2>&1 | head -5 || true
                die "Unknown binary failure"
            fi
        fi

        log "Missing libraries (attempt $LDD_ATTEMPT/$MAX_LDD_ATTEMPTS):"
        echo "$MISSING_LIBS" | sed 's/^/    /'

        # Try to resolve each missing .so via apt-file / dpkg
        RESOLVED_ANY=0
        for LIB in $MISSING_LIBS; do
            log "Resolving $LIB..."

            # Strategy 1: apt-file search (most accurate)
            if ! command -v apt-file >/dev/null 2>&1; then
                sudo_run env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq apt-file 2>/dev/null || true
                sudo_run apt-file update 2>/dev/null || true
            fi

            PKG=""
            if command -v apt-file >/dev/null 2>&1; then
                PKG=$(apt-file search --package-only "/${LIB}$" 2>/dev/null | head -1)
            fi

            # Strategy 2: dpkg reverse (in case lib is already available but in unusual path)
            if [ -z "$PKG" ]; then
                # Try common package name patterns from library name
                # libfoo-1.2.3.so.4  -> libfoo1.2-4 / libfoo  / libfoo0 etc.
                BASE=$(echo "$LIB" | sed -E 's/\.so\.[0-9]+.*//; s/^lib//; s/_/-/g')
                for PKG_GUESS in "lib${BASE}" "lib${BASE}0" "lib${BASE}1"; do
                    if apt-cache show "$PKG_GUESS" >/dev/null 2>&1; then
                        PKG="$PKG_GUESS"
                        break
                    fi
                done
            fi

            # Strategy 3: special cases (hardcoded for known libs)
            if [ -z "$PKG" ]; then
                case "$LIB" in
                    libevent_pthreads*)   PKG="libevent-pthreads-2.1-7" ;;
                    libevent-*)           PKG="libevent-2.1-7" ;;
                    libminiupnpc.so.17)   PKG="libminiupnpc17" ;;
                    libminiupnpc.so.18)   PKG="libminiupnpc18" ;;
                    libboost_*)           PKG=$(echo "$LIB" | sed -E 's/libboost_([^.]+).*/libboost-\1-dev/' | sed 's/_/-/g') ;;
                    libdb_cxx*)           PKG="libdb5.3++" ;;
                    libssl.so.3)          PKG="libssl3" ;;
                    libzmq.so.5)          PKG="libzmq5" ;;
                    libprotobuf.so.*)     PKG="libprotobuf23" ;;
                    libqrencode.so.4)     PKG="libqrencode4" ;;
                    libgmp.so.10)         PKG="libgmp10" ;;
                    libsodium.so.23)      PKG="libsodium23" ;;
                esac
            fi

            if [ -n "$PKG" ]; then
                log "  -> Package: $PKG"
                if sudo_run env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$PKG" 2>&1 | tail -5; then
                    ok "  Installed: $PKG"
                    RESOLVED_ANY=1
                else
                    warn "  Failed to install $PKG"
                fi
            else
                warn "  Could not find a package that provides $LIB"
            fi
        done

        if [ "$RESOLVED_ANY" = "0" ]; then
            err "No packages resolved in this iteration - stopping to avoid infinite loop"
            err "Still missing:"
            ldd ./babacoind 2>&1 | grep "not found" | sed 's/^/    /'
            die "Cannot resolve remaining dependencies automatically"
        fi

        # Reload ld cache for new libs
        sudo_run ldconfig
    done

    if [ $LDD_ATTEMPT -ge $MAX_LDD_ATTEMPTS ]; then
        err "Reached max attempts ($MAX_LDD_ATTEMPTS) resolving dependencies"
        ldd ./babacoind 2>&1 | grep "not found" || true
        die "Giving up"
    fi

    log "Installing to /usr/local/bin/..."
    sudo cp ./babacoind /usr/local/bin/babacoind
    sudo cp ./babacoin-cli /usr/local/bin/babacoin-cli
    sudo cp ./babacoin-tx /usr/local/bin/babacoin-tx
    sudo chmod +x /usr/local/bin/babacoin*

    hash -r

    INSTALLED_VER=$(/usr/local/bin/babacoind -version 2>&1 | head -1)
    [[ "$INSTALLED_VER" == *"v2.0.0"* ]] || die "Install verify failed: $INSTALLED_VER"
    ok "Installed: $INSTALLED_VER"
else
    step "4/10" "Binary is already v2.0.0 - skipped"
fi

# ========================================================================
# STEP 5: External IP and Seed Number
# ========================================================================
step "5/10" "Detecting external IP"

EXTERNAL_IP=""
for service in "ifconfig.me" "ipv4.icanhazip.com" "api.ipify.org" "checkip.amazonaws.com"; do
    if EXTERNAL_IP=$(curl -s -4 --max-time 5 "https://${service}" 2>/dev/null) && [ -n "$EXTERNAL_IP" ]; then
        if [[ "$EXTERNAL_IP" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            break
        fi
        EXTERNAL_IP=""
    fi
done

[ -n "$EXTERNAL_IP" ] || die "Could not detect external IP. Set manually: EXTERNAL_IP=x.x.x.x curl ... | bash"
ok "External IP: $EXTERNAL_IP"

# Auto-assign seed number
if [ "$SEED_NUM" = "auto" ]; then
    if hostname | grep -qE "node-?[0-9]+"; then
        SEED_NUM=$(hostname | grep -oE "[0-9]+" | head -1 | xargs printf "%02d")
    else
        SEED_NUM=$(printf "%02d" $((RANDOM % 90 + 10)))
    fi
    ok "Seed number: $SEED_NUM (auto-detected)"
fi

# ========================================================================
# STEP 6: Generate configuration
# ========================================================================
step "6/10" "Generating babacoin.conf"

mkdir -p "$DATA_DIR"

if [ -f "$DATA_DIR/babacoin.conf" ] && grep -q "^rpcpassword=" "$DATA_DIR/babacoin.conf"; then
    RPC_PASS=$(grep "^rpcpassword=" "$DATA_DIR/babacoin.conf" | cut -d= -f2)
    RPC_USER=$(grep "^rpcuser=" "$DATA_DIR/babacoin.conf" | cut -d= -f2 || echo "babacoin")
    log "Preserving existing RPC credentials"
else
    RPC_USER="babacoin"
    RPC_PASS=$(openssl rand -hex 24)
    log "Generated new RPC credentials"
fi

cat > "$DATA_DIR/babacoin.conf" << EOF
# ============================================================================
# BabaCoin Seed Node - seed${SEED_NUM}
# Generated: $(date)
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

# RPC (local only)
rpcallowip=127.0.0.1
rpcbind=127.0.0.1
rpcuser=${RPC_USER}
rpcpassword=${RPC_PASS}

# Seed behavior
discover=1
dnsseed=1

# DNS-seed peers
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

# Security
upnp=0
natpmp=0
EOF

chmod 600 "$DATA_DIR/babacoin.conf"
ok "Config written: $DATA_DIR/babacoin.conf"

# ========================================================================
# STEP 7: Firewall
# ========================================================================
step "7/10" "Configuring firewall"

# UFW
log "UFW rules..."
sudo ufw allow 22/tcp comment 'SSH' >/dev/null 2>&1 || true
sudo ufw allow 6678/tcp comment 'Babacoin P2P' >/dev/null 2>&1 || true
sudo ufw status | grep -q "Status: active" || sudo ufw --force enable >/dev/null 2>&1 || warn "UFW enable failed"
ok "UFW: 22/tcp, 6678/tcp allowed"

# Oracle iptables fix (Oracle ARM default has INPUT REJECT)
log "iptables rule for port 6678..."
if ! sudo iptables -C INPUT -p tcp --dport 6678 -j ACCEPT 2>/dev/null; then
    sudo iptables -I INPUT -p tcp --dport 6678 -j ACCEPT
    ok "iptables rule added"
else
    ok "iptables rule already present"
fi

# Persist
if command -v netfilter-persistent >/dev/null; then
    sudo netfilter-persistent save >/dev/null 2>&1 || true
    ok "iptables saved (netfilter-persistent)"
elif [ -d /etc/iptables ]; then
    sudo iptables-save | sudo tee /etc/iptables/rules.v4 >/dev/null
    ok "iptables saved to /etc/iptables/rules.v4"
else
    warn "No iptables persistence tool - rules may be lost on reboot"
fi

# ========================================================================
# STEP 8: systemd service
# ========================================================================
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
ExecStart=/usr/local/bin/babacoind -daemon -conf=${DATA_DIR}/babacoin.conf
PIDFile=${DATA_DIR}/babacoind.pid
ExecStop=/usr/local/bin/babacoin-cli stop
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

# ========================================================================
# STEP 9: Start service
# ========================================================================
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

# RPC test
log "Testing RPC..."
RETRY=0
while [ $RETRY -lt 6 ]; do
    babacoin-cli -datadir="$DATA_DIR" getblockchaininfo >/dev/null 2>&1 && break
    RETRY=$((RETRY + 1))
    log "  Attempt $RETRY/6..."
    sleep 10
done

# ========================================================================
# STEP 10: Summary
# ========================================================================
step "10/10" "Installation summary"

title "INSTALLATION COMPLETE"

printf "\n${C}--- System Info ---${N}\n"
printf "  Hostname:      %s\n" "$(hostname)"
printf "  External IP:   ${G}%s${N}\n" "$EXTERNAL_IP"
printf "  Seed Number:   ${G}seed%s${N}\n" "$SEED_NUM"
printf "  Ubuntu:        %s\n" "${VERSION}"
printf "  Architecture:  %s\n" "$ARCH"

printf "\n${C}--- BabaCoin Info ---${N}\n"
printf "  Binary:        %s\n" "$(which babacoind)"
printf "  Version:       %s\n" "$(babacoind -version 2>&1 | head -1)"
printf "  Config:        %s\n" "$DATA_DIR/babacoin.conf"
printf "  Data dir:      %s\n" "$DATA_DIR"
printf "  Service:       %s\n" "$(systemctl is-active babacoind)"

printf "\n${C}--- RPC Credentials (STORE SECURELY!) ---${N}\n"
printf "  User: ${Y}%s${N}\n" "$RPC_USER"
printf "  Pass: ${Y}%s${N}\n" "$RPC_PASS"

printf "\n${C}--- Sync Status ---${N}\n"
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
    printf "  ${Y}RPC not ready yet - check in 30s${N}\n"
fi

printf "\n${C}--- NEXT STEPS ---${N}\n"
printf "  1. Add DNS A record:\n"
printf "     ${Y}seed%s.babacoin.network${N}  ->  ${G}%s${N}\n" "$SEED_NUM" "$EXTERNAL_IP"
printf "  2. Open port 6678/tcp in Oracle Cloud Security List:\n"
printf "     Networking -> Virtual Cloud Networks -> Security Lists -> Ingress\n"
printf "  3. Wait for full sync (~4-8 hours)\n"

printf "\n${C}--- USEFUL COMMANDS ---${N}\n"
cat << 'EOF'
  # Status
  sudo systemctl status babacoind
  babacoin-cli getblockchaininfo | grep -E "blocks|headers"
  babacoin-cli getconnectioncount
  babacoin-cli getpeerinfo | grep -E "addr|subver" | head -20

  # Logs
  sudo journalctl -u babacoind -f
  tail -f ~/.babacoin/debug.log

  # Control
  sudo systemctl restart babacoind
  sudo systemctl stop babacoind

  # Edit config (restart required)
  nano ~/.babacoin/babacoin.conf && sudo systemctl restart babacoind
EOF

printf "\n${G}Installation completed successfully!${N}\n\n"
