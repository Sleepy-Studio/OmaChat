# omachat add-ons and extension points

Status: design and staged plan, 2026-10-02. Nothing in Phases 2-5 is
implemented; this records what omachat actually offers today, what can be built
on it without changing the architecture, and what a real plugin system would
cost.

## Baseline: there is no plugin system

Verified against the source, not assumed:

- **No native plugin loading anywhere.** No `dlopen`, `QLibrary`, or
  `QPluginLoader` in the tree; no "plugin" identifier in `client/src/`,
  `client/qml/`, `daemon/src/`, or `server/src/`.
- **QML is compiled into the binary.** `qt_add_qml_module(omachat URI omachat)`
  (`client/CMakeLists.txt`) AOT-compiles every `.qml`; `client/src/main.cpp:72`
  loads the root with `engine.loadFromModule("omachat", "Main")`. There is no
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
handler (`packaging/desktop/org.omarchy.omachat.desktop`) and dispatches it in
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
whole desktop, not just omachat. `reload()` now resolves an omachat-local file
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
# ~/.config/omachat/theme.toml — omachat only
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

## Phase 6 — candidate verbs, surveyed

A survey of what the `omachat://` scheme could carry, and what each would
require. None of these are implemented; the shipped verbs remain `invite`,
`join` and `server`. Tier is the safety class a verb would need:

| Tier | Meaning |
|---|---|
| **N** | Navigation / read-only. No confirmation. |
| **C** | Mutates state or sends. Confirmation required. |
| **D** | Captures a device (mic, camera, screen). Confirmation **plus** a persistent indicator. Never auto-join. |
| **A** | "Armed": safe unconfirmed **only** if the caller can prove a local trusted origin (see the origin rule below). |

### 6.1 Navigation (tier N)

| Verb | Effect | Notes |
|---|---|---|
| `join/<channelId>` | Focus a text channel | **Shipped** |
| `server/<serverId>` | Focus a server | **Shipped** |
| `channel/<id>?server=<id>` | Server-qualified channel | IDs are globally unique today, but qualifying them is cheap insurance |
| `dm/<userId>` | Open a DM or group thread | Needs a DM index in the client model |
| `message/<channelId>/<messageId>` | **Scroll to and highlight a message** | The highest-value verb: unlocks notifications and cross-app links at once |
| `mention/me` | Next unread mention | The most-used navigation target in any chat app |
| `unread`, `next-unread?server=` | Next unread, cycling servers | |
| `home` | DM / inbox home view | |
| `search?q=…&scope=server` | Search pre-loaded with a query | **Query from a link must land in a search box, never a composer** |
| `settings/<section>` | Focus a settings section | Trivial and safe; powers help docs |
| `profile/me`, `profile/<userId>` | Own profile, or another user's card | |
| `members/<channelId>`, `pins/<channelId>`, `files/<channelId>` | Side panels | |
| `compose/<channelId>` | Focus an empty composer without scrolling the message list | Speed-of-use and an accessibility win |
| `voice/<voiceChannelId>` | Focus a voice channel **without** connecting | Connecting is a separate, tier-D verb |

### 6.2 Lifecycle and moderation (tier C)

| Verb | Effect | Notes |
|---|---|---|
| `invite/<token>` | Join a server | **Shipped**, confirmation is the precedent |
| `create-channel/<serverId>?name=&topic=` | Create a channel | Confirm must show the resolved name **and** topic |
| `add-friend/<userId>` | Send a friend request | This sends a real message to a human; treat exactly like a DM |
| `leave/<serverId>` | Leave a server | Destructive, and links arrive from anywhere |
| `delete-message/<channelId>/<messageId>` | Delete by permalink | Irreversible |
| `pin/…`, `mute/<channelId>`, `read/<channelId>` | Small persistent state changes | `read` scoped to the focused channel could be tier A |

### 6.3 Composition — prefill, never send (tier C)

The category where a URL scheme most easily becomes a remote-input channel.

| Verb | Effect | Notes |
|---|---|---|
| `send/<channelId>?prefill=…` | Open the composer with text **loaded but not sent** | **This is the right shape for every automation link** |
| `reply/<channelId>/<messageId>?prefill=…` | Prefill a reply | |
| `react/<channelId>/<messageId>?emoji=` | Add a reaction | Visible but cheap; the first send-like verb that could reasonably be tier A |
| `attach?path=&to=&prefill=` | Stage a local file without uploading | **Needs a path allowlist** — a link must never be able to name `~/.ssh/id_ed25519` |
| `forward/<channelId>/<messageId>?to=` | Forward a message | |

**The rule: make `?text=` (auto-send) a rejected or confirm-gated verb, and
prefer `?prefill=`.** Every automation use case below wants prefill; none
needs auto-send. Refusing `?text=` outright removes an entire class of
remote-input attack at zero cost, because a URL that can post as you is a
phishing primitive — `<a href>` on any web page could trigger it.

### 6.4 Voice, video and screen capture (tier D)

| Verb | Effect | Notes |
|---|---|---|
| `voice/join/<id>` | Connect to a voice channel | Confirm naming the channel; the mic indicator must persist |
| `voice/join/<id>?deafen` | Join deafened | A common "join quietly" keybind |
| `voice/join/<id>?ring=false` | Join without a ring notification | For scheduled standups |
| `voice/mute`, `voice/deafen` | Toggle local audio | Device state, so a confirm would be noise; scope to the active session |
| `voice/leave` | Leave the current voice channel | **Tier N** — a disconnect is the safe direction |
| `screen-share/start/<id>` | Begin a screen share | **Consider refusing from links entirely.** Expose it as a keybind → `omachatctl`, which is local, deliberate and auditable |

### 6.5 Notifications and alerting

**Shipped: clickable notifications.** `Notifier::notify` used to pass an empty
action list and never subscribed to `ActionInvoked`, so a notification had no
click behaviour at all. Now:

- a message notification carries a `"default"` action, which is what makes the
  notification **body** clickable, not just a button
- the daemon subscribes to `ActionInvoked` in its constructor (subscribing late
  would silently drop clicks whenever the notification server started after it)
- the action key is `channel:<id>`; on click the daemon re-checks the channel is
  still readable and broadcasts the existing `ui.navigate` event, so the GUI
  needs no notification-specific code
- **with no client connected** there is nobody to receive that broadcast, so the
  daemon starts one with `omachat://join/<id>`. The client's single-instance
  handoff makes this harmless if a window appears in between. Launching does not
  make the daemon GUI-capable: it stays headless (`Qt6::Core/Network/Sql/DBus`)
  and only calls `QProcess::startDetached`, the same pattern `xdg-open` already
  uses in `OAuthLoginFlow.cpp`.

The flow, and the answer to "how does a notification arrive if the app is
closed": the **daemon** is what receives messages, not the GUI. `omachatd` runs
headless from `omachat.service` on login, holds the session, and speaks D-Bus to
the notification server. The window is the heavy part and is optional.

| Verb | Direction | Notes |
|---|---|---|
| `message/<channelId>/<messageId>` | **inbound** | Click-to-jump to a specific message. **Blocked** — see below |
| `mention/me` | inbound | Click on a mention notification |
| `mark-read/<serverId>`, `snooze/<channelId>?minutes=` | inbound | Notification action buttons |
| `alert/<channelId>?title=&body=` | **outbound** | A monitoring script raises an in-app notification attributed to a channel. Needs its own rate limit and a visible source label, or it becomes "any process can pop arbitrary text in the user's face" |

**Why `message/` is blocked.** Jump-to-message needs the message body, and the
client only has the id. The obvious approach — page backwards using the id as a
`before` bound — **does not work**, measured against a live server. `messagePage`
pages by `(created_at, id)` compared against the *anchor message's own
timestamp* (`Store.cpp:1067-1070`), not a raw id bound. `before = target`,
`target±1` all returned pages entirely older than the target, while `before = 0`
contained it, because snowflake ids are timestamp-derived and sparse. A correct
implementation needs a **server-side get-message-by-id that checks channel
access** — protocol work, not a client patch.

**Pager ping (not implemented).** A member can send a "pager" that raises a
notification offering to join a voice channel — not a call that connects
automatically. This is a legitimate, common pattern and it does *not* hit the
`message/` blocker, because joining needs only a channel id:

- `omachat://voice/<voiceChannelId>` focuses the voice channel **without**
  connecting (tier N), so a ping can surface the channel with no device capture
- the actual connect is a separate tier-D action, so the user still decides
- the pager action needs its own rate limit, a per-user opt-in, and a server-side
  permission check — any member must not be able to make every client capture its
  microphone on demand
- consider a global "quiet hours" or per-channel opt-out, since a ping that
  interrupts is a paging primitive by design

### 6.6 Automation, webhooks and orchestration

These are not user-clicked. They arrive from `cron`, systemd timers, CI and
webhooks. The right primitive is a **local IPC call with a scoped token**
(what `omachatctl` already is), not a URL any process can fire.

| Use | Example | Tier |
|---|---|---|
| Deploy finished | `send/<id>?prefill=…` from a cron script | C, or A with origin |
| CI failure | `alert/…` from a webhook | A with a per-channel token |
| Green-check on a commit | `react/…/…?emoji=✅` | A with a token — lowest-friction legitimate automation |
| Nightly noise suppression | `read/<id>` | A |
| Standup timer | `voice/join/<id>?ring=false` | D, and it needs a real user opt-in setting, not just a confirm |
| Focus mode | `mute/<id>` from a deep-work script | A (local, reversible) |
| Pomodoro end | `unread` | N |

### 6.7 Cross-application deep links

| Source | Verb | Tier |
|---|---|---|
| Browser extension (GitHub, GitLab, Jira) | `message/…` | N |
| Wiki or docs page | `join/<id>` | N |
| **Email body** | `invite/<token>` | C — the existing flow, and the shape where the confirm earns its keep |
| CRM or contact record | `dm/<userId>` | N |
| Calendar event description | `join/<id>` | N |
| Project template repo | `create-channel/<id>?name=&topic=` | C |
| **omatube share in a channel** | `omatube://s/…` | N — **shipped**, see below |

#### omatube: a shipped cross-application link

`omatube://` is the one non-`omachat` scheme in the link allowlist, and it is
there for a concrete reason: omatube shares videos as links, and a share posted
in a channel would otherwise render as an unclickable wall of base64.

**omachat does not download anything, and this is not a small thing.** It has no
yt-dlp, no media library and no fetch path. Clicking the link calls
`QDesktopServices::openUrl`, which hands a *string* to whichever application
registered `x-scheme-handler/omatube`; omatube then decides what it means and
does the fetching on its own machine. That is the whole design: a share is a
trigger, not a transfer, and keeping the fetch in one application is what lets
the other one be closed.

Three properties worth preserving if this is ever touched:

1. **Click-only.** `main.cpp` filters argv to `omachat://` alone, so an
   `omatube://` URL is never acted on at launch. It opens *only* from a
   deliberate click on the rendered link. Rendering a message does nothing, so
   reading a channel full of shares cannot start a download or steal focus.
2. **The scheme is listed twice** — `MarkdownRenderer::isSafeUrl` and the `urlRe`
   alternation. A scheme present in only one is silently broken:
   allowlisted-but-unmatched is dead code, matched-but-rejected is plain text.
   `check-omatubelink.py` compares them and
   `Markdown.OmatubeShareLinksBecomeClickable` proves a real link comes out as an
   `<a href>`.
3. **It takes focus, on purpose.** Opening omatube raises its window. That is
   acceptable *only* because it is reachable from a click the user made. Do not
   add it to the argv filter, and do not auto-open on receipt.

#### Verifying a change here

```bash
./build/tests/omachat_unit_tests --gtest_filter='Markdown.*'   # behavioural
python3 check-omatubelink.py                                  # the three sites agree
clang-format --dry-run --Werror client/src/text/MarkdownRenderer.cpp  # CI enforces this
```

The last one is not optional: the `format` job in CI runs clang-format with
`--Werror`, so a hand-wrapped line fails the build even though it compiles and
passes every test. Run `clang-format -i` on the files you touched.

`check-omatubelink.py` also runs in CI (the `omatube-links` job), so the three
declarations cannot drift apart without a red build.

### 6.8 Sharing (outbound link generation)

Generating links is the other direction, and it has a constraint the
navigation verbs do not.

| Link | Notes |
|---|---|
| Copy link on a message | **Needs a signed permalink token, not a raw ID** — see the snowflake note below |
| Copy link on a thread | Same |
| `invite/<token>?max_uses=1&expires=` | Matches what self-hosting already supports |

**Snowflake IDs are sequential and timestamp-decodable.** `Snowflake.hpp:11-15`
packs a 41-bit millisecond timestamp, a 10-bit node id and a 12-bit sequence,
so an id reveals when it was created and roughly how many objects exist since.

- **Navigation is safe** — `join` and `server` re-validate against
  `m_channelsById` / `m_serversById`, so a guessed id you cannot read simply
  fails.
- **Sharing is not** — a raw-id permalink leaks channel existence, message
  count and creation time to anyone holding the link. Share links need a
  signed, scoped token.

### 6.9 App lifecycle (tier C, and mostly refusable)

`quit`, `relaunch`, `window/close`, `clear-cache`, `new-window` (needs
multi-window support; `SingleInstance` currently collapses it),
`export/<serverId>` (writes a file — always a save dialog, never a
link-supplied path).

**Consider refusing `quit` outright.** It is trivial to trigger from a web
page once the scheme is registered, and there is a keybind for it.

### 6.10 What any implementation must satisfy

These fall out of the survey, and they apply to every verb above:

1. **Prefill, never auto-send.** Covered in 6.3.
2. **A bare `omachat://` URL has no caller identity.** The desktop entry
   declares `MimeType=x-scheme-handler/omachat`, so *any web page* can trigger
   *any verb*. Any verb that skips confirmation therefore needs an origin
   check, not merely a judgement that it is "low risk".
3. **Render the resolved identity, never the link's spelling.** Every
   confirmation dialog must show resolved server/user/channel names and the
   decoded, length-capped, escaped payload — not the raw query string. This
   closes `create-dm?name=Ada`-style impersonation and `%0A` injection.
4. **No credentials in the scheme beyond the invite token.** URLs end up in
   shell history, `ps` and logs. Automation uses scoped per-channel tokens
   over IPC, never a bearer secret in a URL.
5. **Rate-limit inbound links.** A loop firing `omachat://` URLs must not be
   able to queue 500 confirmation dialogs. Cap the pending queue and collapse
   duplicates.
6. **Screen capture from a link is the one verb to consider refusing.**

### 6.11 Ranked next steps

| Rank | Verb | Why |
|---|---|---|
| 1 | `message/<channelId>/<messageId>` | Unlocks notifications and cross-app links simultaneously |
| 2 | `mention/me` | Most-used navigation target in any chat app |
| 3 | `send/<id>?prefill=` | Every automation integration, with no auto-send risk |
| 4 | Notification default action → `message/…` | Biggest UX win; depends on #1 |
| 5 | `settings/<section>` | Trivial and safe |
| 6 | Copy ID / Copy link in menus | Makes the whole scheme self-service; today there is no GUI way to discover an id |

## Recommendation

Ship Phase 1 knowledge (document the IPC surface), then do Phase 2 and Phase 3
as upstreamable pull requests — both small, both self-contained. Hold Phase 5
until there is a concrete add-on that IPC and URL verbs genuinely cannot
express.

## Local receive audio event

The local daemon provides an opt-in `voice.audio` event for trusted same-user
IPC consumers. A client must
subscribe with the exact `voice.audio` topic; an empty or broad subscription
does not receive audio. Frames contain 20 ms of the decoded and mixed remote
call audio as 48 kHz mono float32 little-endian PCM, base64 encoded. The mix is
decrypted voice and must be treated as sensitive. Consumers should not record
it and should clearly notify call participants when relaying it to another
service.
