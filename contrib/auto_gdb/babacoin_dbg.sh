#!/bin/bash
# use testnet settings,  if you need mainnet,  use ~/.babacoin/babacoind.pid file instead
babacoin_pid=$(<~/.babacoin/testnet3/babacoind.pid)
sudo gdb -batch -ex "source debug.gdb" babacoind ${babacoin_pid}
