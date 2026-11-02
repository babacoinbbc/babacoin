#!/usr/bin/env python3
"""Install/upgrade a mainnet daemon using the latest stable exact-ABI release.

Python 3.6+ (Ubuntu 18.04). No third-party Python modules. Existing wallets,
configuration, chain data, systemd units and network rules are never replaced.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile

REPO = 'babacoinbbc/babacoin'
STABLE_TAG = r'v[2-9][0-9]*\.[0-9]+\.[0-9]+(?:\.[0-9]+)?'
GENESIS = '84de4877419c696744198422de2628087ae2270c73fb370ab4cfe2fe01061854'
PROGRAMS = ('babacoind', 'babacoin-cli', 'babacoin-tx')
PROFILES = {
    ('ubuntu', '18.04'): ('1.65.1', '1.65.1', '6', '1.1', '10'),
    ('ubuntu', '20.04'): ('1.71.0', '1.71.0', '7', '1.1', '17'),
    ('ubuntu', '22.04'): ('1.74.0', '1.74.0', '7', '3', '17'),
    ('ubuntu', '24.04'): ('1.83.0', '1.83.0t64', '7t64', '3t64', '17'),
    ('ubuntu', '26.04'): ('1.90.0', '1.90.0', '7t64', '3t64', '21'),
    ('debian', '12'): ('1.74.0', '1.74.0', '7', '3', '17'),
    ('debian', '13'): ('1.83.0', '1.83.0t64', '7t64', '3t64', '18'),
}


def fail(message):
    raise RuntimeError(message)


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''): h.update(chunk)
    return h.hexdigest()


def profile(os_id, version, machine):
    key = (os_id, version)
    if key not in PROFILES: fail('No validated package for {} {}. Do not mix distribution ABIs.'.format(*key))
    arch = {'x86_64': 'x86_64', 'aarch64': 'aarch64'}.get(machine)
    if not arch: fail('Only x86_64 and aarch64 are supported.')
    boost, chrono, event, ssl, upnp = PROFILES[key]
    packages = ['ca-certificates', 'curl', 'python3', 'libstdc++6', 'libzmq5',
                'libboost-filesystem'+boost, 'libboost-program-options'+boost,
                'libboost-thread'+boost, 'libboost-chrono'+chrono,
                'libevent-2.1-'+event, 'libevent-pthreads-2.1-'+event,
                'libssl'+ssl, 'libminiupnpc'+upnp]
    if key == ('ubuntu', '18.04'): packages.append('libboost-system1.65.1')
    return arch, packages


def read_os(path='/etc/os-release'):
    values = {}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        key, sep, value = line.partition('=')
        if sep and key in ('ID', 'VERSION_ID'):
            parts = shlex.split(value)
            if len(parts) != 1: fail('Invalid OS identification.')
            values[key] = parts[0]
    return values.get('ID', ''), values.get('VERSION_ID', '')


def check_storage(paths, minimum_bytes=512*1024**2, minimum_inodes=4096):
    """Read-only preflight, before apt, backups or daemon shutdown."""
    checked = set()
    for label, requested in paths:
        path = Path(requested)
        while not path.exists():
            if path == path.parent: fail('Cannot inspect storage for '+label)
            path = path.parent
        device = path.stat().st_dev
        if device in checked: continue
        checked.add(device)
        free = shutil.disk_usage(str(path)).free
        if free < minimum_bytes:
            fail('Insufficient free space for {} ({}): {} MiB available; at least {} MiB required before APT. '
                 'Check df -h and df -i; apt-get clean can free package-download cache. '
                 'Do not delete wallets, chain data or backups. The daemon has not been stopped.'
                 .format(label, path, free//(1024**2), minimum_bytes//(1024**2)))
        if hasattr(os, 'statvfs'):
            info = os.statvfs(str(path))
            if info.f_files > 0 and info.f_favail < minimum_inodes:
                fail('Insufficient free inodes for {} ({}): {} available. Check df -i and storage quotas; '
                     'no daemon was stopped and no wallet files were deleted.'.format(label, path, info.f_favail))


def install_dependencies(packages):
    try:
        subprocess.check_call(['apt-get', 'update'])
        subprocess.check_call(['apt-get', 'install', '-y', '--no-install-recommends']+packages)
    except subprocess.CalledProcessError:
        fail('APT preparation failed. Resolve the disk/inode/quota or repository error printed above, then rerun setup. '
             'No daemon has been stopped or replaced. Wallet/chain files should not be deleted to retry installation.')


class HTTPSRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not newurl.startswith('https://'): fail('Non-HTTPS download redirect rejected.')
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def download(url, target, limit, sha=None):
    if not url.startswith('https://'): fail('HTTPS is required.')
    opener = urllib.request.build_opener(HTTPSRedirect())
    request = urllib.request.Request(url, headers={'User-Agent': 'Babacoin-node-setup'})
    count = 0
    last_report = time.monotonic()
    with opener.open(request, timeout=90) as response, Path(target).open('xb') as output:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk: break
            count += len(chunk)
            if count > limit: fail('Download exceeds its declared size limit.')
            output.write(chunk)
            if time.monotonic()-last_report >= 3:
                print('Downloading {}: {} MiB received'.format(Path(target).name, count//(1024*1024)), flush=True)
                last_report = time.monotonic()
    if sha and digest(target) != sha: fail('SHA-256 mismatch: '+Path(target).name)
    return Path(target)


def get_json(url, directory, name, limit=8*1024*1024):
    return json.loads(download(url, directory/name, limit).read_text(encoding='utf-8'))


def asset_url(tag, name):
    if not re.fullmatch(STABLE_TAG, tag): fail('Invalid stable release tag.')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', name) or '..' in name: fail('Invalid release asset name.')
    return 'https://github.com/'+REPO+'/releases/download/'+tag+'/'+name


def fetch_asset(release, name, work, sha=None, limit=None):
    if sha is not None and not re.fullmatch(r'[a-f0-9]{64}', sha): fail('Invalid release SHA-256.')
    return download(asset_url(release['tag_name'], name), work/name, limit or 2*1024**3, sha)


def option(args, name):
    for index in range(len(args)-1, -1, -1):
        if args[index].startswith(name+'='): return args[index].split('=', 1)[1]
        if args[index] == name and index+1 < len(args): return args[index+1]
    return None


def identity(pid, proc=Path('/proc')):
    # starttime prevents signalling an unrelated process after PID reuse.
    try:
        raw = (proc/str(pid)/'stat').read_text()
        fields = raw[raw.rindex(')')+2:].split()
        if fields[0] == 'Z': return None
        return fields[19]
    except (OSError, ValueError, IndexError): return None


def discover(proc=Path('/proc')):
    nodes = []
    for entry in proc.iterdir():
        if not entry.name.isdigit(): continue
        try:
            executable = Path(re.sub(r' \(deleted\)$', '', os.readlink(str(entry/'exe'))))
            if executable.name != 'babacoind' or not executable.is_absolute(): continue
            args = [os.fsdecode(a) for a in (entry/'cmdline').read_bytes().split(b'\0') if a]
            uid = entry.stat().st_uid
            cwd = Path(os.readlink(str(entry/'cwd')))
            # The held mainnet lock identifies the actual datadir even when a
            # configuration file changed it after command-line parsing.
            locks = []
            for fd in (entry/'fd').iterdir():
                try:
                    value = os.readlink(str(fd))
                    if value.endswith('/.lock'):
                        visible, held = Path(value).stat(), fd.stat()
                        if (visible.st_dev, visible.st_ino) != (held.st_dev, held.st_ino):
                            fail('Daemon data is in a different mount namespace. Manual reviewed upgrade required.')
                        locks.append(Path(value).parent.resolve())
                except OSError: pass
            locks = list(set(locks))
            if len(locks) != 1: fail('Cannot uniquely identify the running daemon data lock. Close it normally first.')
            unit = None
            for line in (entry/'cgroup').read_text().splitlines():
                candidate = line.rsplit('/', 1)[-1]
                if candidate.endswith('.service') and 'system.slice/' in line:
                    if not re.fullmatch(r'[A-Za-z0-9_.@:-]+\.service', candidate): fail('Unsafe service name.')
                    unit = candidate
            nodes.append(dict(pid=int(entry.name), token=identity(int(entry.name), proc),
                              binary=executable, args=args[1:], data=locks[0], uid=uid, cwd=cwd, unit=unit))
        except (FileNotFoundError, ProcessLookupError): continue
        except PermissionError: fail('Cannot inspect an existing daemon. Run setup as root.')
    return nodes


def select_node(nodes, explicit):
    if explicit:
        selected = [n for n in nodes if n['data'] == explicit]
        if len(selected) > 1: fail('Multiple daemons use the selected data directory.')
        if nodes and not selected: fail('A different daemon is running. Specify its actual data directory or close it normally first.')
        return selected[0] if selected else None
    if len(nodes) > 1: fail('Multiple daemons found. Specify --data-dir to select exactly one.')
    return nodes[0] if nodes else None


def require_mainnet(data, args):
    for name in ('-testnet', '-regtest', '-devnet'):
        if option(args, name) not in (None, '0', 'false') or name in args:
            fail('This installer only manages mainnet nodes.')
    conf = Path(option(args, '-conf') or str(data/'babacoin.conf'))
    if not conf.is_absolute(): conf = data/conf
    if conf.exists():
        section = ''
        for line in conf.read_text(encoding='utf-8', errors='strict').splitlines():
            line = line.split('#', 1)[0].strip()
            if line.startswith('['): section = line
            key, sep, value = line.partition('=')
            if not section and sep and key.strip() in ('testnet', 'regtest', 'devnet') and value.strip() not in ('0', 'false'):
                fail('Non-mainnet configuration detected; no update performed.')
            if section in ('', '[main]') and sep and key.strip() in ('walletdir', 'blocksdir', 'wallet'):
                fail('Custom wallet/blocks locations require a manual reviewed upgrade; no files changed.')
            if section in ('', '[main]') and sep and key.strip() == 'datadir' and Path(value.strip()).expanduser().resolve() != data:
                fail('Configuration selects another datadir. Specify that actual data directory explicitly.')
            if sep and key.strip() == 'includeconf': fail('Included configuration requires a manual reviewed upgrade.')
    for name in ('-walletdir', '-blocksdir', '-wallet'):
        if option(args, name) is not None: fail('Custom wallet/blocks paths require a manual reviewed upgrade.')
    return conf


def stop_node(node, timeout=300):
    if identity(node['pid']) != node['token']: fail('Daemon identity changed; refusing to signal it.')
    if node['unit']:
        pid = subprocess.check_output(['systemctl', 'show', node['unit'], '-p', 'MainPID', '--value'], universal_newlines=True, encoding='utf-8').strip()
        if pid != str(node['pid']): fail('Systemd service MainPID does not match this daemon.')
        subprocess.check_call(['systemctl', 'stop', '--no-block', node['unit']])
    else:
        os.kill(node['pid'], signal.SIGTERM)  # Core handles SIGTERM as normal shutdown.
    until = time.monotonic()+timeout
    while identity(node['pid']) == node['token']:
        if time.monotonic() > until: fail('Normal shutdown timed out. No forced kill or executable replacement performed.')
        time.sleep(.5)
    if node['unit']:
        state = subprocess.check_output(['systemctl', 'show', node['unit'], '-p', 'ActiveState', '--value'], universal_newlines=True, encoding='utf-8').strip()
        if state not in ('inactive', 'failed'): fail('Service has not finished stopping; nothing replaced.')


def backup_wallet(data, destination):
    files = []
    if data.exists():
        for p in data.iterdir():
            if p.name == 'wallets':
                if p.is_symlink(): fail('Wallet directory symlinks require a manual reviewed backup.')
                files.extend(q for q in p.rglob('*') if q.is_file() or q.is_symlink())
            elif p.name.endswith('.conf') or (p.name.endswith('.dat') and not p.name.startswith('bootstrap')):
                files.append(p)
    records = {}
    for p in files:
        if p.is_symlink() or not p.is_file(): fail('Wallet backup refuses symlinks or special files.')
        target = destination/p.relative_to(data)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(str(p), str(target)); target.chmod(0o600)
        value = digest(p)
        if value != digest(target): fail('Wallet backup verification failed.')
        records[p.relative_to(data).as_posix()] = value
    (destination/'SHA256.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    return records


def ensure_unlocked(data):
    import fcntl
    path = data/'.lock'
    if path.is_symlink(): fail('Data lock path is a symlink.')
    if path.exists():
        with path.open('r+b') as stream:
            try: fcntl.lockf(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError: fail('The data directory is still locked by a wallet/node. Close it normally; no forced kill.')
            fcntl.lockf(stream, fcntl.LOCK_UN)


def make_owned_directory(path, uid, gid):
    missing = []
    cursor = path
    while not cursor.exists(): missing.append(cursor); cursor = cursor.parent
    for item in reversed(missing):
        item.mkdir(mode=0o700); os.chown(str(item), uid, gid)


def validate_data_owner(data, uid, running):
    if data.exists():
        if not data.is_dir(): fail('The data path must be a directory, not a wallet file.')
        if not running and data.stat().st_uid != uid:
            fail('Existing data belongs to another account. Run setup as its original owner; ownership will not be changed.')


def atomic_copy(source, target, uid=None, gid=None):
    # Temporary files are created on the destination filesystem; existing
    # program files alone are replaced atomically. Never a wallet or directory.
    if target.is_symlink() or (target.exists() and not target.is_file()): fail('Unsafe executable destination: '+str(target))
    fd, name = tempfile.mkstemp(prefix='.bbc-program-', dir=str(target.parent))
    temp = Path(name)
    try:
        with os.fdopen(fd, 'wb') as output, Path(source).open('rb') as incoming:
            shutil.copyfileobj(incoming, output); output.flush(); os.fsync(output.fileno())
        temp.chmod(0o755)
        if uid is not None: os.chown(str(temp), uid, gid)
        os.replace(str(temp), str(target))
    finally:
        if temp.exists(): temp.unlink()


def unpack_bootstrap(archive, record, target):
    if record['first_height'] != 0 or record['network_magic'] != '035dd72f' or record['genesis_hash'] != GENESIS:
        fail('Bootstrap is not a full Babacoin mainnet archive.')
    with zipfile.ZipFile(str(archive)) as bundle:
        entries = bundle.infolist()
        if len({e.filename for e in entries}) != len(entries): fail('Duplicate bootstrap ZIP members.')
        if set(e.filename for e in entries) - {'bootstrap.dat', 'CONTENTS.json', 'BOOTSTRAP-README.md'}:
            fail('Unexpected bootstrap ZIP contents.')
        member = bundle.getinfo('bootstrap.dat')
        if member.file_size != record['payload_bytes']: fail('Bootstrap payload size mismatch.')
        count = 0
        with bundle.open(member) as incoming, target.open('xb') as output:
            while True:
                chunk = incoming.read(1024*1024)
                if not chunk: break
                if count == 0 and chunk[:4] != bytes.fromhex('035dd72f'): fail('Wrong bootstrap network framing.')
                count += len(chunk)
                if count > record['payload_bytes']: fail('Oversized bootstrap payload.')
                output.write(chunk)
    if digest(target) != record['payload_sha256']: fail('Bootstrap payload SHA-256 mismatch.')


def write_launchers(root, binary_dir, data, args, user, unit):
    import pwd
    owner = pwd.getpwnam(user)
    filtered = [a for a in args if not a.startswith(('-daemon=', '-datadir=')) and a not in ('-daemon', '-datadir')]
    if '-datadir' in args: fail('Use the standard -datadir=PATH syntax for upgrades.')
    daemon_args = ['-datadir='+str(data), '-daemon=1', '-server=1']+filtered
    rpc_args = ['-datadir='+str(data), '-rpcconnect=127.0.0.1']
    for name in ('-conf', '-rpcport', '-rpcuser', '-rpcpassword'):
        value = option(args, name)
        if value is not None: rpc_args.append(name+'='+value)
    quote = lambda values: ' '.join(shlex.quote(str(v)) for v in values)
    owner_guard = ''
    if owner.pw_uid:
        owner_guard = 'if [[ $EUID -eq 0 ]]; then exec runuser -u '+shlex.quote(user)+' -- "$0" "$@"; fi\n'
    for name, command in {
        'start-babacoin.sh': (['systemctl', 'start', unit] if unit else [binary_dir/'babacoind']+daemon_args),
        'stop-babacoin.sh': (['systemctl', 'stop', unit] if unit else [binary_dir/'babacoin-cli']+rpc_args+['stop']),
        'babacoin-cli.sh': [binary_dir/'babacoin-cli']+rpc_args,
    }.items():
        path = root/name
        if path.is_symlink(): fail('Unsafe launcher destination.')
        guard = 'if [[ $EUID -ne 0 ]]; then exec sudo -- "$0" "$@"; fi\n' if unit and name != 'babacoin-cli.sh' else owner_guard
        path.write_text('#!/usr/bin/env bash\n# Managed by Babacoin node setup.\nset -euo pipefail\n'+guard+'exec '+quote(command)+' "$@"\n', encoding='utf-8', errors='surrogateescape')
        path.chmod(0o700); os.chown(str(path), owner.pw_uid, owner.pw_gid)
    return daemon_args


def start_node(node, binary_dir, data, args, user):
    import pwd
    account = pwd.getpwnam(user)
    if node and node['unit']:
        subprocess.check_call(['systemctl', 'start', '--no-block', node['unit']]); return
    env = os.environ.copy(); env['HOME'] = account.pw_dir
    def owner():
        os.initgroups(user, account.pw_gid); os.setgid(account.pw_gid); os.setuid(account.pw_uid)
    try:
        subprocess.check_call([str(binary_dir/'babacoind')]+args, cwd=str(node['cwd'] if node else account.pw_dir),
                              env=env, preexec_fn=owner)
    except subprocess.CalledProcessError:
        # Command arguments can contain RPC credentials; never echo them.
        fail('Daemon returned a startup error. Inspect its debug.log; command-line credentials were not printed.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path)
    parser.add_argument('--install-root', type=Path)
    parser.add_argument('--bootstrap', choices=('auto', 'never', 'always'), default='auto')
    parser.add_argument('--no-start', action='store_true')
    parser.add_argument('--plan', action='store_true')
    args = parser.parse_args()
    os_id, version = read_os(); arch, packages = profile(os_id, version, os.uname().machine)
    if args.plan:
        print(json.dumps(dict(os=os_id, version=version, architecture=arch, runtime_packages=packages), indent=2)); return
    if os.geteuid() != 0: fail('Use sudo or the install-node.sh entry point for dependency installation.')
    import fcntl
    import pwd
    os.umask(0o077)
    explicit = args.data_dir.expanduser().resolve() if args.data_dir else None
    node = select_node(discover(), explicit)
    account = pwd.getpwuid(node['uid']) if node else pwd.getpwnam(os.environ.get('SUDO_USER') or 'root')
    root = (args.install_root or Path(account.pw_dir)/'.local/opt/babacoin').expanduser().resolve()
    if root.exists() and root.stat().st_uid != account.pw_uid: fail('Installation directory belongs to another account. Choose a separate --install-root.')
    stored = {}
    state_path = root/'INSTALLATION.json'
    if state_path.exists():
        if state_path.is_symlink() or state_path.stat().st_uid != account.pw_uid: fail('Unexpected installation state ownership.')
        stored = json.loads(state_path.read_text(encoding='utf-8'))
        if stored.get('user') != account.pw_name: fail('Installation belongs to another account.')
    data = node['data'] if node else (explicit or Path(stored.get('data_dir', str(Path(account.pw_dir)/'.babacoin'))))
    data = data.resolve()
    validate_data_owner(data, account.pw_uid, bool(node))
    if root == data or root in data.parents or data in root.parents: fail('Program and wallet directories must be separate.')
    check_storage([('APT package lists', Path('/var/lib/apt/lists')), ('APT package cache', Path('/var/cache/apt')),
                   ('installer staging and backups', root), ('node data', data)])
    make_owned_directory(root, account.pw_uid, account.pw_gid)
    with (root/'setup.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        install(args, os_id, version, arch, packages, root, data, node, account, stored)


def install(args, os_id, version, arch, packages, root, data, node, account, stored):
    original_args = node['args'] if node else stored.get('args', [])
    context = node or (dict(unit=stored['unit'], cwd=Path(stored.get('cwd', account.pw_dir))) if stored.get('unit') else None)
    require_mainnet(data, original_args)
    if node and ('-datadir' in original_args or any('\n' in a or '\r' in a for a in original_args)):
        fail('Unsupported daemon argument syntax; no update performed.')
    binary_dir = node['binary'].parent if node else Path(stored.get('binary_dir', str(root/'bin')))
    if not binary_dir.is_absolute() or binary_dir.is_symlink(): fail('Unsafe program directory.')
    if binary_dir == data or binary_dir in data.parents or data in binary_dir.parents: fail('Program and wallet directories overlap.')
    if node and (not node['binary'].exists() or node['binary'].is_symlink()): fail('Running executable path is unavailable; close and upgrade manually.')
    print('Platform: {} {} {}; data: {}'.format(os_id, version, arch, data), flush=True)
    with tempfile.TemporaryDirectory(prefix='.download-', dir=str(root)) as temporary:
        work = Path(temporary)
        # GitHub's stable-release download redirect avoids anonymous REST API
        # quotas. Pin ALL remaining downloads to the tag inside this manifest.
        manifest = get_json('https://github.com/'+REPO+'/releases/latest/download/RELEASE-MANIFEST.json', work, 'RELEASE-MANIFEST.json')
        tag = manifest['tag']
        if not re.fullmatch(STABLE_TAG, tag): fail('Latest release is not a supported stable V2+ release.')
        release = dict(tag_name=tag)
        name = 'babacoin-{}-{}-{}-{}.tar.gz'.format(tag, os_id, version, arch)
        if manifest['tag'] != tag or not re.fullmatch(r'[a-f0-9]{40}', manifest['source_commit']): fail('Invalid release source manifest.')
        records = [p for p in manifest['packages'] if p['name'] == name]
        if len(records) != 1: fail('Package is not uniquely pinned in the release manifest.')
        record = records[0]
        print('Latest stable release: '+tag+'; installing native runtime dependencies.', flush=True)
        install_dependencies(packages)
        archive = fetch_asset(release, name, work, record['sha256'], limit=2*1024**3)
        spec = importlib.util.spec_from_file_location('bbc_archive', str(Path(__file__).with_name('verify-release-archive.py')))
        verifier = importlib.util.module_from_spec(spec); spec.loader.exec_module(verifier)
        unpacked = work/'unpacked'; verifier.extract(archive, unpacked, arch)
        if (unpacked/'SOURCE_COMMIT').read_text(encoding='utf-8').strip() != manifest['source_commit']: fail('Package source mismatch.')
        for program in PROGRAMS:
            if digest(unpacked/program) != record['binaries_sha256'][program]: fail('Executable checksum mismatch.')
        linked = subprocess.check_output(['ldd', str(unpacked/'babacoind')], stderr=subprocess.STDOUT, universal_newlines=True, encoding='utf-8', errors='replace')
        if 'not found' in linked: fail('Native dependencies are unresolved:\n'+linked)
        print(subprocess.check_output([str(unpacked/'babacoind'), '--version'], universal_newlines=True, encoding='utf-8', errors='replace').splitlines()[0], flush=True)
        bootstrap = None
        need_bootstrap = args.bootstrap == 'always' or (args.bootstrap == 'auto' and not (data/'blocks').exists() and not (data/'chainstate').exists())
        bm = None
        if need_bootstrap and not (data/'bootstrap.dat').exists() and not (data/'bootstrap.dat.old').exists():
            try:
                bm = json.loads(fetch_asset(release, 'BOOTSTRAP-MANIFEST.json', work, limit=8*1024*1024).read_text(encoding='utf-8'))
            except urllib.error.HTTPError as error:
                if error.code != 404 or args.bootstrap != 'auto': raise
                print('No bootstrap published for this stable release; normal peer synchronization will be used.', flush=True)
        if bm is not None:
            full = [r for r in bm['archives'] if r['archive_name'] == 'bootstrap.zip' and r['first_height'] == 0]
            if len(full) != 1: fail('No unique full bootstrap archive.')
            full = full[0]
            if full['payload_bytes'] > 16*1024**3: fail('Bootstrap exceeds installer safety limit.')
            data_parent = data if data.exists() else next(p for p in data.parents if p.exists())
            if shutil.disk_usage(str(data_parent)).free < full['payload_bytes']+512*1024**2: fail('Insufficient space for bootstrap and validation. Use --bootstrap never or a larger data disk.')
            if shutil.disk_usage(str(root)).free < full['archive_bytes']+full['payload_bytes']+512*1024**2: fail('Insufficient staging space for bootstrap.')
            print('Downloading the verified full bootstrap. Normal consensus validation still runs.', flush=True)
            zipped = fetch_asset(release, 'bootstrap.zip', work, full['archive_sha256'], limit=8*1024**3)
            if zipped.stat().st_size != full['archive_bytes']: fail('Bootstrap archive size mismatch.')
            bootstrap = work/'bootstrap.dat'; unpack_bootstrap(zipped, full, bootstrap)
        backup = root/('backups/'+time.strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()))
        make_owned_directory(backup, account.pw_uid, account.pw_gid)
        replaced = []
        owners = {}
        stopped = False
        start_attempted = False
        controls = {}
        for name in ('INSTALLATION.json', 'start-babacoin.sh', 'stop-babacoin.sh', 'babacoin-cli.sh'):
            path = root/name
            if path.is_symlink(): fail('Unsafe control-file destination.')
            if path.exists() and (not path.is_file() or path.stat().st_size > 1024*1024): fail('Unexpected control-file destination.')
            if path.exists() and name.endswith('.sh') and b'# Managed by Babacoin node setup.' not in path.read_bytes()[:128]: fail('Refusing to overwrite an unrelated launcher.')
            controls[name] = path.read_bytes() if path.exists() else None
        try:
            # All downloads, integrity checks and staging precede shutdown.
            if node:
                print('Stopping the selected daemon normally; no forced kill.', flush=True)
                stop_node(node); stopped = True
            ensure_unlocked(data)
            (backup/'wallet').mkdir(); backup_wallet(data, backup/'wallet')
            make_owned_directory(binary_dir, account.pw_uid, account.pw_gid)
            originals = backup/'programs'; originals.mkdir()
            for program in PROGRAMS:
                target = binary_dir/program
                if target.is_symlink() or (target.exists() and not target.is_file()): fail('Unsafe existing program path.')
                if target.exists():
                    owners[program] = (target.stat().st_uid, target.stat().st_gid)
                    shutil.copy2(str(target), str(originals/program))
            for program in PROGRAMS:
                target = binary_dir/program
                owner = owners.get(program, (binary_dir.stat().st_uid, binary_dir.stat().st_gid))
                atomic_copy(unpacked/program, target, *owner); replaced.append(program)
            if not data.exists(): make_owned_directory(data, account.pw_uid, account.pw_gid)
            if bootstrap:
                fd, staged_name = tempfile.mkstemp(prefix='.bbc-bootstrap-', dir=str(data)); os.close(fd)
                staged = Path(staged_name)
                try:
                    shutil.copyfile(str(bootstrap), str(staged)); staged.chmod(0o600); os.chown(str(staged), account.pw_uid, account.pw_gid)
                    os.link(str(staged), str(data/'bootstrap.dat'))  # Exclusive publish; never overwrite an existing input.
                finally: staged.unlink()
            daemon_args = write_launchers(root, binary_dir, data, original_args, account.pw_name, context['unit'] if context else None)
            state = dict(release=tag, source_commit=manifest['source_commit'], binary_dir=str(binary_dir), data_dir=str(data), backup=str(backup), user=account.pw_name,
                         args=original_args, unit=context['unit'] if context else None, cwd=str(node['cwd'] if node else account.pw_dir))
            (root/'INSTALLATION.json').write_text(json.dumps(state, indent=2), encoding='utf-8')
            (root/'INSTALLATION.json').chmod(0o600); os.chown(str(root/'INSTALLATION.json'), account.pw_uid, account.pw_gid)
            for p in backup.rglob('*'):
                os.chown(str(p), account.pw_uid, account.pw_gid)
            os.chown(str(backup), account.pw_uid, account.pw_gid)
            if not args.no_start:
                start_attempted = True
                start_node(context, binary_dir, data, daemon_args, account.pw_name)
            stopped = False
            print('Installed {}. Start: {}\nCLI: {}\nBackup: {}'.format(tag, root/'start-babacoin.sh', root/'babacoin-cli.sh', backup), flush=True)
            print('Startup/import can take time. Check the data debug.log and CLI getblockchaininfo; no validation checks were disabled.', flush=True)
        except BaseException:
            if start_attempted:
                # The new process may already have opened/upgraded databases.
                # Never silently downgrade files underneath it after launch.
                print('Startup needs review. Verified new programs and backups retained; inspect debug.log. No automatic downgrade after a start attempt.', file=sys.stderr)
                raise
            for program in reversed(replaced):
                saved = backup/'programs'/program
                if saved.exists(): atomic_copy(saved, binary_dir/program, *owners[program])
                else: (binary_dir/program).unlink()
            for name, contents in controls.items():
                path = root/name
                if contents is None:
                    if path.exists(): path.unlink()
                else:
                    path.write_bytes(contents); path.chmod(0o700 if name.endswith('.sh') else 0o600)
                    os.chown(str(path), account.pw_uid, account.pw_gid)
            if stopped and not args.no_start:
                try: start_node(node, binary_dir, data, ['-datadir='+str(data), '-daemon=1']+[a for a in original_args if not a.startswith(('-datadir=', '-daemon='))], account.pw_name)
                except Exception: print('Automatic recovery could not restart the old daemon. Programs restored; inspect debug.log.', file=sys.stderr)
            raise


if __name__ == '__main__':
    try: main()
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print('ERROR: '+str(error), file=sys.stderr); sys.exit(1)
