#pragma once
#include "Expr.h"
#include "ParserState.h"
#include <utility>

// ============================================================================
// Combinadores originales : literales, secuencia,
// eleccion, repeticion y predicados de lookahead.
// ============================================================================

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
        auto checkpoint = state.save();

        if (a(state)) {
            if (b(state)) {
                return true;
            }
        }

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
        state.restore(checkpoint);

        if (b(state)) {
            return true;
        }
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
                state.restore(checkpoint);
                break;
            }

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

// ============================================================================
// Combinadores nuevos: bind, update, constraint,
// y llamada a no-terminal con atributos heredados/sintetizados.
// ============================================================================

/**
 * @brief Bind parsing expression (𝜗 = p), regla Bind/¬Bind .
 *
 * Ejecuta p; si tiene exito, asocia la variable 𝜗 al prefijo de input
 * efectivamente consumido por p (no al resultado de una expresion, a
 * diferencia de Update). Si p falla, Bind tambien falla y no se modifica
 * el environment.
 *
 * @param var Nombre de la variable de atributo a la que se le asigna el
 *            texto consumido.
 * @param p   Regla cuyo prefijo consumido se captura.
 */
inline Rule Bind(const std::string& var, Rule p) {
    return [var, p = std::move(p)](ParserState& state) -> bool {
        auto checkpoint = state.save();
        size_t start = state.cursor;

        if (p(state)) {
            std::string consumed(state.input.substr(start, state.cursor - start));
            state.setValue(var, Value(consumed));
            return true;
        }

        state.restore(checkpoint);
        return false;
    };
}

/**
 * @brief Update parsing expression (𝜗 ← e), regla Update.
 *
 * Evalua la expresion de atributos e en el environment actual y asocia su
 * resultado a la variable 𝜗. Siempre tiene exito y no consume input; esto
 * es lo que permite computar/propagar gramaticas nuevas (por ejemplo
 * "lan" ← v"lan" ⊳ v"r" en la regla newSyn de μSugar).
 *
 * @param var Nombre de la variable de atributo a actualizar.
 * @param e   Expresion cuyo valor se asigna.
 */
inline Rule Update(const std::string& var, Expr e) {
    return [var, e](ParserState& state) -> bool {
        Value v = Eval(e, state);
        state.setValue(var, v);
        return true;
    };
}

/**
 * @brief Constraint parsing expression (?e), reglas True/False (Figura 7).
 *
 * Evalua e; si el resultado es el booleano true, tiene exito consumiendo
 * nada; si es false, falla. Un valor no-booleano es un error (igual que
 * "Non boolean value at constraint" en la implementacion Haskell).
 *
 * @param e Expresion booleana a evaluar.
 */
inline Rule Constraint(Expr e) {
    return [e](ParserState& state) -> bool {
        Value v = Eval(e, state);
        if (!v.isBool()) return false;
        return v.asBool();
    };
}

/**
 * @brief Llamada a no-terminal con atributos heredados y sintetizados
 * (𝐴 𝑒 𝜗).
 *
 * 1. Evalua langExpr en el environment actual: debe producir un Value que
 *    contenga una Grammar (el "atributo de lenguaje" v1 del paper).
 * 2. Busca dentro de esa Grammar la RuleDef llamada nonTerminal.
 * 3. Evalua cada expresion en args usando el environment ACTUAL (antes de
 *    entrar al scope de la llamada) y las asocia, en orden, a los nombres
 *    de parametros de la RuleDef, en un scope nuevo.
 * 4. Ejecuta el cuerpo de la regla en ese scope nuevo.
 * 5. Si tiene exito, recoge los valores de returns declarados por la
 *    RuleDef y los asocia, en orden, a resultVars en el scope del
 *    llamador.
 * 6. Si falla, se descarta todo (cursor + ambos environments) como
 *    cualquier otra regla que fracasa.
 *
 * @param langExpr    Expresion que produce el Value-Grammar a usar.
 * @param nonTerminal Nombre de la regla a invocar dentro de esa Grammar.
 * @param args        Expresiones para los atributos heredados (evaluadas
 *                    en el environment del llamador).
 * @param resultVars  Nombres, en el scope del llamador, donde se guardan
 *                    los atributos sintetizados que devuelve la regla.
 */
inline Rule Call(Expr langExpr, std::string nonTerminal, std::vector<Expr> args,
                  std::vector<std::string> resultVars) {
    return [langExpr = std::move(langExpr), nonTerminal = std::move(nonTerminal),
            args = std::move(args),
            resultVars = std::move(resultVars)](ParserState& state) -> bool {
        auto checkpoint = state.save();

        Value langVal = Eval(langExpr, state);
        if (!langVal.isGrammar()) {
            state.restore(checkpoint);
            return false;
        }

        const Grammar& grammar = langVal.asGrammar();
        auto it = grammar.find(nonTerminal);
        if (it == grammar.end()) {
            state.restore(checkpoint);
            return false;
        }
        const RuleDef& def = it->second;

        // Los argumentos se evaluan en el environment del LLAMADOR, antes
        // de entrar al scope nuevo (asi el callee no ve variables locales
        // del llamador que no sean sus parametros explicitos).
        std::vector<Value> argVals;
        argVals.reserve(args.size());
        for (auto& a : args) argVals.push_back(Eval(a, state));

        state.pushValueScope();
        for (size_t i = 0; i < def.params.size() && i < argVals.size(); ++i) {
            state.setValue(def.params[i], argVals[i]);
        }

        bool ok = def.body(state);

        std::vector<Value> results;
        if (ok) {
            results.reserve(def.returns.size());
            for (auto& r : def.returns) results.push_back(state.getValue(r));
        }
        state.popValueScope();

        if (!ok) {
            state.restore(checkpoint);
            return false;
        }

        for (size_t i = 0; i < resultVars.size() && i < results.size(); ++i) {
            state.setValue(resultVars[i], results[i]);
        }
        return true;
    };
}