//
// Created by ignac on 3/07/2026.
//

#ifndef LEXER_H
#define LEXER_H

#pragma once
#include "Combinators.h"
#include <cctype>
#include <string>

/**
 * Lexer.h - Parsers básicos para tokens del lenguaje
 *
 * Estos son los "terminales" de nuestra gramática —
 * las piezas más pequeñas que reconocen texto concreto.
 * Corresponden a los terminales 'a' de la Figura 3 del paper.
 */

/**
 * Espacios en blanco — siempre éxito, consume 0 o más espacios.
 * Corresponde a la regla Star(MatchChar(' ')) del paper.
 */
inline Rule Whitespace() {
    return [](ParserState& state) -> bool {
        while (state.has_more() && isspace(state.peek()))
            state.advance();
        return true;
    };
}

/**
 * Palabra clave exacta.
 * Verifica además que no siga una letra/dígito
 * (evita que "printX" matchee como "print").
 *
 * Corresponde a una secuencia de terminales seguida
 * de un not-predicate: 'p'.'r'.'i'.'n'.'t'.!(alnum)
 */
inline Rule Keyword(const std::string& kw) {
    return [kw](ParserState& state) -> bool {
        auto cp = state.save();

        // verificar que hay suficientes caracteres
        if (state.cursor + kw.size() > state.input.size()) {
            state.restore(cp);
            return false;
        }

        // verificar cada caracter de la keyword
        for (size_t i = 0; i < kw.size(); i++) {
            if (state.input[state.cursor + i] != kw[i]) {
                state.restore(cp);
                return false;
            }
        }
        state.cursor += kw.size();

        // not-predicate: no debe seguir letra o dígito
        // esto es !alnum del paper
        if (state.has_more() && isalnum(state.peek())) {
            state.restore(cp);
            return false;
        }
        return true;
    };
}

/**
 * Número entero — uno o más dígitos.
 * Corresponde a [0-9]+ del paper, que es
 * Digit . Star(Digit) donde Digit es
 * Choice('0', Choice('1', ... Choice('8','9')...))
 */
inline Rule Number() {
    return [](ParserState& state) -> bool {
        if (!state.has_more() || !isdigit(state.peek()))
            return false;
        while (state.has_more() && isdigit(state.peek()))
            state.advance();
        return true;
    };
}

/**
 * Identificador — letra minúscula seguida de letras/dígitos.
 * Corresponde a [a-z][a-z0-9]* del paper.
 * Falla si empieza con dígito o mayúscula.
 */
inline Rule Identifier() {
    return [](ParserState& state) -> bool {
        if (!state.has_more() || !islower(state.peek()))
            return false;
        while (state.has_more() &&
               (islower(state.peek()) || isdigit(state.peek())))
            state.advance();
        return true;
    };
}

/**
 * Caracter específico con espacios opcionales alrededor.
 * Útil para símbolos como ';', '{', '}'.
 */
inline Rule Symbol(char c) {
    return [c](ParserState& state) -> bool {
        auto cp = state.save();
        // espacios opcionales antes
        while (state.has_more() && isspace(state.peek()))
            state.advance();
        if (!state.has_more() || state.peek() != c) {
            state.restore(cp);
            return false;
        }
        state.advance();
        return true;
    };
}

#endif //LEXER_H
