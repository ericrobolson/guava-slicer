/// @file test_command.cpp
/// @brief Tests for Command base and CommandStack.
#include <doctest.h>
#include "command.h"

namespace {

/// @brief Test command that increments/decrements a counter.
struct CounterCommand : command::Command {
    int& counter;
    int delta;
    std::string label;

    CounterCommand(int& c, int d, std::string l)
        : counter(c), delta(d), label(std::move(l)) {}

    void execute() override { counter += delta; }
    void undo() override { counter -= delta; }
    std::string name() const override { return label; }
};

} // namespace

TEST_CASE("CommandStack push executes command") {
    command::CommandStack stack;
    int counter = 0;

    stack.push(std::make_unique<CounterCommand>(counter, 5, "add 5"));

    CHECK(counter == 5);
    CHECK(stack.can_undo());
    CHECK_FALSE(stack.can_redo());
    CHECK(stack.undo_count() == 1);
    CHECK(stack.last_undo_name() == "add 5");
}

TEST_CASE("CommandStack undo reverses command") {
    command::CommandStack stack;
    int counter = 0;

    stack.push(std::make_unique<CounterCommand>(counter, 5, "add 5"));
    CHECK(stack.undo());

    CHECK(counter == 0);
    CHECK_FALSE(stack.can_undo());
    CHECK(stack.can_redo());
    CHECK(stack.redo_count() == 1);
    CHECK(stack.last_redo_name() == "add 5");
}

TEST_CASE("CommandStack redo re-executes command") {
    command::CommandStack stack;
    int counter = 0;

    stack.push(std::make_unique<CounterCommand>(counter, 5, "add 5"));
    stack.undo();
    CHECK(stack.redo());

    CHECK(counter == 5);
    CHECK(stack.can_undo());
    CHECK_FALSE(stack.can_redo());
}

TEST_CASE("CommandStack push clears redo stack") {
    command::CommandStack stack;
    int counter = 0;

    stack.push(std::make_unique<CounterCommand>(counter, 5, "add 5"));
    stack.push(std::make_unique<CounterCommand>(counter, 3, "add 3"));
    stack.undo();
    CHECK(stack.can_redo());

    stack.push(std::make_unique<CounterCommand>(counter, 7, "add 7"));
    CHECK_FALSE(stack.can_redo());
    CHECK(stack.undo_count() == 2);
    CHECK(counter == 12); // 5 + 7
}

TEST_CASE("CommandStack undo/redo on empty returns false") {
    command::CommandStack stack;

    CHECK_FALSE(stack.undo());
    CHECK_FALSE(stack.redo());
}

TEST_CASE("CommandStack multiple undo/redo") {
    command::CommandStack stack;
    int counter = 0;

    stack.push(std::make_unique<CounterCommand>(counter, 1, "a"));
    stack.push(std::make_unique<CounterCommand>(counter, 2, "b"));
    stack.push(std::make_unique<CounterCommand>(counter, 3, "c"));
    CHECK(counter == 6);

    stack.undo(); // undo c
    CHECK(counter == 3);
    stack.undo(); // undo b
    CHECK(counter == 1);
    stack.redo(); // redo b
    CHECK(counter == 3);
    stack.redo(); // redo c
    CHECK(counter == 6);
}

TEST_CASE("CommandStack clear empties both stacks") {
    command::CommandStack stack;
    int counter = 0;

    stack.push(std::make_unique<CounterCommand>(counter, 1, "a"));
    stack.push(std::make_unique<CounterCommand>(counter, 2, "b"));
    stack.undo();

    stack.clear();
    CHECK_FALSE(stack.can_undo());
    CHECK_FALSE(stack.can_redo());
    CHECK(stack.undo_count() == 0);
    CHECK(stack.redo_count() == 0);
}

TEST_CASE("CommandStack last names empty when stacks empty") {
    command::CommandStack stack;

    CHECK(stack.last_undo_name().empty());
    CHECK(stack.last_redo_name().empty());
}
