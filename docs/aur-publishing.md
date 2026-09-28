# Publishing to the AUR

`packaging/arch/PKGBUILD` is already AUR-ready as-is: with no environment
variables set it fetches the tagged release from GitHub
(`source=("git+${url}.git#tag=v${pkgver}")`) and builds and tests it exactly
like `scripts/install.sh` and the CI package build do. This has been
verified end to end (clean clone of the real `v0.2.0` tag, full build,
`check()` with all 153 tests passing, package created) — nothing in the
file needs to change for AUR.

What's left is account setup, which only you can do (it needs your own
login on the AUR website):

## One-time setup

1. Create an account at <https://aur.archlinux.org/register/> if you don't
   have one.
2. Generate an SSH key for AUR if you don't want to reuse an existing one:
   ```bash
   ssh-keygen -t ed25519 -f ~/.ssh/aur -C "aur@your-email"
   ```
3. Add the **public** key (`~/.ssh/aur.pub`) to your AUR account under
   My Account → SSH Public Key.
4. Add to `~/.ssh/config` so `git` picks the right key:
   ```
   Host aur.archlinux.org
       IdentityFile ~/.ssh/aur
       User aur
   ```

## First publish

```bash
git clone ssh://aur@aur.archlinux.org/omachat.git /tmp/omachat-aur
cd /tmp/omachat-aur
cp /path/to/OmaChat/packaging/arch/PKGBUILD .
makepkg --printsrcinfo > .SRCINFO
git add PKGBUILD .SRCINFO
git commit -m "Initial import: omachat 0.2.0"
git push
```

The package appears at `https://aur.archlinux.org/packages/omachat` within
a few minutes. From then on, anyone can install with
`yay -S omachat` / `paru -S omachat`.

## Every future release

After tagging a new version in the main repo (see `docs/status.md` →
"Next" for the release process), repeat just the AUR half:

```bash
cd /tmp/omachat-aur
git pull
cp /path/to/OmaChat/packaging/arch/PKGBUILD .   # pkgver already bumped there
makepkg --printsrcinfo > .SRCINFO
git add PKGBUILD .SRCINFO
git commit -m "Update to $(grep -m1 pkgver PKGBUILD | cut -d= -f2)"
git push
```

`makepkg -si` locally first is worth doing before pushing, to catch a
build break before AUR users hit it.
