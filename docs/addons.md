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

The client already registers `omachat://` as a desktop handler
(`packaging/desktop/org.omarchy.OmaChat.desktop`) and dispatches it in
`AppController::openLink` (`client/src/controllers/AppActions.cpp:522`). Only
`omachat://invite/` exists today; anything else is refused with
"Unsupported OmaChat link".

This is a real third-party entry point with a confirmation gate already built
(`client/qml/Main.qml:99` — invite links always ask first). Adding verbs is
additive and low-risk.

Candidate verbs, in rough priority order:

| Verb | Effect | Confirmation |
|---|---|---|
| `omachat://join/CHANNEL` | Focus a channel in the current server | No — read-only navigation |
| `omachat://account/N` | Switch to the Nth connected account | Yes |
| `omachat://settings/SECTION` | Open Settings at a section | No |
| `omachat://voice/join` | Join the active voice channel | Yes — it captures the mic |

Rules for any new verb:

- **Never** execute a verb from a link without confirmation if it mutates
  state, captures a device, or sends a message. The invite precedent is the
  model: parse, describe in plain language, ask.
- Reject unknown verbs with the existing notice rather than silently ignoring
  them, so a typo is visible instead of a dead click.
- Keep the scheme free of credentials and tokens beyond the existing invite
  token, which is already single-use and server-validated.
- A verb must never widen what a link can do on the server: everything still
  goes through the daemon and its permission checks.

## Phase 3 — a real local theme override (small, upstreamable)

`ThemeProvider` reads `$XDG_STATE_HOME/omarchy/current/theme/*.toml`
(`client/src/platform/ThemeProvider.cpp:90`) and accepts `background`,
`foreground`, `accent`, `selection`, `red`, `green`, `yellow` (`loadFile`,
`ThemeProvider.cpp:153`). It file-watches and live-reloads.

The gap: that is the **Omarchy** theme file, so editing it recolors the whole
desktop, not just OmaChat. There is no OmaChat-local theme path.

A minimal change — read an OmaChat-local file first, fall back to Omarchy —
gives per-application theming with no rebuild and no server involvement. This
is the cheapest route to "perks only I see" and is worth proposing upstream.

Also available locally today, no code needed:
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