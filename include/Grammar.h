//
// Created by ignac on 3/07/2026.
//

#ifndef GRAMMAR_H
#define GRAMMAR_H

#pragma once
#include "Combinators.h"
#include "Lexer.h"

/**
 * Grammar.h - Gramática base del lenguaje
 *
 * Implementa las reglas gramaticales usando los combinadores
 * y el lexer. Cada función retorna una Rule que puede ser
 * ejecutada sobre un ParserState.
 *
 * Correspondencia con el paper (Figura 3):
 * Cada función aquí es una regla r ::= <A ϑ::τⁿ e::τᵐ> → p
 * donde p está construido con los combinadores de Combinators.h
 */

bool parse_create(ParserState& state);
bool parse_syntax(ParserState& state);

// ============================================================
// EXPR
// Expr -> Number / Identifier
//
// En notación APEG:
// <Expr g::τlang> → Number g / Identifier g
// ============================================================
inline Rule ExprRule() {
    return [](ParserState& state) -> bool {
        // Intenta Number primero (Choice1 del paper)
        // Si falla, intenta Identifier (Choice2 del paper)
        return Choice(Number(), Identifier())(state);
    };
}

// ============================================================
// PRINT STMT
// PrintStmt -> 'print' Whitespace Expr ';'
//
// En notación APEG:
// <PrintStmt g::τlang> → 'print' . ws . Expr g . ';'
//
// Desglose de p:
//   'print'     → terminal (regla Term del paper)
//   Whitespace  → Star(' ') — siempre éxito
//   Expr g      → llamada a no-terminal
//   ';'         → terminal
// ============================================================
inline Rule PrintStmtRule() {
    return [](ParserState& state) -> bool {
        auto cp = state.save();

        // 'print' — terminal
        if (!Keyword("print")(state)) {
            state.restore(cp);
            return false;
        }

        // Whitespace — obligatorio después de 'print'
        if (!Whitespace()(state)) {
            state.restore(cp);
            return false;
        }

        // Expr — llamada a no-terminal
        if (!ExprRule()(state)) {
            state.restore(cp);
            return false;
        }

        // ';' — terminal con espacios opcionales
        if (!Symbol(';')(state)) {
            state.restore(cp);
            return false;
        }

        return true;
    };
}

// ============================================================
// ASSIGN STMT
// AssignStmt -> Identifier ':=' Expr ';'
//
// En notación APEG:
// <AssignStmt g::τlang> → Identifier g . ws . ':=' . ws . Expr g . ';'
//
// Nota: ':=' son dos terminales en secuencia (':' . '=')
// ============================================================
inline Rule AssignStmtRule() {
    return [](ParserState& state) -> bool {
        auto cp = state.save();

        // Identifier — nombre de la variable
        if (!Identifier()(state)) {
            state.restore(cp);
            return false;
        }

        // Whitespace opcional
        Whitespace()(state);

        // ':' seguido de '=' — dos terminales en secuencia
        if (!MatchChar(':')(state)) {
            state.restore(cp);
            return false;
        }
        if (!MatchChar('=')(state)) {
            state.restore(cp);
            return false;
        }

        // Whitespace opcional
        Whitespace()(state);

        // Expr
        if (!ExprRule()(state)) {
            state.restore(cp);
            return false;
        }

        // ';'
        if (!Symbol(';')(state)) {
            state.restore(cp);
            return false;
        }

        return true;
    };
}

// ============================================================
// STMT
// Stmt -> PrintStmt / AssignStmt / extStmt
//
// En notación APEG:
// <Stmt g::τlang> → PrintStmt g
//                 / AssignStmt g
//                 / extStmt g      ← no-terminal dinámico
//
// extStmt es una regla que se busca en el environment
// en tiempo de ejecución — esto es exactamente la llamada
// a no-terminal A ē ϑ de la Figura 8 del paper.
// La gramática puede extenderse registrando nuevas
// definiciones de "extStmt" en el environment.
// ============================================================
inline Rule StmtRule() {
    return [](ParserState& state) -> bool {
        auto cp = state.save();

        // Whitespace antes de cada statement
        Whitespace()(state);

        // Intenta 'create' — define una nueva gramática
        if (parse_create(state)) return true;
        state.restore(cp);
        Whitespace()(state);

        // Intenta 'syntax' — activa una gramática definida
        if (parse_syntax(state)) return true;
        state.restore(cp);
        Whitespace()(state);

        if (state.execute_rule("stmt"))
            return true;
        state.restore(cp);
        Whitespace()(state);

        // Intenta PrintStmt (Choice1)
        if (PrintStmtRule()(state))
            return true;
        state.restore(cp);
        Whitespace()(state);

        // Intenta AssignStmt (Choice2)
        if (AssignStmtRule()(state))
            return true;

        state.restore(cp);
        return false;
    };
}

inline Rule ProgramRule() {
    return [](ParserState& state) -> bool {
        return Many(StmtRule())(state);
    };
}


inline void setup_base_grammar(ParserState& state) {

    state.register_rule("num", Number());
    state.register_rule("ident", Identifier());
    state.register_rule("ws", Whitespace());

    state.register_rule("stmt", [](ParserState& s) -> bool {
        auto cp = s.save();
        Whitespace()(s);
        if (PrintStmtRule()(s))  return true;
        s.restore(cp);
        Whitespace()(s);
        if (AssignStmtRule()(s)) return true;
        s.restore(cp);
        return false;
    });

}

#endif //GRAMMAR_H
