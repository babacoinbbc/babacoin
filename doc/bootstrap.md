# Mainnet blockchain bootstrap

Download the archives and `SHA256SUMS-BOOTSTRAP` from the
[current V2 release](https://github.com/babacoinbbc/babacoin/releases/tag/v2.0.0-rc.4).
These are public raw blocks, usable across the supported operating systems.
They save block downloading; the node still performs normal proof-of-work,
transaction and consensus validation. Validation and a wallet rescan can take
time. No chainstate snapshot or validation bypass is provided.

## Choose one download

| Archive | Range | Download size | Requirement |
| --- | --- | --- | --- |
| `bootstrap.zip` | Genesis through 1,062,893 | About 611 MB | Use for a new or older mainnet chain |
| `bootstrap-update-762578-1062893.zip` | 762,578 through 1,062,893 | About 146 MB | Already validated through 762,577 on the same mainnet chain |

The update archive requires block 762,577 to have hash
`6b2458b914be35c9ce2bc475f8564a748ac1748f6b74eeff3598c1a25be50a89`.
Check with `getblockhash 762577` in the local debug console. If the height is
unavailable or the hash differs, use the full archive. Do not combine the two.

The pinned last block is 1,062,893:
`9657b4c12c44fef7210ea8cbadce24d47ef58d97abbd7595fd5e74b9962ae209`.
The node downloads newer blocks normally after importing this snapshot.

## Import without replacing wallet data

1. Back up the wallet using its backup command. Exit Babacoin cleanly.
2. Verify the downloaded ZIP against `SHA256SUMS-BOOTSTRAP`. On Windows,
   `Get-FileHash .\bootstrap.zip -Algorithm SHA256` prints its checksum.
   On Linux use `sha256sum bootstrap.zip`; on macOS use
   `shasum -a 256 bootstrap.zip`. Compare the full value with the matching line.
3. Extract the archive into a separate folder, for example `C:\BBC-Bootstrap`.
   Keep your existing wallet, data folder, configuration and backups in place.
4. Add the following argument to your normal Babacoin launch command or a copy
   of its existing shortcut. Keep its existing executable and data-directory
   arguments. Substitute the extracted file's actual absolute path:

   ```text
   "-loadblock=C:\BBC-Bootstrap\bootstrap.dat"
   ```

   For the update archive, use its `bootstrap-update-762578-1062893.dat` file
   instead. Linux/macOS use `-loadblock=/absolute/path/bootstrap.dat`.
5. Allow the normal import, verification and any required wallet rescan to
   finish. Previously validated blocks are skipped. Check the validated block
   height and network state in the wallet. At height 1,062,893, the local
   `getblockhash 1062893` result must match the pinned value above.
6. Close cleanly and remove the extra `-loadblock` argument for future starts.
   Keep or remove the separately extracted archive as desired.

After an interruption, the same `-loadblock` file can be imported again. Do not
delete chain data or replace `wallet.dat` to restart an import. Keep the original
archive outside the data directory. These instructions intentionally use an
explicit load file instead of the automatically renamed `bootstrap.dat` startup
file in the data directory.

Use V2 for current network rules. A bootstrap does not upgrade V1 or make older
clients accept the new 2,000,000 BBC Smartnode registration rules.

## Published checks and limitations

`BOOTSTRAP-MANIFEST.json` pins the exact range, hashes, byte counts and exporter
identity. Every exported block's transaction framing and Merkle root was
checked. An independent Explorer query matched the pinned last block.
`BOOTSTRAP-VALIDATION.json` records a normal V2 consensus import of the first
1,000 blocks into a fresh offline node with its wallet disabled. This sample is
not an independent replay of the complete archive. The consumer's node must
validate the entire imported chain normally.

Only the raw `.dat` payload, an English readme and `CONTENTS.json` are inside
each ZIP. No wallet, key, configuration, credentials, chainstate, block index,
peer list, debug log or PoW cache is distributed.

## Rebuild a pinned archive

Use Python 3.8+ with the standard library and a **cold copy** of a fully
validated node's `blocks` directory, taken after a clean shutdown. Do not export
from files still being written. The output directory must be new and outside
the source blocks directory:

```sh
python3 contrib/bootstrap/export-bootstrap.py \
  --blocks /offline-copy/blocks --output /new/public-bootstrap \
  --tip-height 1062893 \
  --tip-hash 9657b4c12c44fef7210ea8cbadce24d47ef58d97abbd7595fd5e74b9962ae209 \
  --delta-start 762578
```

The exporter follows the supplied tip back to the fixed BBC mainnet genesis,
excludes other branches, validates framing and every transaction Merkle root,
then creates the full and incremental ZIPs. It does not compute GhostRider PoW
or replace the node's consensus checks. ZIP timestamps may vary; verify the
published archive checksum rather than assuming byte-identical recompression.
Run `python3 test/util/bootstrap_export_test.py` for corruption, branch-selection,
special-transaction framing and publication-boundary regression checks.
