#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sstream>

// Forward declaration: a Grammar is itself a first-class Value in APEG.
struct Grammar;

/**
 * @brief A generic, first-class Value in the APEG language.
 *
 * This is the piece that turns the recognizer (a parser that only answers
 * "matched / did not match") into a full language following the paper's model.
 * In an APEG, attributes carry values, and those values can be integers,
 * strings, symbols, lists, AST nodes -- and even *grammars themselves*.
 * Because a Grammar can live inside a Value, grammars become first-class
 * citizens that can be stored, passed as attributes and composed dynamically.
 */
struct Value {
    enum class Kind {
        Unit,   ///< The empty/no-result value.
        Int,    ///< A machine integer.
        Bool,   ///< A boolean.
        Str,    ///< A text string (a literal piece of source, etc.).
        Sym,    ///< A symbol/identifier (semantically distinct from Str).
        List,   ///< An ordered list of Values.
        Node,   ///< An AST node: a labelled Value with children.
        Gram    ///< A grammar, treated as a value (first-class grammar).
    };

    Kind kind = Kind::Unit;
    long i = 0;                          ///< Int payload.
    bool b = false;                      ///< Bool payload.
    std::string s;                       ///< Str / Sym payload, or Node label.
    std::vector<Value> items;            ///< List elements or Node children.
    std::shared_ptr<Grammar> gram;       ///< Gram payload (grammar as a value).

    // --- Factory helpers (clearer than aggregate initialisation) ---

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

    // --- Inspection ---

    bool truthy() const {
        switch (kind) {
            case Kind::Bool: return b;
            case Kind::Int:  return i != 0;
            case Kind::Unit: return false;
            default:         return true;
        }
    }

    /**
     * @brief Renders the value (and any AST it contains) as readable text.
     *        Used by the demos so a video can show the synthesized attributes.
     */
    std::string toString() const {
        std::ostringstream os;
        switch (kind) {
            case Kind::Unit: os << "()"; break;
            case Kind::Int:  os << i;    break;
            case Kind::Bool: os << (b ? "true" : "false"); break;
            case Kind::Str:  os << '"' << s << '"'; break;
            case Kind::Sym:  os << s; break;
            case Kind::Gram: os << "<grammar>"; break;
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

    /**
     * @brief Pretty-prints an AST node as an indented tree (nice for videos).
     */
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
