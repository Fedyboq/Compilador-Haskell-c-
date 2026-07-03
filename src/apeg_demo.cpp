#include "Apeg.h"

#include <iostream>
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

int main() {
    std::cout << "############################################################\n";
    std::cout << "#  APEG in C++  --  full paper model (Value / attributes /  #\n";
    std::cout << "#  first-class adaptable grammar / Bind-Update-Constraint)  #\n";
    std::cout << "############################################################\n";

    demoDeclLanguage();
    demoArithmetic();

    std::cout << "\nDone.\n";
    return 0;
}
