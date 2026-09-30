# OmaChat for the Omarchy bar

Shows your OmaChat voice channel and speaker count in the bar, with a compact
panel for mute, deafen, leave and "open OmaChat".

- Left click: open the panel (`m` mute, `d` deafen, `l` leave, `o` open app, `Esc` close)
- Middle click: toggle mute
- Right click: open OmaChat

The widget talks to `omachatd` over its per-user socket
(`$XDG_RUNTIME_DIR/omachat/omachat.sock`) and receives pushed status events —
it never polls or spawns processes for live state, and it holds no
credentials. If `omachatd` is not running it shows a disconnected state.

Install / remove:

    omachat-omarchy-plugin install
    omachat-omarchy-plugin uninstall
