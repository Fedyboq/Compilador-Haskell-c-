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

struct State;

struct PResult {
    bool  ok = false;
    Value val;

    static PResult fail()               { return PResult{false, Value::Unit()}; }
    static PResult good(Value v = Value::Unit()) { return PResult{true, std::move(v)}; }
};

using Rule  = std::function<PResult(State&, const std::vector<Value>&)>;

using PExpr = std::function<PResult(State&)>;

using AExpr = std::function<Value(State&)>;

struct TypeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct RuleSig {
    std::vector<std::string> params;
    std::vector<std::string> returns;
    std::vector<Type>        paramTypes;
    std::vector<Type>        returnTypes;
};

struct Grammar {
    std::map<std::string, Rule> rules;

    std::map<std::string, Value> attrs;

    std::map<std::string, RuleSig> sigs;

    bool has(const std::string& name) const { return rules.count(name) > 0; }

    Grammar with(const std::string& name, Rule rule) const {
        Grammar g = *this;
        g.rules[name] = std::move(rule);
        return g;
    }
};

using Gamma = std::map<std::string, Type>;

struct LanguageValue {
    Grammar grammar;
    Gamma   gamma;
};

struct State {
    std::string_view input;
    size_t cursor = 0;
    std::shared_ptr<Grammar> grammar;
    std::map<std::string, Value> env;

    bool has_more() const { return cursor < input.size(); }
    char peek() const     { return has_more() ? input[cursor] : '\0'; }

    struct Checkpoint {
        size_t cursor;
        std::shared_ptr<Grammar> grammar;
        std::map<std::string, Value> env;
    };
    Checkpoint save() const { return Checkpoint{cursor, grammar, env}; }
    void restore(const Checkpoint& c) { cursor = c.cursor; grammar = c.grammar; env = c.env; }
};

inline AExpr AVal(Value v) {
    return [v = std::move(v)](State&) { return v; };
}

inline AExpr AVar(std::string name) {
    return [name = std::move(name)](State& s) -> Value {
        auto it = s.env.find(name);
        return it == s.env.end() ? Value::Unit() : it->second;
    };
}

inline PExpr Empty() { return [](State&) { return PResult::good(); }; }

inline PExpr Fail() { return [](State&) { return PResult::fail(); }; }

inline PExpr Lit(char c) {
    return [c](State& s) -> PResult {
        if (s.has_more() && s.peek() == c) { s.cursor++; return PResult::good(Value::Str(std::string(1, c))); }
        return PResult::fail();
    };
}

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

inline PExpr Range(char lo, char hi) {
    return [lo, hi](State& s) -> PResult {
        if (s.has_more() && s.peek() >= lo && s.peek() <= hi) {
            char c = s.peek(); s.cursor++;
            return PResult::good(Value::Str(std::string(1, c)));
        }
        return PResult::fail();
    };
}

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

inline PExpr Star(PExpr e) {
    return [e = std::move(e)](State& s) -> PResult {
        std::vector<Value> acc;
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

inline PExpr Opt(PExpr e) {
    return [e = std::move(e)](State& s) -> PResult {
        auto cp = s.save();
        PResult r = e(s);
        if (r.ok) return r;
        s.restore(cp);
        return PResult::good(Value::Unit());
    };
}

inline PExpr Not(PExpr e) {
    return [e = std::move(e)](State& s) -> PResult {
        auto cp = s.save();
        PResult r = e(s);
        s.restore(cp);
        return r.ok ? PResult::fail() : PResult::good();
    };
}

inline PExpr And(PExpr e) {
    return [e = std::move(e)](State& s) -> PResult {
        auto cp = s.save();
        PResult r = e(s);
        s.restore(cp);
        return r.ok ? PResult::good() : PResult::fail();
    };
}

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

inline PExpr Number() {
    return [](State& s) -> PResult {
        if (!s.has_more() || !std::isdigit((unsigned char)s.peek())) return PResult::fail();
        size_t start = s.cursor;
        while (s.has_more() && std::isdigit((unsigned char)s.peek())) s.cursor++;
        long v = std::stol(std::string(s.input.substr(start, s.cursor - start)));
        return PResult::good(Value::Int(v));
    };
}

inline PExpr Word(std::string w) {
    return [w = std::move(w)](State& s) -> PResult {
        auto cp = s.save();
        PResult r = Ident()(s);
        if (r.ok && r.val.s == w) return PResult::good(Value::Sym(w));
        s.restore(cp);
        return PResult::fail();
    };
}

inline PExpr Capture(std::string var, PExpr e) {
    return [var = std::move(var), e = std::move(e)](State& s) -> PResult {
        PResult r = e(s);
        if (r.ok) s.env[var] = r.val;
        return r;
    };
}

inline PExpr Action(AExpr f) {
    return [f = std::move(f)](State& s) -> PResult {
        return PResult::good(f(s));
    };
}

inline PExpr Bind(std::string var, AExpr f) {
    return [var = std::move(var), f = std::move(f)](State& s) -> PResult {
        s.env[var] = f(s);
        return PResult::good();
    };
}

inline PExpr Constraint(AExpr pred) {
    return [pred = std::move(pred)](State& s) -> PResult {
        return pred(s).truthy() ? PResult::good() : PResult::fail();
    };
}

inline PExpr Update(std::function<void(State&)> modify) {
    return [modify = std::move(modify)](State& s) -> PResult {
        modify(s);
        return PResult::good();
    };
}

inline PExpr Call(std::string name, std::vector<AExpr> args = {}) {
    return [name = std::move(name), args = std::move(args)](State& s) -> PResult {
        auto it = s.grammar->rules.find(name);
        if (it == s.grammar->rules.end()) return PResult::fail();
        std::vector<Value> vals;
        vals.reserve(args.size());
        for (auto& a : args) vals.push_back(a(s));

        std::map<std::string, Value> savedEnv = s.env;
        PResult r = it->second(s, vals);
        s.env = std::move(savedEnv);
        return r;
    };
}

inline Rule ruleOf(PExpr e) {
    return [e = std::move(e)](State& s, const std::vector<Value>&) -> PResult { return e(s); };
}

struct ParseOutcome {
    bool ok = false;
    Value ast;
    size_t pos = 0;
    std::string rest;
};

inline ParseOutcome runGrammar(std::shared_ptr<Grammar> g, const std::string& start,
                               std::string_view input) {
    State s;
    s.input = input;
    s.grammar = std::move(g);

    PResult r = Call(start)(s);
    Ws()(s);

    ParseOutcome out;
    out.pos  = s.cursor;
    out.rest = std::string(s.input.substr(s.cursor));
    out.ast  = r.val;
    out.ok   = r.ok && !s.has_more();
    return out;
}

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
}

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
