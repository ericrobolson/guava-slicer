/// @file command.cpp
/// @brief CommandStack implementation.
#include "command.h"

namespace command {

void CommandStack::push(std::unique_ptr<Command> cmd) {
    cmd->execute();
    undo_stack_.push_back(std::move(cmd));
    redo_stack_.clear();
}

bool CommandStack::undo() {
    if (undo_stack_.empty()) return false;
    auto cmd = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    cmd->undo();
    redo_stack_.push_back(std::move(cmd));
    return true;
}

bool CommandStack::redo() {
    if (redo_stack_.empty()) return false;
    auto cmd = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    cmd->execute();
    undo_stack_.push_back(std::move(cmd));
    return true;
}

bool CommandStack::can_undo() const { return !undo_stack_.empty(); }
bool CommandStack::can_redo() const { return !redo_stack_.empty(); }

std::string CommandStack::last_undo_name() const {
    if (undo_stack_.empty()) return "";
    return undo_stack_.back()->name();
}

std::string CommandStack::last_redo_name() const {
    if (redo_stack_.empty()) return "";
    return redo_stack_.back()->name();
}

uint32_t CommandStack::undo_count() const {
    return static_cast<uint32_t>(undo_stack_.size());
}

uint32_t CommandStack::redo_count() const {
    return static_cast<uint32_t>(redo_stack_.size());
}

void CommandStack::clear() {
    undo_stack_.clear();
    redo_stack_.clear();
}

} // namespace command
