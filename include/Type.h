#pragma once

#include <memory>
#include <string>

/**
 * @file Type.h
 * @brief Representación mínima de tipos (τ) para las firmas de reglas APEG
 *        y el contexto de tipos Γ : ϑ → τ.
 *
 * Portado y adaptado del enfoque de la rama `si` (v1, Tamy). El paper no
 * formaliza el sistema de tipos completo (lo remite a Cardoso et al.), pero
 * sí exige, en la regla L-ext (Figura 10), re-chequear la consistencia de
 * las firmas al componer un `language` con una gramática nueva. Estos tipos
 * son suficientes para modelar ese chequeo sobre nuestro modelo de valores
 * (ver Value.h): Int, Bool, Str/Sym, List, Node (AST), tGrm y tLang.
 */
enum class TypeKind {
    Undefined,  ///< sin tipo declarado (no participa del chequeo)
    Int,
    Bool,
    Str,
    Sym,
    List,
    Node,       ///< nodo de AST
    Grammar,    ///< tGrm: gramática "cruda"
    Lang        ///< tLang: language ya tipado (gramática + Γ)
};

struct Type {
    TypeKind kind = TypeKind::Undefined;

    Type() = default;
    explicit Type(TypeKind k) : kind(k) {}

    static Type Undefined_() { return Type(TypeKind::Undefined); }
    static Type Int()        { return Type(TypeKind::Int); }
    static Type Bool()       { return Type(TypeKind::Bool); }
    static Type Str()        { return Type(TypeKind::Str); }
    static Type Sym()        { return Type(TypeKind::Sym); }
    static Type List()       { return Type(TypeKind::List); }
    static Type Node()       { return Type(TypeKind::Node); }
    static Type Grammar()    { return Type(TypeKind::Grammar); }   // tGrm
    static Type Lang()       { return Type(TypeKind::Lang); }      // tLang

    bool operator==(const Type& o) const { return kind == o.kind; }
    bool operator!=(const Type& o) const { return !(*this == o); }

    std::string toString() const {
        switch (kind) {
            case TypeKind::Undefined: return "?";
            case TypeKind::Int:       return "Int";
            case TypeKind::Bool:      return "Bool";
            case TypeKind::Str:       return "Str";
            case TypeKind::Sym:       return "Sym";
            case TypeKind::List:      return "List";
            case TypeKind::Node:      return "Node";
            case TypeKind::Grammar:   return "tGrm";
            case TypeKind::Lang:      return "tLang";
        }
        return "?";
    }
};
