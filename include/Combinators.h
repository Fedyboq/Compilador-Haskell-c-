#pragma once

#include "ParserState.h"
#include <utility>

/**
 * @brief Matches a single specific character.
 * @param c The character to match.
 * @return A Rule that accepts the character and advances the cursor,
 *         or fails (returns false) without side effects.
 */
inline Rule MatchChar(char c) {
    return [c](ParserState& state) -> bool {
        if (state.has_more() && state.peek() == c) {
            state.advance();
            return true;
        }
        return false;
    };
}

/**
 * @brief Sequential composition combinator (A followed by B).
 * @param a The first rule to execute.
 * @param b The second rule to execute.
 * @return A combined Rule that succeeds only if A succeeds followed by B.
 *         If either rule fails, the entire sequence fails, restoring the
 *         cursor and environment to their states before the sequence started.
 */
inline Rule Sequence(Rule a, Rule b) {
    return [a = std::move(a), b = std::move(b)](ParserState& state) -> bool {
        // Save state checkpoint for backtracking
        auto checkpoint = state.save();
        
        if (a(state)) {
            if (b(state)) {
                return true;
            }
        }
        
        // Backtrack: Restore state to undo any partial matches or environment changes
        state.restore(checkpoint);
        return false;
    };
}

/**
 * @brief Ordered choice combinator (A / B).
 * @param a The first alternative rule to try.
 * @param b The second alternative rule to try.
 * @return A combined Rule that succeeds if A succeeds, or if A fails, backtracks
 *         completely and succeeds if B succeeds.
 */
inline Rule Choice(Rule a, Rule b) {
    return [a = std::move(a), b = std::move(b)](ParserState& state) -> bool {
        auto checkpoint = state.save();
        if (a(state)) {
            return true;
        }
        // Rollback any modifications before trying the second alternative
        state.restore(checkpoint);
        
        if (b(state)) {
            return true;
        }
        // Rollback if both failed
        state.restore(checkpoint);
        return false;
    };
}

/**
 * @brief Kleene star combinator (0 or more matches of A).
 * @param a The rule to match repeatedly.
 * @return A Rule that matches as many times as possible, always returning true.
 *         Ensures no infinite loops if A does not consume input.
 */
inline Rule Many(Rule a) {
    return [a = std::move(a)](ParserState& state) -> bool {
        while (true) {
            auto checkpoint = state.save();
            size_t old_cursor = state.cursor;
            
            if (!a(state)) {
                // If it fails, restore state to undo this last failed match
                state.restore(checkpoint);
                break;
            }
            
            // Loop protection: if no input was consumed, stop immediately
            if (state.cursor == old_cursor) {
                break;
            }
        }
        return true;
    };
}

/**
 * @brief Positive lookahead combinator (&A).
 * @param a The rule to check.
 * @return A Rule that returns the success of A, but always restores the original
 *         cursor and environment state (non-consuming).
 */
inline Rule AndPredicate(Rule a) {
    return [a = std::move(a)](ParserState& state) -> bool {
        auto checkpoint = state.save();
        bool result = a(state);
        state.restore(checkpoint);
        return result;
    };
}

/**
 * @brief Negative lookahead combinator (!A).
 * @param a The rule to check.
 * @return A Rule that returns the inverse of the success of A, but always
 *         restores the original cursor and environment state (non-consuming).
 */
inline Rule NotPredicate(Rule a) {
    return [a = std::move(a)](ParserState& state) -> bool {
        auto checkpoint = state.save();
        bool result = a(state);
        state.restore(checkpoint);
        return !result;
    };
}


