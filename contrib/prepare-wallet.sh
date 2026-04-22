#!/usr/bin/env bash
#
# ============================================================================
# BabaCoin - Wallet Preparation Script
# ============================================================================
# Purpose: Create wallet, generate receive address, backup, export keys.
#
# Usage:
#   ./prepare-wallet.sh              Interactive - walks through setup
#   ./prepare-wallet.sh create       Create new wallet + generate address
#   ./prepare-wallet.sh backup       Backup wallet.dat to safe location
#   ./prepare-wallet.sh info         Show wallet info + balance + address
#   ./prepare-wallet.sh encrypt      Set wallet passphrase (encryption)
#
# Requirements:
#   - babacoind running and synced (or at least started)
#   - babacoin-cli in PATH
# ============================================================================

set -euo pipefail

DATA_DIR="${HOME}/.babacoin"
BACKUP_DIR="${HOME}/babacoin-backups"

# Colors
R="\033[1;31m"; G="\033[1;32m"; Y="\033[1;33m"; B="\033[1;34m"; C="\033[1;36m"; N="\033[0m"
log()   { printf "${C}[%s]${N} %s\n" "$(date +%H:%M:%S)" "$*"; }
ok()    { printf "${G}[ OK ]${N} %s\n" "$*"; }
warn()  { printf "${Y}[WARN]${N} %s\n" "$*"; }
err()   { printf "${R}[FAIL]${N} %s\n" "$*" >&2; }
title() { printf "\n${B}==============================================================${N}\n${B}  %s${N}\n${B}==============================================================${N}\n" "$*"; }
die()   { err "$*"; exit 1; }

# ============================================================================
# Helpers
# ============================================================================

require_running() {
    if ! babacoin-cli getblockchaininfo >/dev/null 2>&1; then
        err "babacoind is not responding. Is it running?"
        err "Try: sudo systemctl status babacoind"
        exit 1
    fi
}

confirm() {
    local prompt="${1:-Continue?}"
    printf "${Y}%s [y/N]${N} " "$prompt"
    read -r ans < /dev/tty || return 1
    case "$ans" in y|Y|yes|YES) return 0 ;; *) return 1 ;; esac
}

# ============================================================================
# CREATE wallet + address
# ============================================================================

cmd_create() {
    title "Create BabaCoin Wallet"
    require_running

    # 1. Check if wallet exists
    local wallet_info
    if wallet_info=$(babacoin-cli getwalletinfo 2>/dev/null); then
        log "A wallet already exists."
        echo "$wallet_info" | grep -E '"walletname"|"walletversion"|"balance"|"unconfirmed_balance"' | sed 's/^/  /'
        echo ""
        if ! confirm "Continue and generate a new address on the existing wallet?"; then
            return 0
        fi
    else
        log "No wallet found. Creating default wallet..."
        # BabaCoin/Dash wallets are auto-created on daemon startup, but force it
        babacoin-cli createwallet "wallet.dat" 2>/dev/null || true
        sleep 2
        babacoin-cli getwalletinfo >/dev/null 2>&1 || die "Failed to initialize wallet"
        ok "Wallet initialized"
    fi

    # 2. Generate a receive address
    log "Generating receive address..."
    local addr
    addr=$(babacoin-cli getnewaddress "seed-receive" 2>/dev/null)
    [ -n "$addr" ] || die "Could not generate address"
    ok "Address generated"

    # 3. Validate it's a Babacoin address (starts with 'B')
    if [[ ! "$addr" =~ ^B ]]; then
        warn "Address does not start with 'B' - unusual: $addr"
    fi

    # 4. Get private key for backup (WARNING: only show once)
    local privkey=""
    if confirm "Export private key for this address? (WARNING: anyone with this key controls the funds)"; then
        privkey=$(babacoin-cli dumpprivkey "$addr" 2>/dev/null) || warn "Could not dump privkey (wallet might be encrypted)"
    fi

    # 5. Show result
    echo ""
    printf "${G}==============================================================${N}\n"
    printf "${G}  WALLET ADDRESS GENERATED${N}\n"
    printf "${G}==============================================================${N}\n\n"
    printf "  ${C}Address:${N}      ${G}%s${N}\n" "$addr"
    printf "  ${C}Label:${N}        seed-receive\n"
    if [ -n "$privkey" ]; then
        printf "  ${C}Private key:${N} ${Y}%s${N}\n" "$privkey"
        printf "  ${R}↑ SAVE THIS PRIVATE KEY OFFLINE. SHOWN ONCE.${N}\n"
    fi
    printf "\n"
    printf "  ${C}Hostname:${N}     %s\n" "$(hostname)"
    printf "  ${C}External IP:${N}  %s\n" "$(curl -fsS --max-time 5 https://api.ipify.org 2>/dev/null || echo 'unknown')"
    printf "\n"
    printf "  ${C}Next steps:${N}\n"
    printf "    1. Copy the address above to your records\n"
    printf "    2. Save the private key in a password manager (1Password, Bitwarden)\n"
    printf "    3. Run: ${Y}$0 backup${N} to save encrypted wallet.dat\n"
    printf "    4. Optionally: ${Y}$0 encrypt${N} to add a passphrase\n"
    printf "\n"
}

# ============================================================================
# BACKUP wallet.dat
# ============================================================================

cmd_backup() {
    title "Backup Wallet"

    local wallet_file="$DATA_DIR/wallets/wallet.dat"
    # BabaCoin may use either new (~/.babacoin/wallets/wallet.dat) or legacy (~/.babacoin/wallet.dat)
    [ ! -f "$wallet_file" ] && wallet_file="$DATA_DIR/wallet.dat"

    [ -f "$wallet_file" ] || die "wallet.dat not found in $DATA_DIR or $DATA_DIR/wallets"

    mkdir -p "$BACKUP_DIR"
    chmod 700 "$BACKUP_DIR"

    local timestamp
    timestamp=$(date +%Y%m%d-%H%M%S)
    local backup_file="$BACKUP_DIR/wallet-$(hostname)-${timestamp}.dat"

    # Use babacoin-cli backupwallet (safer - flushes in-memory state first)
    if babacoin-cli backupwallet "$backup_file" 2>/dev/null; then
        ok "Wallet backed up via RPC to: $backup_file"
    else
        # Fallback: direct file copy (only if daemon is not running)
        log "RPC backup failed, trying file copy..."
        cp -a "$wallet_file" "$backup_file"
        ok "Wallet file copied to: $backup_file"
    fi

    chmod 400 "$backup_file"
    ls -la "$backup_file"
    echo ""
    printf "  ${Y}Transfer this file off-machine:${N}\n"
    printf "    scp '$backup_file' user@safe-host:/path/to/offline/\n"
    printf "\n"
    printf "  ${R}WARNING:${N} Without the passphrase (if encrypted), anyone with\n"
    printf "  this file can spend your funds. Keep it safe.\n"
}

# ============================================================================
# INFO - show wallet status
# ============================================================================

cmd_info() {
    title "Wallet Info"
    require_running

    local wi bci
    wi=$(babacoin-cli getwalletinfo 2>/dev/null) || die "No wallet loaded"
    bci=$(babacoin-cli getblockchaininfo 2>/dev/null)

    printf "\n${C}--- Chain ---${N}\n"
    echo "$bci" | grep -E '"chain"|"blocks"|"headers"|"verificationprogress"|"initialblockdownload"' | sed 's/^/  /'

    printf "\n${C}--- Wallet ---${N}\n"
    echo "$wi" | grep -E '"walletname"|"walletversion"|"balance"|"unconfirmed_balance"|"immature_balance"|"txcount"|"unlocked_until"|"keypoolsize"' | sed 's/^/  /'

    printf "\n${C}--- Addresses ---${N}\n"
    # List addresses by label
    babacoin-cli listlabels 2>/dev/null | while read -r line; do
        label=$(echo "$line" | tr -d '",[] ')
        [ -z "$label" ] && continue
        printf "  ${Y}Label: %s${N}\n" "$label"
        babacoin-cli getaddressesbylabel "$label" 2>/dev/null \
            | grep -oE '"B[A-Za-z0-9]+"' \
            | tr -d '"' \
            | sed 's/^/    /'
    done

    printf "\n${C}--- Peer count: %s ---${N}\n" "$(babacoin-cli getconnectioncount 2>/dev/null)"
}

# ============================================================================
# ENCRYPT - add passphrase to wallet
# ============================================================================

cmd_encrypt() {
    title "Encrypt Wallet"
    require_running

    local wi
    wi=$(babacoin-cli getwalletinfo 2>/dev/null) || die "No wallet loaded"

    if echo "$wi" | grep -q '"unlocked_until"'; then
        warn "Wallet appears to already be encrypted."
        echo ""
        echo "  To unlock for transactions:"
        echo "    babacoin-cli walletpassphrase \"YOUR_PASSPHRASE\" 600"
        return 0
    fi

    warn "Encrypting a wallet is IRREVERSIBLE."
    warn "If you forget the passphrase, YOUR COINS ARE GONE."
    warn "Make a backup FIRST with: $0 backup"
    echo ""
    if ! confirm "Continue with encryption?"; then
        return 0
    fi

    echo ""
    printf "Enter passphrase (min 10 chars, mix of letters/numbers/symbols): "
    read -rs pass1 < /dev/tty
    echo ""
    printf "Re-enter passphrase: "
    read -rs pass2 < /dev/tty
    echo ""

    [ "$pass1" = "$pass2" ] || die "Passphrases do not match"
    [ "${#pass1}" -ge 10 ] || die "Passphrase too short (min 10 chars)"

    log "Encrypting wallet..."
    if babacoin-cli encryptwallet "$pass1"; then
        ok "Wallet encrypted successfully"
        warn "Daemon will stop now. Restart with: sudo systemctl start babacoind"
        warn "To send transactions later: babacoin-cli walletpassphrase \"YOUR_PASSPHRASE\" 600"
    else
        die "Encryption failed"
    fi
}

# ============================================================================
# DOGECOIN-STYLE HD SEED (mnemonic) backup — only if using HD wallet
# ============================================================================

cmd_seed() {
    title "HD Seed"
    require_running

    local seed
    seed=$(babacoin-cli dumphdinfo 2>/dev/null) || {
        warn "dumphdinfo not available - wallet might not be HD."
        warn "For non-HD wallets, use '$0 backup' to save wallet.dat"
        return 0
    }
    echo "$seed"
    echo ""
    printf "${R}↑ THIS IS YOUR MASTER SEED - SAVE OFFLINE, NEVER SHARE${N}\n"
}

# ============================================================================
# INTERACTIVE mode
# ============================================================================

cmd_interactive() {
    title "BabaCoin Wallet Manager"
    require_running

    local blocks peer_count is_synced
    blocks=$(babacoin-cli getblockchaininfo 2>/dev/null | grep -oE '"blocks": [0-9]+' | awk '{print $2}')
    peer_count=$(babacoin-cli getconnectioncount 2>/dev/null)
    is_synced=$(babacoin-cli getblockchaininfo 2>/dev/null | grep -oE '"initialblockdownload": (true|false)' | awk '{print $2}')

    printf "\n  Node:          %s\n" "$(hostname)"
    printf "  Blocks:        %s\n" "$blocks"
    printf "  Peers:         %s\n" "$peer_count"
    printf "  Syncing:       %s\n\n" "$is_synced"

    if [ "$is_synced" = "true" ]; then
        warn "Node is still in initial block download. Wallet operations are fine,"
        warn "but you cannot see incoming transactions until sync completes."
        echo ""
    fi

    echo "What would you like to do?"
    echo ""
    echo "  1) Create wallet + generate receive address"
    echo "  2) Backup wallet.dat"
    echo "  3) Show wallet info + balance + addresses"
    echo "  4) Encrypt wallet (set passphrase)"
    echo "  5) Show HD seed (master key)"
    echo "  6) Exit"
    echo ""
    printf "Choose [1-6]: "
    read -r choice < /dev/tty
    echo ""

    case "$choice" in
        1) cmd_create ;;
        2) cmd_backup ;;
        3) cmd_info ;;
        4) cmd_encrypt ;;
        5) cmd_seed ;;
        6) exit 0 ;;
        *) die "Invalid choice: $choice" ;;
    esac
}

# ============================================================================
# Main dispatcher
# ============================================================================

case "${1:-interactive}" in
    create)      cmd_create ;;
    backup)      cmd_backup ;;
    info)        cmd_info ;;
    encrypt)     cmd_encrypt ;;
    seed)        cmd_seed ;;
    interactive) cmd_interactive ;;
    help|-h|--help)
        sed -n '/^# Usage:/,/^# Requirements:/p' "$0" | sed 's/^# //'
        ;;
    *)
        err "Unknown command: $1"
        echo ""
        echo "Usage: $0 [create|backup|info|encrypt|seed|interactive|help]"
        exit 1
        ;;
esac
