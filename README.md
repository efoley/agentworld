# agentworld

A small native window (C++, Dear ImGui, GLFW) with one frog per mudline lead
session. Each frog is either working (typing on a laptop), idle (asleep), or
has a question (hand up, speech bubble, pulsing card).

## Build and run

    cmake -S . -B build && cmake --build build -j
    open build/agentworld.app

CMake fetches GLFW, Dear ImGui and nlohmann/json on the first configure;
nothing needs installing. Right-click the window for *Always on top* and *Quit*.

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

