#include "ParserState.h"
#include "Combinators.h"
#include "Lexer.h"
#include "Grammar.h"
#include "MetaParser.h"
#include <iostream>
#include <cassert>

// ── Función helper para verificar que se consumió todo ────
bool all_consumed(const ParserState& state) {
    for (size_t i = state.cursor; i < state.input.size(); i++)
        if (!isspace(state.input[i])) return false;
    return true;
}

// ── Helper para correr un programa y reportar resultado ───
bool run_program(const std::string& input, bool verbose = true) {
    ParserState state(input);
    setup_base_grammar(state);
    ProgramRule()(state);
    bool ok = all_consumed(state);
    if (verbose) {
        std::cout << "  consumido: " << state.cursor
                  << "/" << input.size() << "\n";
        if (!ok)
            std::cout << "  restante: '"
                      << state.input.substr(state.cursor) << "'\n";
    }
    return ok;
}

// ══════════════════════════════════════════════════════════
// TEST 1 — Gramática base sin extensiones
// Verifica que print y assign funcionan solos
// ══════════════════════════════════════════════════════════
void test_base_grammar() {
    std::cout << "[Test 1] gramatica base sin extensiones\n";

    std::string input =
        "print 42;\n"
        "x := 10;\n"
        "print x;\n"
        "y := abc;\n"
        "print 99;\n";

    bool ok = run_program(input);
    std::cout << "  resultado: " << (ok ? "EXITO" : "FALLO") << "\n\n";
    assert(ok);
}

// ══════════════════════════════════════════════════════════
// TEST 2 — create define una gramática sin activarla
// La gramática se guarda pero 'repeat' no es válido todavía
// ══════════════════════════════════════════════════════════
void test_create_does_not_activate() {
    std::cout << "[Test 2] create no activa la gramatica\n";

    // Solo el create — nada de syntax
    // ProgramRule debe parsear el create exitosamente
    // pero 'repeat' no debe ser válido en el nivel superior
    std::string input_create =
        "create loops {\n"
        "  stmt -> 'repeat' . num . 'times' . '{' . stmt . '}' ;\n"
        "}\n";

    ParserState state(input_create);
    setup_base_grammar(state);
    ProgramRule()(state);

    bool create_parsed = all_consumed(state);
    bool loops_defined = state.defined_grammars.count("loops") > 0;
    bool repeat_valid  = state.execute_rule("stmt");

    std::cout << "  create parseado: "
              << (create_parsed ? "SI" : "NO") << "\n";
    std::cout << "  'loops' en defined_grammars: "
              << (loops_defined ? "SI" : "NO") << "\n";
    std::cout << "  'repeat' valido sin syntax: "
              << (repeat_valid ? "SI (MAL)" : "NO (BIEN)") << "\n\n";

    assert(create_parsed);
    assert(loops_defined);
    assert(!repeat_valid);
}

// ══════════════════════════════════════════════════════════
// TEST 3 — syntax activa y desactiva correctamente
// 'repeat' válido dentro del bloque, no fuera
// ══════════════════════════════════════════════════════════
void test_syntax_scope() {
    std::cout << "[Test 3] syntax activa dentro del bloque\n";

    // Dentro del bloque syntax: repeat es valido
    std::string input_dentro =
        "create loops {\n"
        "  stmt -> 'repeat' . num . 'times' . '{' . stmt . '}' ;\n"
        "}\n"
        "syntax loops {\n"
        "  repeat 3 times { print 42; }\n"
        "}\n";

    bool dentro = run_program(input_dentro);
    std::cout << "  repeat dentro de syntax: "
              << (dentro ? "EXITO" : "FALLO") << "\n";

    // Fuera del bloque syntax: repeat no es valido
    std::string input_fuera =
        "create loops {\n"
        "  stmt -> 'repeat' . num . 'times' . '{' . stmt . '}' ;\n"
        "}\n"
        "repeat 3 times { print 42; }\n";

    bool fuera = run_program(input_fuera, false);
    std::cout << "  repeat fuera de syntax: "
              << (!fuera ? "FALLO (BIEN)" : "EXITO (MAL)") << "\n\n";

    assert(dentro);
    assert(!fuera);
}

// ══════════════════════════════════════════════════════════
// TEST 4 — programa completo mezclando base y extensión
// print/assign antes, syntax en el medio, print/assign después
// ══════════════════════════════════════════════════════════
void test_full_program() {
    std::cout << "[Test 4] programa completo con extension\n";

    std::string input =
        "create loops {\n"
        "  stmt -> 'repeat' . num . 'times' . '{' . stmt . '}' ;\n"
        "}\n"
        "print 1;\n"
        "x := 10;\n"
        "syntax loops {\n"
        "  repeat 3 times { print 42; }\n"
        "  repeat 2 times { x := 99; }\n"
        "}\n"
        "print 2;\n"
        "y := 20;\n";

    bool ok = run_program(input);
    std::cout << "  resultado: " << (ok ? "EXITO" : "FALLO") << "\n\n";
    assert(ok);
}

// ══════════════════════════════════════════════════════════
// TEST 5 — gramática custom con choice en el patrón
// El usuario define dos alternativas con '/'
// ══════════════════════════════════════════════════════════
void test_custom_choice() {
    std::cout << "[Test 5] gramatica custom con choice\n";

    std::string input =
        "create tipos {\n"
        "  stmt -> 'int' . ident . ';' / 'str' . ident . ';' ;\n"
        "}\n"
        "syntax tipos {\n"
        "  int contador;\n"
        "  str nombre;\n"
        "  int total;\n"
        "}\n";

    bool ok = run_program(input);
    std::cout << "  resultado: " << (ok ? "EXITO" : "FALLO") << "\n\n";
    assert(ok);
}

// ══════════════════════════════════════════════════════════
// TEST 6 — gramática custom expresiva: declaración de vars
// Demuestra que el usuario puede definir cualquier sintaxis
// ══════════════════════════════════════════════════════════
void test_custom_vardecl() {
    std::cout << "[Test 6] gramatica custom: declaracion de variables\n";

    std::string input =
        "create vardecl {\n"
        "  stmt -> 'let' . ident . '=' . num . ';' ;\n"
        "}\n"
        "print 1;\n"
        "syntax vardecl {\n"
        "  let x = 10;\n"
        "  let total = 42;\n"
        "  let contador = 0;\n"
        "}\n"
        "print 2;\n";

    bool ok = run_program(input);
    std::cout << "  resultado: " << (ok ? "EXITO" : "FALLO") << "\n\n";
    assert(ok);
}

// ══════════════════════════════════════════════════════════
// TEST 7 — dos extensiones simultáneas
// syntax activa dos gramáticas a la vez con ','
// ══════════════════════════════════════════════════════════
void test_two_extensions() {
    std::cout << "[Test 7] dos extensiones simultaneas\n";

    std::string input =
        "create saludos {\n"
        "  stmt -> 'hello' . ident . ';' ;\n"
        "}\n"
        "create despedidas {\n"
        "  stmt -> 'bye' . ident . ';' ;\n"
        "}\n"
        "syntax saludos, despedidas {\n"
        "  hello mundo;\n"
        "  bye juan;\n"
        "  hello pedro;\n"
        "}\n";

    bool ok = run_program(input);
    std::cout << "  resultado: " << (ok ? "EXITO" : "FALLO") << "\n\n";
    assert(ok);
}

// ══════════════════════════════════════════════════════════
// TEST 8 — extensiones secuenciales (no simultáneas)
// Cada bloque syntax tiene su propio scope
// ══════════════════════════════════════════════════════════
void test_sequential_extensions() {
    std::cout << "[Test 8] extensiones secuenciales\n";

    std::string input =
        "create ext1 {\n"
        "  stmt -> 'foo' . ident . ';' ;\n"
        "}\n"
        "create ext2 {\n"
        "  stmt -> 'bar' . num . ';' ;\n"
        "}\n"
        "syntax ext1 {\n"
        "  foo hola;\n"
        "  foo mundo;\n"
        "}\n"
        "syntax ext2 {\n"
        "  bar 42;\n"
        "  bar 99;\n"
        "}\n"
        "print 1;\n";

    bool ok = run_program(input);
    std::cout << "  resultado: " << (ok ? "EXITO" : "FALLO") << "\n\n";
    assert(ok);
}

// ══════════════════════════════════════════════════════════
// TEST 9 — backtracking: create fallido no contamina env
// Si el bloque create es inválido, nada cambia
// ══════════════════════════════════════════════════════════
void test_failed_create() {
    std::cout << "[Test 9] create fallido no contamina el environment\n";

    // create sin cerrar la llave — inválido
    std::string input_roto = "create roto { stmt -> 'x' ;";

    ParserState state(input_roto);
    setup_base_grammar(state);
    ProgramRule()(state);

    bool roto_definido = state.defined_grammars.count("roto") > 0;
    std::cout << "  'roto' en defined_grammars: "
              << (roto_definido ? "SI (MAL)" : "NO (BIEN)") << "\n";
    std::cout << "  cursor (esperado 0): " << state.cursor << "\n\n";

    assert(!roto_definido);
    assert(state.cursor == 0);
}

// ══════════════════════════════════════════════════════════
// TEST 10 — programa real complejo
// Mezcla todo: base, múltiples creates, múltiples syntax
// ══════════════════════════════════════════════════════════
void test_complex_program() {
    std::cout << "[Test 10] programa complejo real\n";

    std::string input =
        "create loops {\n"
        "  stmt -> 'repeat' . num . 'times' . '{' . stmt . '}' ;\n"
        "}\n"
        "create vardecl {\n"
        "  stmt -> 'let' . ident . '=' . num . ';' ;\n"
        "}\n"
        "create tipos {\n"
        "  stmt -> 'int' . ident . ';' / 'str' . ident . ';' ;\n"
        "}\n"
        "print 1;\n"
        "x := 10;\n"
        "syntax vardecl {\n"
        "  let total = 100;\n"
        "  let contador = 0;\n"
        "}\n"
        "syntax tipos {\n"
        "  int resultado;\n"
        "  str nombre;\n"
        "}\n"
        "syntax loops {\n"
        "  repeat 3 times { print 42; }\n"
        "}\n"
        "print 2;\n"
        "y := 20;\n";

    bool ok = run_program(input);
    std::cout << "  resultado: " << (ok ? "EXITO" : "FALLO") << "\n\n";
    assert(ok);
}

// ══════════════════════════════════════════════════════════
// MAIN
// ══════════════════════════════════════════════════════════
int main() {
    std::cout << "===========================================\n";
    std::cout << " APEG C++ - Parser Dinamico Completo\n";
    std::cout << "===========================================\n\n";

    std::cout << "--- Gramática Base ---\n\n";
    test_base_grammar();
    test_create_does_not_activate();

    std::cout << "--- Scoping Dinámico ---\n\n";
    test_syntax_scope();
    test_full_program();

    std::cout << "--- Gramáticas Custom ---\n\n";
    test_custom_choice();
    test_custom_vardecl();

    std::cout << "--- Múltiples Extensiones ---\n\n";
    test_two_extensions();
    test_sequential_extensions();

    std::cout << "--- Robustez ---\n\n";
    test_failed_create();

    std::cout << "--- Programa Complejo ---\n\n";
    test_complex_program();

    std::cout << "===========================================\n";
    std::cout << " Todos los tests pasaron!\n";
    std::cout << "===========================================\n";
    return 0;
}

