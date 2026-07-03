#pragma once

#include "Apeg.h"

#include <optional>
#include <string>
#include <string_view>
#include <cctype>
#include <utility>

/**
 * @file MetaParser.h
 * @brief Concrete syntax for user-defined grammars, read from TEXT.
 *
 * Ported in spirit from branch `Ignacio` (v2), but reimplemented on top of
 * this engine's immutable Grammar. It compiles a textual PEG pattern into one
 * of our `PExpr` parsing expressions, so a user can WRITE a grammar extension
 * as source and have it become a first-class Grammar value:
 *
 *     create loops { stmt -> 'repeat' . num . 'times' . '{' . stmt . '}' ; }
 *
 * Pattern grammar compiled here (identifiers become Call(name), resolved at
 * run time against the current grammar):
 *
 *     pattern := seq ('/' seq)*        (ordered choice)
 *     seq     := prefix ('.' prefix)*  (sequence)
 *     prefix  := '!' term | term       (negative lookahead)
 *     term    := factor '*' | factor   (Kleene star)
 *     factor  := '\'' literal '\'' | '(' pattern ')' | identifier
 *
 * v2's two bugs are gone by construction: our grammars are immutable values
 * (no Checkpoint to forget to restore), and rules are std::function held by
 * value (no dangling Rule& reference).
 */
namespace meta {

/// A minimal text cursor for the meta level (independent of the parse State).
struct Scanner {
    std::string_view src;
    size_t pos = 0;

    bool eof() const { return pos >= src.size(); }
    char peek() const { return pos < src.size() ? src[pos] : '\0'; }
    void skipWs() { while (pos < src.size() && std::isspace((unsigned char)src[pos])) pos++; }

    bool startsWith(const std::string& w) {
        skipWs();
        return pos + w.size() <= src.size() && src.substr(pos, w.size()) == std::string_view(w);
    }
    bool accept(char c) { skipWs(); if (peek() == c) { pos++; return true; } return false; }

    /// Accept a keyword with a word boundary (so "created" != "create").
    bool acceptWord(const std::string& w) {
        skipWs();
        if (pos + w.size() > src.size() || src.substr(pos, w.size()) != std::string_view(w))
            return false;
        size_t after = pos + w.size();
        if (after < src.size() && (std::isalnum((unsigned char)src[after]) || src[after] == '_'))
            return false;
        pos = after;
        return true;
    }

    /// Read a lowercase identifier ([a-z][a-z0-9]*), or "" if none.
    std::string ident() {
        skipWs();
        std::string n;
        if (pos < src.size() && std::islower((unsigned char)peek()))
            while (pos < src.size() &&
                   (std::islower((unsigned char)src[pos]) || std::isdigit((unsigned char)src[pos])))
                n += src[pos++];
        return n;
    }
};

/// Wrap a parsing expression so it tolerates leading whitespace.
inline PExpr Tok(PExpr p) { return Seq({ Ws(), std::move(p) }); }

std::optional<PExpr> compilePattern(Scanner& sc);  // forward (mutual recursion)

inline std::optional<PExpr> compileFactor(Scanner& sc) {
    sc.skipWs();

    // 'literal'  -> match the exact text
    if (sc.peek() == '\'') {
        sc.pos++;
        std::string lit;
        while (!sc.eof() && sc.peek() != '\'') { lit += sc.peek(); sc.pos++; }
        if (sc.eof()) return std::nullopt;   // unterminated literal
        sc.pos++;                            // closing quote
        return Tok(Text(lit));
    }

    // ( pattern )  -> the inner pattern
    if (sc.peek() == '(') {
        sc.pos++;
        auto inner = compilePattern(sc);
        if (!inner) return std::nullopt;
        sc.skipWs();
        if (sc.peek() != ')') return std::nullopt;
        sc.pos++;
        return inner;
    }

    // identifier  -> call a non-terminal (resolved against the live grammar)
    if (std::islower((unsigned char)sc.peek())) {
        std::string name = sc.ident();
        return Tok(Call(name));
    }
    return std::nullopt;
}

inline std::optional<PExpr> compileTerm(Scanner& sc) {
    auto f = compileFactor(sc);
    if (!f) return std::nullopt;
    sc.skipWs();
    if (sc.peek() == '*') { sc.pos++; return Star(*f); }
    return f;
}

inline std::optional<PExpr> compilePrefix(Scanner& sc) {
    sc.skipWs();
    if (sc.peek() == '!') {
        sc.pos++;
        auto t = compileTerm(sc);
        if (!t) return std::nullopt;
        return Not(*t);
    }
    return compileTerm(sc);
}

inline std::optional<PExpr> compileSeq(Scanner& sc) {
    auto first = compilePrefix(sc);
    if (!first) return std::nullopt;
    std::vector<PExpr> parts{*first};
    while (true) {
        size_t save = sc.pos;
        sc.skipWs();
        if (sc.peek() == '.') {
            sc.pos++;
            auto n = compilePrefix(sc);
            if (!n) { sc.pos = save; break; }
            parts.push_back(*n);
        } else { sc.pos = save; break; }
    }
    return parts.size() == 1 ? parts[0] : Seq(parts);
}

inline std::optional<PExpr> compilePattern(Scanner& sc) {
    auto first = compileSeq(sc);
    if (!first) return std::nullopt;
    std::vector<PExpr> alts{*first};
    while (true) {
        size_t save = sc.pos;
        sc.skipWs();
        if (sc.peek() == '/') {
            sc.pos++;
            auto n = compileSeq(sc);
            if (!n) { sc.pos = save; break; }
            alts.push_back(*n);
        } else { sc.pos = save; break; }
    }
    return alts.size() == 1 ? alts[0] : Choice(alts);
}

/**
 * @brief Parse a  create <name> { (ident -> pattern ;)* }  block.
 * @return the grammar's name and the compiled Grammar (rules only), or
 *         nullopt on a syntax error. Does NOT activate it (like newSyn).
 */
inline std::optional<std::pair<std::string, Grammar>> parseCreate(Scanner& sc) {
    if (!sc.acceptWord("create")) return std::nullopt;
    std::string gname = sc.ident();
    if (gname.empty()) return std::nullopt;
    if (!sc.accept('{')) return std::nullopt;

    Grammar g;
    while (true) {
        sc.skipWs();
        if (sc.peek() == '}') break;

        std::string rname = sc.ident();
        if (rname.empty()) return std::nullopt;
        sc.skipWs();
        if (!(sc.peek() == '-' && sc.pos + 1 < sc.src.size() && sc.src[sc.pos + 1] == '>'))
            return std::nullopt;
        sc.pos += 2;

        auto pat = compilePattern(sc);
        if (!pat) return std::nullopt;
        sc.skipWs();
        if (sc.peek() != ';') return std::nullopt;
        sc.pos++;

        Rule r = ruleOf(*pat);
        if (g.rules.count(rname)) g.rules[rname] = ruleChoice(g.rules[rname], r);  // A -> p1 / p2
        else g.rules[rname] = r;
    }
    if (!sc.accept('}')) return std::nullopt;
    return std::make_pair(std::move(gname), std::move(g));
}

}  // namespace meta
