#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sstream>

struct Grammar;

struct LanguageValue;

struct Value {
    enum class Kind {
        Unit,
        Int,
        Bool,
        Str,
        Sym,
        List,
        Node,
        Gram,
        Lang
    };

    Kind kind = Kind::Unit;
    long i = 0;
    bool b = false;
    std::string s;
    std::vector<Value> items;
    std::shared_ptr<Grammar> gram;
    std::shared_ptr<LanguageValue> lang;

    static Value Unit()                  { return Value{}; }
    static Value Int(long v)             { Value x; x.kind = Kind::Int;  x.i = v; return x; }
    static Value Bool(bool v)            { Value x; x.kind = Kind::Bool; x.b = v; return x; }
    static Value Str(std::string v)      { Value x; x.kind = Kind::Str;  x.s = std::move(v); return x; }
    static Value Sym(std::string v)      { Value x; x.kind = Kind::Sym;  x.s = std::move(v); return x; }
    static Value List(std::vector<Value> v) {
        Value x; x.kind = Kind::List; x.items = std::move(v); return x;
    }
    static Value Node(std::string label, std::vector<Value> children = {}) {
        Value x; x.kind = Kind::Node; x.s = std::move(label); x.items = std::move(children); return x;
    }
    static Value Gram(std::shared_ptr<Grammar> g) {
        Value x; x.kind = Kind::Gram; x.gram = std::move(g); return x;
    }
    static Value Lang(std::shared_ptr<LanguageValue> l) {
        Value x; x.kind = Kind::Lang; x.lang = std::move(l); return x;
    }

    bool truthy() const {
        switch (kind) {
            case Kind::Bool: return b;
            case Kind::Int:  return i != 0;
            case Kind::Unit: return false;
            default:         return true;
        }
    }

    std::string toString() const {
        std::ostringstream os;
        switch (kind) {
            case Kind::Unit: os << "()"; break;
            case Kind::Int:  os << i;    break;
            case Kind::Bool: os << (b ? "true" : "false"); break;
            case Kind::Str:  os << '"' << s << '"'; break;
            case Kind::Sym:  os << s; break;
            case Kind::Gram: os << "<grammar>"; break;
            case Kind::Lang: os << "<language>"; break;
            case Kind::List: {
                os << '[';
                for (size_t k = 0; k < items.size(); ++k) {
                    if (k) os << ", ";
                    os << items[k].toString();
                }
                os << ']';
                break;
            }
            case Kind::Node: {
                os << '(' << s;
                for (const auto& c : items) os << ' ' << c.toString();
                os << ')';
                break;
            }
        }
        return os.str();
    }

    std::string toTree(int indent = 0) const {
        std::ostringstream os;
        std::string pad(static_cast<size_t>(indent) * 2, ' ');
        if (kind == Kind::Node) {
            os << pad << s << '\n';
            for (const auto& c : items) os << c.toTree(indent + 1);
        } else {
            os << pad << toString() << '\n';
        }
        return os.str();
    }
};
