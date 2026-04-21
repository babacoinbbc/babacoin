# Oracle ARM Ubuntu 20.04 → 22.04 Upgrade Guide

> Complete guide to upgrade Oracle Cloud ARM (VM.Standard.A1.Flex) Ubuntu 20.04 LTS (Focal) machines to 22.04 LTS (Jammy).

## ⚠️ Read This First

**In-place upgrade risks:**
- SSH access may be lost (cloud-init / netplan conflicts)
- Bootloader issues → VPS may fail to boot
- Oracle Cloud specific packages may conflict

**Never start without a snapshot.**

---

## 📋 Stage 0 — Preparation (MANDATORY)

### 0.1 — Take a Boot Volume Snapshot (Oracle Cloud Console)

1. **Oracle Cloud Console** → menu
2. **Compute → Instances → [your instance]**
3. Scroll down to **Boot volume** → click the volume link
4. **Block Volume Backups → Create Backup**
5. Type: **Incremental** (faster)
6. Name: `bbc-node-XX-pre-upgrade-22.04-YYYYMMDD`
7. **Create**
8. Wait ~5-10 min for "Available" status

**Note**: First 5 incremental backups under 10GB are **free** in Oracle Free Tier.

### 0.2 — Local Preparation

```bash
# SSH into the VPS
ssh ubuntu@<VPS_IP>

# Stop any running babacoind
sudo systemctl stop babacoind 2>/dev/null || true
sudo killall babacoind 2>/dev/null || true
sleep 3

# Back up critical files to ~/backup/
mkdir -p ~/backup
cp -r ~/.babacoin/babacoin.conf ~/backup/ 2>/dev/null || true
cp -r ~/.babacoin/wallet.dat ~/backup/ 2>/dev/null || true
sudo cp /etc/ssh/sshd_config ~/backup/sshd_config.focal
sudo cp -r /etc/netplan ~/backup/netplan.focal
sudo iptables-save > ~/backup/iptables.focal.rules

# Snapshot of system state
echo "=== Pre-upgrade status ===" | tee ~/upgrade-log.txt
date >> ~/upgrade-log.txt
cat /etc/os-release >> ~/upgrade-log.txt
uname -a >> ~/upgrade-log.txt
df -h / >> ~/upgrade-log.txt
free -h >> ~/upgrade-log.txt

echo "Preparation complete"
```

---

## 📋 Stage 1 — Fully Update 20.04

```bash
# Full update (can take ~5-15 min)
sudo apt update
sudo apt upgrade -y
sudo apt dist-upgrade -y
sudo apt autoremove --purge -y
sudo apt autoclean

# Install update tools
sudo apt install -y update-manager-core screen

# Set upgrade policy to LTS only
sudo sed -i 's/^Prompt=.*/Prompt=lts/' /etc/update-manager/release-upgrades
grep "^Prompt" /etc/update-manager/release-upgrades
# Expected: Prompt=lts
```

---

## 📋 Stage 2 — Choose Upgrade Method

**Two approaches:**

### Method A: `do-release-upgrade` (Recommended, safer)

```bash
# Start inside screen (survives SSH disconnects)
screen -S upgrade

# Run upgrade command
sudo do-release-upgrade
```

**Possible outcomes:**

#### Case A1: "No new release found"

When 20.04 reaches EOL, the prompt may fail. In that case:

```bash
# Switch to Method B (below)
```

#### Case A2: "Do you want to start the upgrade? [y/N]"

→ **`y`** + Enter. Upgrade begins.

### Method B: Manual `sources.list` Modification

When `do-release-upgrade` fails:

```bash
# Start inside screen
screen -S upgrade

# Backup
sudo cp /etc/apt/sources.list /etc/apt/sources.list.focal
[ -d /etc/apt/sources.list.d ] && sudo cp -r /etc/apt/sources.list.d /etc/apt/sources.list.d.focal

# Replace focal with jammy
sudo sed -i 's/focal/jammy/g' /etc/apt/sources.list
if [ -d /etc/apt/sources.list.d ]; then
    for f in /etc/apt/sources.list.d/*.list; do
        [ -f "$f" ] && sudo sed -i 's/focal/jammy/g' "$f"
    done
fi

# Also catch -security/-updates/-backports
sudo sed -i 's/focal-security/jammy-security/g' /etc/apt/sources.list
sudo sed -i 's/focal-updates/jammy-updates/g' /etc/apt/sources.list
sudo sed -i 's/focal-backports/jammy-backports/g' /etc/apt/sources.list

# Clean apt cache
sudo rm -rf /var/lib/apt/lists/*
sudo apt clean

# Refresh from new repos
sudo apt update

# Dist-upgrade with config preservation
sudo DEBIAN_FRONTEND=noninteractive apt-get dist-upgrade -y \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold"

# Cleanup
sudo apt autoremove --purge -y
sudo apt autoclean
```

---

## 🚨 Answering Prompts During `do-release-upgrade` (Method A)

#### Prompt 1: SSH Backup Port
```
To make recovery in case of failure easier, an additional sshd will
be started on port '1022'. If anything goes wrong with the running ssh
you can still connect to the additional one. If you run a firewall,
you may need to temporarily open this port.

Continue? [yN]
```
→ **`y`** (safety net)

#### Prompt 2: Config File Decisions
```
Configuration file '/etc/ssh/sshd_config'
 ==> Modified (by you or by a script) since installation.
 ==> Package distributor has shipped an updated version.
   What would you like to do about it?
   1. install the package maintainer's version
   2. keep the local version currently installed
   3. show the differences between the versions
   4. show a side-by-side difference between the versions
```

Per-file decisions:

| File | Answer | Reason |
|---|---|---|
| `/etc/ssh/sshd_config` | **N (keep local)** | Preserve SSH access |
| `/etc/netplan/*` | **N (keep local)** | Preserve network config |
| `/etc/cloud/*` | **N (keep local)** | Preserve Oracle cloud-init |
| `/etc/sudoers` | **N (keep local)** | Preserve sudo access |
| `/etc/default/grub` | **Y (install new)** | Required for new kernel |
| `/etc/apt/sources.list` | **Y (install new)** | New repo URLs |
| Other `.conf` | **N (keep local)** | Safe default |

#### Prompt 3: Service Restarts
```
Daemons using outdated libraries
Which services should be restarted?
```
→ **Space** to select all, **Tab + OK**

#### Prompt 4: Obsolete Packages
```
Removing obsolete packages...
```
→ **`y`**

#### Prompt 5: Reboot
```
System upgrade is complete.
Restart required.
To finish the upgrade, a restart is required.
Continue? [yN]
```
→ **`y`**

---

## 🔴 SSH Disconnection Recovery

**Situation 1: SSH dropped during upgrade**

This is normal — since you ran it inside `screen`, the upgrade **continues running**. Wait 2-3 minutes and reconnect:

```bash
# New SSH session
ssh ubuntu@<VPS_IP>

# If successful, rejoin the screen
screen -r upgrade
# See upgrade progress
```

**Situation 2: SSH won't connect at all**

1. Oracle Cloud Console → **Instance → Console Connection**
2. **Launch Cloud Shell connection** (or SSH connection)
3. You'll see what's going on
4. If upgrade finished but reboot is hung → **Instance Actions → Reboot**
5. If still won't boot → **restore from boot volume snapshot**

**Situation 3: Using the backup SSH port 1022**

If you accepted the backup SSH during upgrade:

```bash
# Connect via port 1022
ssh -p 1022 ubuntu@<VPS_IP>
```

---

## 📋 Stage 3 — Reboot

After upgrade completes:

```bash
# Exit screen (Ctrl+A, then D)
# Close SSH

sudo reboot
```

**Wait 1-3 minutes**, then reconnect:

```bash
ssh ubuntu@<VPS_IP>
```

---

## 📋 Stage 4 — Verification

```bash
# Version check
cat /etc/os-release | grep "VERSION="
# Expected: VERSION="22.04.x LTS (Jammy Jellyfish)"

# Kernel
uname -r
# Expected: 5.15.x or newer

# Network
ip -4 addr show
ping -c 3 github.com

# Disk
df -h /

# systemd health
systemctl --failed
# Ideal: "0 loaded units listed"

# Listening ports
ss -tlnp | grep LISTEN
```

**All good → Ubuntu 22.04 successfully installed**.

---

## 🛠️ Stage 4.5 — Cleanup & Oracle Cloud Repairs

Post-upgrade sanity checks:

```bash
# 1. Any broken packages?
sudo apt --fix-broken install -y

# 2. Clean old kernels
sudo apt autoremove --purge -y

# 3. Oracle Cloud agent (for VPS monitoring)
sudo systemctl status oracle-cloud-agent 2>/dev/null || true

# 4. SSH listens on correct port (22 only)
sudo ss -tlnp | grep sshd
# If port 1022 is still listening (backup SSH), you can remove it:
sudo cat /etc/ssh/sshd_config | grep -i "port"

# 5. Snap updates
sudo snap refresh

# 6. Iptables restore (if needed)
# Oracle's default INPUT REJECT should still be active
sudo iptables -L INPUT -n | head -20
```

---

## 📋 Stage 5 — Install BabaCoin

Now on 22.04, run the **one-shot installer**:

```bash
curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | bash
```

Or specify the seed number manually:

```bash
curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | SEED_NUM=03 bash
```

The installer:
- Automatically backs up the old `/usr/bin/babacoind` v1
- Installs v2.0.0 Ubuntu 22.04 ARM64 to `/usr/local/bin/babacoind`
- Installs all dependencies (boost 1.74, miniupnpc 17, protobuf 23, etc.)
- Creates config, configures firewall, installs systemd service

---

## 🆘 Troubleshooting

### Issue 1: `E: The repository 'http://ports.ubuntu.com/ubuntu-ports jammy Release' does not have a Release file`

```bash
# Fix sources.list manually
sudo vim /etc/apt/sources.list
# Ensure all lines say "jammy" not "focal"

sudo apt update
```

### Issue 2: `package X has unmet dependencies`

```bash
sudo apt --fix-broken install -y
sudo dpkg --configure -a
sudo apt dist-upgrade -y
```

### Issue 3: VPS won't boot after reboot

1. **Oracle Console → Instance → Console Connection**
2. Connect to serial console
3. Check errors: typically netplan or grub
4. **Most practical fix: Restore boot volume from snapshot**

### Issue 4: "Unable to resolve host"

```bash
# Check hostname settings
hostnamectl

# Check /etc/hosts
cat /etc/hosts
# Should contain: "127.0.1.1 node-X.local node-X"
```

### Issue 5: BabaCoin script: "libminiupnpc.so.17: cannot open shared object file"

The installer handles this automatically, but manually:

```bash
sudo apt install -y libminiupnpc17
# If not available in 22.04:
sudo apt install -y libminiupnpc18
sudo ln -sf /usr/lib/aarch64-linux-gnu/libminiupnpc.so.18 /usr/lib/aarch64-linux-gnu/libminiupnpc.so.17
sudo ldconfig
```

### Issue 6: `do-release-upgrade` says "Checking for a new Ubuntu release" but nothing happens

```bash
# Reinstall meta package
sudo apt install --reinstall update-manager-core -y

# Clear cache
sudo rm -rf /var/lib/ubuntu-release-upgrader-core

# Retry
sudo do-release-upgrade -d

# If still stuck → use Method B (manual)
```

---

## 📊 Estimated Time

| Stage | Duration |
|---|---|
| 0. Snapshot | 5-10 min |
| 1. 20.04 update | 5-15 min |
| 2. Upgrade process | 20-45 min |
| 3. Reboot | 2-3 min |
| 4. Verification | 2 min |
| 5. BabaCoin install | 3-5 min |
| **TOTAL** | **~1 hour** |

**With sync**: + 4-8 hours

---

## 🎯 Plan for 20 VPS Upgrades

Parallelize across multiple VPSs:

```bash
# Open 3 terminals per batch:
# Terminal 1: node-01
# Terminal 2: node-02
# Terminal 3: node-03
# ... (3-5 VPSs at a time is reasonable)
```

**Practical strategy:**
1. **Full test on 1 VPS first** (see if everything works end-to-end)
2. If successful, **batch 5 at a time** in parallel
3. Each batch ~1 hour → 20 VPSs = ~4 hours total

---

## 📝 DNS Update Reminder

After each VPS's upgrade + BabaCoin install, update DNS (Cloudflare, Route53, etc.):

```
seed01.babacoin.network  A  <VPS1_IP>   TTL: 300
seed02.babacoin.network  A  <VPS2_IP>   TTL: 300
...
seed20.babacoin.network  A  <VPS20_IP>  TTL: 300
```

IPs likely didn't change (VPSs weren't recreated), but verify anyway.

---

## 🎉 Done!

Following this guide you can safely upgrade Oracle ARM Ubuntu 20.04 → 22.04. For issues:

- Discord: https://discord.babacoin.network
- Telegram: https://t.me/babacoinbbc
- GitHub Issues: https://github.com/babacoinbbc/babacoin/issues
