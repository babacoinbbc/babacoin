# Oracle ARM Ubuntu 20.04 → 22.04 Upgrade Rehberi

> Oracle Cloud üzerindeki ARM (VM.Standard.A1.Flex) Ubuntu 20.04 LTS (Focal) makinelerini 22.04 LTS (Jammy)'e yükseltmek için tam rehber.

## ⚠️ Önce Okumalı

**In-place upgrade riskleri:**
- SSH erişimini kaybedebilirsin (cloud-init/netplan çakışması)
- Bootloader sorunu → VPS açılmayabilir
- Oracle Cloud'un özel paketleri çakışabilir

**Bu yüzden snapshot olmadan ASLA başlama.**

---

## 📋 Aşama 0 — Hazırlık (ZORUNLU)

### 0.1 — Oracle Cloud Console'dan Snapshot Al

1. **Oracle Cloud Console** → menu
2. **Compute → Instances → [VPS'inin adı]**
3. Alt kısımda **Boot volume** → volume linkine tıkla
4. **Block Volume Backups → Create Backup**
5. Type: **Incremental** (hızlı)
6. Name: `bbc-node-XX-pre-upgrade-22.04-YYYYMMDD`
7. **Create**
8. ~5-10 dk bekle — "Available" durumuna geçsin

**Not**: Free tier'da ilk 5 incremental backup **ücretsiz** (10 GB'a kadar).

### 0.2 — Yerel Hazırlık

```bash
# VPS'e SSH ile bağlan
ssh ubuntu@<VPS_IP>

# Çalışan babacoind'i kapat (varsa)
sudo systemctl stop babacoind 2>/dev/null || true
sudo killall babacoind 2>/dev/null || true
sleep 3

# Önemli dosyaları ~/backup/'a yedekle
mkdir -p ~/backup
cp -r ~/.babacoin/babacoin.conf ~/backup/ 2>/dev/null || true
cp -r ~/.babacoin/wallet.dat ~/backup/ 2>/dev/null || true
sudo cp /etc/ssh/sshd_config ~/backup/sshd_config.focal
sudo cp -r /etc/netplan ~/backup/netplan.focal
sudo iptables-save > ~/backup/iptables.focal.rules

# Durum özeti
echo "=== Upgrade öncesi durum ===" | tee ~/upgrade-log.txt
date >> ~/upgrade-log.txt
cat /etc/os-release >> ~/upgrade-log.txt
uname -a >> ~/upgrade-log.txt
df -h / >> ~/upgrade-log.txt
free -h >> ~/upgrade-log.txt

echo "✅ Hazırlık tamamlandı"
```

---

## 📋 Aşama 1 — Mevcut 20.04 Sistemini Güncelle

```bash
# Full update (uzun sürebilir ~5-15 dk)
sudo apt update
sudo apt upgrade -y
sudo apt dist-upgrade -y
sudo apt autoremove --purge -y
sudo apt autoclean

# update-manager ve screen kur
sudo apt install -y update-manager-core screen

# Upgrade policy → LTS sürümlerine izin ver
sudo sed -i 's/^Prompt=.*/Prompt=lts/' /etc/update-manager/release-upgrades
grep "^Prompt" /etc/update-manager/release-upgrades
# Beklenen çıktı: Prompt=lts
```

---

## 📋 Aşama 2 — Upgrade Metodu Seç

**İki yaklaşım var:**

### Yöntem A: `do-release-upgrade` (Önerilen, güvenli)

```bash
# Screen içinde başlat (SSH kopsa bile devam eder)
screen -S upgrade

# Upgrade komutunu çalıştır
sudo do-release-upgrade
```

**Olası çıktılar:**

#### Durum A1: "No new release found"

20.04 EOL olduğu için prompt gelmeyebilir. Bu durumda:

```bash
# Manuel yöntem B'ye geç (aşağıda)
```

#### Durum A2: "Do you want to start the upgrade? [y/N]"

→ **`y`** + Enter. Süreç başlar.

### Yöntem B: Manuel Sources.list Değişikliği

`do-release-upgrade` çalışmazsa:

```bash
# Screen içinde başlat
screen -S upgrade

# Backup
sudo cp /etc/apt/sources.list /etc/apt/sources.list.focal
[ -d /etc/apt/sources.list.d ] && sudo cp -r /etc/apt/sources.list.d /etc/apt/sources.list.d.focal

# focal → jammy değişimi
sudo sed -i 's/focal/jammy/g' /etc/apt/sources.list
if [ -d /etc/apt/sources.list.d ]; then
    for f in /etc/apt/sources.list.d/*.list; do
        [ -f "$f" ] && sudo sed -i 's/focal/jammy/g' "$f"
    done
fi

# Ubuntu ports için (ARM repo) kontrolü
grep -E "ports.ubuntu.com|archive.ubuntu.com" /etc/apt/sources.list

# Eğer -updates-security listesi de varsa:
sudo sed -i 's/focal-security/jammy-security/g' /etc/apt/sources.list
sudo sed -i 's/focal-updates/jammy-updates/g' /etc/apt/sources.list
sudo sed -i 's/focal-backports/jammy-backports/g' /etc/apt/sources.list

# Apt cache temizle
sudo rm -rf /var/lib/apt/lists/*
sudo apt clean

# Yeni repo'dan güncelle
sudo apt update

# Dist-upgrade — config dosyalarını koru
sudo DEBIAN_FRONTEND=noninteractive apt-get dist-upgrade -y \
    -o Dpkg::Options::="--force-confdef" \
    -o Dpkg::Options::="--force-confold"

# Temizlik
sudo apt autoremove --purge -y
sudo apt autoclean
```

---

## 🚨 Upgrade Sırasında Çıkan Soruları Yanıtlama

### `do-release-upgrade` ile (Yöntem A) karşına çıkacak sorular:

#### Soru 1: SSH Backup Port
```
To make recovery in case of failure easier, an additional sshd will 
be started on port '1022'. If anything goes wrong with the running ssh 
you can still connect to the additional one. If you run a firewall, 
you may need to temporarily open this port.

Continue? [yN]
```
→ **`y`** (güvenlik ağı)

#### Soru 2: Config File Uyarıları
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

Dosya bazında karar:

| Dosya | Cevap | Sebep |
|---|---|---|
| `/etc/ssh/sshd_config` | **N (keep local)** | SSH erişimini kaybetme |
| `/etc/netplan/*` | **N (keep local)** | Network ayarları |
| `/etc/cloud/*` | **N (keep local)** | Oracle cloud-init |
| `/etc/sudoers` | **N (keep local)** | Sudo erişimi |
| `/etc/default/grub` | **Y (install new)** | Yeni kernel için |
| `/etc/apt/sources.list` | **Y (install new)** | Yeni repo URL'leri |
| Diğer `.conf` | **N (keep local)** | Güvenli default |

#### Soru 3: Service Restart
```
Daemons using outdated libraries
Which services should be restarted?
```
→ **Space** ile hepsini seç, **Tab + OK**

#### Soru 4: Obsolete Packages
```
Removing obsolete packages...
```
→ **`y`**

#### Soru 5: Reboot
```
System upgrade is complete.
Restart required.
To finish the upgrade, a restart is required.
Continue? [yN]
```
→ **`y`**

---

## 🔴 SSH Koparsa Ne Yapmalı

**Durum 1: Upgrade sırasında SSH koptu**

Normal — screen içinde çalıştırdığın için upgrade **devam ediyor**. 2-3 dakika bekle, tekrar bağlanmayı dene.

```bash
# Yeni SSH session ile
ssh ubuntu@<VPS_IP>

# Eğer bağlanırsan
screen -r upgrade
# Upgrade progress'ini gör
```

**Durum 2: SSH hiç bağlanmıyor**

1. Oracle Cloud Console → **Instance → Console Connection**
2. **Launch Cloud Shell connection** (veya SSH connection)
3. Terminalde görürsün ne olduğunu
4. Eğer upgrade bitmiş ama reboot takılmışsa → **Instance Actions → Reboot**
5. Hâlâ açılmıyorsa → snapshot'tan restore

**Durum 3: Backup SSH port 1022'yi kullanma**

Eğer upgrade sırasında `y` deyip backup SSH başlattıysan:

```bash
# Port 1022 ile bağlan
ssh -p 1022 ubuntu@<VPS_IP>
```

---

## 📋 Aşama 3 — Reboot

Upgrade bittikten sonra:

```bash
# Screen'den çık (Ctrl+A, sonra D)
# SSH kapat

sudo reboot
```

**1-3 dakika bekle**, sonra yeniden bağlan:

```bash
ssh ubuntu@<VPS_IP>
```

---

## 📋 Aşama 4 — Doğrulama

```bash
# Versiyon kontrolü
cat /etc/os-release | grep "VERSION="
# Beklenen: VERSION="22.04.x LTS (Jammy Jellyfish)"

# Kernel
uname -r
# Beklenen: 5.15.x veya üstü

# Network
ip -4 addr show
ping -c 3 github.com

# Disk
df -h /

# systemd sağlık
systemctl --failed
# "0 loaded units listed" ideal

# Ağ bağlantıları
ss -tlnp | grep LISTEN
```

**Tüm bunlar OK ise → 22.04 başarılı**.

---

## 🛠️ Aşama 4.5 — Cleanup & Oracle Cloud Repairs

Upgrade sonrası bazı şeyleri kontrol et:

```bash
# 1. Broken paketler var mı
sudo apt --fix-broken install -y

# 2. Eski kernel'leri temizle
sudo apt autoremove --purge -y

# 3. Oracle Cloud agent güncel mi (VPS monitoring için)
sudo systemctl status oracle-cloud-agent 2>/dev/null || true

# 4. SSH servisi doğru dinliyor mu (sadece 22)
sudo ss -tlnp | grep sshd
# Eğer hâlâ 1022 de varsa (backup SSH), silinebilir:
# Config'e bak:
sudo cat /etc/ssh/sshd_config | grep -i "port"

# 5. snap güncel
sudo snap refresh

# 6. iptables restore (ihtiyaç olursa)
# Oracle default INPUT REJECT politikası hâlâ çalışıyor olmalı
sudo iptables -L INPUT -n | head -20
```

---

## 📋 Aşama 5 — BabaCoin'i Yeniden Kur

Artık 22.04'te olduğumuz için, **tek-tuş script'i** çalıştırabiliriz:

```bash
curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | bash
```

Ya da eğer seed number belirlemek istersen:

```bash
curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/setup-seed-oracle-22.04-arm.sh | SEED_NUM=03 bash
```

Script:
- Eski `/usr/bin/babacoind` v1'i otomatik yedekler
- Yeni `/usr/local/bin/babacoind` v2.0.0 Ubuntu 22.04 ARM64 kurar
- Tüm bağımlılıkları kurar (boost 1.74, miniupnpc 17, protobuf 23, vb.)
- Config oluşturur, firewall'u açar, systemd servisi kurar

---

## 🆘 Sorun Giderme

### Sorun 1: `E: The repository 'http://ports.ubuntu.com/ubuntu-ports jammy Release' does not have a Release file`

```bash
# HTTPS'e geçir veya mirror'ı düzelt
sudo apt edit-sources
# focal kalmışsa jammy yap

# Ya da elle
sudo vim /etc/apt/sources.list
# Tüm satırlar "jammy" olmalı

sudo apt update
```

### Sorun 2: `package X has unmet dependencies`

```bash
sudo apt --fix-broken install -y
sudo dpkg --configure -a
sudo apt dist-upgrade -y
```

### Sorun 3: Reboot sonrası VPS açılmıyor

1. **Oracle Console → Instance → Console Connection**
2. Serial console'a bağlan
3. Error görürsen: genellikle netplan veya grub
4. **En pratik çözüm: Boot volume'ü snapshot'tan restore**

### Sorun 4: "Unable to resolve host"

```bash
# Hostname ayarlarını kontrol
hostnamectl

# /etc/hosts kontrol
cat /etc/hosts
# "127.0.1.1 node-X.local node-X" satırı olmalı
```

### Sorun 5: BabaCoin script "libminiupnpc.so.17: cannot open shared object file"

Script bunu otomatik hallediyor ama elle:

```bash
sudo apt install -y libminiupnpc17
# Ubuntu 22.04'te yoksa:
sudo apt install -y libminiupnpc18
sudo ln -sf /usr/lib/aarch64-linux-gnu/libminiupnpc.so.18 /usr/lib/aarch64-linux-gnu/libminiupnpc.so.17
sudo ldconfig
```

### Sorun 6: "do-release-upgrade" diyor "Checking for a new Ubuntu release" ama hiçbir şey olmuyor

```bash
# Meta package yeniden kur
sudo apt install --reinstall update-manager-core -y

# Cache temizle
sudo rm -rf /var/lib/ubuntu-release-upgrader-core

# Yeniden dene
sudo do-release-upgrade -d

# Hâlâ olmuyorsa Yöntem B (manuel)
```

---

## 📊 Tahmini Süre

| Aşama | Süre |
|---|---|
| 0. Snapshot | 5-10 dk |
| 1. 20.04 güncelleme | 5-15 dk |
| 2. Upgrade süreci | 20-45 dk |
| 3. Reboot | 2-3 dk |
| 4. Doğrulama | 2 dk |
| 5. BabaCoin kurulum | 3-5 dk |
| **TOPLAM** | **~1 saat** |

**Sync dahil**: + 4-8 saat

---

## 🎯 20 VPS İçin Toplu Plan

20 VPS'i paralel işlemek için:

```bash
# Her VPS için 3 terminal aç
# Terminal 1: node-01
# Terminal 2: node-02
# Terminal 3: node-03
# ... (aynı anda 3-5 VPS yapabilirsin)

# Her birinde aynı adımlar
```

**Pratik strateji:**
1. **İlk önce 1 VPS'te tam test** (node-3 şu anda test gibi oldu zaten)
2. Test başarılıysa, **paralel 5'er grup** halinde diğerlerini yap
3. Her grup ~1 saat → 20 VPS = ~4 saat

---

## 📝 DNS Güncelleme Hatırlatıcısı

Her VPS upgrade + BabaCoin kurulumu bitince Cloudflare/DNS'ini güncelle:

```
seed01.babacoin.network  A  <VPS1_IP>   TTL: 300
seed02.babacoin.network  A  <VPS2_IP>   TTL: 300
...
seed20.babacoin.network  A  <VPS20_IP>  TTL: 300
```

IP'ler değişmedi ama seed node'lar artık aktif olduğu için **bu kayıtlar hâlâ aynı IP'yi gösterebilir**. Kontrol et.

---

## 🎉 Bitti!

Bu rehberi takip ederek Oracle ARM Ubuntu 20.04 → 22.04 geçişini güvenli şekilde yapabilirsin. Sorun olursa:
- Discord: https://discord.babacoin.network
- Telegram: https://t.me/babacoinbbc
- GitHub Issues: https://github.com/babacoinbbc/babacoin/issues
