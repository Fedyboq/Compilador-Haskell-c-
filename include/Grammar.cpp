#include "Value.h"
#include "Combinators.h"  // Choice; separado de Value.h para evitar ciclo de includes
#include <utility>

Grammar composeGrammars(const Grammar& g1, const Grammar& g2) {
    // Regla G-ext : si g1 tiene A -> p1 y g2 tiene A -> p2,
    // la gramatica compuesta g1 ⊎ g2 tiene A -> p1 / p2. Es una operacion
    // puramente sintactica: no valida tipos ni firmas (igual que el paper).
    Grammar result = g1;

    for (const auto& [name, def] : g2) {
        auto it = result.find(name);
        if (it == result.end()) {
            result[name] = def;
            continue;
        }

        RuleDef merged;
        // Se conservan params/returns (y sus tipos, si estan declarados)
        // de la primera definicion; el paper no especifica reconciliacion
        // de firmas al unir dos reglas con el mismo nombre, asume que
        // corresponden a la misma interfaz.
        merged.params = it->second.params;
        merged.returns = it->second.returns;
        merged.paramTypes = it->second.paramTypes;
        merged.returnTypes = it->second.returnTypes;
        merged.body = Choice(it->second.body, def.body);
        result[name] = std::move(merged);
    }

    return result;
}

namespace {

// Verifica que dos definiciones para el mismo no-terminal A tengan firmas
// compatibles: misma aridad de params/returns, y donde ambos lados
// declaran un tipo para la misma posicion, ese tipo debe coincidir. Esta
// es la propiedad de "consistencia de las manipulaciones" que Cardoso et
// al. [5] proponen garantizar con su type system (citado en la Seccion 1
// del paper), aplicada aqui al momento de re-chequear tipos en L-ext.
void checkSignatureCompatible(const std::string& name, const RuleDef& a, const RuleDef& b) {
    if (a.params.size() != b.params.size())
        throw TypeError("L-ext: '" + name + "' tiene distinta cantidad de parametros (" +
                         std::to_string(a.params.size()) + " vs " + std::to_string(b.params.size()) +
                         ")");
    if (a.returns.size() != b.returns.size())
        throw TypeError("L-ext: '" + name + "' tiene distinta cantidad de returns (" +
                         std::to_string(a.returns.size()) + " vs " + std::to_string(b.returns.size()) +
                         ")");

    auto checkTypes = [&](const std::vector<Type>& ta, const std::vector<Type>& tb, const char* what) {
        size_t n = std::min(ta.size(), tb.size());
        for (size_t i = 0; i < n; ++i) {
            bool bothTyped = ta[i].kind != TypeKind::Undefined && tb[i].kind != TypeKind::Undefined;
            if (bothTyped && ta[i] != tb[i]) {
                throw TypeError("L-ext: '" + name + "' tiene tipo incompatible en " + what +
                                 " #" + std::to_string(i) + " (" + ta[i].toDebugString() + " vs " +
                                 tb[i].toDebugString() + ")");
            }
        }
    };
    checkTypes(a.paramTypes, b.paramTypes, "parametro");
    checkTypes(a.returnTypes, b.returnTypes, "return");
}

}  // namespace

std::pair<Grammar, Gamma> extendLanguage(const Grammar& g1, const Gamma& gamma1, const Grammar& g2) {
    // 1. Union sintactica de gramaticas: v_g ⊎ v'_g (igual que G-ext).
    Grammar merged = composeGrammars(g1, g2);

    // 2. Γ ⊢ v_g ⊎ v'_g ⤳ Γ′: chequeo de consistencia. Para cada
    //    no-terminal presente en AMBAS gramaticas, sus firmas declaradas
    //    deben ser compatibles (si una de las dos no declaro tipos, no
    //    hay conflicto que chequear en esa posicion).
    for (const auto& [name, def2] : g2) {
        auto it1 = g1.find(name);
        if (it1 != g1.end()) {
            checkSignatureCompatible(name, it1->second, def2);
        }
    }

    // 3. Γ′ = Γ extendido con los tipos declarados por las reglas de g2
    //    (nuevas o compartidas) para cada variable de atributo (params y
    //    returns) que traiga un tipo explicito.
    Gamma gammaPrime = gamma1;
    for (const auto& [name, def] : g2) {
        for (size_t i = 0; i < def.params.size() && i < def.paramTypes.size(); ++i) {
            if (def.paramTypes[i].kind != TypeKind::Undefined) gammaPrime[def.params[i]] = def.paramTypes[i];
        }
        for (size_t i = 0; i < def.returns.size() && i < def.returnTypes.size(); ++i) {
            if (def.returnTypes[i].kind != TypeKind::Undefined)
                gammaPrime[def.returns[i]] = def.returnTypes[i];
        }
    }

    return {std::move(merged), std::move(gammaPrime)};
}