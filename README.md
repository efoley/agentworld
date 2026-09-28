# agentworld

A small native window (C++, Dear ImGui, GLFW) with one frog per mudline lead
session. Each frog is either working (typing on a laptop), idle (asleep), or
has a question (hand up, speech bubble, pulsing card).

## Build and run

    cmake -S . -B build && cmake --build build -j
    open build/agentworld.app

CMake fetches GLFW, Dear ImGui and nlohmann/json on the first configure;
nothing needs installing.

    build/agentworld.app/Contents/MacOS/agentworld --status   # print states, no window
    build/agentworld.app/Contents/MacOS/agentworld --demo     # made-up agents in every state

## Where the states come from

Every running Claude Code session keeps `~/.claude/sessions/<pid>.json` with
its name, cwd and live `status`. A frog is shown for each live session whose
cwd is under the workspace (`--root`, `$AGENTWORLD_ROOT`, or
`~/development/mudline-workspace`) and whose name was set by you
(`nameSource: user`), which picks out the leads and skips derived names like
`mudline-eric-35`.

- **working**: status `busy` or `shell`.
- **question**: an idle session whose last turn ended in `AskUserQuestion`, or
  whose closing block (the text after the last blank line) contains a real
  `?`. `?` in query strings and `code` spans don't count. Any status other than
  busy/shell/idle is treated as waiting on you too.
- **idle**: everything else.

Only the last 512 KB of each transcript is read, and only when it has changed.
Hover a frog to see the closing block it was judged on. When a frog starts
asking and the window isn't focused, the Dock icon bounces.

## Right-clicking a frog

Left-clicking does nothing. Right-click a frog for:

- **Open in Claude Desktop** opens `claude://code/<bridgeSessionId>`, the
  session's Remote Control id from its registry file. The item is greyed out
  for sessions without Remote Control.
- **Show in terminal** brings the session's terminal tab to the front.

Right-click the empty pond for *Always on top* and *Quit*.

For **Show in terminal**:

- **Ghostty 1.3+ on macOS** works out of the box through its AppleScript
  dictionary. The app looks for the terminal titled `<glyph> <session name>`
  (Claude Code sets this title), and falls back to the first terminal in the
  session's directory. macOS asks once for permission to control Ghostty. An
  ad-hoc-signed rebuild may ask again.
- **Anything else:** put an executable at `~/.config/agentworld/focus`. It is
  run with `<session name> <cwd> <claude pid> <tty>` and should exit 0 when it
  found the pane, or non-zero to fall back to Ghostty. The tty makes the
  multiplexers easy. These sketches have not been tested here:

  ```sh
  # tmux
  pane=$(tmux list-panes -a -F '#{pane_tty} #{pane_id}' | awk -v t="$4" '$1==t {print $2}')
  [ -n "$pane" ] || exit 1
  tmux switch-client -t "$pane" && tmux select-window -t "$pane" && tmux select-pane -t "$pane"
  open -a Ghostty   # or whichever terminal hosts the tmux client

  # WezTerm
  pane=$(wezterm cli list --format json | jq -r --arg t "$4" '.[] | select(.tty_name==$t) | .pane_id')
  [ -n "$pane" ] || exit 1
  wezterm cli activate-pane --pane-id "$pane" && open -a WezTerm
  ```
