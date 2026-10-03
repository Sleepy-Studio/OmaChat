# OmaChat add-ons and extension points

Status: design and staged plan, 2026-10-02. Nothing in Phases 2-5 is
implemented; this records what OmaChat actually offers today, what can be built
on it without changing the architecture, and what a real plugin system would
cost.

## Baseline: there is no plugin system

Verified against the source, not assumed:

- **No native plugin loading anywhere.** No `dlopen`, `QLibrary`, or
  `QPluginLoader` in the tree; no "plugin" identifier in `client/src/`,
  `client/qml/`, `daemon/src/`, or `server/src/`.
- **QML is compiled into the binary.** `qt_add_qml_module(omachat URI OmaChat)`
  (`client/CMakeLists.txt`) AOT-compiles every `.qml`; `client/src/main.cpp:72`
  loads the root with `engine.loadFromModule("OmaChat", "Main")`. There is no
  `addImportPath` and no disk load, so **QML cannot be injected at runtime**.
- **The server is authoritative.** Roles, channel permissions, and operator
  actions are enforced server-side. A client holds no authority over them.

Consequences, stated plainly:

| Wanted | Possible today | Mechanism |
|---|---|---|
| Local add-ons | **Yes** | Local IPC (`shared/src/omachat/ipc/IpcMessage.hpp`) |
| Server-wide add-ons | **Only as shipped features** | New protocol capability + schema migration |
| Downloadable add-ons | **No** | Needs an architecture change |

A "server-wide add-on" is not a module the server loads. It is a capability
added to the protocol and enforced by the server, gated like
`instance.operator.v1` (`server/src/core/AuthHandlers.cpp:92`).

## The extension point that already exists: local IPC

`shared/src/omachat/ipc/IpcMessage.hpp` documents why it is shaped this way:

> JSON was chosen over protobuf here so that the Omarchy shell plugin
> (Quickshell QML) and shell scripts can speak the protocol without code
> generation.

Newline-delimited compact JSON over a mode-0600 Unix socket at
`$XDG_RUNTIME_DIR/omachat/omachat.sock`:

```
request  {"v":1,"id":7,"method":"voice.join","params":{...}}
response {"v":1,"id":7,"ok":true,"result":{...}}
         {"v":1,"id":7,"ok":false,"error":{"code":"PermissionDenied","message":"..."}}
event    {"v":1,"event":"voice.state","data":{...}}
```

17 stable error codes are declared in `IpcMessage.hpp:47-64`. Methods dispatch
through a `QHash` registry (`daemon/src/application/DaemonMethods.cpp:491`);
an unregistered method returns `UnknownMethod`. `omachatctl` is a full client
of this protocol, so anything reachable from the CLI is reachable from any
language with a Unix socket.

The shipped Omarchy bar widget uses exactly three methods —
`daemon.status`, `events.subscribe`, `voice.leave` — which is the reference
example of a third-party add-on.

**Security posture:** the socket is mode 0600 and lives in the user's private
runtime directory, so the trust boundary is the local user account. An add-on
runs with the same privileges as the user, not as a confined sandbox. There is
no signing, no permission manifest, and no revocation.

## Phase 1 — local add-ons (no upstream change needed)

Available immediately to anyone with the socket:

1. **Shell / Python / Node add-ons** speaking IPC directly. Full access to
   everything `omachatctl` exposes.
2. **Quickshell panels** — the pattern the Omarchy widget already uses.
3. **`omachatctl` wrappers** for keybindings and scripts.

Limits worth knowing: there is no event replay (a subscriber sees events from
the moment it connects), no push API beyond `events.subscribe`, and anything
requiring new daemon behaviour needs a daemon code change.

## Phase 2 — extend `omachat://` (small, upstreamable)

**Partly implemented.** The client already registers `omachat://` as a desktop
handler (`packaging/desktop/org.omarchy.OmaChat.desktop`) and dispatches it in
`AppController::openLink` (`client/src/controllers/AppActions.cpp:522`).

Shipped so far:

| Verb | Effect | Confirmation |
|---|---|---|
| `omachat://invite/TOKEN` | Join a server by invite | Yes — existing |
| `omachat://join/<channelId>` | Focus a channel | No — navigation only |
| `omachat://server/<serverId>` | Focus a server | No — navigation only |

Both new verbs validate against the models the client already holds
(`m_channelsById` / `m_serversById`), so a link cannot reach a channel the user
cannot read, and an unknown verb or empty id names the problem instead of
reporting a generic "unsupported link".

Not implemented, and deliberately still gated — anything that mutates state,
captures a device, or sends a message:

| Verb | Confirmation |
|---|---|
| `omachat://account/N` | Yes — switches identity |
| `omachat://voice/join` | Yes — captures the mic |
| `omachat://settings/SECTION` | No, once added |

Rules for any new verb:

- **Never** execute a mutating verb from a link without confirmation. Parse,
  describe in plain language, ask. The invite path (`client/qml/Main.qml:99`)
  is the model.
- Reject unknown verbs visibly rather than silently ignoring them, so a typo is
  not a dead click.
- Keep credentials out of the scheme beyond the existing single-use,
  server-validated invite token.
- A verb must never widen what a link can do on the server: everything still
  goes through the daemon and its permission checks.

`tests/manual/url-verbs-check.sh` renders offscreen with each URL and asserts
startup and render. It does **not** claim a verb navigated — without a live
daemon there are no channels, so a correct "no such channel" path and a silent
no-op look identical. The check's header says so.

`tests/manual/url-verbs-live-check.sh` closes that gap. It drives the real
forwarding path (a second `omachat` invocation hands the URL to the running
instance over the single-instance socket) and reads the compositor's window
title to see which channel is actually displayed:

```
join/<real>          shows that channel
join/<other real>    moves to a visibly different one
join/<nonexistent>   leaves the window where it was
```

It only reads `hyprctl clients`; it never focuses, moves or raises a window.
Verified against a live account: `#general` → `@Howie Duhzit`, with a
nonexistent id refused without moving.

**A caveat on `server/`:** it navigates, but the window title often does not
change, because `selectServer` ends in `ensureSelection()` which immediately
auto-selects a channel. Two different servers can both show `#general`. The
observable effect is the server rail and member list, not the title — so
assert on `status`, not the title, when testing that verb.

## Phase 3 — a real local theme override (small, upstreamable)

**Implemented.** `ThemeProvider` reads `$XDG_STATE_HOME/omarchy/current/theme/colors.toml`
(`client/src/platform/ThemeProvider.cpp:90`) and accepts `background`,
`foreground`, `accent`, `selection`, `red`, `green`, `yellow` (`loadFile`,
`ThemeProvider.cpp:153`). It file-watches and live-reloads.

The gap it had: that is the **Omarchy** theme file, so editing it recolors the
whole desktop, not just OmaChat. `reload()` now resolves an OmaChat-local file
first (`ThemeProvider::localThemeFile`) and falls back to Omarchy:

| Priority | Source |
|---|---|
| 1 | `$OMACHAT_THEME_FILE` (explicit, and what the test uses) |
| 2 | `~/.config/omachat/theme.toml` |
| 3 | `$XDG_STATE_HOME/omarchy/current/theme/colors.toml` |
| 4 | built-in dark palette |

The local file uses the **same schema**, so a partial file only overrides the
keys it names and inherits the rest from Omarchy. It is watched too, so edits
apply live. Per-user and per-application: no rebuild, no server, no
desktop-wide side effects.

```toml
# ~/.config/omachat/theme.toml — OmaChat only
background = "#12141a"
foreground = "#e6e8ec"
accent     = "#7aa2f7"
```

Verified by `tests/manual/theme-override-check.sh`, which renders offscreen and
reads the background pixel back out of the PNG: Omarchy-only, local-wins,
partial-falls-back, and no-theme-at-all. The `local-wins` case fails against the
previous code, so the check discriminates rather than merely passing.

Other local settings, no code needed:
`~/.config/omachat/config.toml` carries all nine shortcut bindings
(`shared/src/omachat/config/ClientConfig.cpp:38`), `[ui] compact_mode`,
`scale`, `reduced_motion`, `[audio]` and `[video]` preferences, and per-user
local volume.

## Phase 4 — server-wide features (protocol work, not modules)

Anything shared between users must be enforced by the server, which means the
real cost is protocol and schema, not a plugin loader:

- additive protobuf fields, capability-gated so older clients still connect
- a server schema migration (`server/src/storage/Store.cpp` is at v13)
- daemon method + CLI parity
- GUI, plus integration tests for permission denial and reconnect

`docs/channel-customization-plan.md` is the worked example of this cost, and
its product rules apply to any extension: customization must not imply
end-to-end encryption, and defaults must stay equivalent to today's behaviour.

## Phase 5 — a genuine plugin system (architecture change)

Only if add-ons must load code or content at runtime. The pieces, in
dependency order:

1. **Manifest and discovery** — where add-ons live, what they declare.
2. **A load path** — the real blocker. QML is AOT-compiled into the binary, so
   this means either shipping a runtime-importable QML module and calling
   `addImportPath`, or dropping to a non-QML extension format.
3. **Trust and sandboxing** — the hardest part, and the reason to be cautious.
   Native loading means native code with the user's full privileges. A
   capability manifest would have to be enforced, not decorative.
4. **Distribution** — "downloadable" needs signing, versioning, update policy,
   and revocation. None of that exists.

Until Phases 1-3 are exhausted, this is not worth doing: the existing IPC
surface already covers the add-on use cases that are actually being asked for,
and it is safe by construction because it holds no code-loading path.

## Recommendation

Ship Phase 1 knowledge (document the IPC surface), then do Phase 2 and Phase 3
as upstreamable pull requests — both small, both self-contained. Hold Phase 5
until there is a concrete add-on that IPC and URL verbs genuinely cannot
express.