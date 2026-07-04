#include "Apeg.h"
#include "MetaParser.h"

#include <iostream>
#include <map>
#include <memory>
#include <string>

static void banner(const std::string& title) {
    std::cout << "\n" << title << "\n";
}

static void report(const std::string& input, const ParseOutcome& out) {
    std::cout << "\n  Entrada  : \"" << input << "\"\n";
    if (out.ok) {
        std::cout << "  Resultado: ACEPTADO\n";
        std::cout << "  AST      : " << out.ast.toString() << "\n";
        std::cout << out.ast.toTree(2);
    } else {
        std::cout << "  Resultado: RECHAZADO (columna " << out.pos
                  << ", resta \"" << out.rest << "\")\n";
    }
}

static std::shared_ptr<Grammar> makeDeclLanguage() {
    auto g = std::make_shared<Grammar>();

    g->rules["Declared"] = ruleOf(Fail());

    g->rules["DefStmt"] = ruleOf(Seq({
        Text("def"), Ws(),
        Capture("id", Ident()), Ws(),

        Constraint([](State& s) -> Value {
            std::string id = s.env.at("id").s;
            auto it = s.grammar->attrs.find("declared");
            if (it != s.grammar->attrs.end())
                for (const auto& d : it->second.items)
                    if (d.s == id) return Value::Bool(false);
            return Value::Bool(true);
        }),

        Update([](State& s) {
            std::string id = s.env.at("id").s;
            Rule oldDeclared = s.grammar->rules.at("Declared");
            Rule newDeclared = [id, oldDeclared](State& st, const std::vector<Value>&) -> PResult {
                auto cp = st.save();
                PResult r = Word(id)(st);
                if (r.ok) return r;
                st.restore(cp);
                return oldDeclared(st, {});
            };
            Grammar g2 = s.grammar->with("Declared", newDeclared);
            Value decl = g2.attrs.count("declared") ? g2.attrs["declared"] : Value::List({});
            decl.items.push_back(Value::Sym(id));
            g2.attrs["declared"] = decl;
            s.grammar = std::make_shared<Grammar>(std::move(g2));
        }),

        Action([](State& s) { return Value::Node("def", { s.env.at("id") }); })
    }));

    g->rules["UseStmt"] = ruleOf(Seq({
        Text("use"), Ws(),
        Capture("id", Call("Declared")), Ws(),
        Action([](State& s) { return Value::Node("use", { s.env.at("id") }); })
    }));

    g->rules["Stmt"] = ruleOf(Choice({ Call("DefStmt"), Call("UseStmt") }));

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
    banner("DEMO A");

    auto g = makeDeclLanguage();

    report("def x; def y; use x; use y;", runGrammar(g, "Program", "def x; def y; use x; use y;"));

    report("def x; use z;", runGrammar(g, "Program", "def x; use z;"));

    report("def x; def x;", runGrammar(g, "Program", "def x; def x;"));
}

static PExpr binaryLevel(const std::string& sub, std::vector<char> ops) {

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
    banner("DEMO B");

    auto g = makeArithmetic();
    for (const std::string& in : {std::string("2+3*4-1"), std::string("(2+3)*4"), std::string("10-2-3")}) {
        ParseOutcome out = runGrammar(g, "Expr", in);
        report(in, out);
        if (out.ok) std::cout << "  Valor    : " << eval(out.ast) << "\n";
    }
}

static Grammar gramWithStmt(PExpr body, RuleSig sig) {
    Grammar g;
    g.rules["stmt"] = ruleOf(std::move(body));
    g.sigs["stmt"]  = std::move(sig);
    return g;
}

static void demoTypedLext() {
    banner("DEMO C");

    RuleSig sig;
    sig.params = {"x"};   sig.paramTypes  = {Type::Int()};
    sig.returns = {"ok"}; sig.returnTypes = {Type::Bool()};
    Grammar g1 = gramWithStmt(Text("a"), sig);
    Gamma gamma1{{"x", Type::Int()}, {"ok", Type::Bool()}};

    Value langVal = Value::Lang(std::make_shared<LanguageValue>(LanguageValue{g1, gamma1}));
    std::cout << "\n  language base como valor: " << langVal.toString()
              << " (stmt : x::Int -> ok::Bool)\n";

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

    RuleSig badSig;
    badSig.returns = {"ok"}; badSig.returnTypes = {Type::Bool()};
    Grammar g3 = gramWithStmt(Text("z"), badSig);
    try {
        extendLanguage(g1, gamma1, g3);
        std::cout << "\n  L-ext con firma INCOMPATIBLE -> no lanzo error (mal)\n";
    } catch (const TypeError& e) {
        std::cout << "\n  L-ext con firma INCOMPATIBLE -> TypeError (esperado):\n    "
                  << e.what() << "\n";
    }
}

static std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

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
    std::cout << "\n  " << title << ":\n";
    auto base = makeBaseLanguage();
    std::map<std::string, Grammar> registry;

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
    banner("DEMO D");

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

    runExtProgram("El mismo 'repeat' fuera del bloque -> rechazado",
        "create loops {\n"
        "  stmt -> 'repeat' . num . 'times' . '{' . stmt . '}' ;\n"
        "}\n"
        "repeat 3 times { print 42; }\n");
}

int main() {
    demoDeclLanguage();
    demoArithmetic();
    demoTypedLext();
    demoConcreteSyntax();
    return 0;
}
