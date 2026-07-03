#include "ParserState.h"
#include "Combinators.h"
#include "Expr.h"
#include <iostream>
#include <cassert>

// ============================================================================
// Tests originales (sin cambios) - mecanismo de scoping dinamico de reglas
// ============================================================================

const Rule DefineVar = [](ParserState& state) -> bool {
    if (state.has_more() && state.peek() == '+') {
        auto cp = state.save();
        state.advance();
        if (state.has_more()) {
            char c = state.peek();
            state.advance();
            state.register_rule("var", MatchChar(c));
            return true;
        }
        state.restore(cp);
    }
    return false;
};

const Rule UseVar = [](ParserState& state) -> bool {
    return state.execute_rule("var");
};

void run_test_1() {
    std::cout << "[Test 1] Simple sequence matching...\n";
    ParserState state("xyz");
    Rule xy = Sequence(MatchChar('x'), MatchChar('y'));

    bool result = xy(state);
    std::cout << "  Parse result of 'xy' on 'xyz': " << (result ? "SUCCESS" : "FAIL") << "\n";
    std::cout << "  Cursor position (expected 2): " << state.cursor << "\n";
    assert(result == true);
    assert(state.cursor == 2);
    std::cout << "  Test 1 passed.\n\n";
}

void run_test_2() {
    std::cout << "[Test 2] Dynamic Scoping & Environment Nesting...\n";
    ParserState state("aa");

    state.register_rule("var", MatchChar('a'));

    bool res1 = UseVar(state);
    assert(res1 == true);

    state.push_scope();
    state.register_rule("var", MatchChar('b'));

    bool res2 = UseVar(state);
    assert(res2 == false);

    state.pop_scope();
    bool res3 = UseVar(state);
    assert(res3 == true);

    std::cout << "  Test 2 passed.\n\n";
}

void run_test_3() {
    std::cout << "[Test 3] Semantic Backtracking & Rule Rollback...\n";
    ParserState state("+xz");

    Rule rule_failed_branch = Sequence(DefineVar, MatchChar('y'));
    Rule rule_success_branch = Sequence(DefineVar, MatchChar('z'));
    Rule main_rule = Choice(rule_failed_branch, rule_success_branch);

    bool result = main_rule(state);
    assert(result == true);
    assert(state.cursor == 3);

    ParserState state_use("x");
    state_use.env = state.env;
    bool res_use = UseVar(state_use);
    assert(res_use == true);

    std::cout << "  Test 3 passed.\n\n";
}

void run_adv_test_1() {
    std::cout << "[Adv Test 1] Many (Kleene Star) & Loop Protection...\n";
    {
        ParserState state("xxxx");
        Rule rule = Many(MatchChar('x'));
        bool result = rule(state);
        assert(result == true);
        assert(state.cursor == 4);
    }
    {
        ParserState state("yxxx");
        Rule rule = Many(MatchChar('x'));
        bool result = rule(state);
        assert(result == true);
        assert(state.cursor == 0);
    }
    {
        ParserState state("xyz");
        Rule non_consuming = [](ParserState&) -> bool { return true; };
        Rule rule = Many(non_consuming);
        bool result = rule(state);
        assert(result == true);
        assert(state.cursor == 0);
    }
    std::cout << "  Adv Test 1 passed.\n\n";
}

void run_adv_test_2() {
    std::cout << "[Adv Test 2] Lookahead Predicates (And / Not)...\n";
    {
        ParserState state("xyz");
        Rule look_x = AndPredicate(MatchChar('x'));
        bool result = look_x(state);
        assert(result == true);
        assert(state.cursor == 0);
    }
    {
        ParserState state("xyz");
        Rule look_not_y = NotPredicate(MatchChar('y'));
        bool result = look_not_y(state);
        assert(result == true);
        assert(state.cursor == 0);
    }
    {
        ParserState state("xyz");
        Rule look_not_x = NotPredicate(MatchChar('x'));
        bool result = look_not_x(state);
        assert(result == false);
        assert(state.cursor == 0);
    }
    std::cout << "  Adv Test 2 passed.\n\n";
}

void run_adv_test_3() {
    std::cout << "[Adv Test 3] Lookahead + Rule Scoping Rollback...\n";
    ParserState state("+x");
    Rule look_define = AndPredicate(DefineVar);
    bool result = look_define(state);
    assert(result == true);
    assert(state.cursor == 0);

    ParserState test_use("x");
    test_use.env = state.env;
    bool use_result = UseVar(test_use);
    assert(use_result == false);
    std::cout << "  Adv Test 3 passed.\n\n";
}

// ============================================================================
// Tests nuevos: Bind, Update, Constraint (Figura 7)
// ============================================================================

// Regla auxiliar: matchea y consume un caracter distinto de espacio.
const Rule NonSpaceChar = [](ParserState& state) -> bool {
    if (state.has_more() && state.peek() != ' ') {
        state.advance();
        return true;
    }
    return false;
};

void run_test_bind() {
    std::cout << "[Test Bind] 𝜗 = p captura el prefijo consumido...\n";
    ParserState state("hello world");
    Rule greeting = Bind("w", Many(NonSpaceChar));

    bool result = greeting(state);
    std::cout << "  Parse result: " << (result ? "SUCCESS" : "FAIL") << "\n";
    std::cout << "  Valor capturado en 'w': " << state.getValue("w").toDebugString() << "\n";
    assert(result == true);
    assert(state.getValue("w").isString());
    assert(state.getValue("w").asString() == "hello");

    // ¬Bind: si p falla, Bind falla y no toca el environment.
    ParserState state2("!!!");
    Rule fails = Bind("w", MatchChar('x'));
    bool result2 = fails(state2);
    assert(result2 == false);
    assert(state2.getValue("w").isUndefined());

    std::cout << "  Test Bind passed.\n\n";
}

void run_test_update_constraint() {
    std::cout << "[Test Update+Constraint] 𝜗 ← e y ?e...\n";
    ParserState state("");

    // n ← 5 . m ← n + 3 . ?(m < 10)
    Rule prog = Sequence(
        Update("n", Expr::Lit(Value(5))),
        Sequence(Update("m", Expr::BinOp("+", Expr::Ref("n"), Expr::Lit(Value(3)))),
                 Constraint(Expr::BinOp("<", Expr::Ref("m"), Expr::Lit(Value(10))))));

    bool result = prog(state);
    std::cout << "  n=" << state.getValue("n").toDebugString()
              << " m=" << state.getValue("m").toDebugString() << "\n";
    std::cout << "  Constraint (m < 10) result: " << (result ? "SUCCESS" : "FAIL") << "\n";
    assert(result == true);
    assert(state.getValue("m").asInt() == 8);

    // Ahora el constraint debe fallar: m=8, pedimos ?(m < 5)
    ParserState state2("");
    Rule prog2 = Sequence(Update("m", Expr::Lit(Value(8))),
                           Constraint(Expr::BinOp("<", Expr::Ref("m"), Expr::Lit(Value(5)))));
    bool result2 = prog2(state2);
    std::cout << "  Constraint (8 < 5) result (expected FAIL): " << (result2 ? "SUCCESS" : "FAIL")
              << "\n";
    assert(result2 == false);

    std::cout << "  Test Update+Constraint passed.\n\n";
}

// ============================================================================
// Test: composicion de gramaticas (⊎) y Call con parametros (Figura 8, 10)
// ============================================================================

void run_test_grammar_compose_and_call() {
    std::cout << "[Test Grammar+Call] Composicion de gramaticas y llamada a no-terminal...\n";

    // Gramatica 1: digit -> '1' / '2' / '3'
    RuleDef digitLow;
    digitLow.params = {};
    digitLow.returns = {"matched"};
    digitLow.body = Sequence(
        Bind("d", Choice(MatchChar('1'), Choice(MatchChar('2'), MatchChar('3')))),
        Update("matched", Expr::Ref("d")));
    Grammar g1{{"digit", digitLow}};

    // Gramatica 2: digit -> '4' / '5'
    RuleDef digitHigh;
    digitHigh.params = {};
    digitHigh.returns = {"matched"};
    digitHigh.body = Sequence(Bind("d", Choice(MatchChar('4'), MatchChar('5'))),
                               Update("matched", Expr::Ref("d")));
    Grammar g2{{"digit", digitHigh}};

    // g1 ⊎ g2: digit ahora acepta '1'..'5'
    Grammar combined = composeGrammars(g1, g2);
    assert(combined.count("digit") == 1);

    for (char c : {'1', '2', '3', '4', '5'}) {
        std::string input(1, c);  // vida propia: ParserState guarda un string_view
        ParserState state(input);
        Rule callDigit =
            Call(Expr::Lit(Value(combined)), "digit", /*args=*/{}, /*resultVars=*/{"out"});
        bool ok = callDigit(state);
        std::cout << "  digit('" << c << "') -> " << (ok ? "SUCCESS" : "FAIL")
                  << ", out=" << state.getValue("out").toDebugString() << "\n";
        assert(ok == true);
        assert(state.getValue("out").asString() == std::string(1, c));
    }

    ParserState failState("9");
    Rule callDigit =
        Call(Expr::Lit(Value(combined)), "digit", /*args=*/{}, /*resultVars=*/{"out"});
    bool failResult = callDigit(failState);
    std::cout << "  digit('9') -> " << (failResult ? "SUCCESS" : "FAIL") << " (expected FAIL)\n";
    assert(failResult == false);
    assert(failState.cursor == 0);  // rollback total en fallo

    std::cout << "  Test Grammar+Call passed.\n\n";
}

// ============================================================================
// Test: extension tipada de un language (⊳, regla L-ext, Figura 10) y
// contexto de tipos Γ. A diferencia de G-ext (que solo une gramaticas
// crudas sin garantias), L-ext parte de un language ya tipado (Grammar+Γ)
// y re-chequea consistencia de firmas al fusionar reglas con el mismo
// nombre, produciendo un Γ' extendido.
// ============================================================================

void run_test_language_ext() {
    std::cout << "[Test L-ext] Extension tipada de un language (Γ, Figura 10)...\n";

    // language1: stmt(x::Int) -> ok::Bool ; matchea 'a'
    RuleDef stmtA;
    stmtA.params = {"x"};
    stmtA.paramTypes = {Type::Int()};
    stmtA.returns = {"ok"};
    stmtA.returnTypes = {Type::Bool()};
    stmtA.body = Sequence(MatchChar('a'), Update("ok", Expr::Lit(Value(true))));
    Grammar g1{{"stmt", stmtA}};
    Gamma gamma1{{"x", Type::Int()}, {"ok", Type::Bool()}};
    LanguageValue lang1{g1, gamma1};

    // --- Caso 1: extension con firma compatible (mismos tipos/aridad) ---
    // g2 (grammar cruda, misma firma para 'stmt'): matchea '!'.
    RuleDef stmtBang;
    stmtBang.params = {"x"};
    stmtBang.paramTypes = {Type::Int()};
    stmtBang.returns = {"ok"};
    stmtBang.returnTypes = {Type::Bool()};
    stmtBang.body = Sequence(MatchChar('!'), Update("ok", Expr::Lit(Value(true))));
    Grammar g2{{"stmt", stmtBang}};

    Expr extExpr = Expr::GrammarExt(Expr::Lit(Value(lang1)), Expr::Lit(Value(g2)));
    ParserState dummy("");
    Value extended = Eval(extExpr, dummy);

    std::cout << "  L-ext con firmas compatibles -> "
              << (extended.isLanguage() ? "language OK" : "FALLO INESPERADO") << "\n";
    assert(extended.isLanguage());
    const LanguageValue& lang2 = extended.asLanguage();
    std::cout << "  Γ' tiene " << lang2.gamma.size() << " variables tipadas (esperado 2)\n";
    assert(lang2.gamma.size() == 2);
    assert(lang2.gamma.at("x") == Type::Int());
    assert(lang2.gamma.at("ok") == Type::Bool());

    // La gramatica combinada debe aceptar 'a' y '!' (Choice priorizado,
    // igual que G-ext), demostrando que L-ext no perdio la union sintactica.
    for (char c : {'a', '!'}) {
        std::string input(1, c);
        ParserState state(input);
        Rule callStmt =
            Call(Expr::Lit(Value(lang2.grammar)), "stmt", {Expr::Lit(Value(1))}, {"ok"});
        bool ok = callStmt(state);
        std::cout << "  stmt('" << c << "') sobre language extendido -> "
                  << (ok ? "SUCCESS" : "FAIL") << "\n";
        assert(ok == true);
    }

    // --- Caso 2: extension con firma incompatible (distinta aridad) ---
    // g3 declara 'stmt' sin parametros: choca con x::Int declarado en g1.
    RuleDef stmtBadArity;
    stmtBadArity.params = {};
    stmtBadArity.returns = {"ok"};
    stmtBadArity.returnTypes = {Type::Bool()};
    stmtBadArity.body = MatchChar('z');
    Grammar g3{{"stmt", stmtBadArity}};

    bool threw = false;
    std::string errMsg;
    try {
        Expr badExtExpr = Expr::GrammarExt(Expr::Lit(Value(lang1)), Expr::Lit(Value(g3)));
        Eval(badExtExpr, dummy);
    } catch (const TypeError& e) {
        threw = true;
        errMsg = e.what();
    }
    std::cout << "  L-ext con firmas incompatibles -> "
              << (threw ? "TypeError (esperado)" : "NO LANZO ERROR (mal)") << "\n";
    if (threw) std::cout << "    mensaje: " << errMsg << "\n";
    assert(threw == true);

    std::cout << "  Test L-ext passed.\n\n";
}

void run_test_call_with_params() {
    std::cout << "[Test Call params] Atributos heredados y sintetizados...\n";

    // repeatChar(n): matchea el caracter 'x' n veces, devuelve count.
    RuleDef repeatChar;
    repeatChar.params = {"n"};
    repeatChar.returns = {"count"};
    repeatChar.body = [](ParserState& state) -> bool {
        long long n = state.getValue("n").asInt();
        for (long long i = 0; i < n; ++i) {
            if (!MatchChar('x')(state)) return false;
        }
        state.setValue("count", Value(n));
        return true;
    };
    Grammar g{{"repeatChar", repeatChar}};

    ParserState state("xxxyyy");
    Rule call3x = Call(Expr::Lit(Value(g)), "repeatChar", {Expr::Lit(Value(3))}, {"result"});
    bool ok = call3x(state);
    std::cout << "  repeatChar(3) sobre 'xxxyyy' -> " << (ok ? "SUCCESS" : "FAIL")
              << ", cursor=" << state.cursor
              << ", result=" << state.getValue("result").toDebugString() << "\n";
    assert(ok == true);
    assert(state.cursor == 3);
    assert(state.getValue("result").asInt() == 3);
    // El parametro 'n' NO debe filtrarse al scope del llamador.
    assert(state.getValue("n").isUndefined());

    std::cout << "  Test Call params passed.\n\n";
}

// ============================================================================
// Demo: mini extensible-language al estilo μSugar (Figura 2 y 21)
//
// Reproduce en miniatura el patron newSyn / extBlock:
//   - "sigma" es un mapa nombre -> Grammar (como en ruleNewSyn).
//   - Una syntax nueva se registra en sigma sin afectar la gramatica base.
//   - Un "extBlock" activa una syntax componiendola con la base via ⊎
//     (el operador <+: del paper) SOLO dentro de ese bloque.
// ============================================================================

void run_demo_mini_extensible_language() {
    std::cout << "[Demo] Mini lenguaje extensible al estilo μSugar...\n";

    // --- Gramatica base: stmt -> 'a' (un statement trivial) ---
    RuleDef baseStmt;
    baseStmt.returns = {"ok"};
    baseStmt.body = Sequence(MatchChar('a'), Update("ok", Expr::Lit(Value(true))));
    Grammar baseGrammar{{"stmt", baseStmt}};

    // --- newSyn "shout": define una extension de sintaxis para stmt ---
    // Equivalente a:  define shout { stmt -> '!' ; }   (Figura 2, lineas 1-4)
    RuleDef shoutStmt;
    shoutStmt.returns = {"ok"};
    shoutStmt.body = Sequence(MatchChar('!'), Update("ok", Expr::Lit(Value(true))));
    Grammar shoutGrammar{{"stmt", shoutStmt}};

    // sigma: mapa de nombre de syntax -> Grammar (como "sigma" en ruleNewSyn)
    Value::MapType sigma;
    sigma["shout"] = Value(shoutGrammar);

    ParserState state("a!");

    // --- Bloque regular: solo la gramatica base esta activa ---
    Rule callStmt = Call(Expr::Ref("g"), "stmt", {}, {"r1"});
    state.setValue("g", Value(baseGrammar));
    bool firstOk = callStmt(state);
    std::cout << "  Statement regular ('a') sin extension -> "
              << (firstOk ? "SUCCESS" : "FAIL") << "\n";
    assert(firstOk == true);
    assert(state.cursor == 1);

    // Sin la extension activa, '!' NO séria un stmt valido:
    bool wouldFailWithoutExt = callStmt(state);
    assert(wouldFailWithoutExt == false);  // '!' no esta en baseGrammar

    // --- extBlock "shout": activa la extension componiendola con la base,
    //     SOLO para lo que sigue (equivalente a `syntax shout { ... }`) ---
    // extendedGrammar = g ⊎ sigma["shout"]   (linea 29 de ruleExtStmt)
    Grammar extended = composeGrammars(baseGrammar, sigma["shout"].asGrammar());
    state.setValue("g", Value(extended));

    Rule callStmtExt = Call(Expr::Ref("g"), "stmt", {}, {"r2"});
    bool secondOk = callStmtExt(state);
    std::cout << "  Statement extendido ('!') con 'shout' activo -> "
              << (secondOk ? "SUCCESS" : "FAIL") << "\n";
    assert(secondOk == true);
    assert(state.cursor == 2);

    // Fuera del bloque, restauramos g a la gramatica base: 'shout' deja de
    // estar disponible (igual que el scope de `syntax` en μSugar termina
    // al cerrar su bloque).
    state.setValue("g", Value(baseGrammar));

    std::cout << "  Demo passed: extension activada/desactivada por scope, "
                 "igual que 'syntax' en μSugar.\n\n";
}

int main() {
    std::cout << "===========================================\n";
    std::cout << " APEG C++ Parser Prototype - Full Demo     \n";
    std::cout << "===========================================\n\n";

    run_test_1();
    run_test_2();
    run_test_3();

    run_adv_test_1();
    run_adv_test_2();
    run_adv_test_3();

    run_test_bind();
    run_test_update_constraint();
    run_test_grammar_compose_and_call();
    run_test_language_ext();
    run_test_call_with_params();
    run_demo_mini_extensible_language();

    std::cout << "All tests passed successfully!\n";
    return 0;
}