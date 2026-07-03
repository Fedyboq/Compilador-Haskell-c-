# Changelog

Todos los cambios relevantes de este proyecto se documentan en este archivo.

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

El profesor pidió, para el bonus, un **video mostrando que el programa funciona**. Ideas
para que se vea sólido y deje claro el aporte del paper:

1. **Abrir con el contraste PEG vs APEG.** Ejecuta Demo A y detente en el caso
   `def x; use z;` (RECHAZADO). Explica en voz: *"un PEG normal no puede decidir esto,
   porque depende de qué se declaró antes; nuestro parser adapta su gramática en caliente"*.
   Es el punto más vendedor del proyecto.

2. **Recorre los tres casos de Demo A** pausando en cada uno: aceptado (mostrar el AST),
   uso-sin-declarar (error de parseo), redefinición (error **semántico** vía `Constraint`).
   Menciona que la redefinición cubre el criterio de *Semantic Analysis* de la rúbrica.

3. **Muestra el AST de Demo B** (el árbol impreso) y su evaluación. Esto evidencia la
   *"construcción dinámica de AST"* y sirve como mini **AST viewer** textual.

4. **Agregar una demo de "operador/keyword nuevo en runtime"** (recomendado, ~30 min de
   trabajo): un programa que primero *define* una nueva sintaxis con `Update` y luego la
   *usa*. Haría la extensibilidad aún más evidente que el ejemplo `def/use`.

5. **Leer el programa desde un archivo o `stdin`** en vez de tenerlo fijo en el código.
   En el video se vería cómo *escribes* un programa del DSL y el parser reacciona en vivo;
   es mucho más convincente que inputs hardcodeados.

6. **Añadir un caso de error en Demo B** (p. ej. `2+*3` o `(2+3`) para mostrar que el
   parser también detecta errores sintácticos y reporta la columna donde se detuvo.

7. **Detalles de grabación**: terminal con fuente grande y buen contraste, inputs cortos
   y legibles, y una narración de 1–2 frases por demo explicando *qué operador del paper*
   (Bind / Update / Constraint) se está viendo en acción.
