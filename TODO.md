# TODO — port a macOS (Apple Silicon)

Plan de trabajo para la rama `macos-port`. Cada tarea está escrita para que la ejecute
un agente (Opus) de forma autónoma: objetivo, ficheros, especificación, cómo se mide
y criterio de aceptación. El contexto largo está en `memory.md`; aquí solo lo operativo.

Estado del port (2026-10-12): título, menú y carga a 29–30 FPS (tasa nativa). Partida
(Bowerstone, inicio) a **7,7 FPS**, límite en la GPU. Todo lo demás funciona.

## Reglas para todas las tareas

- **Medir siempre, y validar con la captura.** El F3 falsea los FPS (fuerza un repintado
  por frame). Los FPS se leen del log del juego: `out/build/mac-arm64-release/logs/fable_2_NNN.log`,
  una línea por segundo `guest vblank pacer: guest frame (gate) ~N Hz`. Antes de dar un
  resultado por bueno, mirar la captura `*_3d.png`: tiene que verse la escena 3D
  (personaje, muralla con brasero, nieve), no el menú.
- **Guion de medida:** `tools/mac/prof3d.sh <nombre> [args del juego]` (tarea T0):
  arranca, pulsa E/S/E para cargar la partida, captura, hace `sample` 10 s y saca la
  cadencia. Línea base: `fps: 7.7 7.6 ...`. Una caché de shaders fría cambia los tiempos:
  si la captura no muestra el 3D, repetir.
- **Cambios en el SDK** (ReXGlue, fork himdo en `b097123`): se hacen en el checkout
  `/Volumes/fable2build/rexglue-sdk` (imagen APFS `fable2build.sparsebundle` en el disco
  EXT; se monta con `hdiutil attach`), se compilan con
  `REXGLUE_SDK_SOURCE=/Volumes/fable2build/rexglue-sdk RUNTIME_SDK_BUILD=/Volumes/fable2build/build-runtime tools/build_runtime_sdk.sh Release`
  y luego `./build.sh -release fable_2`. El `git diff` del checkout se vuelca a
  `thirdparty/rexglue-sdk-fork-fixes.patch` (lo aplica `prepare_runtime_sdk.py`). Cada
  cambio terminado va además a una rama en `rasty94/rexglue-sdk` para PR al fork
  (como himdo/rexglue-sdk#11).
- **No tocar cabeceras de `include/rex/`** salvo necesidad: recompila el juego entero
  (~40 min). Si se tocan, borrar `CMakeFiles/fable_2_recomp.dir/cmake_pch.hxx.pch`.
- **Un solo juego a la vez** en la máquina (las pruebas usan el foco de la ventana).
- Sin atribuciones de herramienta en commits ni PRs.

## Línea base medida (escena 3D, 1 frame ≈ 142 ms)

| Magnitud | Valor |
|---|---|
| Draws por frame | ~3920 (kVertex 2849, QuadPatch teselado 580, point sprites 448, rect 50) |
| Render passes por frame | **~491** (409 ruta fbo + 82 transferencias EDRAM); coste medio ~100 µs cada uno (volcado y recarga de attachments en TBDR) ≈ 47 ms/frame |
| `vkCmdCopyBuffer` por frame | **431** subidas de shared memory (`vulkan/shared_memory.cpp:328`), 41 lecturas de memexport (`command_processor.cpp:4342`), 1 (`:4217`), 3–4 (`shared_memory.cpp:309`). Cada copia cierra el render pass |
| Teselación | 580 draws/frame, todos kQuadPatch: 485 continuos con `VGT_HOS_MAX_TESS_LEVEL`=15 (factor 16 uniforme) y 95 adaptativos (factores 2–16). MoltenVK reinicia el render pass por draw: ~20 µs cada uno ≈ 12 ms/frame |
| Attachments | 1 color + depth con MSAA 2x en 104 passes, MSAA 4x en 60; extents de EDRAM (1120x1176, 1040x2528, 320x8192…) |
| GPU | ocupada 73% a frecuencia máxima, ~105 ms de trabajo por frame; CPU explica el 8% |
| Presupuesto para 30 FPS | 33 ms de GPU por frame → hay que quitar ~75 ms |

Descartado (sin efecto o peor): `--readback_memexport=false`, `--vulkan_submit_on_primary_buffer_end=false`,
`--render_target_path_vulkan=fsi` (5 FPS e imagen corrupta). `fbo` es el valor por defecto.

## Tareas

### T0. Herramientas de medida en el repo — hecho
- **Hecho:** `tools/mac/` con `prof3d.sh`, `scene.sh`, `winlist.js`, `oncpu.py`, `gdfx_extract.py`, `apply_diag.py`+`tempdiag_block.inc`, `trace/` (xt, a1-a5) y README; salida en `out/mac-measure/`.
- **Objetivo:** que el guion de medida no viva en el scratchpad de una sesión.
- **Qué:** crear `tools/mac/` con `prof3d.sh`, `scene.sh`, `winlist.js` (lista de ventanas por
  JXA), `oncpu.py` (tiempo propio por hilo a partir de `sample`) y los scripts de análisis
  de trazas de Metal (`xt.py`, `a1.py`–`a5.py`: exportan tablas con `xctrace export` y
  resuelven las referencias `id`/`ref`). Origen: scratchpad de la sesión del 2026-10-12
  (`/private/tmp/claude-502/.../scratchpad/`); si ya no existe, reescribirlos desde la
  descripción de `memory.md`. Añadir las excepciones en `.gitignore` (`*.sh`, `*.py` están
  ignorados por defecto) y un `tools/mac/README.md` corto con el protocolo de medida.
- **Aceptación:** `tools/mac/prof3d.sh base` reproduce la línea base y deja la captura.

### T1. Subidas de shared memory fuera del render pass — hecha (cvar on por defecto en macOS)
- **Estado (2026-10-12):** implementado en el parche del fork tras el cvar
  `vulkan_shared_memory_upload_first` (por defecto **false**; probar con `--vulkan_shared_memory_upload_first=true`).
  Mismo binario, escena de Bowerstone, A/B con instrumentación temporal:
  copias en línea 431 → ~8/frame (las ~430 restantes van al búfer de subidas previo); passes fbo 409 → ~145/frame
  (+ ~81 de transferencias EDRAM, sin cambio: total 491 → ~226); FPS 7,9 → 9,7 (+23%; escena de la cinemática con
  Rose 6,0 → 8,2). Sin artefactos frente a `p3d4_3d.png` (texto de UI, nieve). **No cumple FPS ≥ 12 ni < 150 passes
  en total:** lo que queda son los 41 drenados de memexport (T2), los reinicios de la teselación (T3) y las
  transferencias EDRAM (T4). Con `--readback_memexport=false` además: ~10,3. Activar por defecto tras T2/T3.
- **Diseño:** el criterio es "página no **accedida** por la GPU en esta submission" (pedida con `RequestRange(s)` o
  escrita con `RangeWrittenByGpu`), no solo "no subida": una página leída por un draw anterior de la misma
  submission no puede subirse antes que ese draw. Hook `SharedMemory::RangeAccessedByGpu` (virtual, base) + bitmap
  por submission en `VulkanSharedMemory` (reiniciado en su `EndSubmission`); `upload_command_buffer_` en
  `VulkanCommandProcessor` reproducido antes del principal. Barreras propias del búfer de subidas: todos los usos →
  transfer al abrir, transfer → todos los usos al cerrar; el seguimiento `Use()` del búfer principal no se toca.
- **Objetivo:** que las 431 copias por frame no cierren el render pass.
- **Dónde:** `src/graphics/vulkan/shared_memory.cpp` (`UploadRanges`, :272),
  `src/graphics/vulkan/command_processor.cpp` (`SubmitBarriers`, `EndSubmission`,
  reproducción del `deferred_command_buffer_` en :5500), `include`/`.h` del backend Vulkan
  solo si es imprescindible.
- **Especificación:** todos los comandos se graban en un `DeferredCommandBuffer` software
  que se reproduce en el `VkCommandBuffer` al cerrar la submission. Añadir un segundo búfer
  diferido, `upload_command_buffer_`, que se reproduce **antes** del principal en la misma
  submission. `UploadRanges` graba ahí las copias cuando ninguna de las páginas del rango se
  ha subido ya en la submission actual (bitmap o conjunto de páginas subidas, reiniciado en
  `BeginSubmission`); si alguna página ya se subió en esta submission (el guest la ha vuelto
  a escribir entre dos draws), se usa la ruta actual (cierra el pass y copia en línea), que
  mantiene la corrección exacta. Las barreras: una sola `transfer write → vertex/index/
  uniform/shader read` sobre el búfer de shared memory al final del búfer de subidas; el
  seguimiento de uso (`Use(Usage::kTransferDestination)`) no debe dejar el búfer en estado
  transfer para el principal. El pool de upload buffers debe vivir hasta que la submission
  termine (ya es así para la ruta actual; comprobarlo).
- **Medida:** con la instrumentación temporal de `scratchpad/apply_diag.py` (o equivalente)
  contar render passes y copias por frame antes y después; FPS con `prof3d.sh`.
- **Aceptación:** copias en línea por frame < 20; render passes por frame < 150; FPS en 3D
  ≥ 12 (no alcanzado: 9,7; el criterio de seguridad sí, y por eso va activado); sin artefactos nuevos en la captura (comparar con `p3d4_3d.png`). Documentar en
  el commit los contadores antes/después.
- **Riesgo:** reutilización de una página dentro de la misma submission (ring buffers de
  UI/partículas): cubierta por la ruta de fallback. Vigilar texto de UI y partículas.

### T2. Memexport: dejar de drenar la GPU por draw — pendiente (tras T1)
- **Objetivo:** quitar los 41 drenados completos (`vkWaitForFences`) y las 41 copias por frame.
- **Dónde:** `command_processor.cpp` `IssueDraw_MemexportReadbackFastPath/FullPath`
  (:4180–4370); cvars `readback_memexport*` en `src/graphics/command_processor.cpp:79-88`.
- **Por qué cae siempre a la ruta lenta:** la clave `(dirección, tamaño)` se repite dentro
  de la misma submission, así que el slot anterior nunca está completado (:4353-4362); el
  commit 4c21ec4 del fork no lo arregla (solo el caso de submission ya enviada).
- **Opciones, en orden:** (a) `readback_memexport_no_drain`: si el slot no está listo,
  saltar la lectura de ese draw y alternar el slot (los datos llegan un frame tarde);
  (b) lectura diferida de verdad en el reclaim de fences, con búfer por submission;
  (c) apagar la lectura por defecto en macOS si (a) da glitches.
- **Aceptación:** `vkWaitForFences` desaparece del perfil del hilo "GPU Commands" en 3D;
  sin regresión visual en el perro/héroe ni montañas negras (himdo/Fable-2-Recomp#68).

### T3. Teselación continua emulada en el vertex shader — pendiente (tras T1)
- **Objetivo:** que los 485 draws continuos del terreno no pasen por la teselación de
  Metal (MoltenVK: VS+TCS en compute + reinicio del render pass por draw).
- **Dónde:** `src/graphics/shader.h` (`HostVertexShaderType`, hay hueco en los 4 bits),
  `src/graphics/primitive_processor.cpp` (:291-358, selección del modo; generar índices),
  `src/graphics/spirv_translator.cpp` (:1913-1919 montaje del dominio para
  QuadDomainPatchIndexed; :2323-2400 ya emula el dominio en la ruta compute de memexport,
  reutilizar), `src/graphics/vulkan/pipeline_cache.cpp` (:209-270; topología TriangleList
  sin TCS/TES), constantes de sistema (`command_processor.cpp:6120`).
- **Especificación:** nuevo `HostVertexShaderType` "QuadDomainPatchIndexed emulado": por
  parche, `N = VGT_HOS_MAX_TESS_LEVEL + 1` (16 en Fable 2), rejilla de (N+1)×(N+1) vértices
  y 2·N·N triángulos, índice de vértice → (parche, u, v) con `gl_VertexIndex`; el VS del
  dominio se ejecuta como VS normal con r0.x = parche, r0.yz = (u, v), r1.x = 0. Con factor
  par entero el espaciado `fractional_even` coincide con el entero, así que la geometría es
  idéntica. Solo para el modo continuo con factor uniforme; el adaptativo (95 draws) sigue
  con teselación real. Activable por cvar (`vulkan_emulate_continuous_tessellation`, por
  defecto on en macOS) para poder comparar.
- **Aceptación:** reinicios de render pass por frame < 120 (solo adaptativos); terreno
  idéntico a simple vista en dos capturas A/B; FPS ≥ 20 tras T1.
- **Riesgo:** grietas entre parches continuos y adaptativos vecinos (los adaptativos
  siguen siendo reales, así que no debería); coste de vértices igual (512 tris/parche).

### T4. Verificar 30 FPS estables y limpiar — pendiente (tras T1–T3)
- Medir tres escenas (Bowerstone, exterior, interior) y pantalla completa.
- Si no llega: transferencias de EDRAM (82 passes/frame, diseño fbo de Xenia) y MSAA 4x
  (60 passes/frame): estudiar antes de tocar.

### T5. Subir los arreglos al fork y upstream — pendiente
- PR al fork por cada cambio del SDK que quede validado (T1, T2, T3) y los que ya están
  en `rexglue-sdk-fork-fixes.patch` sin PR: miembros del pacer fuera de `#if WIN32`,
  `/DEF:` solo en Windows, `SIG_DFL` para señales no atendidas, no repintar al suprimir
  por el pacer. PR #11 (presentador) pendiente de revisión.
- Bug a reportar a MoltenVK: `MVKDispatchableVulkanAPIObject::getVkHandle()` reescribe
  `ICD_LOADER_MAGIC` sobre el puntero de despacho del loader al notificar un mensaje a un
  *debug messenger*; la siguiente llamada por el loader (`vkAllocateCommandBuffers`) lee
  `0x01CDC0DE` y falla. Reproducido con MoltenVK 1.4.1 (`701747d`). Mientras,
  `vulkan_log_debug_messages=false` por defecto en macOS (`fable_2_app.h`).
- Deuda en el proyecto que debería ser del SDK: limitador de repintados de UI fuera de
  Win32 (`Presenter::WaitForUITickFromUIThread` es no-op; lo correcto es un
  `CVDisplayLink`; hoy lo tapa `pace_to_guest_vblank=true` por defecto) y el *hint*
  `SDL_VIDEO_MAC_FULLSCREEN_SPACES=0` (`main.cpp`).

### T6. Pendientes menores — pendiente
- Flecha abajo (cruceta) en el menú: el SDK la traduce bien; probar a mano (el guion la
  pulsa demasiado rápido).
- Líneas finas claras en aristas (piedras, espada) y halo morado en personajes: capturar
  con GPU Frame Capture de Xcode (`MTL_CAPTURE_ENABLED=1`) y localizar el draw.
- `skip_intro_videos = true` en el `fable2_config.toml` del build (solo pruebas): revertir
  o documentar.
- `graphify-out/` se regenera en cada commit por el hook y aparece modificado; el hook
  añadió una vez `graphify-out/*` a `.gitignore` (quitado). Vigilar.

### T7. Launcher para macOS (ISO, mods, configuración) — pendiente (al final)
- **Objetivo:** lo que hoy hace el launcher WPF en Windows (perfiles de juego, edición de
  `fable2_config.toml` y `fable_2.toml`, botón de guardar, mods) y lo que en macOS se hace
  a mano: elegir la ISO, extraer el contenido (`tools/stage_content.sh`; extracción GDFX
  del scratchpad de la sesión del 2026-10-12, `gdfx_extract.py`, a incorporar en T0),
  lanzar el juego con los flags adecuados.
- **Primero estudiar, luego prototipar.** Opciones a comparar: (a) app nativa SwiftUI/AppKit
  (la mejor integración: permisos, Spaces, firma); (b) reutilizar el código C# del launcher
  con Avalonia (misma lógica de config, `Fable2ConfigFile.cs`, y `tests/test_config_parity.py`
  sigue valiendo); (c) un asistente de terminal en `tools/mac/` como mínimo viable mientras
  se decide. Criterios: paridad con el launcher de Windows (ver `launcher/` y
  `docs/screenshots/launcher.png`), mantenimiento de una sola fuente de verdad para las
  claves de configuración, y que la extracción de la ISO no requiera herramientas externas.
- **Entregables del estudio:** comparativa de una página en `docs/`, lista de funciones del
  launcher de Windows con su equivalente propuesto, y estimación por opción. No construir
  nada hasta elegir.

## Hecho (resumen; detalle en `memory.md` y en el log de git)

- Build desde fuente del runtime del fork en macOS (`tools/build_runtime_sdk.sh`,
  `stage_renderer_sdk.py --platform`), `build.sh` usa el runtime compilado.
- Presentador Vulkan: pipeline de pintado recreado por frame (himdo/rexglue-sdk#11,
  rexglue/rexglue-sdk#507).
- Repintados a 120 Hz bloqueando la cola de la GPU → `pace_to_guest_vblank` por defecto
  (6 → 30 FPS en menús); hilo de UI sin bucle al suprimir.
- Cuelgue en blanco (crash en bucle por el bug de MoltenVK) → mensajes de depuración off
  y `SIG_DFL` en el manejador POSIX.
- Trazador de funciones fuera de Release en macOS (`FABLE2_FUNC_TRACE`).
- Pantalla completa negra (Spaces), teclado y ratón como mando, pruebas de paridad de
  config, graphify.
