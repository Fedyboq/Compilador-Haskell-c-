#include "Apeg.h"
#include "MetaParser.h"

#include <iostream>
#include <map>
#include <memory>
#include <string>

// ===========================================================================
//  Small helpers for a clean, video-friendly console output
// ===========================================================================

static void banner(const std::string& title) {
    std::cout << "\n============================================================\n";
    std::cout << " " << title << "\n";
    std::cout << "============================================================\n";
}

static void report(const std::string& input, const ParseOutcome& out) {
    std::cout << "\n  Input : \"" << input << "\"\n";
    if (out.ok) {
        std::cout << "  Result: ACCEPTED\n";
        std::cout << "  AST   : " << out.ast.toString() << "\n";
        std::cout << "  Tree  :\n";
        std::string tree = out.ast.toTree(2);
        std::cout << tree;
    } else {
        std::cout << "  Result: REJECTED\n";
        std::cout << "  Stopped at column " << out.pos << " -> \"" << out.rest << "\"\n";
    }
}

// ===========================================================================
//  DEMO A -- An extensible "declare then use" language.
//
//  Grammar (informally):
//     Program <- ( Stmt ';' )*
//     Stmt    <- DefStmt / UseStmt
//     DefStmt <- "def" Ident        // adapts the grammar so the id is usable,
//                                    // and rejects redefinitions (semantic check)
//     UseStmt <- "use" Declared     // Declared only matches previously def-ined ids
//     Declared <- (grown at runtime)
//
//  This is impossible in a plain PEG: whether "use z" parses depends on what
//  the program declared earlier.  The parser edits its own grammar mid-parse
//  (Update) and enforces semantic rules (Constraint).
// ===========================================================================

static std::shared_ptr<Grammar> makeDeclLanguage() {
    auto g = std::make_shared<Grammar>();

    // The dynamic non-terminal. Initially it accepts nothing.
    g->rules["Declared"] = ruleOf(Fail());

    // DefStmt: declare a fresh identifier.
    g->rules["DefStmt"] = ruleOf(Seq({
        Text("def"), Ws(),
        Capture("id", Ident()), Ws(),

        // Semantic check: reject redefinition of an already-declared id.
        // The set of declared ids is a grammar-level attribute, so it threads
        // through the parse (persists across statements, rolls back on failure).
        Constraint([](State& s) -> Value {
            std::string id = s.env.at("id").s;
            auto it = s.grammar->attrs.find("declared");
            if (it != s.grammar->attrs.end())
                for (const auto& d : it->second.items)
                    if (d.s == id) return Value::Bool(false);   // already declared -> fail
            return Value::Bool(true);
        }),

        // ADAPT THE GRAMMAR: grow "Declared" so it now also matches this id,
        // and record the id in the grammar's "declared" attribute.
        Update([](State& s) {
            std::string id = s.env.at("id").s;
            Rule oldDeclared = s.grammar->rules.at("Declared");
            Rule newDeclared = [id, oldDeclared](State& st, const std::vector<Value>&) -> PResult {
                auto cp = st.save();
                PResult r = Word(id)(st);
                if (r.ok) return r;              // matched the freshly declared id
                st.restore(cp);
                return oldDeclared(st, {});      // otherwise fall back to previous ids
            };
            Grammar g2 = s.grammar->with("Declared", newDeclared);
            Value decl = g2.attrs.count("declared") ? g2.attrs["declared"] : Value::List({});
            decl.items.push_back(Value::Sym(id));
            g2.attrs["declared"] = decl;
            s.grammar = std::make_shared<Grammar>(std::move(g2));
        }),

        // Synthesized attribute: an AST node.
        Action([](State& s) { return Value::Node("def", { s.env.at("id") }); })
    }));

    // UseStmt: use an identifier that MUST already be declared.
    g->rules["UseStmt"] = ruleOf(Seq({
        Text("use"), Ws(),
        Capture("id", Call("Declared")), Ws(),
        Action([](State& s) { return Value::Node("use", { s.env.at("id") }); })
    }));

    g->rules["Stmt"] = ruleOf(Choice({ Call("DefStmt"), Call("UseStmt") }));

    // Program: a sequence of ';'-terminated statements, collected into an AST.
    g->rules["Program"] = ruleOf(Seq({
        Ws(),
        Capture("stmts", Star(Seq({
            Capture("st", Call("Stmt")), Ws(), Lit(';'), Ws(),
            Action(AVar("st"))
        }))),
        Action([](State& s) {
            Value prog = Value::Node("program", {});
            prog.items = s.env.at("stmts").items;
            return prog;
        })
    }));

    return g;
}

static void demoDeclLanguage() {
    banner("DEMO A - Extensible 'declare then use' language (adaptable grammar)");
    std::cout << "\n  The parser edits its OWN grammar while parsing:\n";
    std::cout << "   * 'def x' teaches the grammar a new usable identifier (Update).\n";
    std::cout << "   * 'use x' only parses if x was declared before (data-dependent).\n";
    std::cout << "   * redefining an id is rejected as a semantic error (Constraint).\n";

    auto g = makeDeclLanguage();

    // 1) Well-formed program: every 'use' refers to a declared id.
    report("def x; def y; use x; use y;", runGrammar(g, "Program", "def x; def y; use x; use y;"));

    // 2) Use before declaration: 'use z' has no matching grammar rule -> rejected.
    report("def x; use z;", runGrammar(g, "Program", "def x; use z;"));

    // 3) Redefinition: declaring 'x' twice violates a semantic constraint.
    report("def x; def x;", runGrammar(g, "Program", "def x; def x;"));
}

// ===========================================================================
//  DEMO B -- Arithmetic with synthesized attributes (dynamic AST construction).
//
//     Expr   <- Term  ( ('+'|'-') Term )*
//     Term   <- Factor( ('*'|'/') Factor )*
//     Factor <- Number | '(' Expr ')'
//
//  Each non-terminal SYNTHESIZES an AST (left-associative).  A tiny evaluator
//  then walks that AST -- parser front-end + evaluation back-end, in miniature.
// ===========================================================================

static PExpr binaryLevel(const std::string& sub, std::vector<char> ops) {
    // head sub, then repeat (op sub), folding left-associatively into a Node.
    std::vector<PExpr> opChoices;
    for (char c : ops) opChoices.push_back(Lit(c));

    return Seq({
        Ws(),
        Capture("head", Call(sub)),
        Capture("tail", Star(Seq({
            Ws(),
            Capture("op", Choice(opChoices)), Ws(),
            Capture("rhs", Call(sub)),
            Action([](State& s) {
                // partial node carrying the operator and its right operand
                return Value::Node(s.env.at("op").s, { s.env.at("rhs") });
            })
        }))),
        Action([](State& s) -> Value {
            Value acc = s.env.at("head");
            for (const auto& part : s.env.at("tail").items)
                acc = Value::Node(part.s, { acc, part.items.at(0) });
            return acc;
        })
    });
}

static std::shared_ptr<Grammar> makeArithmetic() {
    auto g = std::make_shared<Grammar>();
    g->rules["Expr"]   = ruleOf(binaryLevel("Term",   {'+', '-'}));
    g->rules["Term"]   = ruleOf(binaryLevel("Factor", {'*', '/'}));
    g->rules["Factor"] = ruleOf(Choice({
        Seq({ Ws(), Number() }),
        // Parentheses: synthesize the INNER expression, not the ')' token.
        Seq({ Ws(), Lit('('), Capture("inner", Call("Expr")), Ws(), Lit(')'),
              Action(AVar("inner")) })
    }));
    return g;
}

static long eval(const Value& v) {
    if (v.kind == Value::Kind::Int) return v.i;
    if (v.kind == Value::Kind::Node && v.items.size() == 2) {
        long a = eval(v.items[0]);
        long b = eval(v.items[1]);
        if (v.s == "+") return a + b;
        if (v.s == "-") return a - b;
        if (v.s == "*") return a * b;
        if (v.s == "/") return b != 0 ? a / b : 0;
    }
    return 0;
}

static void demoArithmetic() {
    banner("DEMO B - Arithmetic: synthesized attributes build an AST");

    auto g = makeArithmetic();
    for (const std::string& in : {std::string("2+3*4-1"), std::string("(2+3)*4"), std::string("10-2-3")}) {
        ParseOutcome out = runGrammar(g, "Expr", in);
        report(in, out);
        if (out.ok) std::cout << "  Value : " << eval(out.ast) << "  (evaluated from the AST)\n";
    }
}

// ===========================================================================
//  DEMO C -- Typed language extension (L-ext, Figure 10). Ported from v1.
//
//  A *language* is a type-checked grammar + its typing context Γ. Extending it
//  (⊳) first composes the grammars (⊎), then RE-CHECKS that any non-terminal
//  defined on both sides has a compatible signature. A clash -> TypeError.
//  This is the paper's central theoretical distinction: G-ext (pure syntax)
//  vs L-ext (syntax + type consistency).
// ===========================================================================

static Grammar gramWithStmt(PExpr body, RuleSig sig) {
    Grammar g;
    g.rules["stmt"] = ruleOf(std::move(body));
    g.sigs["stmt"]  = std::move(sig);
    return g;
}

static void demoTypedLext() {
    banner("DEMO C - Typed language + L-ext (Gamma & type checking, Fig. 10)");

    // Base language: stmt(x::Int) -> ok::Bool ; matches 'a'.
    RuleSig sig;
    sig.params = {"x"};   sig.paramTypes  = {Type::Int()};
    sig.returns = {"ok"}; sig.returnTypes = {Type::Bool()};
    Grammar g1 = gramWithStmt(Text("a"), sig);
    Gamma gamma1{{"x", Type::Int()}, {"ok", Type::Bool()}};

    // A language is a first-class Value (Value::Lang).
    Value langVal = Value::Lang(std::make_shared<LanguageValue>(LanguageValue{g1, gamma1}));
    std::cout << "\n  language base como valor: " << langVal.toString()
              << " (stmt : x::Int -> ok::Bool)\n";

    // Case 1: compatible extension (same signature), body matches '!'.
    Grammar g2 = gramWithStmt(Text("!"), sig);
    try {
        auto [merged, gammaP] = extendLanguage(g1, gamma1, g2);
        std::cout << "\n  L-ext con firma COMPATIBLE -> OK; Gamma' tiene "
                  << gammaP.size() << " variables tipadas\n";
        auto gp = std::make_shared<Grammar>(merged);
        for (const std::string& in : {std::string("a"), std::string("!")}) {
            ParseOutcome out = runGrammar(gp, "stmt", in);
            std::cout << "    stmt(\"" << in << "\") sobre language extendido -> "
                      << (out.ok ? "ACEPTADO" : "RECHAZADO") << "\n";
        }
    } catch (const TypeError& e) {
        std::cout << "    inesperado: " << e.what() << "\n";
    }

    // Case 2: incompatible extension (different arity) -> TypeError.
    RuleSig badSig;
    badSig.returns = {"ok"}; badSig.returnTypes = {Type::Bool()};   // params vacío -> aridad distinta
    Grammar g3 = gramWithStmt(Text("z"), badSig);
    try {
        extendLanguage(g1, gamma1, g3);
        std::cout << "\n  L-ext con firma INCOMPATIBLE -> no lanzo error (mal)\n";
    } catch (const TypeError& e) {
        std::cout << "\n  L-ext con firma INCOMPATIBLE -> TypeError (esperado):\n    "
                  << e.what() << "\n";
    }
}

// ===========================================================================
//  DEMO D -- Concrete syntax read from TEXT (create / syntax). Ported from v2.
//
//  A user writes a grammar extension as source; `create` compiles it into a
//  first-class Grammar; `syntax name { ... }` composes it with the base via
//  the generic ⊎ operator and parses the block with that extended grammar.
//  Outside the block the extension is gone -- scoping is automatic because
//  grammars are immutable values (v2's Checkpoint bug cannot occur here).
// ===========================================================================

static std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// The fixed base language: print/assign statements over numbers/identifiers.
static std::shared_ptr<Grammar> makeBaseLanguage() {
    auto g = std::make_shared<Grammar>();
    g->rules["num"]        = ruleOf(Seq({ Ws(), Number() }));
    g->rules["ident"]      = ruleOf(Seq({ Ws(), Ident() }));
    g->rules["expr"]       = ruleOf(Choice({ Call("num"), Call("ident") }));
    g->rules["printStmt"]  = ruleOf(Seq({ Ws(), Word("print"), Ws(), Call("expr"), Ws(), Lit(';') }));
    g->rules["assignStmt"] = ruleOf(Seq({ Ws(), Ident(), Ws(), Text(":="), Ws(), Call("expr"), Ws(), Lit(';') }));
    g->rules["stmt"]       = ruleOf(Choice({ Call("printStmt"), Call("assignStmt") }));
    return g;
}

// Parse ONE statement of grammar `g` at text[pos]; advance pos on success.
static bool parseOneStmt(const std::shared_ptr<Grammar>& g, const std::string& text, size_t& pos) {
    State s;
    s.input = text;
    s.cursor = pos;
    s.grammar = g;
    PResult r = Call("stmt")(s);
    if (r.ok) { pos = s.cursor; return true; }
    return false;
}

static void runExtProgram(const std::string& title, const std::string& text) {
    std::cout << "\n  ---- " << title << " ----\n";
    auto base = makeBaseLanguage();
    std::map<std::string, Grammar> registry;   // named extensions from `create`

    meta::Scanner sc;
    sc.src = text;
    bool ok = true;
    std::string err;

    while (ok) {
        sc.skipWs();
        if (sc.eof()) break;

        if (sc.startsWith("create")) {
            auto c = meta::parseCreate(sc);
            if (!c) { ok = false; err = "bloque 'create' mal formado"; break; }
            registry[c->first] = c->second;
            std::cout << "  [create] gramatica '" << c->first << "' definida ("
                      << c->second.rules.size() << " regla/s), NO activada\n";
            continue;
        }

        if (sc.startsWith("syntax")) {
            sc.acceptWord("syntax");
            std::vector<std::string> names{sc.ident()};
            while (sc.accept(',')) names.push_back(sc.ident());
            if (!sc.accept('{')) { ok = false; err = "'syntax' sin '{'"; break; }

            // Activate: base ⊎ each named extension (generic G-ext operator).
            Grammar active = *base;
            for (const auto& n : names) {
                auto it = registry.find(n);
                if (it == registry.end()) { ok = false; err = "gramatica '" + n + "' no definida"; break; }
                active = composeGrammars(active, it->second);
            }
            if (!ok) break;
            auto activePtr = std::make_shared<Grammar>(std::move(active));

            std::cout << "  [syntax ";
            for (size_t i = 0; i < names.size(); ++i) { if (i) std::cout << ", "; std::cout << names[i]; }
            std::cout << "] activada; parseando bloque con gramatica extendida...\n";

            while (true) {
                sc.skipWs();
                if (sc.peek() == '}') { sc.pos++; break; }
                if (sc.eof()) { ok = false; err = "bloque 'syntax' sin cerrar"; break; }
                size_t before = sc.pos;
                if (!parseOneStmt(activePtr, text, sc.pos)) {
                    ok = false; err = "statement invalido dentro de 'syntax'"; break;
                }
                std::cout << "      ok (extendido): " << trimmed(text.substr(before, sc.pos - before)) << "\n";
            }
            if (!ok) break;
            std::cout << "  [syntax] bloque cerrado; extension desactivada\n";
            continue;
        }

        // Otherwise: a plain base statement.
        size_t before = sc.pos;
        if (!parseOneStmt(base, text, sc.pos)) {
            ok = false; err = "statement base invalido"; break;
        }
        std::cout << "  ok (base): " << trimmed(text.substr(before, sc.pos - before)) << "\n";
    }

    if (ok) std::cout << "  => PROGRAMA ACEPTADO\n";
    else    std::cout << "  => RECHAZADO en columna " << sc.pos << ": " << err
                      << "  -> \"" << trimmed(text.substr(sc.pos > text.size() ? text.size() : sc.pos)) << "\"\n";
}

static void demoConcreteSyntax() {
    banner("DEMO D - Sintaxis concreta desde texto: 'create' / 'syntax'");
    std::cout << "\n  El usuario ESCRIBE una extension de gramatica como texto;\n";
    std::cout << "  'create' la compila a una Grammar de primera clase y 'syntax'\n";
    std::cout << "  la activa (via el operador generico union) solo dentro del bloque.\n";

    // 1) 'repeat' es valido DENTRO de un bloque syntax loops { ... }.
    runExtProgram("Extension activa dentro del bloque",
        "create loops {\n"
        "  stmt -> 'repeat' . num . 'times' . '{' . stmt . '}' ;\n"
        "}\n"
        "print 1;\n"
        "x := 10;\n"
        "syntax loops {\n"
        "  repeat 3 times { print 42; }\n"
        "}\n"
        "print 2;\n");

    // 2) El MISMO 'repeat' FUERA del bloque es rechazado (la extension no existe).
    runExtProgram("El mismo 'repeat' fuera del bloque -> rechazado",
        "create loops {\n"
        "  stmt -> 'repeat' . num . 'times' . '{' . stmt . '}' ;\n"
        "}\n"
        "repeat 3 times { print 42; }\n");
}

int main() {
    std::cout << "############################################################\n";
    std::cout << "#  APEG in C++  --  full paper model (Value / attributes /  #\n";
    std::cout << "#  first-class adaptable grammar / Bind-Update-Constraint / #\n";
    std::cout << "#  typed L-ext / concrete 'create'-'syntax' from text)      #\n";
    std::cout << "############################################################\n";

    demoDeclLanguage();
    demoArithmetic();
    demoTypedLext();
    demoConcreteSyntax();

    std::cout << "\nDone.\n";
    return 0;
}
