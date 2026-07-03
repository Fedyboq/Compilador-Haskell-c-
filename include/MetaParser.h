//
// Created by ignac on 3/07/2026.
//

#ifndef METAPARSER_H
#define METAPARSER_H

#pragma once
#include "Combinators.h"
#include "Lexer.h"
#include <optional>
#include <memory>

/**
 * MetaParser.h - Parser de gramáticas definidas por el usuario
 *
 * Implementa el parseo de:
 *   create <nombre> { <reglas> }   → define una gramática
 *   syntax <nombre> { <stmts> }    → activa una gramática
 *
 * La clave: parse_pattern() lee un patrón PEG del input
 * y devuelve directamente una Rule ejecutable — sin AST
 * intermedio. La Rule se construye mientras se parsea.
 *
 * Correspondencia con el paper:
 *   parse_pattern() ≡ evaluar m_p (meta-operadores del paper)
 *   parse_create()  ≡ ruleNewSyn (Figura 21)
 *   parse_syntax()  ≡ ruleExtStmt (Figura 21)
 */

// Forward declaration — necesitamos parsear Pattern que
// puede contener sub-patterns (recursión mutua)
std::optional<Rule> parse_pattern(ParserState& state);

// ============================================================
// parse_pfactor
// Pfactor -> Identifier / '\'' chars '\'' / '(' Pattern ')'
//
// Es la unidad básica de un patrón:
//   - un identificador → referencia a no-terminal
//   - un literal entre comillas simples → terminal
//   - un grupo entre paréntesis → sub-patrón
// ============================================================
std::optional<Rule> parse_pfactor(ParserState& state) {
    auto cp = state.save();
    Whitespace()(state);

    // Caso 1: literal entre comillas simples
    // Ej: 'repeat' → MatchString("repeat")
    if (state.has_more() && state.peek() == '\'') {
        state.advance(); // consume la comilla de apertura

        std::string literal;
        while (state.has_more() && state.peek() != '\'') {
            literal += state.peek();
            state.advance();
        }

        if (!state.has_more() || state.peek() != '\'') {
            // No se cerró la comilla — fallo
            state.restore(cp);
            return std::nullopt;
        }
        state.advance(); // consume la comilla de cierre

        // Devuelve una Rule que matchea este literal exacto
        return [literal](ParserState& s) -> bool {
            auto scp = s.save();
            // verifica que el input tenga el literal
            if (s.cursor + literal.size() > s.input.size()) {
                s.restore(scp);
                return false;
            }
            for (size_t i = 0; i < literal.size(); i++) {
                if (s.input[s.cursor + i] != literal[i]) {
                    s.restore(scp);
                    return false;
                }
            }
            s.cursor += literal.size();
            return true;
        };
    }

    // Caso 2: grupo entre paréntesis
    // Ej: (p1 . p2) → el patrón interior
    if (state.has_more() && state.peek() == '(') {
        state.advance(); // consume '('

        auto inner = parse_pattern(state);
        if (!inner) {
            state.restore(cp);
            return std::nullopt;
        }

        Whitespace()(state);
        if (!state.has_more() || state.peek() != ')') {
            state.restore(cp);
            return std::nullopt;
        }
        state.advance(); // consume ')'

        return inner;
    }

    // Caso 3: identificador → referencia a no-terminal
    // Ej: stmt → execute_rule("stmt") en tiempo de ejecución
    if (state.has_more() && islower(state.peek())) {
        std::string name;
        while (state.has_more() &&
               (islower(state.peek()) || isdigit(state.peek()))) {
            name += state.peek();
            state.advance();
        }

        // IMPORTANTE: devuelve una Rule que en tiempo de
        // ejecución busca "name" en el environment
        // Esto es exactamente la llamada a no-terminal
        // A ē ϑ de la Figura 8 del paper
        return [name](ParserState& s) -> bool {
            return s.execute_rule(name);
        };
    }

    state.restore(cp);
    return std::nullopt;
}

// ============================================================
// parse_pterm
// Pterm -> Pfactor '*' / Pfactor
//
// Agrega repetición opcional al factor
// Ej: num* → Many(execute_rule("num"))
// ============================================================
std::optional<Rule> parse_pterm(ParserState& state) {
    auto cp = state.save();
    Whitespace()(state);

    auto factor = parse_pfactor(state);
    if (!factor) {
        state.restore(cp);
        return std::nullopt;
    }

    Whitespace()(state);

    // '*' después del factor → repetición
    if (state.has_more() && state.peek() == '*') {
        state.advance();
        Rule repeated = *factor;
        // Many(factor) — cero o más repeticiones
        return [repeated](ParserState& s) -> bool {
            return Many(repeated)(s);
        };
    }

    return factor;
}

// ============================================================
// parse_prefix
// Prefix -> '!' Pterm / Pterm
//
// Agrega not-predicate opcional
// Ej: !'+' → NotPredicate(MatchChar('+'))
// ============================================================
std::optional<Rule> parse_prefix(ParserState& state) {
    auto cp = state.save();
    Whitespace()(state);

    // '!' → not-predicate
    if (state.has_more() && state.peek() == '!') {
        state.advance();

        auto term = parse_pterm(state);
        if (!term) {
            state.restore(cp);
            return std::nullopt;
        }

        Rule inner = *term;
        return [inner](ParserState& s) -> bool {
            return NotPredicate(inner)(s);
        };
    }

    return parse_pterm(state);
}

// ============================================================
// parse_pseq
// Pseq -> Prefix ('.' Prefix)*
//
// Secuencia de prefijos unidos por '.'
// Ej: 'repeat' . num . 'times'
//   → Sequence(MatchString("repeat"),
//       Sequence(execute_rule("num"),
//         MatchString("times")))
// ============================================================
std::optional<Rule> parse_pseq(ParserState& state) {
    auto cp = state.save();
    Whitespace()(state);

    // Primer elemento
    auto first = parse_prefix(state);
    if (!first) {
        state.restore(cp);
        return std::nullopt;
    }

    Rule result = *first;

    // ('.' Prefix)* — elementos adicionales
    while (true) {
        auto before = state.save();
        Whitespace()(state);

        if (!state.has_more() || state.peek() != '.') {
            state.restore(before);
            break;
        }
        state.advance(); // consume '.'

        auto next = parse_prefix(state);
        if (!next) {
            state.restore(before);
            break;
        }

        // Combina en Sequence
        Rule left = result;
        Rule right = *next;
        result = [left, right](ParserState& s) -> bool {
            auto scp = s.save();
            if (!left(s)) { s.restore(scp); return false; }
            Whitespace()(s);
            if (!right(s)) { s.restore(scp); return false; }
            return true;
        };
    }

    return result;
}

// ============================================================
// parse_pattern
// Pattern -> Pseq ('/' Pseq)*
//
// Elección priorizada entre secuencias
// Ej: num / identifier
//   → Choice(execute_rule("num"),
//       execute_rule("identifier"))
// ============================================================
std::optional<Rule> parse_pattern(ParserState& state) {
    auto cp = state.save();
    Whitespace()(state);

    // Primera alternativa
    auto first = parse_pseq(state);
    if (!first) {
        state.restore(cp);
        return std::nullopt;
    }

    Rule result = *first;

    // ('/' Pseq)* — alternativas adicionales
    while (true) {
        auto before = state.save();
        Whitespace()(state);

        if (!state.has_more() || state.peek() != '/') {
            state.restore(before);
            break;
        }
        state.advance(); // consume '/'

        auto next = parse_pseq(state);
        if (!next) {
            state.restore(before);
            break;
        }

        // Combina en Choice
        Rule left = result;
        Rule right = *next;
        result = [left, right](ParserState& s) -> bool {
            return Choice(left, right)(s);
        };
    }

    return result;
}

// ============================================================
// parse_rule
// Rule -> Identifier '->' Pattern ';'
//
// Parsea una regla individual dentro de un bloque create
// Devuelve un UserRule con nombre y Rule construida
// ============================================================
std::optional<UserRule> parse_rule(ParserState& state) {
    auto cp = state.save();
    Whitespace()(state);

    // Nombre del no-terminal
    if (!state.has_more() || !islower(state.peek())) {
        state.restore(cp);
        return std::nullopt;
    }

    std::string name;
    while (state.has_more() &&
           (islower(state.peek()) || isdigit(state.peek()))) {
        name += state.peek();
        state.advance();
    }

    Whitespace()(state);

    // '->'
    if (state.cursor + 2 > state.input.size() ||
        state.input[state.cursor] != '-' ||
        state.input[state.cursor + 1] != '>') {
        state.restore(cp);
        return std::nullopt;
    }
    state.cursor += 2;

    // Pattern → construye la Rule directamente
    auto pattern = parse_pattern(state);
    if (!pattern) {
        state.restore(cp);
        return std::nullopt;
    }

    Whitespace()(state);

    // ';'
    if (!state.has_more() || state.peek() != ';') {
        state.restore(cp);
        return std::nullopt;
    }
    state.advance();

    return UserRule{name, *pattern};
}

// ============================================================
// parse_create
// ExtStmt -> 'create' Identifier '{' Rule+ '}'
//
// Parsea un bloque de definición de gramática
// Guarda la gramática en state.defined_grammars
// NO la activa todavía
//
// Corresponde a ruleNewSyn del paper (Figura 21)
// ============================================================
bool parse_create(ParserState& state) {
    auto cp = state.save();
    Whitespace()(state);

    // 'create'
    if (!Keyword("create")(state)) {
        state.restore(cp);
        return false;
    }

    Whitespace()(state);

    // Nombre de la gramática
    if (!state.has_more() || !islower(state.peek())) {
        state.restore(cp);
        return false;
    }

    std::string grammar_name;
    while (state.has_more() &&
           (islower(state.peek()) || isdigit(state.peek()))) {
        grammar_name += state.peek();
        state.advance();
    }

    Whitespace()(state);

    // '{'
    if (!state.has_more() || state.peek() != '{') {
        state.restore(cp);
        return false;
    }
    state.advance();

    // Rule+ — una o más reglas
    UserGrammar grammar;
    grammar.name = grammar_name;

    bool found_at_least_one = false;
    while (true) {
        Whitespace()(state);

        // Si ya vemos '}', terminamos
        if (state.has_more() && state.peek() == '}')
            break;

        auto rule = parse_rule(state);
        if (!rule) break;

        grammar.rules.push_back(*rule);
        found_at_least_one = true;
    }

    if (!found_at_least_one) {
        state.restore(cp);
        return false;
    }

    Whitespace()(state);

    // '}'
    if (!state.has_more() || state.peek() != '}') {
        state.restore(cp);
        return false;
    }
    state.advance();

    // Guarda la gramática definida — NO la activa
    // Esto es exactamente lo que hace newSyn en μSugar:
    // guarda en el mapa sigma, no extiende el lenguaje
    state.defined_grammars[grammar_name] = grammar;
    return true;
}

// ============================================================
// parse_syntax
// UseStmt -> 'syntax' Identifier (',' Identifier)* '{' Stmt* '}'
//
// Activa una gramática previamente definida con 'create'
// dentro de un bloque con scope limitado
//
// Corresponde a ruleExtStmt del paper (Figura 21)
// ============================================================
bool parse_syntax(ParserState& state) {
    auto cp = state.save();
    Whitespace()(state);

    // 'syntax'
    if (!Keyword("syntax")(state)) {
        state.restore(cp);
        return false;
    }

    Whitespace()(state);

    // Recolecta nombres de gramáticas a activar
    std::vector<std::string> grammar_names;

    // Primer nombre
    if (!state.has_more() || !islower(state.peek())) {
        state.restore(cp);
        return false;
    }

    std::string first_name;
    while (state.has_more() &&
           (islower(state.peek()) || isdigit(state.peek()))) {
        first_name += state.peek();
        state.advance();
    }
    grammar_names.push_back(first_name);

    // (',' Identifier)* — nombres adicionales
    while (true) {
        auto before = state.save();
        Whitespace()(state);

        if (!state.has_more() || state.peek() != ',') {
            state.restore(before);
            break;
        }
        state.advance(); // consume ','

        Whitespace()(state);

        std::string next_name;
        while (state.has_more() &&
               (islower(state.peek()) || isdigit(state.peek()))) {
            next_name += state.peek();
            state.advance();
        }

        if (next_name.empty()) {
            state.restore(before);
            break;
        }
        grammar_names.push_back(next_name);
    }

    Whitespace()(state);

    // '{'
    if (!state.has_more() || state.peek() != '{') {
        state.restore(cp);
        return false;
    }
    state.advance();

    // Activa las gramáticas — push_scope + register_rule
    // Esto es el operador ⊳ (G-ext) del paper:
    // combina la gramática base con las extensiones
    state.push_scope();

    for (const auto& gname : grammar_names) {
        auto it = state.defined_grammars.find(gname);
        if (it == state.defined_grammars.end()) {
            // Gramática no encontrada — fallo
            state.pop_scope();
            state.restore(cp);
            return false;
        }

        // Registra cada regla de la gramática en el scope nuevo
        // Si la regla ya existe, la combina con Choice (⊳)
        for (const auto& urule : it->second.rules) {
            if (state.has_rule(urule.name)) {
                // Ya existe — combinar con Choice (operador ⊳)
                auto env_snapshot = state.env;
                std::string rname = urule.name;
                Rule vieja = [env_snapshot, rname](ParserState& s) -> bool {
                    for (auto sit = env_snapshot.rbegin();
                         sit != env_snapshot.rend(); ++sit) {
                        auto found = sit->find(rname);
                        if (found != sit->end())
                            return found->second(s);
                    }
                    return false;
                };
                Rule nueva = urule.rule;
                state.register_rule(urule.name,
                    [vieja, nueva](ParserState& s) -> bool {
                        return Choice(vieja, nueva)(s);
                    }
                );
            } else {
                // No existe — registrar directamente
                state.register_rule(urule.name, urule.rule);
            }
        }
    }

    // Parsea los statements dentro del bloque
    // usando la gramática extendida
    while (true) {
        Whitespace()(state);

        if (!state.has_more() || state.peek() == '}')
            break;

        // Intenta ejecutar "stmt" con la gramática combinada
        if (!state.execute_rule("stmt"))
            break;
    }

    Whitespace()(state);

    // '}'
    if (!state.has_more() || state.peek() != '}') {
        state.pop_scope();
        state.restore(cp);
        return false;
    }
    state.advance();

    // Desactiva la extensión — pop_scope
    // Equivale a salir del bloque syntax { } de μSugar
    state.pop_scope();

    return true;
}

#endif //METAPARSER_H
