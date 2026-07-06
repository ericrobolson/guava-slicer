/// @file command.h
/// @brief Command pattern base class and undo/redo stack.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace command {

/// @brief Abstract base for all undoable commands.
struct Command {
    virtual ~Command() = default;

    /// @brief Apply this command's mutation.
    virtual void execute() = 0;

    /// @brief Reverse this command's mutation.
    virtual void undo() = 0;

    /// @brief Human-readable description of this command (e.g., "Rotate X 90°").
    virtual std::string name() const = 0;
};

/// @brief Manages undo/redo stacks for executed commands.
///
/// Pushing a new command executes it and clears the redo stack.
/// Undo pops the most recent command and moves it to the redo stack.
/// Redo pops from the redo stack and re-executes.
class CommandStack {
public:
    /// @brief Execute a command and push it onto the undo stack. Clears redo stack.
    void push(std::unique_ptr<Command> cmd);

    /// @brief Undo the most recent command. Returns false if nothing to undo.
    bool undo();

    /// @brief Redo the most recently undone command. Returns false if nothing to redo.
    bool redo();

    /// @brief Whether there are commands to undo.
    bool can_undo() const;

    /// @brief Whether there are commands to redo.
    bool can_redo() const;

    /// @brief Name of the last executed command, or empty string if stack is empty.
    std::string last_undo_name() const;

    /// @brief Name of the last undone command (next redo), or empty string.
    std::string last_redo_name() const;

    /// @brief Number of commands on the undo stack.
    uint32_t undo_count() const;

    /// @brief Number of commands on the redo stack.
    uint32_t redo_count() const;

    /// @brief Clear both stacks.
    void clear();

private:
    std::vector<std::unique_ptr<Command>> undo_stack_;
    std::vector<std::unique_ptr<Command>> redo_stack_;
};

} // namespace command
