# OmaChat UI/UX improvement plan

Last reviewed: 2026-10-04. Conversation-flow improvements shipped in v0.2.2. Desktop appearance, persistent operation feedback and administration safeguards are implemented; minimum-size dark/light conversation acceptance and artwork loading stability pass; compact administration and drawer/focus acceptance also pass.

## Resume and user acceptance

Resume from [desktop-session-handoff.md](desktop-session-handoff.md).
On 2026-10-02 the user confirmed desktop voice and streaming work and marked that
acceptance checkpoint complete. Persistent upload/failed-send/reconnect feedback
is now implemented in the working tree. Channel creation and unsaved-settings protection are also implemented. Minimum-size dark/light conversation, keyboard search and recovery acceptance now pass.
The refreshed local desktop package is installed October 4. The subsequent source continuation adds measured theme/shared-control states, keyboard/layout polish, scoped bounded artwork caching and conservative safe category deletion; see the handoff for verification. Previous implementation commit `92ec294` has green
native and Android GitHub CI; new source has local verification only.

The theme audit covers 22 locally installed palettes, portable representative
snapshots and adversarial selections: normal/action text 4.5:1 and meaningful
focus/input cues 3:1. Disabled controls are intentionally dimmed. Role-custom
colors, artwork overlays and independent human acceptance remain separate.
Future restart-persistent send recovery is scoped in
[desktop-send-recovery-design.md](desktop-send-recovery-design.md), not implemented.

The parallel source continuation passes clean Release configure/build,
**236/236 CTest**, both QML lint targets (existing warnings), and native Wayland
keyboard/conversation/search/sidebar acceptance. The dark/light compact/wide
matrix passes at 100%/150%, with one-pixel compositor rounding permitted for the
wide size. All 22 installed palettes pass the native shared-control audit.
Evidence is retained in `build/desktop-continuation-validation/evidence/`.
Current source is uncommitted and uninstalled; earlier package artifacts do not
contain these later changes.

## Next-session execution preference — 2026-10-04

The user explicitly requests parallel subagents to complete as much remaining
work as practical efficiently. Follow the ownership, first-wave assignments,
refill queue and integration gates in
[desktop-session-handoff.md](desktop-session-handoff.md#next-session-parallel-execution--explicit-user-direction-2026-10-04).
Start with theme/shared controls, desktop keyboard/polish and channel artwork
cache work in independent lanes; the parent coordinates integration. Advance
Android and security in isolated later lanes where prerequisites permit.

## Local checkpoint installed — 2026-10-04

The user authorizes installation now. Refreshed main/debug 0.2.2-1 packages are
installed, byte-verified against the retained artifacts and host-integrity-checked
with zero altered paths. The daemon is active and connected; the updated client
is open and visible under native Wayland. Voice is disconnected for the user to
rejoin. No hosted deployment, Git commit/push or public release is performed.

## Compact administration and keyboard acceptance — 2026-10-03

Role settings uses a compact selector and scrolling form with fixed actions.
Focused colors/permissions scroll into view; role selection, colors and member
assignment chips support keyboard activation. Member drawer/profile and composer
emoji flows restore usable focus on closure. Composer/reaction emoji pickers fit
the window; search and Unicode/custom grids support keyboard selection. Compact
custom emoji upload and Delete controls fit the visible panel.

The real production-QML/controller/server/daemon matrix passes at actual
720×460/150% and 1440×900/100% sizes in dark/light, offscreen and native Wayland.
It exercises a second member, assignment/removal, custom emoji deletion,
color/permission editing with Revert, emoji insertion and drawer/profile return.
Captures are inspected. Full 211-test pass preceded final focus/reaction additions;
all 12 operation tests pass afterwards, with both QML lint targets (existing
warnings). Release package checks pass 209/209 plus 2/2 theme tests; the final refreshed
Release GUI suites pass 14/14 and the extracted executable passes a native
Wayland live-sandbox launch. The verified local package is installed October 4
with explicit user authorization.

## Implementation status

- **Source complete for Priority 1:** Compact windows place channels and members in temporary drawers; the bottom bar keeps its key controls visible. The composer has a pointer-accessible Send/Save action and a shortcut hint. Empty/error states have contextual actions. Search shows in-context progress/errors and opens a matching message with earlier history plus a clear return to latest. The first Priority 2 item also has a keyboard path: focus the message list, choose a message with arrow keys, and press Enter for actions.
- **Verified:** Full build, QML lint, and 125 unit tests pass. A live isolated sandbox rendered the real main view at 720×460 and 1200×760 using the development screenshot mode; the compact chat and composer are visible without clipping.
- **Still to validate:** Independent human acceptance, custom role colors and artwork overlays. Theme token/shared-control coverage includes all 22 installed palettes. Compact role/member/emoji and drawer/focus acceptance pass. Desktop voice and streaming are user-confirmed working. The earlier October 4 package remains installed; subsequent source changes and the hosted server are not installed/deployed.

For repeatable visual checks, `omachat --screenshot /tmp/omachat.png` remains the self-contained CI smoke test. Set `OMACHAT_SCREENSHOT_LIVE=1` with `OMACHAT_SOCKET` pointing to an isolated test daemon to capture real chat; `OMACHAT_SCREENSHOT_WIDTH`, `OMACHAT_SCREENSHOT_HEIGHT`, and `OMACHAT_SCREENSHOT_DELAY_MS` control the capture. Do not point this mode at a production account for test fixtures.

## Desktop accessibility continuation — 2026-10-02

Settings now exposes an Appearance tab with live interface scale, reduced motion,
reset, and persistent save/error feedback. Preferences are saved atomically by
`config.set_ui`; invalid requests and failed writes retain the prior state.
Saved app scale now uses the same app metrics at launch and during edits, while
Qt retains the user's desktop display scaling.

This uncovered two existing scale/motion bugs: QML preferred ThemeProvider's
accessible default constructor over its factory, creating a second provider;
and C++ `Theme.px()` did not establish a binding dependency on scale. QML now
uses the instance configured by main(), and the small `Metrics.px()` singleton
reads `Theme.scale` inside each binding. Existing metric calls use that helper.
A regression exercises the real QML factory, startup values, live metric changes,
reduced motion, and engine ownership. Qt's construction rule is documented in
[QML_SINGLETON](https://doc.qt.io/qt-6/qqmlintegration-h.html#QML_SINGLETON).

Small text in Settings, message metadata, and the composer now uses `textMuted`.
That token meets 4.5:1 against the normal background, sidebar, input, and raised
surfaces in the built-in dark palette and the test light palette. A complete
all-theme and selected/disabled-state audit remains open.

Use `OMACHAT_SCREENSHOT_SETTINGS=1` with the existing live-sandbox screenshot
mode to capture the actual Settings dialog. The capture waits for its transition.
Final validation: native build and **199/199 CTest tests** pass, including the
real QML singleton/live-metric/contrast tests and daemon preference validation,
reload, and failed-save rollback. Both QML lint targets finish successfully;
existing warnings elsewhere remain. A temporary native QtTest driver against
an isolated real daemon exercises keyboard scale changes, keyboard reduced
motion, saved preferences, and pointer reset. Actual 1200×760/100% and
720×460/150% dark/light captures are checked; compact Settings uses a section
picker and keeps Reset visible. A Wayland sandbox launch and Settings capture
also succeed (the compositor chooses the final window size; a nonfatal portal
registration warning remains). The full conversation workflow walkthrough remains open. Desktop voice and
streaming acceptance is complete by user confirmation on 2026-10-02.

No installed package or hosted server is changed by this source continuation.

## Persistent operation feedback — 2026-10-02

Composer retains upload progress and terminal outcomes beside the affected
conversation, with Cancel for active transfers and Dismiss for outcomes. A bounded
scrolling area protects the input in narrow layouts. File completion is labelled
separately from message delivery. Active transfers are rediscovered through the
existing daemon `transfer.list`; account-qualified keys prevent cross-account
collisions and stale snapshot reconciliation avoids orphaned progress rows.

Failed sends retain text, file paths, reply and action state without replacing a
newer draft. Safe Retry reuses that operation only after an explicit rejection,
with the original account/channel, current permission and an active connection.
Repeated Retry during a pending send is ignored. Timeout/transport uncertainty
asks the user to check delivery and offers Copy text/Dismiss. StatusBanner keeps a
Review send action available when the user leaves an affected channel.

These recovery records last for the GUI session; disk-backed failed-send recovery
is not implemented. Active transfers remain daemon-owned. The GUI limits retained
send operations to 20 and terminal transfer history to approximately 100 rows.

Native build, **205/205 CTest tests**, both QML lint targets (existing warnings),
and whitespace checks pass. Six new real-daemon/controller tests cover rejection,
retry without duplicate delivery, cancellation, disconnect/reconnect, account and
channel scope, and read-only permissions. Production Composer QML passes pointer
Retry at 150% and keyboard Retry at 100%, offscreen and under native Wayland.
Narrow captures are checked; full-main-window/dark-light/workflow acceptance remains.
Source and docs are uncommitted; no installed package or hosted server changed.

## Administration continuation — 2026-10-02

Channel creation initially shows type, name and category. Optional settings keeps
topic, description, artwork and the accurate access explanation available before
submission. Create waits for acknowledgement; rejected input remains in place.

Server/channel/role saves show persistent results beside their section. Dirty
editors ask on Close/Escape, block outside dismissal, and cannot close while a
save is pending. Role switching asks before discarding a draft. Rejected role
saves retain dirty state; remote updates and save replies preserve newer edits.
Accepted channel/server fields display server-normalized values. Artwork/category
changes still apply immediately, with explanatory text in the editor.

Native build, **208/208 CTest**, both QML lint targets (existing warnings) and
whitespace checks pass. Three production-QML/real-daemon tests cover rejected
input, optional fields, normalized values, remote/newer edits, role switching,
Escape/outside protection and discard decisions. Pointer/Escape flows pass
at 720×460 offscreen (150% creation, 100% settings) and native Wayland; captures
are inspected. Native compositor size differs from the offscreen test window.
Full-main-window/light-theme/conversation/compact-role acceptance remains open.
Source/tests/docs are uncommitted; installed package and hosted server unchanged.

## Conversation and artwork acceptance — 2026-10-02

Server and channel banners reserve geometry while previews download/decode and
retain an unavailable-artwork fallback; failed downloads offer Retry. Category
icons retain a fixed slot with a fallback. Existing server-rail initials and
channel-type fallback icons already preserve their geometry. Decode failures
currently offer the fallback only; the cached file is not forcibly re-downloaded.

Full-window production QML is exercised against the real isolated server/daemon
and controller. Keyboard send/reply, nine-result search, exact-match navigation,
Return to latest, Review send navigation and pointer Dismiss pass at minimum size
(720×460), 150% scale, dark/light palettes, offscreen and native Wayland. Captures
are inspected. Search results now scroll within a window-bounded panel; the
warning banner uses a readable themed surface rather than a dark yellow fill.

Native build, **210/210 CTest**, both QML lint targets (existing warnings), and
whitespace checks pass. Compact role/member/emoji, broader drawer/focus and
all-theme selected/disabled states remain open. A clean Release Arch build passes 208 package checks plus two separately run
clean theme tests. Main/debug packages and verification evidence are preserved in
`build/desktop-checkpoint/` with checksums and uncommitted-source provenance. The
clean executable launches under native Wayland against an isolated real sandbox.
Installation and hosted deployment remain unchanged.

## Product direction

Keep OmaChat a fast, native communications workspace: compact, readable, and keyboard friendly. Use the active Omarchy palette as the visual identity. Spend accent color on selection, focus, speaking, and actions that need attention; keep routine chrome quiet. Preserve the user's place when opening search, settings, and channel details. Add motion only where it explains a state change, using the existing reduced-motion setting.

The existing client already has useful foundations: a virtualized message list, theme and scale support, keyboard shortcuts, focus rings on common buttons, accessible labels in many views, and clear voice status. This plan builds on those foundations rather than replacing the layout or introducing a visual framework.

## Evidence and limits

- Source reviewed: `client/qml/views/`, `client/qml/dialogs/`, `client/qml/pages/`, `client/qml/components/`, `client/qml/Main.qml`, and `ThemeProvider`.
- The repository's `docs/screenshots/main.png` shows a 1200×760 sample. It is useful for overall hierarchy, but newer media and artwork changes may not be represented. Current source was used for behavioral findings.
- No live usability sessions or current multi-size render were run for this planning pass. Visual judgments below need validation against the installed client.

## Priority 1: core conversation flow

| Change | Why it matters | Implementation area | Acceptance check |
|---|---|---|---|
| Make the layout respond to available width. Collapse the member list first, then offer the channel list as a reversible drawer or toggle; keep a useful chat width at the 720×460 minimum. | The rail, channel list, and member list reserve 62 + 236 + 220 scaled pixels before chat, while the member list only auto-hides above/below one width threshold. | `MainView.qml`, `ChannelSidebar.qml`, `MemberList.qml`, `ChatPane.qml` | At minimum size and at 1.5× UI scale, messages, composer, and voice controls remain usable without clipped critical actions; switching panes preserves selection and scroll position. |
| Make sending discoverable without weakening Enter-to-send. Add a visible send action when there is text or an attachment, with the shortcut explained near the composer. | The composer supports Enter and Shift+Enter, but has no visible send control. First-time mouse users have to infer how to send. | `Composer.qml` | A new user can send text and a file with a pointer; Enter and Shift+Enter still work; disabled and sending states are clear. |
| Give search a complete task flow: searching, no results, error, result focus, and navigation to the exact matching message. | Search currently lists results and selects a channel on click; it does not visibly establish that the matching message was reached. | `SearchPanel.qml`, `ChatPane.qml`, message navigation/controller as needed | From a whole-server result, the destination message is visible and highlighted briefly. Keyboard selection works. A failed search offers retry and retains the query. |
| Make empty and failure states actionable. | Empty conversation, no channel, and failed message loading states are mostly descriptive text. | `EmptyState.qml`, `ChatPane.qml`, `ChannelSidebar.qml` | Each state has one relevant action when action is possible: start a conversation, open switcher, send first message, or retry loading. Read-only channels do not imply that posting is available. |

## Priority 2: accessibility and state clarity

| Change | Why it matters | Implementation area | Acceptance check |
|---|---|---|---|
| Give message actions a keyboard path independent of hover. Show the selected message's actions or a context menu on keyboard focus. | `MessageDelegate.qml` reveals its action strip only on hover; the right-click menu is a pointer path. | `MessageDelegate.qml`, `ChatPane.qml` | Tab and arrow-key users can reach a message and reply, react, copy, edit, or delete as permitted. Focus returns predictably after an action. |
| Audit small text and icon contrast across Omarchy themes. Treat `textFaint` as decorative or large-text color, and use a stronger token for 11–12 px timestamps, helper text, and status copy. | `ThemeProvider` targets 3:1 for `textFaint`, while several small text labels use it. | `ThemeProvider`, QML views and dialogs | Normal text reaches 4.5:1, key non-text controls and focus cues reach 3:1, in built-in dark and representative light/dark Omarchy themes. |
| Expose UI scale and reduced motion in Settings, and check focus/target sizes at each scale. | The runtime supports both, but Settings does not expose them; message toolbar buttons can be 28×26 scaled pixels. | `SettingsDialog.qml`, `ThemeProvider`, shared controls | Scale is adjustable in app and survives restart. Reduced motion removes nonessential animation. Pointer targets and keyboard focus remain practical at 1× and 1.5×. |
| Use durable feedback for operations that can outlast a toast: upload, reconnect, send failure, OAuth linking, and screen share. | The global notice is transient, while these actions can need a retry or a clear final state. | `Main.qml`, `Composer.qml`, `StatusBanner.qml`, related dialogs | An operation's outcome remains visible in its context until resolved or dismissed; the user can retry where retry is safe. Screen sharing remains unmistakable when the stream panel is collapsed. |

## Priority 3: complex flows and visual craft

| Change | Why it matters | Implementation area | Acceptance check |
|---|---|---|---|
| Shorten channel creation's first step to type, name, and category. Put topic, description, artwork, and access review behind an optional second step. | The current create dialog presents every field before the first channel exists. | `CreateChannelDialog.qml` | Default channel creation takes only the essential inputs; advanced options remain available before submission; permissions are described accurately. |
| Protect unsaved settings and show save results beside the edited field or section. | Server and channel settings contain editable fields and dirty state, but close and concurrent-update behavior need a focused usability pass. | `ServerSettingsDialog.qml`, `ChannelSettingsDialog.qml`, `SettingsDialog.qml` | Closing a dirty editor asks whether to discard; rejected edits retain input and show the server response; successful saves clearly identify what was saved. |
| Reserve image/banner space while assets load and supply a clear fallback. | Banners are shown only when `Image.Ready`, which can shift surrounding content after a download. | `ChatPane.qml`, `ChannelSidebar.qml`, `ServerRail.qml` | Loading artwork does not move the composer or selected navigation; failed artwork keeps an intentional fallback and can retry. |
| Refine density and motion after task flows pass. Use consistent type sizes, spacing, hover/focus treatment, and short directional transitions for drawers and contextual panels. | The current screen is coherent but some content and controls are visually quiet; motion should clarify where panels came from. | Shared QML controls and views | One visual language across dark/light themes; no decorative gradients or new heavy dependencies; reduced-motion path verified. |

## Delivery order

1. **Baseline:** Capture the current installed client at 1200×760, 720×460, and 1.5× scale in built-in and Omarchy light/dark themes. Walk five tasks: join a channel, send/reply, find an old message, join/leave voice, and create a channel. Record click count, dead ends, focus loss, and clipping.
2. **Conversation pass:** Responsive panes, composer send affordance, search destination, and actionable empty/error states. Validate each task with pointer and keyboard.
3. **Access pass:** Message keyboard actions, contrast, Settings controls for scale/motion, and persistent operation feedback. Verify with accessibility tooling and manual focus traversal.
4. **Administration pass:** Channel creation and settings save/discard flows, then artwork loading stability.
5. **Polish pass:** Tune typography, spacing, and motion against real captures. Update the repository screenshot only after the interface represents the current build.

## Release gate

Run QML lint, client build, and relevant existing tests for changed behavior. Then test the five baseline tasks on a live local sandbox at minimum and normal window sizes, with keyboard only and pointer only. Include offline/reconnect, read-only channel, long names, pending uploads, screen sharing, and reduced motion. For changes that affect media or voice, exercise the installed GUI rather than relying on an offscreen screenshot alone.

Success means a first-time user can complete the five tasks without hidden knowledge, an experienced user keeps the fast keyboard path, and no critical control disappears at the minimum supported window size.
