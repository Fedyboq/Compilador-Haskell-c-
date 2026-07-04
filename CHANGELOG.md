# Changelog

Todos los cambios relevantes de este proyecto se documentan en este archivo.

---

## [consolidacion-tres-ramas] — 2026-07-03

Rama: `SefedeamU`. Sobre nuestro motor (v3), se integraron los puntos fuertes de
las otras dos ramas del equipo, cada una fiel a una parte distinta del paper:

- **de `si` (v1, Tamy):** el **sistema de tipos** y la **extensión tipada de
  lenguajes (L-ext)** — el aporte teórico central (distinción G-ext vs L-ext).
- **de `Ignacio` (v2):** la **sintaxis concreta leída desde texto**
  (`create` / `syntax`), la más cercana a la Figura 1/2 como lenguaje de usuario.

Ambas se **reimplementaron sobre el motor inmutable de v3**, lo que elimina por
construcción los dos bugs conocidos de v2 (el `Checkpoint` que no restauraba las
gramáticas definidas, y el `Ref(Rule&)` colgante).

### Agregado

- **`include/Type.h`** — sistema de tipos mínimo (`Int, Bool, Str, Sym, List,
  Node, tGrm, tLang`) para las firmas de reglas y el contexto Γ. *(de v1)*
- **`include/Apeg.h`** *(ampliado)*:
  - `RuleSig` (firma `⟨A ϑ::τⁿ e::τᵐ⟩`) y `Grammar::sigs` para tipar reglas.
  - **`composeGrammars` (operador ⊎ / G-ext)** genérico y reusable — la pieza
    que a v3 le faltaba. Componer dos gramáticas fusiona reglas homónimas como
    elección priorizada `A → p1 / p2`.
  - **`LanguageValue`** (`Grammar` + `Gamma`) y `Value::Lang` — un *language*
    como valor de primera clase (tLang).
  - **`extendLanguage` (operador ⊳ / L-ext)**: compone y luego **re-chequea la
    consistencia de tipos**; lanza **`TypeError`** si dos reglas homónimas
    tienen firmas incompatibles. *(de v1)*
- **`include/MetaParser.h`** — compila **patrones PEG escritos como texto** a
  expresiones de parsing del motor (`'literal'`, referencias a no-terminal, `.`,
  `/`, `*`, `!`, grupos), y parsea bloques `create <n> { regla* }`. *(de v2)*
- **`src/apeg_demo.cpp`** *(ampliado)* con dos demos nuevas:
  - **Demo C — L-ext tipado**: extiende un `language` con una firma compatible
    (acepta las dos sintaxis) y con una incompatible (**`TypeError`**).
  - **Demo D — `create`/`syntax` desde texto**: el usuario escribe
    `create loops { stmt -> 'repeat' . num . 'times' . '{' . stmt . '}' ; }` y
    `repeat` solo es válido **dentro** de `syntax loops { ... }`; fuera se
    rechaza. El *scoping* es automático porque las gramáticas son inmutables.

### Notas de diseño

- **⊎ inmutable = backtracking correcto gratis.** Como `composeGrammars`
  devuelve una gramática nueva y el estado la lleva por valor (shared_ptr), no
  hay estado mutable compartido que corromper: entrar/salir de un bloque
  `syntax` restaura la gramática base sin esfuerzo. Esto es justo lo que a v2 se
  le escapaba con su `Checkpoint`.
- **Tipos opcionales.** `RuleSig` es metadato: las reglas que no necesitan
  tipos dejan la firma vacía y no participan del chequeo de L-ext.

---

## [modelo-apeg-completo] — 2026-07-03

Rama: `SefedeamU`.

**Contexto.** El MVP inicial era un *reconocedor*: sus reglas eran
`Rule = function<bool(ParserState&)>`, es decir, solo respondían "casó / no casó".
Al probar una gramática más compleja (como la del paper) el modelo se quedaba corto:
no manejaba la gramática como un objeto manipulable, no tenía un sistema de valores
genéricos ni los operadores semánticos formales, ni no-terminales con atributos.
Esta contribución cierra esa brecha y lleva el proyecto al **modelo completo del paper**
(*Adaptable Parsing Expression Grammars* — Reis, Bigonha, Di Iorio & Amorim), sin romper
el MVP existente.

### Agregado

- **`include/Value.h`** — Sistema de **valores de primera clase** (`Value`): enteros,
  booleanos, strings, símbolos, listas, nodos de AST y **gramáticas**. Que una gramática
  quepa dentro de un `Value` es lo que la vuelve *first-class*. Incluye utilidades de
  impresión (`toString`, `toTree`) para visualizar los AST.

- **`include/Apeg.h`** — Modelo APEG completo:
  - **`Grammar`** como valor de primera clase y **adaptable**: mapa `no-terminal → Rule`
    más `attrs` (atributos de la gramática que se propagan por todo el parseo).
    Sus actualizaciones son **inmutables** (`with(...)` devuelve una gramática nueva).
  - **`State`** extendido: cursor de entrada + `grammar` (adaptable) + `env` (atributos
    **locales** por invocación). `Checkpoint` guarda cursor + gramática + atributos →
    **backtracking semántico** (al fallar una rama se deshace también la gramática y los
    atributos que se hubieran definido).
  - **Combinadores PEG con atributos**: `Lit`, `Text`, `Range`, `Seq`, `Choice`, `Star`,
    `Plus`, `Opt`, `Not`, `And`, y tokens `Ident`, `Number`, `Word`.
  - **Operadores propios de APEG**:
    - `Capture(var, e)` — guarda el valor sintetizado de `e` en un atributo.
    - `Action(f)` — acción semántica (construye el valor/AST sintetizado).
    - **`Bind(var, f)`** — asigna un atributo (no consume entrada).
    - **`Constraint(pred)`** — predicado semántico; casa solo si la condición se cumple.
    - **`Update(mod)`** — **adapta la gramática** en tiempo de parseo (núcleo "adaptable").
    - `Call(name, args)` — invoca un no-terminal con atributos heredados y **ámbito local**
      propio para sus atributos.
  - `runGrammar(...)` — motor de ejecución de una gramática sobre una entrada.

- **`src/apeg_demo.cpp`** — Dos demostraciones ejecutables del modelo:
  - **Demo A — Lenguaje extensible "declara-luego-usa".** El parser **modifica su propia
    gramática** mientras trabaja:
    - `def x; def y; use x; use y;` → ACEPTADO (con su AST).
    - `def x; use z;` → RECHAZADO (usar sin declarar; imposible en un PEG puro).
    - `def x; def x;` → RECHAZADO (redefinición → análisis semántico vía `Constraint`).
  - **Demo B — Aritmética.** Cada no-terminal **sintetiza un AST** (precedencia y
    asociatividad correctas); un evaluador recorre el AST. Ej.: `2+3*4-1 → 13`,
    `(2+3)*4 → 20`, `10-2-3 → 5`.

### Cambiado

- **`CMakeLists.txt`** — Ahora compila **dos** ejecutables: `apeg_parser_mvp` (MVP
  original) y `apeg_demo` (modelo completo). Ambos se emiten en `build/bin/`.

### Conservado (sin modificar)

- `src/main.cpp`, `include/ParserState.h`, `include/Combinators.h` — el MVP original se
  mantiene intacto como punto de partida y comparación (sus pruebas siguen pasando).

### Notas de diseño

- **Atributos locales vs. estado propagado.** `env` es local a cada `Call` (los atributos
  de una regla no se filtran al llamador). La gramática y sus `attrs` **sí** se propagan
  (persisten en éxito, se revierten en backtracking). Esto refleja el modelo del paper:
  la gramática γ es el estado que se hilvana por todo el parseo; los atributos de cada
  regla son locales a su invocación.
- **Protección de bucle** en `Star`/`Plus`: si una sub-expresión no consume entrada, la
  repetición se detiene (evita bucles infinitos).

### Cómo compilar y ejecutar

```bash
cmake -S . -B build
cmake --build build
./build/bin/apeg_demo         # modelo completo del paper
./build/bin/apeg_parser_mvp   # MVP original (reconocedor)
```

Requiere C++17 (probado con g++ 15 y CMake ≥ 3.12).

---

## Recomendaciones para las demos (video del bonus)

El profesor pidió, para el bonus, un **video mostrando que el programa funciona**. Con la
consolidación, el ejecutable `apeg_demo` corre **cuatro demos** que cubren, entre las tres,
todo el arco del paper. Guion sugerido, en orden:

1. **Demo D como apertura (`create`/`syntax`).** Es la más "lenguaje de verdad": se ve el
   usuario *escribiendo* una extensión de sintaxis en texto. Detente en el contraste:
   `repeat 3 times { ... }` **funciona dentro** de `syntax loops { }` y **se rechaza fuera**.
   Frase: *"el mismo texto es válido o no según qué gramática esté activa; eso es un APEG"*.

2. **Demo A (declara-luego-usa).** El parser **adapta su propia gramática**: `use z` se
   rechaza porque `z` no fue declarado, y `def x; def x;` se rechaza por redefinición
   (**análisis semántico** vía `Constraint`). Muestra el **AST** del caso aceptado.

3. **Demo C (L-ext con tipos).** Aquí está el **aporte teórico central** (por si el profe
   pregunta por la Figura 10): extender un *language* con una firma **compatible** funciona,
   pero con una **incompatible** lanza `TypeError`. Es la distinción **G-ext vs L-ext**.

4. **Demo B (AST aritmético).** Cierra mostrando el **árbol impreso** (`toTree`) y su
   evaluación (`2+3*4-1 → 13`): evidencia la *"construcción dinámica de AST"* y hace de
   mini **AST viewer**.

**Mejoras opcionales si sobra tiempo:**

- **Leer el programa desde archivo o `stdin`** en vez de tenerlo fijo en el código: en el
  video se vería cómo *escribes* un programa del DSL y el parser reacciona en vivo.
- **Casos de error de sintaxis** (p. ej. `2+*3` o `(2+3` en Demo B) para mostrar el reporte
  de la columna donde se detuvo.
- **Grabación**: terminal con fuente grande y buen contraste; una narración de 1–2 frases
  por demo nombrando *qué operador del paper* se ve en acción (⊎, ⊳/L-ext, Update,
  Constraint).
