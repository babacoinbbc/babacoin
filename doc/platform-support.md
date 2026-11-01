# V2 platform support and validation levels

A configured CI target is not a result. Check the actual jobs attached to the
release source commit. The release is assembled only when every required job
succeeds; `RELEASE-MANIFEST.json` identifies all packaged executables and archives.

| Target | Build/test environment | Important limits |
|---|---|---|
| Ubuntu 18.04/20.04/22.04/24.04/26.04 x86_64 | Matching container, native x86_64 runner | Matching runtime libraries; old OS security maintenance separate |
| The same Ubuntu LTS versions, ARM64 | Matching container, native ARM64 runner | Not a physical Raspberry Pi or a 32-bit ARM test |
| Debian 12/13 x86_64 | Matching container, native x86_64 runner | Ubuntu libraries are not substituted |
| Debian 12/13 ARM64 | Matching container, native ARM64 runner | Raspberry Pi OS 64-bit compatibility target, not Pi-specific firmware/display validation |
| Windows x64 | Native UCRT64 build, separate Windows Server 2022 test runner | No claim of Windows 7/8/32-bit support or Microsoft signing |
| Windows 11 ARM64 | x64 package executed on an ARM64 runner under Windows emulation | No native Windows ARM64 executable; requires working x64 emulation |
| macOS Intel | Native macOS 15 Intel runner | Older Mac releases are not certified |
| macOS Apple Silicon | Native macOS 15 ARM64 runner | GUI is bundled and ad hoc signed; Apple notarization not supplied |

Raspberry Pi OS Bookworm and Trixie use Debian 12 and 13 bases respectively.
Choose the matching ARM64 Debian package on a **64-bit OS**. Raspberry Pi model,
RAM, storage, display stack, temperature and kernel still require device checks.
A 64-bit CPU running a 32-bit OS cannot run an ARM64 binary. See the official
[Raspberry Pi OS documentation](https://www.raspberrypi.com/documentation/computers/os.html).

The platform matrix does not currently certify native Windows ARM64, Android,
iOS, FreeBSD, OpenBSD, 32-bit Windows/ARM, every Linux derivative, Ubuntu interim
releases or old macOS versions. There is no fallback that silently downloads a
different distribution's binary. Additional platforms belong on the roadmap
until their own build, runtime, wallet and packaging tests pass.

For manual Debian userspace reproduction, use the disposable Docker command in
`doc/ubuntu-compatibility.md` with `debian:12` or `debian:13` and a new output
directory. The same dependency/test script records the actual OS identity and
names the resulting archive accordingly. Never execute that dependency installer
on an existing production node.

Windows-on-ARM jobs explicitly verify `RUNNER_ARCH=ARM64`, execute the x64 tests
and record both host and executable architectures. They do not create a falsely
labelled native ARM64 package. GitHub runner availability is documented in the
[hosted-runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
