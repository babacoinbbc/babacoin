# BabaCoin Contrib Scripts

Operator scripts and guides for running BabaCoin nodes in production.

## Oracle Cloud ARM Scripts

These scripts target the default **Oracle Cloud VM.Standard.A1.Flex** ARM instance with the stock Ubuntu image.

### `setup-seed-oracle-22.04-arm.sh`

One-shot seed node installer for **Ubuntu 22.04 LTS aarch64**. Installs BabaCoin v2.0.0, configures the daemon, opens firewall ports, and registers a systemd service.

```bash
curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | bash

# With specific seed number
curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | SEED_NUM=03 bash

# Non-interactive (for automation)
curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | SEED_NUM=03 AUTO_YES=1 bash
```

**What it does:**
1. Checks for existing v2.0.0 (skip binary install if already installed)
2. Stops any running daemon gracefully
3. Installs all runtime dependencies (boost 1.74, miniupnpc 17, etc.)
4. Downloads and installs v2.0.0 ARM64 binary to `/usr/local/bin/`
5. Auto-detects external IP
6. Auto-assigns seed number from hostname (e.g. `node-3` → `03`)
7. Generates `babacoin.conf` with strong random RPC password
8. Configures UFW and opens Oracle's default iptables DROP policy
9. Creates systemd service with auto-restart
10. Starts the daemon and verifies RPC works

### `upgrade-oracle-arm-20-to-22.sh`

Automated in-place upgrade from **Ubuntu 20.04 (Focal)** to **22.04 (Jammy)** on Oracle ARM instances. Uses manual `sources.list` migration which is more reliable than `do-release-upgrade` on EOL 20.04.

```bash
# ALWAYS run inside screen/tmux!
screen -S upgrade

# Take an Oracle Cloud boot volume snapshot FIRST
curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/upgrade-oracle-arm-20-to-22.sh | bash
```

Env vars:
- `AUTO_YES=1` - Skip confirmation prompts (only if you know what you're doing)
- `AUTO_REBOOT=1` - Automatically reboot when upgrade finishes

After upgrade completes and you've rebooted, run `setup-seed-oracle-22.04-arm.sh` to install BabaCoin on the fresh 22.04 system.

### `UPGRADE-20.04-TO-22.04.md`

Manual step-by-step guide for the 20.04 → 22.04 upgrade, for operators who prefer not to use the automated script or need to troubleshoot issues.

## Assumptions

- Default user is `ubuntu` with passwordless sudo (Oracle Cloud default)
- SSH access via key (no password auth)
- Port 6678/tcp allowed in Oracle Cloud Security List (Ingress Rule)
- Stable network connection during installation/upgrade

## Non-Oracle Environments

These scripts assume Oracle Cloud defaults. For other environments:

- **DigitalOcean, Hetzner, etc.**: Scripts should work if the default user has passwordless sudo. Check `/etc/sudoers.d/` for your distribution's default.
- **Bare metal / home server**: Add `ubuntu ALL=(ALL) NOPASSWD:ALL` to sudoers (or adapt the scripts to use interactive sudo).
- **AWS EC2 Ubuntu**: Works as-is (default `ubuntu` user has NOPASSWD sudo).

## License

Same as BabaCoin Core - MIT License.
