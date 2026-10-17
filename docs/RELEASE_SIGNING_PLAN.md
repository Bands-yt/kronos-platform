# Signed releases: the plan

Nothing here is switched on. It needs accounts and money, so it needs the owner.

## What gets signed
- Windows installer and zip: an Authenticode code-signing certificate (about 100 to 400 USD a year, or Azure
  Trusted Signing). Without it Windows SmartScreen warns on every download.
- Linux tarball: a detached signature (minisign or GPG) next to the file.
- Every release publishes `SHA256SUMS` (the hashes). The tested helper is `sha256Hex` in
  `core/BackendLogicTools.hpp`; `downloadComplete` in `core/AudioNetTools.hpp` checks a file against a hash.

## Update manifest
The updater downloads a small manifest (version, file URLs, SHA-256 of each file, signature of the manifest).
It refuses a manifest whose signature is wrong and a file whose hash does not match. Keep the signing key
offline; the build machine only gets a signing subkey that can be revoked.

## Steps for the owner
1. Pick Azure Trusted Signing or buy a certificate.
2. Put the secret in GitHub Actions secrets (never in the repo; `findSecrets` in `core/AudioNetTools.hpp`
   scans for leaks).
3. Add a signing step to `release.yml` after the build.
