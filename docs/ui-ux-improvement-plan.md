# OmaChat UI/UX improvement plan

Last reviewed: 2026-10-02. Conversation-flow improvements shipped in v0.2.2. The first desktop accessibility continuation is implemented; later priorities remain planned.

## Implementation status

- **Source complete for Priority 1:** Compact windows place channels and members in temporary drawers; the bottom bar keeps its key controls visible. The composer has a pointer-accessible Send/Save action and a shortcut hint. Empty/error states have contextual actions. Search shows in-context progress/errors and opens a matching message with earlier history plus a clear return to latest. The first Priority 2 item also has a keyboard path: focus the message list, choose a message with arrow keys, and press Enter for actions.
- **Verified:** Full build, QML lint, and 125 unit tests pass. A live isolated sandbox rendered the real main view at 720×460 and 1200×760 using the development screenshot mode; the compact chat and composer are visible without clipping.
- **Still to validate:** Physical pointer and keyboard click-through for drawers, sending, and search; a current light-theme and 1.5× scale pass; voice controls while an actual call is connected. No installed package or hosted deployment was changed.

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
registration warning remains). Full desktop workflow and physical two-machine
voice/screen-sharing acceptance are still open.

No installed package or hosted server is changed by this source continuation.

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
