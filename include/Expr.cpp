#include "Expr.h"
#include "ParserState.h"
#include <stdexcept>

Expr Expr::Lit(Value v) {
    Expr e;
    e.kind = Kind::Lit;
    e.literal = std::move(v);
    return e;
}

Expr Expr::Ref(std::string n) {
    Expr e;
    e.kind = Kind::Ref;
    e.name = std::move(n);
    return e;
}

Expr Expr::BinOp(std::string op, Expr l, Expr r) {
    Expr e;
    e.kind = Kind::BinOp;
    e.op = std::move(op);
    e.a = std::make_shared<Expr>(std::move(l));
    e.b = std::make_shared<Expr>(std::move(r));
    return e;
}

Expr Expr::MapGet(Expr m, Expr k) {
    Expr e;
    e.kind = Kind::MapGet;
    e.a = std::make_shared<Expr>(std::move(m));
    e.b = std::make_shared<Expr>(std::move(k));
    return e;
}

Expr Expr::MapSet(Expr m, Expr k, Expr v) {
    Expr e;
    e.kind = Kind::MapSet;
    e.a = std::make_shared<Expr>(std::move(m));
    e.b = std::make_shared<Expr>(std::move(k));
    e.c = std::make_shared<Expr>(std::move(v));
    return e;
}

Expr Expr::MapLit(std::vector<Expr> ks, std::vector<Expr> vs) {
    Expr e;
    e.kind = Kind::MapLit;
    e.keys = std::move(ks);
    e.vals = std::move(vs);
    return e;
}

Expr Expr::GrammarExt(Expr l, Expr r) {
    Expr e;
    e.kind = Kind::GrammarExt;
    e.a = std::make_shared<Expr>(std::move(l));
    e.b = std::make_shared<Expr>(std::move(r));
    return e;
}

namespace {

// Clave de mapa: los literales de mapa del paper indexan por valores,
// pero en la practica (y en el ejemplo de μSugar) siempre son strings.
std::string toMapKey(const Value& v) {
    if (v.isString()) return v.asString();
    if (v.isInt()) return std::to_string(v.asInt());
    throw std::runtime_error("Eval: clave de mapa debe ser string o int");
}

Value applyBinOp(const std::string& op, const Value& l, const Value& r) {
    if (op == "+") {
        if (l.isInt() && r.isInt()) return Value(l.asInt() + r.asInt());
        if (l.isString() && r.isString()) return Value(l.asString() + r.asString());
        throw std::runtime_error("Eval: '+' requiere dos ints o dos strings");
    }
    if (op == "-") return Value(l.asInt() - r.asInt());
    if (op == "<") return Value(l.asInt() < r.asInt());
    if (op == "=") return Value(l == r);
    throw std::runtime_error("Eval: operador desconocido '" + op + "'");
}

}  // namespace

Value Eval(const Expr& e, ParserState& state) {
    switch (e.kind) {
        case Expr::Kind::Lit:
            // Regla Lit: (Θ, l) ⤳ L(l)
            return e.literal;

        case Expr::Kind::Ref:
            // Regla A-ref: (Θ, 𝜗) ⤳ V_Θ[𝜗]
            return state.getValue(e.name);

        case Expr::Kind::BinOp: {
            // Regla Op: (Θ, e ⊕ e') ⤳ F(⊕, v, v')
            Value l = Eval(*e.a, state);
            Value r = Eval(*e.b, state);
            return applyBinOp(e.op, l, r);
        }

        case Expr::Kind::MapGet: {
            // Regla A-map: (Θ, e[[e']]) ⤳ σ(s)
            Value m = Eval(*e.a, state);
            Value k = Eval(*e.b, state);
            if (!m.isMap()) throw std::runtime_error("Eval: MapGet sobre valor que no es mapa");
            auto& map = m.asMap();
            auto it = map.find(toMapKey(k));
            return it == map.end() ? Value() : it->second;
        }

        case Expr::Kind::MapSet: {
            // Regla U-map: (Θ, e[e'/e'']) ⤳ σ[s/v]
            Value m = Eval(*e.a, state);
            Value k = Eval(*e.b, state);
            Value v = Eval(*e.c, state);
            if (!m.isMap()) throw std::runtime_error("Eval: MapSet sobre valor que no es mapa");
            Value::MapType updated = m.asMap();
            updated[toMapKey(k)] = v;
            return Value(std::move(updated));
        }

        case Expr::Kind::MapLit: {
            // Regla L-map: (Θ, {e/e'}^n) ⤳ [s_i/v_j]
            Value::MapType map;
            size_t n = std::min(e.keys.size(), e.vals.size());
            for (size_t i = 0; i < n; ++i) {
                Value k = Eval(e.keys[i], state);
                Value v = Eval(e.vals[i], state);
                map[toMapKey(k)] = v;
            }
            return Value(std::move(map));
        }

        case Expr::Kind::GrammarExt: {
            // El paper overlapea la notacion ⊳ para dos reglas distintas
            // G-ext, cuando ambos operandos son Grammars
            // "crudas"; y L-ext, cuando el primer operando ya es un
            // language (Grammar + Γ) y se re-chequea consistencia de
            // tipos sobre el resultado. La distincion ocurre a nivel de
            // valor en tiempo de evaluacion, igual que en la implementacion
            // Haskell del paper (Seccion 4: "the distinction occurs at the
            // type level").
            Value l = Eval(*e.a, state);
            Value r = Eval(*e.b, state);

            if (l.isLanguage()) {
                // Regla L-ext: (Θ, e) ⤳ v_g/Γ  (Θ, e') ⤳ v'_g
                //              Γ ⊢ v_g ⊎ v'_g ⤳ Γ′
                //              (Θ, e ⊳ e') ⤳ (v_g ⊎ v'_g)/Γ′
                if (!r.isGrammar())
                    throw std::runtime_error(
                        "Eval: L-ext requiere una Grammar como segundo operando");
                const LanguageValue& lang = l.asLanguage();
                auto [mergedGrammar, gammaPrime] = extendLanguage(lang.grammar, lang.gamma, r.asGrammar());
                LanguageValue result{std::move(mergedGrammar), std::move(gammaPrime)};
                return Value(std::move(result));
            }

            // Regla G-ext: (Θ, e) ⤳ v_g  (Θ, e') ⤳ v'_g
            //              (Θ, e ⊳ e') ⤳ v_g ⊎ v'_g
            if (!l.isGrammar() || !r.isGrammar())
                throw std::runtime_error(
                    "Eval: GrammarExt requiere dos Grammars, o un language y una Grammar");
            return Value(composeGrammars(l.asGrammar(), r.asGrammar()));
        }
    }
    throw std::runtime_error("Eval: Expr::Kind no manejado");
}