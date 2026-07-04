#pragma once

#include "Apeg.h"

#include <optional>
#include <string>
#include <string_view>
#include <cctype>
#include <utility>

namespace meta {

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

inline PExpr Tok(PExpr p) { return Seq({ Ws(), std::move(p) }); }

std::optional<PExpr> compilePattern(Scanner& sc);

inline std::optional<PExpr> compileFactor(Scanner& sc) {
    sc.skipWs();

    if (sc.peek() == '\'') {
        sc.pos++;
        std::string lit;
        while (!sc.eof() && sc.peek() != '\'') { lit += sc.peek(); sc.pos++; }
        if (sc.eof()) return std::nullopt;
        sc.pos++;
        return Tok(Text(lit));
    }

    if (sc.peek() == '(') {
        sc.pos++;
        auto inner = compilePattern(sc);
        if (!inner) return std::nullopt;
        sc.skipWs();
        if (sc.peek() != ')') return std::nullopt;
        sc.pos++;
        return inner;
    }

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
        if (g.rules.count(rname)) g.rules[rname] = ruleChoice(g.rules[rname], r);
        else g.rules[rname] = r;
    }
    if (!sc.accept('}')) return std::nullopt;
    return std::make_pair(std::move(gname), std::move(g));
}

}
