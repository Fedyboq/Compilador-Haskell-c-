#pragma once
#include "Value.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @brief Representa el estado de parsing: cursor sobre el input, el
 * environment de reglas dinamicas (mecanismo original del MVP), y el
 * value-environment de atributos (V en Θ = ⟨V, Γ⟩ del paper).
 */
struct ParserState {
    std::string_view input;
    size_t cursor = 0;

    // --- Environment de reglas con nombre (mecanismo original del MVP) ---
    // Permite registrar dinamicamente una Rule con nombre en el scope
    // actual y ejecutarla por nombre mas tarde. Se conserva tal cual para
    // no romper el demo original (DefineVar/UseVar en main.cpp).
    std::vector<std::unordered_map<std::string, Rule>> env;

    // --- Value-environment de atributos (V del par Θ = ⟨V, Γ⟩) ---
    // Pila de scopes que mapean nombres de variables de atributo (𝜗) a
    // Values. Call() (ver Combinators.h) empuja un scope nuevo para los
    // parametros heredados de cada llamada a no-terminal y lo saca al
    // terminar, tras recolectar los atributos sintetizados.
    std::vector<std::unordered_map<std::string, Value>> valueEnv;

    /**
     * @brief Checkpoint completo del estado (cursor + ambos environments)
     * para soportar backtracking total, tal como exige la semantica de
     * las Figuras 4-8 del paper (todo cambio se descarta en un fallo,
     * salvo el caso especial de NotPredicate).
     */
    struct Checkpoint {
        size_t cursor;
        std::vector<std::unordered_map<std::string, Rule>> env;
        std::vector<std::unordered_map<std::string, Value>> valueEnv;
    };

    explicit ParserState(std::string_view in) : input(in) {
        env.emplace_back();
        valueEnv.emplace_back();
    }

    // --- Cursor Navigation & Inspection ---

    bool has_more() const { return cursor < input.length(); }

    char peek() const { return has_more() ? input[cursor] : '\0'; }

    void advance(size_t steps = 1) { cursor += steps; }

    // --- Rule-name scoping (legacy, mecanismo original del MVP) ---

    void push_scope() { env.emplace_back(); }

    void pop_scope() {
        if (env.size() > 1) env.pop_back();
    }

    void register_rule(const std::string& name, Rule rule) { env.back()[name] = std::move(rule); }

    bool execute_rule(const std::string& name) {
        for (auto it = env.rbegin(); it != env.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second(*this);
        }
        return false;
    }

    // --- Value-environment scoping (atributos 𝜗, Sección 3) ---

    void pushValueScope() { valueEnv.emplace_back(); }

    void popValueScope() {
        if (valueEnv.size() > 1) valueEnv.pop_back();
    }

    /// Escribe/sobreescribe una variable en el scope actual (el mas interno).
    void setValue(const std::string& name, Value v) { valueEnv.back()[name] = std::move(v); }

    /**
     * Busca una variable desde el scope mas interno hacia el global.
     * Devuelve un Value indefinido (⊥) si no se encuentra en ningun scope,
     * igual que la convencion 𝜎(𝑘) = ⊥ del paper.
     */
    Value getValue(const std::string& name) const {
        for (auto it = valueEnv.rbegin(); it != valueEnv.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        return Value();
    }

    bool hasValue(const std::string& name) const {
        for (auto it = valueEnv.rbegin(); it != valueEnv.rend(); ++it) {
            if (it->count(name)) return true;
        }
        return false;
    }

    // --- Backtracking Checkpoints ---

    Checkpoint save() const { return Checkpoint{cursor, env, valueEnv}; }

    void restore(const Checkpoint& cp) {
        cursor = cp.cursor;
        env = cp.env;
        valueEnv = cp.valueEnv;
    }
};