# Native dependency provenance

`libsodium-1.0.22-stable-20260928.tar.gz` is the unmodified upstream source
snapshot published 2026-09-28, retrieved 2026-10-01 from
https://download.libsodium.org/libsodium/releases/libsodium-1.0.22-stable.tar.gz.
It is vendored because the upstream stable URL changes; builds must use exactly
the reviewed snapshot rather than silently adopting new code.

SHA-256: `625054219273c74fd204b669d97070fb60cf16b6d68b9909aff7a3530792262f`.
CMake verifies this hash before extraction. The accompanying upstream Minisign
signature was cryptographically verified against the public key published at
https://doc.libsodium.org/installation:
`RWQf6LRCGA9i53mlYecO4IzT51TGPpvWucNSCh1CBM0QTaLn73Y7GFO3`.

To verify again with Minisign:

```sh
minisign -Vm libsodium-1.0.22-stable-20260928.tar.gz \
  -P RWQf6LRCGA9i53mlYecO4IzT51TGPpvWucNSCh1CBM0QTaLn73Y7GFO3
```

Static PIC libsodium, Opus and libc++ are linked into `libomachat_crypto.so`.
The APK includes ISC, OmaChat MIT and NDK LLVM notices under `assets/licenses/`.
No third-party binary, desktop service, or Qt library is packaged. Updates require
review, a new snapshot/hash/signature and interoperability/regression validation.

## Opus

`opus-1.6.1.tar.gz` is unmodified upstream source, downloaded 2026-10-02
from https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz.
SHA-256: `6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1`,
matched against the upstream [stable download page](https://opus-codec.org/downloads/).
CMake verifies it before extraction and statically links the PIC library into
`libomachat_crypto.so`; its BSD license is packaged in `assets/licenses/opus.txt`.
This hash was verified over HTTPS; no detached-signature verification is claimed.
Testing/programs, DRED and OSCE are disabled; ordinary float encode/decode and PLC
use the desktop's unchanged Qt-free `OpusCodec.cpp`. This is source reuse within
OmaChat, not an independent codec implementation or source audit. Desktop/Android
wire compatibility is checked with independently installed host libopus and real
relay frames. No Opus binary is downloaded during builds.
