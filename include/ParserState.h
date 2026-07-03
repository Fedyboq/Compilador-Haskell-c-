#pragma once

#include <functional>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <string>
#include <utility>

// Forward declaration of ParserState so Rule can reference it
struct ParserState;

// A Rule in APEG is represented as a function that attempts to parse the input,
// modifying the ParserState on success, and returning true/false.
using Rule = std::function<bool(ParserState&)>;

struct UserRule {
    std::string name;
    Rule rule;
};

struct UserGrammar {
    std::string name;
    std::vector<UserRule> rules;
};

using GrammarMap = std::unordered_map<std::string, UserGrammar>;

/**
 * @brief Represents the parsing state, including the input text cursor
 * and the layered/scoped rule environment characteristic of APEGs.
 */
struct ParserState {
    std::string_view input;
    size_t cursor = 0;
    
    // Scoped environments: a stack of maps from rule name to Rule.
    // The top of the stack (back of the vector) is the most local scope.
    std::vector<std::unordered_map<std::string, Rule>> env;

    GrammarMap defined_grammars;

    /**
     * @brief A Checkpoint captures the current cursor and a snapshot of
     * the scoped rule environment to support full rollback on backtracking.
     */
    struct Checkpoint {
        size_t cursor;
        std::vector<std::unordered_map<std::string, Rule>> env;
        GrammarMap defined_grammars;
    };

    /**
     * @brief Constructor initializing with input.
     * Starts with a default global scope layer.
     */
    explicit ParserState(std::string_view in) : input(in) {
        env.emplace_back(); // Push initial global scope layer
    }

    // --- Cursor Navigation & Inspection ---
    
    /**
     * @brief Checks if there are more characters left to parse.
     */
    bool has_more() const {
        return cursor < input.length();
    }

    /**
     * @brief Returns the character at the current cursor position.
     */
    char peek() const {
        if (!has_more()) return '\0';
        return input[cursor];
    }

    /**
     * @brief Advances the cursor by the given number of steps.
     */
    void advance(size_t steps = 1) {
        cursor += steps;
    }

    // --- Scoping & Rule Environment Management ---

    /**
     * @brief Enters a new scope layer (adds a new empty map to the stack).
     */
    void push_scope() {
        env.emplace_back();
    }

    /**
     * @brief Leaves the current scope layer. The global scope (index 0) cannot be popped.
     */
    void pop_scope() {
        if (env.size() > 1) {
            env.pop_back();
        }
    }

    /**
     * @brief Registers a rule in the current (innermost/top) scope layer.
     */
    void register_rule(const std::string& name, Rule rule) {
        env.back()[name] = std::move(rule);
    }

    /**
     * @brief Searches for a rule by name starting from the innermost scope down to global scope,
     * and executes it if found.
     * @return true if the rule was found and succeeded; false otherwise.
     */
    bool execute_rule(const std::string& name) {
        // Search from top (rbegin) to bottom (rend) of the stack
        for (auto it = env.rbegin(); it != env.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                // Execute the rule on this state
                return found->second(*this);
            }
        }
        return false; // Rule not found in any scope
    }

    bool has_rule(const std::string& name) const {
        for (auto it = env.rbegin(); it != env.rend(); ++it) {
            if (it->find(name) != it->end())
                return true;
        }
        return false;
    }

    // --- Backtracking Checkpoints ---

    /**
     * @brief Captures the current state (cursor and environment stack) for backtracking.
     */
    Checkpoint save() const {
        return Checkpoint{cursor, env};
    }

    /**
     * @brief Restores the state to a previously saved checkpoint.
     */
    void restore(const Checkpoint& cp) {
        cursor = cp.cursor;
        env = cp.env;
    }
};
