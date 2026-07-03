#pragma once

#include "Value.h"
#include "Type.h"

#include <string_view>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <string>
#include <cctype>
#include <stdexcept>
#include <utility>
#include <algorithm>

/**
 * @file Apeg.h
 * @brief Full Adaptable PEG model, faithful to the course paper
 *        (Reis, Bigonha, Di Iorio & Amorim -- Adaptable Parsing Expression
 *        Grammars).
 *
 * This is the layer that was missing in the original MVP.  The MVP had a
 * *recognizer* (`Rule = function<bool(ParserState&)>`): it could only say
 * "matched / did not match".  An APEG is a *language*:
 *
 *   1. Values are first-class (see Value.h) -- including grammars themselves.
 *   2. The grammar is carried inside the parsing state and can be *updated*
 *      while parsing (the "adaptable" part).
 *   3. Non-terminals behave like functions: they receive inherited attributes
 *      (arguments) and produce a synthesized attribute (typically an AST).
 *   4. Three formal operators bridge parsing and semantics:
 *        - Bind        : assign an attribute.
 *        - Constraint  : a semantic predicate (succeeds iff a condition holds).
 *        - Update      : produce a new grammar from the current one.
 */

struct State;

/**
 * @brief Result of running a parsing expression: success flag plus the
 *        synthesized attribute (the value/AST produced on success).
 */
struct PResult {
    bool  ok = false;
    Value val;

    static PResult fail()               { return PResult{false, Value::Unit()}; }
    static PResult good(Value v = Value::Unit()) { return PResult{true, std::move(v)}; }
};

/// A non-terminal rule: receives inherited attributes, yields a PResult.
using Rule  = std::function<PResult(State&, const std::vector<Value>&)>;
/// A parsing expression: consumes input, yields a PResult.
using PExpr = std::function<PResult(State&)>;
/// An attribute expression: a pure computation over the current state's attributes.
using AExpr = std::function<Value(State&)>;

/// Thrown when composing/extending grammars produces incompatible rule
/// signatures (the L-ext type-consistency check of Figure 10 fails).
struct TypeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/**
 * @brief Declared signature of a non-terminal: its inherited attributes
 *        (params) and synthesized attributes (returns), with optional types.
 *        Models the paper's rule head  ⟨A ϑ::τⁿ  e::τᵐ⟩.  Signatures are
 *        optional metadata: rules that don't need type checking leave them
 *        empty. They are consulted by extendLanguage (L-ext).
 */
struct RuleSig {
    std::vector<std::string> params;
    std::vector<std::string> returns;
    std::vector<Type>        paramTypes;
    std::vector<Type>        returnTypes;
};

/**
 * @brief A Grammar is a map from non-terminal name to Rule -- and it is a
 *        first-class Value (see Value::Gram).  Updates are *immutable*: they
 *        return a fresh Grammar, which is what makes semantic backtracking of
 *        grammar changes possible.
 */
struct Grammar {
    std::map<std::string, Rule> rules;

    /// Grammar-level attributes: state that belongs to the grammar itself and
    /// therefore *threads* through the parse (persists on success, rolls back
    /// on backtracking, exactly like the grammar rules).  This is where truly
    /// global information lives -- e.g. the set of declared identifiers -- as
    /// opposed to the per-call local attributes kept in State::env.
    std::map<std::string, Value> attrs;

    /// Optional per-rule signatures (params/returns + types) used by the
    /// L-ext type-consistency check. Empty for rules that don't need types.
    std::map<std::string, RuleSig> sigs;

    bool has(const std::string& name) const { return rules.count(name) > 0; }

    /// Return a copy of this grammar with `name` bound to `rule` (add or replace).
    Grammar with(const std::string& name, Rule rule) const {
        Grammar g = *this;
        g.rules[name] = std::move(rule);
        return g;
    }
};

/// Typing context Γ : ϑ → τ (a snapshot of which attribute variables a given
/// language has typed). Flat map, packaged inside a LanguageValue.
using Gamma = std::map<std::string, Type>;

/**
 * @brief A language value: "a type-checked grammar together with its typing
 *        context" (tLang in the paper). Unlike a raw Grammar (pure syntax,
 *        no guarantees), a LanguageValue has passed the L-ext consistency
 *        check at least once. It is a first-class Value (Value::Lang).
 */
struct LanguageValue {
    Grammar grammar;
    Gamma   gamma;
};

/**
 * @brief The parsing state, extended from the MVP to carry the grammar as a
 *        value and an environment of attribute bindings.
 */
struct State {
    std::string_view input;
    size_t cursor = 0;
    std::shared_ptr<Grammar> grammar;       ///< current grammar (adaptable, first-class)
    std::map<std::string, Value> env;       ///< attribute environment

    bool has_more() const { return cursor < input.size(); }
    char peek() const     { return has_more() ? input[cursor] : '\0'; }

    /// Snapshot of cursor + grammar + attributes, for full semantic backtracking.
    struct Checkpoint {
        size_t cursor;
        std::shared_ptr<Grammar> grammar;
        std::map<std::string, Value> env;
    };
    Checkpoint save() const { return Checkpoint{cursor, grammar, env}; }
    void restore(const Checkpoint& c) { cursor = c.cursor; grammar = c.grammar; env = c.env; }
};

// ===========================================================================
//  Attribute expressions (pure computations over attributes)
// ===========================================================================

/// A literal attribute value.
inline AExpr AVal(Value v) {
    return [v = std::move(v)](State&) { return v; };
}

/// Read an attribute from the environment (Unit if unbound).
inline AExpr AVar(std::string name) {
    return [name = std::move(name)](State& s) -> Value {
        auto it = s.env.find(name);
        return it == s.env.end() ? Value::Unit() : it->second;
    };
}

// ===========================================================================
//  Core PEG combinators (now attribute-aware: they synthesize Values)
// ===========================================================================

/// Always succeeds, consuming nothing.
inline PExpr Empty() { return [](State&) { return PResult::good(); }; }

/// Always fails.
inline PExpr Fail() { return [](State&) { return PResult::fail(); }; }

/// Match a single character literal.
inline PExpr Lit(char c) {
    return [c](State& s) -> PResult {
        if (s.has_more() && s.peek() == c) { s.cursor++; return PResult::good(Value::Str(std::string(1, c))); }
        return PResult::fail();
    };
}

/// Match an exact string of characters.
inline PExpr Text(std::string w) {
    return [w = std::move(w)](State& s) -> PResult {
        if (s.cursor + w.size() <= s.input.size() &&
            s.input.substr(s.cursor, w.size()) == std::string_view(w)) {
            s.cursor += w.size();
            return PResult::good(Value::Str(w));
        }
        return PResult::fail();
    };
}

/// Match a character in the inclusive range [lo, hi].
inline PExpr Range(char lo, char hi) {
    return [lo, hi](State& s) -> PResult {
        if (s.has_more() && s.peek() >= lo && s.peek() <= hi) {
            char c = s.peek(); s.cursor++;
            return PResult::good(Value::Str(std::string(1, c)));
        }
        return PResult::fail();
    };
}

/// Skip optional whitespace; always succeeds.
inline PExpr Ws() {
    return [](State& s) -> PResult {
        while (s.has_more()) {
            char c = s.peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') s.cursor++;
            else break;
        }
        return PResult::good();
    };
}

/// Sequence: run every expression; the synthesized value is the last one's.
inline PExpr Seq(std::vector<PExpr> es) {
    return [es = std::move(es)](State& s) -> PResult {
        auto cp = s.save();
        Value last = Value::Unit();
        for (auto& e : es) {
            PResult r = e(s);
            if (!r.ok) { s.restore(cp); return PResult::fail(); }
            last = std::move(r.val);
        }
        return PResult::good(std::move(last));
    };
}

/// Ordered choice: try each alternative in order, backtracking between them.
inline PExpr Choice(std::vector<PExpr> es) {
    return [es = std::move(es)](State& s) -> PResult {
        for (auto& e : es) {
            auto cp = s.save();
            PResult r = e(s);
            if (r.ok) return r;
            s.restore(cp);
        }
        return PResult::fail();
    };
}

/// Kleene star (0+). Collects synthesized values into a List. Loop-protected.
inline PExpr Star(PExpr e) {
    return [e = std::move(e)](State& s) -> PResult {
        std::vector<Value> acc;
        while (true) {
            auto cp = s.save();
            size_t before = s.cursor;
            PResult r = e(s);
            if (!r.ok) { s.restore(cp); break; }
            if (s.cursor == before) break;         // no progress -> stop (no infinite loop)
            acc.push_back(std::move(r.val));
        }
        return PResult::good(Value::List(std::move(acc)));
    };
}

/// One or more (1+). Collects synthesized values into a List.
inline PExpr Plus(PExpr e) {
    return [e = std::move(e)](State& s) -> PResult {
        PResult first = e(s);
        if (!first.ok) return PResult::fail();
        std::vector<Value> acc{std::move(first.val)};
        while (true) {
            auto cp = s.save();
            size_t before = s.cursor;
            PResult r = e(s);
            if (!r.ok) { s.restore(cp); break; }
            if (s.cursor == before) break;
            acc.push_back(std::move(r.val));
        }
        return PResult::good(Value::List(std::move(acc)));
    };
}

/// Optional (0 or 1). Always succeeds.
inline PExpr Opt(PExpr e) {
    return [e = std::move(e)](State& s) -> PResult {
        auto cp = s.save();
        PResult r = e(s);
        if (r.ok) return r;
        s.restore(cp);
        return PResult::good(Value::Unit());
    };
}

/// Negative lookahead (!e): succeeds iff e fails; consumes nothing.
inline PExpr Not(PExpr e) {
    return [e = std::move(e)](State& s) -> PResult {
        auto cp = s.save();
        PResult r = e(s);
        s.restore(cp);
        return r.ok ? PResult::fail() : PResult::good();
    };
}

/// Positive lookahead (&e): succeeds iff e succeeds; consumes nothing.
inline PExpr And(PExpr e) {
    return [e = std::move(e)](State& s) -> PResult {
        auto cp = s.save();
        PResult r = e(s);
        s.restore(cp);
        return r.ok ? PResult::good() : PResult::fail();
    };
}

// ===========================================================================
//  Tokens
// ===========================================================================

/// Identifier token: [A-Za-z_][A-Za-z0-9_]*  ->  synthesizes a Sym.
inline PExpr Ident() {
    return [](State& s) -> PResult {
        auto isFirst = [](char c) { return std::isalpha((unsigned char)c) || c == '_'; };
        auto isRest  = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
        if (!s.has_more() || !isFirst(s.peek())) return PResult::fail();
        size_t start = s.cursor;
        s.cursor++;
        while (s.has_more() && isRest(s.peek())) s.cursor++;
        return PResult::good(Value::Sym(std::string(s.input.substr(start, s.cursor - start))));
    };
}

/// Integer token: [0-9]+  ->  synthesizes an Int.
inline PExpr Number() {
    return [](State& s) -> PResult {
        if (!s.has_more() || !std::isdigit((unsigned char)s.peek())) return PResult::fail();
        size_t start = s.cursor;
        while (s.has_more() && std::isdigit((unsigned char)s.peek())) s.cursor++;
        long v = std::stol(std::string(s.input.substr(start, s.cursor - start)));
        return PResult::good(Value::Int(v));
    };
}

/// Match an identifier token equal to a specific word (used for dynamic keywords).
inline PExpr Word(std::string w) {
    return [w = std::move(w)](State& s) -> PResult {
        auto cp = s.save();
        PResult r = Ident()(s);
        if (r.ok && r.val.s == w) return PResult::good(Value::Sym(w));
        s.restore(cp);
        return PResult::fail();
    };
}

// ===========================================================================
//  APEG-specific operators: attributes, semantics, and adaptation
// ===========================================================================

/// Run `e`, and on success store its synthesized value into attribute `var`.
inline PExpr Capture(std::string var, PExpr e) {
    return [var = std::move(var), e = std::move(e)](State& s) -> PResult {
        PResult r = e(s);
        if (r.ok) s.env[var] = r.val;
        return r;
    };
}

/// Semantic action: consumes nothing, sets the synthesized value to f(state).
inline PExpr Action(AExpr f) {
    return [f = std::move(f)](State& s) -> PResult {
        return PResult::good(f(s));
    };
}

/// Bind: assign attribute `var` to f(state). Consumes nothing, always succeeds.
inline PExpr Bind(std::string var, AExpr f) {
    return [var = std::move(var), f = std::move(f)](State& s) -> PResult {
        s.env[var] = f(s);
        return PResult::good();
    };
}

/// Constraint: a semantic predicate. Succeeds iff pred(state) is truthy.
///  Consumes no input -- this is how APEG expresses data-dependent parsing
///  (e.g. "this identifier must have been declared", "no redefinition").
inline PExpr Constraint(AExpr pred) {
    return [pred = std::move(pred)](State& s) -> PResult {
        return pred(s).truthy() ? PResult::good() : PResult::fail();
    };
}

/// Update: adapt the grammar in the current state. This is THE adaptable core:
///  the parser modifies the language it is parsing, mid-parse.  Because the
///  grammar is a value (a shared_ptr swapped for a fresh one), backtracking
///  restores it automatically.
inline PExpr Update(std::function<void(State&)> modify) {
    return [modify = std::move(modify)](State& s) -> PResult {
        modify(s);
        return PResult::good();
    };
}

/// Call a non-terminal, passing inherited attributes (evaluated from AExprs).
///  Each call gets its OWN local attribute scope (State::env): the callee's
///  local bindings do not leak back to the caller.  Threaded state (the grammar
///  and its grammar-level attrs, plus the input cursor) is NOT scoped -- it
///  flows through, which is what makes grammar adaptation persist.
inline PExpr Call(std::string name, std::vector<AExpr> args = {}) {
    return [name = std::move(name), args = std::move(args)](State& s) -> PResult {
        auto it = s.grammar->rules.find(name);
        if (it == s.grammar->rules.end()) return PResult::fail();
        std::vector<Value> vals;
        vals.reserve(args.size());
        for (auto& a : args) vals.push_back(a(s));

        std::map<std::string, Value> savedEnv = s.env;   // open a local scope
        PResult r = it->second(s, vals);
        s.env = std::move(savedEnv);                     // close it (restore caller's attrs)
        return r;
    };
}

// ===========================================================================
//  Helpers to build grammars and run them
// ===========================================================================

/// Wrap a parsing expression as a Rule that ignores its inherited attributes.
inline Rule ruleOf(PExpr e) {
    return [e = std::move(e)](State& s, const std::vector<Value>&) -> PResult { return e(s); };
}

/// Outcome of running a whole grammar over an input.
struct ParseOutcome {
    bool ok = false;
    Value ast;
    size_t pos = 0;              ///< cursor where parsing stopped
    std::string rest;           ///< remaining unconsumed text (for error display)
};

/**
 * @brief Run grammar `g` from the `start` non-terminal over `input`.
 *        Requires the input to be fully consumed (modulo trailing whitespace).
 */
inline ParseOutcome runGrammar(std::shared_ptr<Grammar> g, const std::string& start,
                               std::string_view input) {
    State s;
    s.input = input;
    s.grammar = std::move(g);

    PResult r = Call(start)(s);
    Ws()(s);   // allow trailing whitespace

    ParseOutcome out;
    out.pos  = s.cursor;
    out.rest = std::string(s.input.substr(s.cursor));
    out.ast  = r.val;
    out.ok   = r.ok && !s.has_more();
    return out;
}

// ===========================================================================
//  Grammar composition (⊎) and typed language extension (⊳ / L-ext)
//  Ported and adapted onto this engine from branch `si` (v1). Because our
//  grammar updates are immutable, ⊎ is a pure function returning a NEW grammar
//  -- there is no shared mutable state to corrupt, and backtracking over a
//  composed grammar is automatically correct.
// ===========================================================================

/// Rule-level ordered choice: used to merge two definitions of the same
/// non-terminal when composing grammars (A -> p1 / p2).
inline Rule ruleChoice(Rule a, Rule b) {
    return [a = std::move(a), b = std::move(b)](State& s, const std::vector<Value>& args) -> PResult {
        auto cp = s.save();
        PResult r = a(s, args);
        if (r.ok) return r;
        s.restore(cp);
        r = b(s, args);
        if (r.ok) return r;
        s.restore(cp);
        return PResult::fail();
    };
}

/// Grammar composition ⊎ (G-ext, Figure 10). If a non-terminal A is defined
/// in both grammars, the result has A -> p1 / p2 (ordered choice). Purely
/// syntactic: no type checking here (that is L-ext's job). A generic, reusable
/// operator -- the piece v3 lacked.
inline Grammar composeGrammars(const Grammar& g1, const Grammar& g2) {
    Grammar result = g1;
    for (const auto& [name, rule] : g2.rules) {
        auto it = result.rules.find(name);
        if (it == result.rules.end()) result.rules[name] = rule;
        else it->second = ruleChoice(it->second, rule);
    }
    for (const auto& [name, sig] : g2.sigs)
        if (!result.sigs.count(name)) result.sigs[name] = sig;
    for (const auto& [k, v] : g2.attrs)
        if (!result.attrs.count(k)) result.attrs[k] = v;
    return result;
}

namespace apeg_detail {
/// Signatures of two definitions of the same non-terminal must agree in
/// arity, and in the declared type of every position typed on both sides.
inline void checkSigCompatible(const std::string& name, const RuleSig& a, const RuleSig& b) {
    if (a.params.size() != b.params.size())
        throw TypeError("L-ext: '" + name + "' distinta aridad de params (" +
            std::to_string(a.params.size()) + " vs " + std::to_string(b.params.size()) + ")");
    if (a.returns.size() != b.returns.size())
        throw TypeError("L-ext: '" + name + "' distinta aridad de returns (" +
            std::to_string(a.returns.size()) + " vs " + std::to_string(b.returns.size()) + ")");
    auto check = [&](const std::vector<Type>& ta, const std::vector<Type>& tb, const char* what) {
        size_t n = std::min(ta.size(), tb.size());
        for (size_t i = 0; i < n; ++i) {
            bool bothTyped = ta[i].kind != TypeKind::Undefined && tb[i].kind != TypeKind::Undefined;
            if (bothTyped && ta[i] != tb[i])
                throw TypeError("L-ext: '" + name + "' tipo incompatible en " + what + " #" +
                    std::to_string(i) + " (" + ta[i].toString() + " vs " + tb[i].toString() + ")");
        }
    };
    check(a.paramTypes, b.paramTypes, "param");
    check(a.returnTypes, b.returnTypes, "return");
}
}  // namespace apeg_detail

/// Typed language extension ⊳ (L-ext, Figure 10): compose the grammars (⊎),
/// re-check type consistency of every non-terminal present in BOTH, then
/// extend Γ with the types declared by g2. Throws TypeError on a signature
/// clash. This is L-ext's distinguishing property over plain G-ext.
inline std::pair<Grammar, Gamma> extendLanguage(const Grammar& g1, const Gamma& gamma1,
                                                const Grammar& g2) {
    Grammar merged = composeGrammars(g1, g2);
    for (const auto& [name, sig2] : g2.sigs) {
        auto it = g1.sigs.find(name);
        if (it != g1.sigs.end()) apeg_detail::checkSigCompatible(name, it->second, sig2);
    }
    Gamma gammaPrime = gamma1;
    for (const auto& [name, sig] : g2.sigs) {
        for (size_t i = 0; i < sig.params.size() && i < sig.paramTypes.size(); ++i)
            if (sig.paramTypes[i].kind != TypeKind::Undefined) gammaPrime[sig.params[i]] = sig.paramTypes[i];
        for (size_t i = 0; i < sig.returns.size() && i < sig.returnTypes.size(); ++i)
            if (sig.returnTypes[i].kind != TypeKind::Undefined) gammaPrime[sig.returns[i]] = sig.returnTypes[i];
    }
    return {std::move(merged), std::move(gammaPrime)};
}
