#pragma once
#include "Value.h"
#include <memory>
#include <string>
#include <vector>

struct ParserState;

/**
 * @brief Nodo del lenguaje de expresiones de atributos 'e' (Figura 3 y 9-10
 * del paper). Es el lenguaje que las reglas usan para computar valores,
 * incluyendo la construccion y composicion de gramaticas sobre la marcha.
 *
 * Cubre:
 *  - Lit:        literal (regla Lit)
 *  - Ref:        referencia a variable de atributo 𝜗 (regla A-ref)
 *  - BinOp:      operador binario e ⊕ e' sobre enteros/bools (regla Op)
 *  - MapGet:     acceso a mapa e[[e']] (regla A-map)
 *  - MapSet:     actualizacion de mapa e[e'/e''] (regla U-map)
 *  - MapLit:     literal de mapa {e/e'}^n (regla L-map)
 *  - GrammarExt: composicion de gramaticas e ⊳ e' (regla G-ext / L-ext,
 *                el operador <+: del DSL Haskell)
 */
struct Expr {
    enum class Kind { Lit, Ref, BinOp, MapGet, MapSet, MapLit, GrammarExt };

    Kind kind;
    Value literal;                  // Lit
    std::string name;               // Ref
    std::string op;                 // BinOp: "+", "-", "<", "="
    std::shared_ptr<Expr> a, b, c;  // operandos (segun el Kind)
    std::vector<Expr> keys, vals;   // MapLit: pares clave/valor paralelos

    static Expr Lit(Value v);
    static Expr Ref(std::string n);
    static Expr BinOp(std::string op, Expr l, Expr r);
    static Expr MapGet(Expr m, Expr k);
    static Expr MapSet(Expr m, Expr k, Expr v);
    static Expr MapLit(std::vector<Expr> ks, std::vector<Expr> vs);
    static Expr GrammarExt(Expr l, Expr r);
};

/**
 * @brief Evalua una expresion de atributos en el environment actual del
 * ParserState. Corresponde a la relacion (Θ, e) ⤳ v del paper.
 */
Value Eval(const Expr& e, ParserState& state);
