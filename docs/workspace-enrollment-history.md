# omachat status

Updated: 2026-10-17. Historical record of the spoke enrollment; for current
branch and worktree state see [docs/STATUS.md](STATUS.md).

## Current scope and implementation

omachat is located at `/home/omka/ombrella/omachat` as an independently rooted Git repository. It contains the Qt client, daemon, server, and CLI. The seven required spoke documents exist and are tailored to the project. A dark landing and an ombrand-rendered identity review page are present; three editable proposals are listed and none is adopted. Optional integration contracts with omatube, omban, and omavb are documented. Local IPC remains same-user and opt-in for voice audio.

Known limits: live external integration calls were not exercised, and landing-page content has not had screenshot-based manual review. The pre-existing uncommitted README delta was overwritten during scaffold drafting; the committed README was restored and supplemented, but that original local README delta could not be recovered.

## Entry points

- GUI: `omachat`; daemon: `omachatd`; CLI: `omachatctl`.
- Build and development: `docs/development.md` and `scripts/dev-instance.sh`.
- Landing route: `omachat-landing`, port 9143, through `omlocal launch omachat-landing --open`.
- Identity review: `site/identity/proposals/index.html`.

## Verification

Checks run for this update:

- `python3 omachat/scripts/check-landing-links.py omachat` — passed; exactly one landing link points to identity studies and is labeled ombrand.
- `python3 creative/ombrand/scripts/render-identity.py --project omachat --output omachat/site/identity/proposals` — passed.
- `bash -n coordinate/omlocal/bin/omlocal` — passed. The obsolete `site/landing.json` marker was removed; it was not valid indexable content.
- `omrag index omachat creative/ombrand/brands/omachat README.md OMBRELLA.md docs/STATUS.md` — refreshed the moved spoke and identity source. Freshness was checked after removing the non-indexable landing JSON marker; final freshness is refreshed below.
- `knowledge/omnoda/.venv/bin/omnoda --scope ombrella load` — passed; the graph contains 51 projects, including `omachat`. A graph query confirmed the `omachat` project vertex.
- `cmake -S . -B /tmp/omachat-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build /tmp/omachat-build` — passed. The first configure attempt hit the stale ignored `build/` cache from the former path and a case-mismatched CMake include; a clean build directory and corrected include path resolved it.
- `ctest --test-dir /tmp/omachat-build --output-on-failure` — passed, 195/195 tests.
- `bash -n coordinate/omlocal/bin/omlocal` and `bash coordinate/omlocal/run-tests.sh` — the omachat route assertion passed; full suite reported 93 passed and 9 existing failures in the audioaddons block, binding drift, missing eisgen cases, dashboard fallback assertions, and launcher installation.
- `./coordinate/omlocal/install.sh --launcher-only` — installed the launcher and created backup `~/.local/bin/omlocal.bak.20261017-013654`. `omlocal launch omachat-landing --open` opened the page; Hyprland reports title `omachat — local chat and voice` visible in workspace `omachat`; landing and identity routes return HTTP 200.
- The Bluetooth microphone was switched to the connected TWS-6 headset profile. PipeWire reports omachat's active voice capture stream linked to the Bluetooth microphone; no recording was made.

Not run: screenshot-based visual review, live external integration calls, and a microphone recording test. PipeWire routing is verified from the active capture link only.

## Repository snapshot

Before this status edit: branch `omachat-ombrella-spoke`, HEAD `8bb6417` (the original local HEAD), and 94 status entries. The original checkout was on `main`, 16 commits ahead of origin, and already had 26 modified/untracked entries before the move. This spoke branch has no upstream and no commit has been created. The original source changes remain in the worktree; the enrollment work is also uncommitted. The pre-existing local README edits were overwritten during scaffold drafting; the committed HEAD README was restored and supplemented with the spoke sections, but that uncommitted README delta could not be recovered.

## Next steps

Recheck omrag freshness and omnoda membership after this snapshot; open the registered landing page in the project workspace; inspect and selectively stage changes before any commit. No push or PR is authorized.


Final index check: 295 current, zero stale, 2 intentionally not indexed (`CLAUDE.md` and `GEMINI.md` import wrappers), zero missing or unreadable among 297 discovered files. The `ombrella` graph load completed with 51 projects, 243 files and 862 vertices; querying confirmed the `omachat` project vertex. The temporary JanusGraph process was stopped after verification. The new Codex process is running with `/home/omka/ombrella/omachat` as its working directory; landing title is visible on workspace `omachat`.


## Omarchy menu launcher repair — 2026-10-17

The `projects.omachat` action in `~/.config/omarchy/extensions/omarchy-menu.jsonc` points to the current checkout and runs it through `uwsm-app`. The underlying issue was that `scripts/dev-instance.sh start` backgrounded its GUI and returned, so UWSM ended the scope and stopped the client. The `start` command now keeps the client in the foreground and focuses its window when Hyprland maps it. The menu config has a timestamped backup beside it, and `omarchy menu refresh` returned `ok`.

Verification: `bash -n scripts/dev-instance.sh` passed. Running the exact menu command opened client PID 1230980; Hyprland reported `org.omarchy.OmaChat`, title `#general — OmaChat`, workspace `omcollabs`, and the client stayed alive in the UWSM scope. The connected local daemon reports `omka@omachat.sleepystudio.xyz:6473`. I focused the client through Hyprland and confirmed it is now the active window. The focus predicate used by the updated launcher matches this live window.

Repository state after this fix: branch `omachat-ombrella-spoke`, HEAD `8bb6417`, with the existing dirty/untracked work described above plus `scripts/dev-instance.sh` and this status note. The menu configuration and backup are user-level files outside the omachat Git repository; no commit was created.

## Channel message notifications — 2026-10-17

Channel message alerts are enabled by default and have their own Notifications setting. Direct message alerts remain separately configurable; mentions can still notify when channel message alerts are turned off. Existing DND, muted-channel, and focused-channel suppression is preserved. Message notification sounds use the local `message-new-instant` event at `-18 dB` when `canberra-gtk-play` is available.

The Omarchy notification service was cloned through `omarchy plugin clone omarchy.notifications` to `~/.config/omarchy/plugins/omka.notifications`. Its toast stack now appears at bottom-right, older cards peek out and expand when hovered, and cards animate in, out, and when the stack shifts. Existing hover-paused expiry, click-to-open, and dismiss interactions remain. Verification: `omarchy restart shell`, plugin clone, `omarchy-shell shell rescanPlugins`, and enabled state from `omarchy-plugin-list --json` succeeded. `cmake --build /tmp/omachat-build --target omachat omachatd -j2` passed after the final source change; `git diff --check` passed for the notification source/document files. No tests were run.

Repository state after this change: branch `omachat-ombrella-spoke`, HEAD `8bb6417`, 96 `git status --short` entries, including the already dirty/untracked work and the notification changes in the README, settings UI/controller, daemon, config, protocol docs, and this status file. The Omarchy plugin lives outside the repository. No commit was created.
