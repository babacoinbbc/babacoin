#!/usr/bin/env python3
"""Run only in a disposable CI container: real download, apt, start and upgrade."""
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

if os.environ.get('BBC_DISPOSABLE_BUILD') != '1': raise SystemExit('Disposable container required.')
source = Path(__file__).resolve().parents[1]
work = Path(tempfile.mkdtemp(prefix='bbc-installer-smoke-'))
install = work/'app'; data = work/'data'
command = ['bash', str(source/'install-node.sh'), '--install-root', str(install), '--data-dir', str(data), '--bootstrap', 'never']
subprocess.check_call(command+['--no-start'])
state = json.loads((install/'INSTALLATION.json').read_text())
binary = Path(state['binary_dir'])/'babacoind'
sha = hashlib.sha256(binary.read_bytes()).hexdigest()
subprocess.check_call([str(install/'start-babacoin.sh'), '-connect=0', '-listen=0', '-dnsseed=0',
    '-discover=0', '-upnp=0', '-listenonion=0', '-maxconnections=0', '-walletbroadcast=0', '-keypool=2'])

def rpc(method, *params):
    auth = base64.b64encode((data/'.cookie').read_bytes().strip()).decode()
    request = urllib.request.Request('http://127.0.0.1:17668',
        json.dumps({'jsonrpc':'1.0','id':'setup-smoke','method':method,'params':list(params)}).encode(),
        {'Authorization':'Basic '+auth, 'Content-Type':'application/json'})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try: response = opener.open(request, timeout=3)
    except urllib.error.HTTPError as error: response = error
    with response: result = json.load(response)
    if result.get('error'): raise RuntimeError(result['error'])
    return result['result']

def ready():
    until = time.monotonic()+180
    while time.monotonic() < until:
        try:
            info = rpc('getnetworkinfo'); chain = rpc('getblockchaininfo')
            assert info['version'] >= 2000200 and info['connections'] == 0
            assert chain['chain'] == 'main' and chain['blocks'] == 0
            return info
        except (OSError, ValueError, RuntimeError): time.sleep(.5)
    raise RuntimeError('Offline node failed to become ready: '+str(data/'debug.log'))

try:
    before = ready()
    address = rpc('getnewaddress')
    assert rpc('getaddressinfo', address)['ismine']
    # Upgrade a LIVE daemon. The installer must locate its real lock, close it
    # normally, back up the wallet and restart with all offline flags retained.
    subprocess.check_call(command)
    after = ready()
    assert after['version'] == before['version']
    assert rpc('getaddressinfo', address)['ismine']
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == sha
    updated = json.loads((install/'INSTALLATION.json').read_text())
    backup = Path(updated['backup'])/'wallet'
    proof = json.loads((backup/'SHA256.json').read_text())
    assert 'wallet.dat' in proof and hashlib.sha256((backup/'wallet.dat').read_bytes()).hexdigest() == proof['wallet.dat']
    assert updated['data_dir'] == str(data)
    print(json.dumps(dict(result='PASS', release=state['release'], version=after['version'],
        fresh_dependency_install=True, live_graceful_upgrade=True, wallet_ownership_preserved=True,
        verified_wallet_backup=True, external_connections=0, source_unchanged=True)))
finally:
    try: rpc('stop')
    except (OSError, RuntimeError, ValueError): pass
