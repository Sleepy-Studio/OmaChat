# Shared artwork hosted rollout

Prepared 2026-10-14. This is an execution checklist, not evidence of deployment.
Resume from [desktop-session-handoff.md](desktop-session-handoff.md).

## Target and current evidence

- Coolify application: `32t4edl2sggh7drj9v2ov2qf`, Compose service `omachat-server`.
- Backend revision: `83441d0a5a894276bcd53f54d2ef6da64a40ccd6`.
- Image: `ghcr.io/sleepy-studio/omachat-server`.
- OCI index digest: `sha256:b280e8128c09fc229bec90748b8677462f4ad670e277be97c9a2eb3bb96d7bad`.
- Linux amd64 manifest: `sha256:1b1029168b9ca5d0daf6605a080cbe83a4fd2155b2485dcb8cf6e65b34c59d6b`.
- Registry `latest` still matches that index at 05:16 UTC on October 14.
- Coolify reports running/healthy. Connected daemon lacks `artwork.framing.v1`.
- Latest deployment metadata remains the October 13 finished webhook; it does
  not prove which image is running. No scheduled application tasks exist.
- Voice and screen sharing are active at this recheck. Recheck before downtime.

## Backup before migration

1. Obtain hosted shell/terminal access. Resolve the actual container and inspect
   its `/var/lib/omachat` mount. Coolify lists volume
   `32t4edl2sggh7drj9v2ov2qf_omachat-data`; verify it on the host rather than
   assuming a Docker storage-root path. Read only the database/files path fields
   from `server.toml`; it can contain OAuth secrets.
2. Record the current container image ID, repository digest, image revision,
   mount identity and timestamp. Preserve the older image for rollback.
3. When voice, streams and transfers are idle, announce downtime and stop the
   application. Verify no server or other process writes the volume.
4. Archive the entire stopped volume to a private destination outside that
   volume, preserving ownership and permissions. Include the SQLite database,
   any WAL/SHM files, attachments, configuration and TLS identity. Default paths
   are `omachat.db`, `files/`, `server.toml`, `cert.pem` and `key.pem`; confirm
   actual configuration first. A live copy of only `omachat.db` is insufficient.
5. Hash the archive and extract into a separate private restore directory.
   Check the extracted database with `PRAGMA integrity_check`,
   `PRAGMA foreign_key_check`, and `PRAGMA user_version`: require `ok`, no FK
   rows and version **14**. Confirm attachment files and identity/configuration
   are present. Do not start the new server against the rollback copy.
6. Record verification output, archive location and checksum without copying
   secrets into repository logs or the shared vault. If verification fails,
   restart the unchanged older application and resolve backup integrity first.

## Deploy and verify

1. Recheck the registry digest and pull the matching image on the hosted server.
   The current Compose contract uses `OMACHAT_IMAGE_TAG` with default `latest`;
   it does not offer an image-digest variable. Verify the resolved image before
   starting it. A Coolify finished status alone is insufficient.
2. Queue the application deployment using the available Coolify deploy action.
   Token deploy ability remains unproven until the authorized action succeeds.
3. Check container health, running image ID/digest and revision. Distinguish the
   multi-platform index digest from its amd64 manifest digest when comparing.
4. Confirm the daemon reconnects with the same trusted TLS identity and advertises
   `artwork.framing.v1`. Confirm hosted `PRAGMA user_version` is **15**.
5. Check history, ordinary artwork and attachment access. In a designated test
   channel, verify opt-in retained source/reframe and full/small display variants;
   verify source denial for a non-manager. Remove only fixtures created here.

## Rollback

Older binaries reject schema 15. On failure, stop the new application before
restoring the verified full-volume backup and older image. Do not merely restart
the older image against the migrated volume. Preserve a private copy of failed
state first; restoring the backup discards post-backup writes. Restore original
ownership and permissions, then verify schema 14, health, TLS identity and client
reconnection. Record the actual outcome in the handoff.
