#pragma once
#include "status.h"

// Brings the terminal running `agent` to the front, without blocking the UI.
// An executable ~/.config/agentworld/focus is tried first, with the session
// name, cwd, pid and tty as arguments; if it is missing or exits non-zero,
// Ghostty (macOS) is asked for the terminal titled with the session name, or
// failing that the one in the session's directory.
void focusAgent(const Agent& agent);

// Opens the session in the Claude desktop app via claude://code/<session id>.
// Needs the session's Remote Control id; does nothing without one.
void openInClaudeDesktop(const Agent& agent);
