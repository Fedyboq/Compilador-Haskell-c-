#include "ParserState.h"
#include "Combinators.h"
#include <iostream>
#include <cassert>

// A custom rule that dynamically defines a rule named "var"
// by parsing '+<char>' and registering 'var' to match that '<char>'.
// Example: If input is "+x", it matches and registers "var" to match 'x'.
const Rule DefineVar = [](ParserState& state) -> bool {
    if (state.has_more() && state.peek() == '+') {
        auto cp = state.save();
        state.advance();
        if (state.has_more()) {
            char c = state.peek();
            state.advance();
            // Dynamically register the rule in the current scope
            state.register_rule("var", MatchChar(c));
            return true;
        }
        state.restore(cp);
    }
    return false;
};

// A helper rule that executes "var" if it is defined in the current environment
const Rule UseVar = [](ParserState& state) -> bool {
    return state.execute_rule("var");
};

void run_test_1() {
    std::cout << "[Test 1] Simple sequence matching...\n";
    ParserState state("xyz");
    Rule xy = Sequence(MatchChar('x'), MatchChar('y'));
    
    bool result = xy(state);
    std::cout << "  Parse result of 'xy' on 'xyz': " << (result ? "SUCCESS" : "FAIL") << "\n";
    std::cout << "  Cursor position (expected 2): " << state.cursor << "\n";
    assert(result == true);
    assert(state.cursor == 2);
    std::cout << "  Test 1 passed.\n\n";
}

void run_test_2() {
    std::cout << "[Test 2] Dynamic Scoping & Environment Nesting...\n";
    ParserState state("aa");
    
    // Register 'var' matching 'a' in the global scope
    state.register_rule("var", MatchChar('a'));
    
    std::cout << "  Executing 'var' in global scope...\n";
    bool res1 = UseVar(state);
    std::cout << "    Match result (expected SUCCESS): " << (res1 ? "SUCCESS" : "FAIL") << "\n";
    assert(res1 == true);

    // Push local scope and register 'var' matching 'b'
    state.push_scope();
    state.register_rule("var", MatchChar('b'));
    
    // State is at index 1 now. Let's try matching 'var' (which expects 'b' in the local scope, but input has 'a')
    std::cout << "  Executing 'var' in local scope (expects 'b', input has 'a')...\n";
    bool res2 = UseVar(state);
    std::cout << "    Match result (expected FAIL): " << (res2 ? "SUCCESS" : "FAIL") << "\n";
    assert(res2 == false);

    // Pop the local scope. Global scope 'var' (matching 'a') is restored.
    state.pop_scope();
    std::cout << "  Popped local scope. Executing 'var' in global scope...\n";
    bool res3 = UseVar(state);
    std::cout << "    Match result (expected SUCCESS): " << (res3 ? "SUCCESS" : "FAIL") << "\n";
    assert(res3 == true);
    
    std::cout << "  Test 2 passed.\n\n";
}

void run_test_3() {
    std::cout << "[Test 3] Semantic Backtracking & Rule Rollback...\n";
    // Input is "+xz".
    // We want to parse with:
    // Choice(
    //    Sequence(DefineVar, MatchChar('y')),  // First choice: Defines 'var' as 'x', then expects 'y' (will fail)
    //    Sequence(DefineVar, MatchChar('z'))   // Second choice: Defines 'var' as 'x', then expects 'z' (should succeed)
    // )
    ParserState state("+xz");
    
    Rule rule_failed_branch = Sequence(DefineVar, MatchChar('y'));
    Rule rule_success_branch = Sequence(DefineVar, MatchChar('z'));
    Rule main_rule = Choice(rule_failed_branch, rule_success_branch);

    std::cout << "  Executing Choice of failing and succeeding branches...\n";
    bool result = main_rule(state);
    std::cout << "    Parse result (expected SUCCESS): " << (result ? "SUCCESS" : "FAIL") << "\n";
    std::cout << "    Cursor position (expected 3): " << state.cursor << "\n";
    assert(result == true);
    assert(state.cursor == 3);

    // Let's verify that the definition of 'var' registered by DefineVar in the successful branch persists
    std::cout << "  Verifying that the dynamically registered rule 'var' is still active...\n";
    ParserState state_use("x");
    // Copy the environment from the parsed state to verify rule persistence
    state_use.env = state.env;
    bool res_use = UseVar(state_use);
    std::cout << "    UseVar result on 'x' (expected SUCCESS): " << (res_use ? "SUCCESS" : "FAIL") << "\n";
    assert(res_use == true);

    std::cout << "  Test 3 passed.\n\n";
}

void run_adv_test_1() {
    std::cout << "[Adv Test 1] Many (Kleene Star) & Loop Protection...\n";
    
    // Case 1: Match multiple occurrences
    {
        ParserState state("xxxx");
        Rule rule = Many(MatchChar('x'));
        bool result = rule(state);
        std::cout << "  Many('x') on 'xxxx' result (expected SUCCESS): " << (result ? "SUCCESS" : "FAIL") << "\n";
        std::cout << "  Cursor (expected 4): " << state.cursor << "\n";
        assert(result == true);
        assert(state.cursor == 4);
    }
    
    // Case 2: Zero matches (should still succeed and not move cursor)
    {
        ParserState state("yxxx");
        Rule rule = Many(MatchChar('x'));
        bool result = rule(state);
        std::cout << "  Many('x') on 'yxxx' result (expected SUCCESS): " << (result ? "SUCCESS" : "FAIL") << "\n";
        std::cout << "  Cursor (expected 0): " << state.cursor << "\n";
        assert(result == true);
        assert(state.cursor == 0);
    }

    // Case 3: Loop protection for non-consuming rules
    {
        ParserState state("xyz");
        Rule non_consuming = [](ParserState&) -> bool { return true; };
        Rule rule = Many(non_consuming);
        
        std::cout << "  Running Many on a non-consuming rule (testing loop protection)...\n";
        bool result = rule(state); // If loop protection fails, this hangs infinitely
        std::cout << "    Result (expected SUCCESS): " << (result ? "SUCCESS" : "FAIL") << "\n";
        std::cout << "    Cursor (expected 0): " << state.cursor << "\n";
        assert(result == true);
        assert(state.cursor == 0);
    }
    
    std::cout << "  Adv Test 1 passed.\n\n";
}

void run_adv_test_2() {
    std::cout << "[Adv Test 2] Lookahead Predicates (And / Not)...\n";
    
    // Positive lookahead (&'x')
    {
        ParserState state("xyz");
        Rule look_x = AndPredicate(MatchChar('x'));
        bool result = look_x(state);
        std::cout << "  AndPredicate('x') on 'xyz' result (expected SUCCESS): " << (result ? "SUCCESS" : "FAIL") << "\n";
        std::cout << "  Cursor position (expected 0): " << state.cursor << "\n";
        assert(result == true);
        assert(state.cursor == 0);
    }

    // Negative lookahead (!'y') at start (should succeed because it's not 'y')
    {
        ParserState state("xyz");
        Rule look_not_y = NotPredicate(MatchChar('y'));
        bool result = look_not_y(state);
        std::cout << "  NotPredicate('y') on 'xyz' result (expected SUCCESS): " << (result ? "SUCCESS" : "FAIL") << "\n";
        std::cout << "  Cursor position (expected 0): " << state.cursor << "\n";
        assert(result == true);
        assert(state.cursor == 0);
    }

    // Negative lookahead (!'x') at start (should fail because it is 'x')
    {
        ParserState state("xyz");
        Rule look_not_x = NotPredicate(MatchChar('x'));
        bool result = look_not_x(state);
        std::cout << "  NotPredicate('x') on 'xyz' result (expected FAIL): " << (result ? "SUCCESS" : "FAIL") << "\n";
        std::cout << "  Cursor position (expected 0): " << state.cursor << "\n";
        assert(result == false);
        assert(state.cursor == 0);
    }
    
    std::cout << "  Adv Test 2 passed.\n\n";
}

void run_adv_test_3() {
    std::cout << "[Adv Test 3] Lookahead + Rule Scoping Rollback...\n";
    
    // We try running DefineVar inside a positive lookahead: AndPredicate(DefineVar)
    // The lookahead should succeed on "+x", but when it finishes, it MUST backtrack/restore
    // the state. This means the dynamic rule "var" (which would match 'x') should NOT exist.
    ParserState state("+x");
    
    Rule look_define = AndPredicate(DefineVar);
    bool result = look_define(state);
    
    std::cout << "  AndPredicate(DefineVar) on '+x' result (expected SUCCESS): " << (result ? "SUCCESS" : "FAIL") << "\n";
    std::cout << "  Cursor position after lookahead (expected 0): " << state.cursor << "\n";
    assert(result == true);
    assert(state.cursor == 0);
    
    // Verify "var" is NOT defined
    std::cout << "  Verifying that 'var' is NOT defined (since lookahead restored the env)...\n";
    ParserState test_use("x");
    test_use.env = state.env; // Copy the environment stack
    bool use_result = UseVar(test_use);
    std::cout << "    UseVar result (expected FAIL): " << (use_result ? "SUCCESS" : "FAIL") << "\n";
    assert(use_result == false);
    
    std::cout << "  Adv Test 3 passed.\n\n";
}

int main() {
    std::cout << "===========================================\n";
    std::cout << " APEG C++ Parser Prototype (MVP) Demo      \n";
    std::cout << "===========================================\n\n";

    run_test_1();
    run_test_2();
    run_test_3();
    
    run_adv_test_1();
    run_adv_test_2();
    run_adv_test_3();

    std::cout << "All tests passed successfully!\n";
    return 0;
}
