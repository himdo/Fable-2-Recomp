# Launcher para macOS: estudio (T7)

Estudio sin código. Fuentes: `launcher/` (WPF, .NET 8), `launcher/README.md`,
`docs/screenshots/launcher.png`, `README.md`, `tools/stage_content.sh`,
`tools/mac/gdfx_extract.py`, `constants/game_versions.json` (+ `tools/generate_game_versions.py`),
`config/fable2_config.toml`, `src/core/fable2_config.cpp`, `tests/test_config_parity.py`, `build.sh`.

## 1. Inventario del launcher de Windows y equivalente en macOS

| # | Función (Windows) | Dónde | Hoy en macOS (manual) | Propuesta macOS |
|---|---|---|---|---|
| 1 | Detectar la carpeta del juego (la propia del launcher; recordada en `launcher-game-path.txt`) | `MainWindow.DetectGameDirectory`, `LauncherState` | Ninguna: se pasa `--game_data_root` o se copia con `stage_content.sh` | Igual: por defecto el directorio del binario `fable_2` |
| 2 | Elegir carpeta (exige `default.xex`) | `BrowseClicked` | Ruta a mano en `--game_data_root` | Selector de carpeta / argumento |
| 3 | Validar edición: SHA-256 del XEX contra el catálogo + ficheros requeridos/rechazados (mezcla retail/TU1) | `GameCompatibility.cs` (69 l.) | Solo el chequeo del propio juego al arrancar (`--skipHash` lo salta) | Leer `constants/game_versions.json` directamente (fuente única) |
| 4 | Comprobar que el ejecutable soporta el perfil (`fable2_build.json`, lista `profiles`) | `GameLaunchPlan.cs` (33 l.) | No; el descriptor ya se genera junto a `fable_2` | Igual, con nombre `fable_2` (sin `.exe`) |
| 5 | Extraer ISO en dos fases: XEX primero + hash; si no cuadra, "Extract Anyways"/Stop (borra el XEX); luego el resto; progreso por bytes y por carpeta raíz; cancelación | `DiscExtraction.cs` (297 l.) + `X360Extract.Core` (1072 l.: GDFX, ISO 9660, STFS) | `python3 -I tools/mac/gdfx_extract.py <iso> <dir>` (82 l., solo GDFX, sin hash previo, sin progreso, sin cancelar) + `tools/stage_content.sh` | Ver §2.1 |
| 6 | Gráficos: resolución de salida, escala interna 1–4x, anisotrópico, FXAA, VSync, modo de pantalla, límite de FPS (30…240/ilimitado), `guest_vblank_pacing` derivado | `GraphicsSettings.cs` (51 l.), `GraphicsOptions.cs` (14 l.) | Editar `fable_2.toml` o flags `--window_width`, `--fullscreen`… | Mismas claves; modo de pantalla adaptado (§2.4) |
| 7 | Presets Original/Balanced/Enhanced/Ultra | `PresetClicked` (en el code-behind) | No | Igual (sacar los valores a la capa de lógica) |
| 8 | Resumen "Effective quality" (interna/salida) | `RefreshSummary` | No | Igual |
| 9 | Guardar sin destruir: actualiza en sitio, añade claves que faltan, copia `.launcher-backup` una vez, escritura atómica; `launcher-settings.toml` como overlay para que el runtime no pise la resolución | `LauncherConfigFile.cs` (98 l.) | Editor de texto | Misma semántica |
| 10 | Opciones avanzadas (`fable2_config.toml`): keybinds editables con captura de tecla, mouse look + sensibilidad, parches `[patches]`, `hotfunc_yield_every` | `Fable2ConfigFile.cs` (314 l.) + ~450 l. de UI en `MainWindow.xaml.cs` | Editor de texto | Igual; la captura de tecla necesita mapear códigos macOS a los nombres de `ParseVirtualKey` |
| 11 | Lanzar: guarda, arranca `fable_2.exe --game_data_root=<raíz>` y cierra el launcher | `LaunchGame` | Terminal: `out/build/mac-arm64-release/fable_2 [--fullscreen=false] [--game_data_root …]` | Igual + flags de §2.5 |
| 12 | Mods | No existen en el launcher (el README del launcher dice que no hay reemplazos de texturas ni editores). Lo más parecido: `[patches]` y el script F5 (`--f5_lua_path`, `data/scripts/recomp/`). `plans/main-menu-mod-item.md` es un plan de juego, no de launcher | `--f5_lua_path` | Opcional: elegir el `.lua` de F5 (lista de `data/scripts/recomp/*.lua`) |

## 2. Lo específico de macOS

1. **Elegir la ISO y extraerla sin herramientas externas.** `gdfx_extract.py` (stdlib) ya extrae
   GDFX. Le falta, respecto al C#: (a) extraer y hashear `default.xex` antes que el resto y aplicar
   el mismo gate que `GameCompatibilityInspector.CheckHash` (con `hashlib` y el JSON del
   catálogo); (b) progreso y cancelación limpia; (c) ISO 9660 y STFS (las ISO de Fable 2 son GDFX;
   STFS solo se copia en bruto, así que no hace falta). Destino: junto al binario, como en Windows
   (README: "extracts straight into the folder the launcher is running from"), y después
   `stage_content.sh` deja de ser necesario. Espacio: varios GB por copia; avisar si se duplica.
2. **Verificar el XEX (perfil GOTY, hash).** Perfil por defecto `goty-us-eu`; 6 perfiles
   compatibles y 4 rechazados en `constants/game_versions.json`. Comprobar además los
   `requiredFiles`/`rejectFiles` tras extraer (lo que hace `Classify`) y el `profiles` de
   `fable2_build.json`.
3. **Permisos del sistema.** El juego no necesita Accesibilidad ni Grabación de pantalla: solo
   las piden los scripts de prueba (`osascript` envía teclas, `screencapture`), y TCC se los
   atribuye al proceso responsable (Terminal/VS Code, o la app launcher si la hubiera). El
   launcher debe *no* pedirlos en uso normal; como mucho, un modo "pruebas" que compruebe
   `AXIsProcessTrusted` / `CGPreflightScreenCaptureAccess` y abra Ajustes en la sección
   correcta.
4. **Pantalla completa (Spaces).** Hoy el binario usa la pantalla completa de SDL sin Spaces
   (la de Spaces se veía negra); `SDL_VIDEO_MAC_FULLSCREEN_SPACES=1` fuerza la nativa. El modo
   "exclusive" de Windows no tiene sentido en macOS. Propuesta: Ventana / Pantalla completa
   (sin Spaces) / Pantalla completa nativa (experimental, pone la variable de entorno).
5. **Saves, logs y config junto al binario.** `saves/`, `cache/`, `logs/fable_2_NNN.log`,
   `fable_2.toml` y `fable2_config.toml` viven al lado de `fable_2` (y de sus `.dylib`). No hay
   bundle `.app` (CMake no usa `MACOSX_BUNDLE`). Si un día se empaqueta como `.app`, escribir
   dentro del bundle rompe la firma: habría que mover estado a `~/Library/Application Support`
   y eso cambia el contrato del runtime (decisión, §5). Útil: botones "Abrir carpeta de saves /
   logs" y copia de seguridad de `saves/`.
6. **Lanzar con flags.** `--game_data_root=<raíz>` (si el contenido no está junto al binario),
   `--fullscreen=true|false`, `--window_width/--window_height`, `--skipHash` (solo con
   confirmación), variables `REX_*`/`SDL_*`. El directorio de trabajo debe ser el del binario.
7. **FPS del log del pacer.** Leer el log más reciente de `logs/` y extraer
   `guest vblank pacer: guest frame (gate) ~N Hz` (una línea por segundo). No usar F3 (fuerza un
   repintado por frame, ver `tools/mac/README.md`).
8. **Ruido a limpiar.** El build de macOS deja `StartWithoutAudio.cmd` y `fable2-functrace.cmd`
   junto al binario; el launcher no debe ofrecerlos.

## 3. Comparativa de opciones

### Cuánto del C# es portable (cuenta en ficheros y líneas)

| Grupo | Ficheros | Líneas | Portable |
|---|---|---|---|
| Lógica sin WPF | `Fable2ConfigFile.cs` 314, `DiscExtraction.cs` 297, `LauncherConfigFile.cs` 98, `GameCompatibility.cs` 69, `GraphicsSettings.cs` 51, `GameLaunchPlan.cs` 33, `Constants/GraphicsOptions.cs` 14 | **876** (7) | Sí, tal cual (solo `fable_2.exe` → nombre por plataforma) |
| Librería de disco | `tools/x360extract/lib/*.cs` (ya `net8.0`, sin Windows) | **1072** (4) | Sí |
| Generado | `constants/GameVersions.cs` | — | Sí |
| WPF puro | `MainWindow.xaml` 436, `MainWindow.xaml.cs` 1116, `App.xaml` 89, `App.xaml.cs` 7 | **1648** (4) | No; hay lógica mezclada en el code-behind (presets, orquestación de la extracción ~160 l., `KeyToName` con `System.Windows.Input.Key`) |
| Tests | `tests/Fable2.Launcher.ConfigTests/Program.cs` | 377 | Sí |

En total ~1950 líneas de lógica reutilizable frente a ~1650 de UI que habría que rehacer.

### Tabla

| Criterio | (a) SwiftUI nativa | (b) Avalonia (C# reutilizado) | (c) Asistente de terminal `tools/mac/` |
|---|---|---|---|
| Paridad con Windows | Total, con el mejor encaje (selector nativo, captura de teclas con `NSEvent`) | Total; la UI se parece a la de Windows | Parcial: todo menos la captura interactiva de teclas (se escribe `E:A`) y la experiencia visual |
| Fuente única de claves | **Peor**: tercera implementación del escritor de config (C#, C++, Swift); `test_config_parity.py` habría que ampliarlo con un regex sobre Swift | **Mejor**: el mismo `Fable2ConfigFile.cs` en ambos launchers; parity test y ConfigTests siguen valiendo sin cambios | Buena si el escritor es *genérico* (toma las claves del propio `config/fable2_config.toml` con `tomllib`, sin lista propia) y el parity test añade un caso |
| Catálogo de versiones | Copiar el JSON al bundle | `GameVersions.cs` generado, igual que hoy | Lee `constants/game_versions.json` directo |
| Extracción ISO | Portar GDFX + gate a Swift (~300 l.) | Ya hecha (`X360Extract.Core`) | Ampliar `gdfx_extract.py` (~80 l. más) |
| Esfuerzo | Alto | Medio | Bajo |
| Mantenimiento | 3 lenguajes para la misma lógica | 1 lógica, 2 UIs (WPF + Avalonia) — o 1 si Windows también pasa a Avalonia | Python stdlib, ya usado en el repo |
| Dependencias | Xcode/Swift (ya instalado: Swift 6.4) | .NET 8 SDK (no instalado hoy) + Avalonia por NuGet; app autocontenida ~100 MB | Ninguna nueva; Python 3.11+ (`tomllib`). macOS no trae Python de serie fuera de las CLT |
| Firma / notarización | Developer ID + notarización estándar; sin cuenta de pago, Gatekeeper avisa | Igual, más *hardened runtime* con `allow-jit`; `dotnet publish` no genera `.app` (Info.plist a mano) | No aplica (scripts); el binario `fable_2` sigue con firma ad-hoc |
| Experiencia | La mejor (permisos, Spaces, carpetas, Dock) | Buena, no nativa | Aceptable para quien ya compila con `build.sh` |

## 4. Estimación y recomendación

| Opción | Días | Riesgos |
|---|---|---|
| (c) asistente de terminal | **2–3** | UX de terminal; el escritor genérico debe preservar comentarios y orden igual que el C# (reusar el algoritmo de `LauncherConfigFile.WriteValues`); sin captura de teclas |
| (b) Avalonia | **7–10** | Separar la lógica del code-behind a una librería `net8.0` común (1–2 días); diálogos (Avalonia no tiene `MessageBox`), `StorageProvider`, mapa de `Key`; empaquetado `.app` + notarización con JIT; añadir .NET al entorno de build de macOS |
| (a) SwiftUI | **10–14** | Portar extracción, catálogo y escritor de config; tercer contrato de claves; mantener dos UIs que divergen; pruebas en Swift |

**Recomendación: (c) ahora, (b) después si hace falta GUI; (a) solo si se distribuye a usuarios
no técnicos con firma de pago.**

Ruta incremental:
1. **(c) v0** — `tools/mac/fable2.py` (o ampliar `gdfx_extract.py`) con subcomandos:
   `extract <iso>` (XEX → hash → gate → resto, con progreso; destino el dir del binario),
   `verify` (catálogo + marcadores + `fable2_build.json`), `config get/set` y `preset <nombre>`
   (claves de `fable_2.toml` como `GraphicsSettings` y de `fable2_config.toml` leídas del
   template), `run [--window|--spaces] [--game-data-root]` y `fps` (cola del log del pacer).
   Un test: el escritor no altera comentarios y las claves coinciden con `test_config_parity.py`.
2. **Extraer la lógica C#** del code-behind (presets, orquestación de extracción) a una librería
   `net8.0` compartida. Mejora el launcher de Windows aunque no se haga (b).
3. **(b) Avalonia** sobre esa librería, si (c) se queda corto. Valorar sustituir también el WPF
   para tener un solo launcher.
4. (a) solo con Developer ID y si la app debe sentirse nativa; reutilizar (c) como motor no
   sirve en una `.app` distribuida (no hay Python garantizado).

Lo que se descarta: `XboxImageExtractor` u otras herramientas externas (requisito de T7), y meter
la extracción en el binario del juego (posible, pero mezcla responsabilidades).

## 5. Decisiones que necesita el usuario

1. ¿Público objetivo en macOS: solo quien compila con `build.sh` (basta c) o usuarios que
   descargan un binario (exige GUI y firma)?
2. ¿Hay cuenta de Apple Developer (99 $/año) para firmar y notarizar? Sin ella, (a) y (b) dan
   el aviso de Gatekeeper igual que (c).
3. ¿Aceptar .NET 8 en el entorno de macOS (para b)? ¿Y pasar el launcher de Windows a Avalonia
   para tener uno solo?
4. ¿Destino de la extracción: junto al binario (como Windows) o una raíz de contenido aparte con
   `--game_data_root` (evita duplicar el contenido entre Debug y Release)?
5. ¿Se mantiene "Extract Anyways" para hashes desconocidos en macOS, o se rechaza sin opción?
6. ¿Estado (saves/logs/config) junto al binario para siempre, o `~/Library/Application Support`
   si se empaqueta como `.app` (cambio en el runtime)?
7. ¿Exponer la pantalla completa nativa de Spaces (hoy negra) o solo ventana / sin Spaces?
8. ¿"Mods" en el launcher significa solo `[patches]` + selector del Lua de F5, o algo más (p. ej.
   el ítem de menú de `plans/main-menu-mod-item.md`)?
9. ¿Incluir en el launcher el modo "pruebas" (comprobación de permisos de Accesibilidad y
   Grabación de pantalla) o dejarlo en `tools/mac/`?
