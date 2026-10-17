# Shared artwork framing

Implementation: 2026-10-13. Validation and rollout evidence is recorded in
[desktop-session-handoff.md](desktop-session-handoff.md).

The crop dialog offers **Keep the full image for managers to reframe** on servers
advertising `artwork.framing.v1`. New image selections start unchecked. Without
that choice, the upload contains only the reviewed crop, retaining the existing
behavior and device-local crop memory. Choosing to keep the image also uploads a
metadata-free, oriented source raster. Reframe retrieves that source and restores
the shared framing for explicit review. Clearing the choice on replacement drops
the retained-source contract for the new asset.

Framing is an immutable source-pixel rectangle: source width/height, x/y,
width/height, and icon/banner kind. Its normalized centre is the focal point.
UI crop-travel fractions are converted to this rectangle before upload; they are
not mislabeled as source focal coordinates. The server validates bounds, aspect
ratio, zoom range and actual decoded source dimensions. Replacements get a new
attachment ID; duplication shares the immutable asset. Channel updates retain
the existing artwork comparison and explicit conflict reload.

Uploads and each stored raster are limited to 2 MiB. Static PNG, JPEG and WebP
are accepted within 8192 pixels per dimension and 16 megapixels; animation,
invalid framing and unknown coordinate fields are rejected or discarded.
Metadata is stripped by copying pixels into a fresh raster. A source which
cannot fit its limit fails visibly. Opaque sources can be recompressed as JPEG
at quality 90; retained images are normalized rasters, not the original file
bytes, and repeated JPEG reframing can change pixels. Framing stays exact.

The server prepares images in two bounded workers. It rechecks the live upload
identity and management permission before committing. Atomic source/small writes,
the full crop and attachment row form one published asset; failed publication
removes its files. Schema 15 persists framing alongside the attachment and gives
it a nullable upload carrier. Surviving server/channel references retain artwork
through carrier deletion, including removal of a server's last channel. Detached,
unreferenced pending artifacts are reclaimed; message deletion still cascades.
Migration preserves dependent emoji rows/indexes, checks foreign keys before
commit and restores enforcement on success and rollback.

The default download remains the canonical crop (512×512 icon or 1024×256 banner)
so older clients keep working. `SMALL` returns a 128×128 icon or 512×128 banner;
legacy assets fall back to their existing crop. The desktop requests SMALL for
icon-only assets; banners and assets used in both slots keep the full crop.
Existing cached full rasters remain valid. SOURCE requires management permission
for a currently authorized channel reference or the owning server. Members can
retrieve display variants but cannot retrieve the full source. Authorization is
checked on every chunk, including resumed downloads. Uploaders can retrieve an
unclaimed source while they still manage its carrier; normal pending-attachment
expiry reclaims unused replacements. Sources have no public URL.

Daemon API:

- `channel.artwork.set` / `server.artwork.set`: optional `framing` object with
  integer `source_width`, `source_height`, `x`, `y`, `width`, `height`; `kind`
  supplies the slot. Sending framing explicitly opts into source retention.
- `attachment.download`: optional `artwork_variant` = `full`, `small`, or
  `source`; successful framed downloads also return the framing object.
- CLI: `omachatctl --json attachment get ID --variant source --output PATH`.

Creation staging retains the source and framing under an account/server-bound
opaque token only when the retention choice is enabled. Remote sources use a
private temporary file and bounded loading; stale account/server/access results
never enter the current draft. Source errors retain a useful replacement-image
path for assets uploaded before source retention existed.

Human usability acceptance, broader notification/organization phases and the
Android physical-device and MLS/SFrame gates remain separate.
