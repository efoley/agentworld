#pragma once
#include <string>
#include <vector>

enum class AgentState { Working, Idle, Question };

struct Agent {
    std::string name;     // session name, e.g. "mudline-pm"
    std::string status;   // raw status from the session registry
    AgentState state = AgentState::Idle;
    std::string detail;   // the pending question or last message, for the tooltip
};

// Reads ~/.claude/sessions for live, user-named sessions whose cwd is under
// `root`, and classifies each as working, idle, or waiting on a question.
std::vector<Agent> readAgents(const std::string& root);

const char* stateName(AgentState s);
