#pragma once
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include "Type.h"

// Forward declaration: Value.h no depende de ParserState.h para evitar
// dependencia circular (ParserState SI depende de Value, para su
// value-environment).
struct ParserState;

// Un Rule en APEG es una funcion que intenta parsear el input, modificando
// el ParserState en caso de exito, y devolviendo true/false.
using Rule = std::function<bool(ParserState&)>;

/**
 * @brief Definicion de una regla con nombre dentro de una Grammar.
 *
 * Corresponde a una produccion APEG completa:
 *   ⟨A 𝜗::𝜏^n  𝑒::𝜏^m⟩ → p   (regla r)
 *
 * - params:      nombres de los atributos heredados (𝜗^n), en orden.
 * - returns:     nombres de los atributos sintetizados (𝑒^m) que la regla
 *                debe dejar escritos en el value-environment al terminar.
 * - paramTypes:  tipos declarados 𝜏^n para params (paralelo a params).
 *                Opcional: si esta vacio, o si algun elemento es
 *                TypeKind::Undefined, esa variable se trata como no
 *                tipada (no participa en el chequeo de L-ext).
 * - returnTypes: tipos declarados 𝜏^m para returns (paralelo a returns),
 *                misma convencion de opcionalidad que paramTypes.
 * - body:        la expresion de parsing p ya compilada a un Rule
 *                ejecutable, que lee sus parametros y escribe sus
 *                resultados usando ParserState::getValue / setValue.
 */
struct RuleDef {
    std::vector<std::string> params;
    std::vector<std::string> returns;
    std::vector<Type> paramTypes;
    std::vector<Type> returnTypes;
    Rule body;
};

/**
 * @brief Una Grammar es el "atributo de lenguaje" de APEG: un conjunto de
 * reglas con nombre (mapeo no-terminal -> RuleDef). Es un Value de primera
 * clase que puede pasarse, guardarse en mapas, y componerse con otras
 * gramaticas via el operador ⊎ (composeGrammars).
 */
using Grammar = std::unordered_map<std::string, RuleDef>;

/**
 * @brief Composicion de gramaticas (operador ⊎, Figura 10, regla G-ext).
 *
 * Si ambas gramaticas definen una regla para el mismo nombre A, el
 * resultado tiene A -> p1 / p2 (eleccion priorizada entre ambos cuerpos),
 * exactamente como especifica el paper. Si solo una la define, se copia
 * tal cual. Es una construccion puramente sintactica: no se verifican
 * tipos ni compatibilidad de firmas (igual que en el paper).
 *
 * Implementada en Grammar.cpp porque necesita la semantica de Choice,
 * que vive en Combinators.h (evita include circular).
 */
Grammar composeGrammars(const Grammar& g1, const Grammar& g2);

/**
 * @brief Contexto de tipos Γ : 𝜗 → 𝜏 del paper: mapea nombres de variables
 * de atributo a su tipo declarado. Es un mapa "plano" (no una pila de
 * scopes) porque va empaquetado dentro de un language value, como un
 * snapshot de que variables tiene tipificadas ese language en particular.
 */
using Gamma = std::unordered_map<std::string, Type>;

/**
 * @brief Un language : "a language is a value
 * containing a type-checked grammar together with its typing context".
 * A diferencia de una Grammar cruda (que es pura sintaxis, sin garantias),
 * un LanguageValue representa una gramatica que ya paso por el chequeo de
 * consistencia de L-ext al menos una vez, junto con el Γ resultante.
 */
struct LanguageValue {
    Grammar grammar;
    Gamma gamma;
};

/**
 * @brief Error de tipos lanzado por extendLanguage cuando dos reglas para
 * el mismo no-terminal tienen firmas incompatibles (Γ ⊢ v_g ⊎ v'_g ⤳ Γ′
 * de la regla L-ext, Figura 10, falla).
 */
struct TypeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/**
 * @brief Extension tipada de un language (regla L-ext, Figura 10):
 *
 *   (Θ, e) ⤳ v_g/Γ   (Θ, e′) ⤳ v′_g   Γ ⊢ v_g ⊎ v′_g ⤳ Γ′
 *   ───────────────────────────────────────────────────── L-ext
 *          (Θ, e ⊳ e′) ⤳ (v_g ⊎ v′_g)/Γ′
 *
 * Primero compone las gramaticas via composeGrammars (⊎, igual que
 * G-ext). Despues re-chequea consistencia de tipos sobre el resultado:
 * para cada no-terminal presente en AMBAS gramaticas, sus firmas
 * declaradas (aridad de params/returns, y los tipos de cada uno cuando
 * estan declarados en ambos lados) deben coincidir; si no, lanza
 * TypeError. Finalmente construye Γ′ extendiendo Γ con los tipos
 * declarados por las reglas de g2.
 *
 * Alcance: el paper remite el sistema de tipos completo a Cardoso et al.
 * [5] y ni su propio prototipo Haskell implementa el chequeo de buena
 * formacion; esta funcion implementa la propiedad central que ese type
 * system busca garantizar -- consistencia de firmas al fusionar reglas
 * con el mismo nombre -- sin pretender ser un sistema de tipos completo.
 *
 * @param g1     Grammar del language que se esta extendiendo.
 * @param gamma1 Γ actual de ese language.
 * @param g2     Grammar "cruda" con la que se extiende (segundo operando
 *               de ⊳, tipicamente recien construida, p.ej. via newSyn).
 * @return La gramatica compuesta junto con el Γ′ resultante.
 * @throws TypeError si algun no-terminal compartido tiene firmas
 *         incompatibles.
 */
std::pair<Grammar, Gamma> extendLanguage(const Grammar& g1, const Gamma& gamma1, const Grammar& g2);

/**
 * @brief Valor del lenguaje de atributos: enteros, booleanos,
 * strings, mapas (string -> Value) y Grammars, todos como ciudadanos de
 * primera clase que pueden viajar por el value-environment.
 */
class Value {
public:
    using MapType = std::unordered_map<std::string, Value>;

    Value() : data_(std::monostate{}) {}
    Value(bool b) : data_(b) {}
    Value(long long i) : data_(i) {}
    Value(int i) : data_(static_cast<long long>(i)) {}
    Value(double d) : data_(d) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(Grammar g) : data_(std::move(g)) {}
    Value(MapType m) : data_(std::make_shared<MapType>(std::move(m))) {}
    Value(LanguageValue lv) : data_(std::make_shared<LanguageValue>(std::move(lv))) {}

    bool isUndefined() const { return std::holds_alternative<std::monostate>(data_); }
    bool isBool() const { return std::holds_alternative<bool>(data_); }
    bool isInt() const { return std::holds_alternative<long long>(data_); }
    bool isDouble() const { return std::holds_alternative<double>(data_); }
    bool isString() const { return std::holds_alternative<std::string>(data_); }
    bool isGrammar() const { return std::holds_alternative<Grammar>(data_); }
    bool isMap() const { return std::holds_alternative<std::shared_ptr<MapType>>(data_); }
    bool isLanguage() const { return std::holds_alternative<std::shared_ptr<LanguageValue>>(data_); }

    bool asBool() const { return std::get<bool>(data_); }
    long long asInt() const { return std::get<long long>(data_); }
    double asDouble() const { return std::get<double>(data_); }
    const std::string& asString() const { return std::get<std::string>(data_); }
    const Grammar& asGrammar() const { return std::get<Grammar>(data_); }
    Grammar& asGrammarMut() { return std::get<Grammar>(data_); }
    MapType& asMap() { return *std::get<std::shared_ptr<MapType>>(data_); }
    const MapType& asMap() const { return *std::get<std::shared_ptr<MapType>>(data_); }
    LanguageValue& asLanguage() { return *std::get<std::shared_ptr<LanguageValue>>(data_); }
    const LanguageValue& asLanguage() const { return *std::get<std::shared_ptr<LanguageValue>>(data_); }

    // Igualdad estructural para tipos "planos" (usada por operadores
    // relacionales del lenguaje de atributos: '=', etc.)
    bool operator==(const Value& other) const {
        if (data_.index() != other.data_.index()) return false;
        if (isUndefined()) return true;
        if (isBool()) return asBool() == other.asBool();
        if (isInt()) return asInt() == other.asInt();
        if (isDouble()) return asDouble() == other.asDouble();
        if (isString()) return asString() == other.asString();
        // Grammar, Map y Language no se comparan estructuralmente (igual
        // que en la implementacion Haskell original, que no lo necesita).
        return false;
    }

    std::string toDebugString() const {
        if (isUndefined()) return "<undef>";
        if (isBool()) return asBool() ? "true" : "false";
        if (isInt()) return std::to_string(asInt());
        if (isDouble()) return std::to_string(asDouble());
        if (isString()) return "\"" + asString() + "\"";
        if (isGrammar()) return "<grammar:" + std::to_string(asGrammar().size()) + " rules>";
        if (isMap()) return "<map:" + std::to_string(asMap().size()) + " entries>";
        if (isLanguage())
            return "<language:" + std::to_string(asLanguage().grammar.size()) + " rules, " +
                   std::to_string(asLanguage().gamma.size()) + " typed vars>";
        return "<?>";
    }

private:
    std::variant<std::monostate, bool, long long, double, std::string, Grammar,
                 std::shared_ptr<MapType>, std::shared_ptr<LanguageValue>>
        data_;
};