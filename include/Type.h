#pragma once
#include <memory>
#include <string>

/**
 * @brief Representacion minima de tipos (𝜏, meta-sintaxis 𝑚𝑡 ::= 𝜏̊ en la
 * Figura 3) para la tipificacion de atributos 𝜗 :: 𝜏 usada en las firmas
 * de reglas (⟨𝐴 𝜗::𝜏^𝑛 𝑒::𝜏^𝑚⟩ → 𝑝) y en el contexto de tipos Γ : 𝜗 → 𝜏.
 *
 * El paper no formaliza en este trabajo el sistema de tipos completo (lo
 * remite a Cardoso et al. [5], "An Attribute Language Definition for
 * Adaptable Parsing Expression Grammars"); tampoco lo implementa por
 * completo su propio prototipo Haskell ("no realiza el chequeo de buena
 * formacion"). Esta es una representacion suficiente para modelar los
 * tipos que aparecen en los ejemplos del paper (tLang, tMap tGrm, INT,
 * String, etc., Figuras 19-21) y para poder implementar la regla L-ext
 * (Figura 10), que exige re-chequear consistencia de tipos al componer
 * un language con una nueva gramatica.
 */
enum class TypeKind { Undefined, Int, Bool, Double, String, Grammar, Lang, Map };

struct Type {
    TypeKind kind = TypeKind::Undefined;
    std::shared_ptr<Type> elem;  // solo se usa cuando kind == Map (tipo del valor)

    Type() = default;
    explicit Type(TypeKind k, std::shared_ptr<Type> e = nullptr) : kind(k), elem(std::move(e)) {}

    static Type Undefined_() { return Type(TypeKind::Undefined); }
    static Type Int() { return Type(TypeKind::Int); }
    static Type Bool() { return Type(TypeKind::Bool); }
    static Type Double() { return Type(TypeKind::Double); }
    static Type String() { return Type(TypeKind::String); }
    // tGrm: tipo de una Grammar "cruda" (aun no promovida a language).
    static Type Grammar() { return Type(TypeKind::Grammar); }
    // tLang: tipo de un language (grammar + Γ, ya type-checked).
    static Type Lang() { return Type(TypeKind::Lang); }
    // tMap t: tipo de un mapa cuyos valores son de tipo t (p.ej. tMap tGrm).
    static Type Map(Type valueType) {
        return Type(TypeKind::Map, std::make_shared<Type>(std::move(valueType)));
    }

    bool operator==(const Type& other) const {
        if (kind != other.kind) return false;
        if (kind == TypeKind::Map) {
            if (!elem || !other.elem) return elem == other.elem;
            return *elem == *other.elem;
        }
        return true;
    }
    bool operator!=(const Type& other) const { return !(*this == other); }

    std::string toDebugString() const {
        switch (kind) {
            case TypeKind::Undefined: return "?";
            case TypeKind::Int: return "Int";
            case TypeKind::Bool: return "Bool";
            case TypeKind::Double: return "Double";
            case TypeKind::String: return "String";
            case TypeKind::Grammar: return "tGrm";
            case TypeKind::Lang: return "tLang";
            case TypeKind::Map: return "tMap " + (elem ? elem->toDebugString() : std::string("?"));
        }
        return "?";
    }
};