#pragma once

#include <memory>
#include <string>

enum class TypeKind {
    Undefined,
    Int,
    Bool,
    Str,
    Sym,
    List,
    Node,
    Grammar,
    Lang
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
    static Type Grammar()    { return Type(TypeKind::Grammar); }
    static Type Lang()       { return Type(TypeKind::Lang); }

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
