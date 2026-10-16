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
- **Cambios en el SDK**: commits en la rama `macos` de `rasty94/rexglue-sdk` (sobre himdo
  `a728090`); el submódulo apunta a ella y ya no hay parche. Todo lo pesado vive en el volumen
  APFS `/Volumes/EXT/fable2/` (checkout `rexglue-sdk`, `build-runtime`, `out` —el `out/` del repo es
  un enlace a él—, `tmp`, `logs`); nada pesado en el disco interno. Compilar con
  `TMPDIR=/Volumes/EXT/fable2/tmp/ REXGLUE_SDK_SOURCE=/Volumes/EXT/fable2/rexglue-sdk RUNTIME_SDK_BUILD=/Volumes/EXT/fable2/build-runtime tools/build_runtime_sdk.sh Release`
  y luego `./build.sh -release fable_2`. Cada arreglo que valga para himdo va además a una
  rama propia para PR (como himdo/rexglue-sdk#11).
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

### T2. Memexport: dejar de drenar la GPU por draw — implementada la opción (a), OFF por defecto; falta la variante correcta
- **Estado (2026-10-12):** cvar `readback_memexport_no_drain` en el parche del fork (por defecto **true** en
  macOS, false en el resto; recarga en caliente). Si el slot anterior no está completado, se salta la lectura
  de ese draw y se alterna el slot; la siguiente lectura con esa clave copia el slot cuando su submission
  termina. Rama `t2-memexport-no-drain` en `pr-wt` (sobre `t1-upload-first`, que trae 4c21ec4: allí el caso
  "submission ya enviada" espera solo esa fence y el salto queda para la submission abierta).
  Medido (mismo binario, A/B con `--readback_memexport_no_drain=false`): partida 9,7 → 10,6 FPS; con el
  `sample` de 10 s enganchado 5,7–7,0 → 9,0–9,8; `vkWaitForFences` bajo `FullPath` en "GPU Commands"
  944–972 de ~1970 muestras → 0 (la espera que queda está en `BeginSubmission`, límite de submissions en
  vuelo: es la GPU). Sin cambios visuales (héroe, Rose, nieve, fuego); el perro no sale en esta escena.
- **Corrección del diagnóstico:** no eran 41 drenados por frame: la vía rápida sí leía el ~97% de los draws
  (las 41 copias de `:4342`); caía a `FullPath` ~1 vez por frame (`:4217`), pero ese drenado completo
  serializaba CPU y GPU. Con no_drain el guest recibe muchos menos datos: solo el 1–10% de las lecturas (frente
  al ~97%) encuentran el slot completado, porque sin el drenado la GPU va siempre un frame por detrás. Si
  aparece el perro/héroe negro o #68, probar `--readback_memexport_no_drain=false` y pasar a (b).
- **Estado (2026-10-12):** `readback_memexport_no_drain` da +0,9 FPS (9,7 → 10,6) y quita el
  `vkWaitForFences` del hilo "GPU Commands", pero con la GPU un frame por detrás solo el 1–10% de
  las lecturas encuentran el slot completado: el guest recibe muchos menos datos de memexport
  (equivale casi a apagar la lectura). Perro y #68 sin comprobar. Por eso queda OFF. El diagnóstico
  de "41 drenados por frame" era falso: la vía rápida lee el 97% de los draws; el drenado es ~1 por
  frame, pero serializa CPU y GPU. Variante correcta: esperar solo a la fence de la submission del
  slot (como 4c21ec4 upstream, que la rama del PR ya trae) y saltar únicamente cuando el slot es de
  la submission abierta; o la opción (b).
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

### T3. Teselación continua emulada en el vertex shader — hecha (fork `macos`, `464aac6`), 9,7 → 13,7 FPS
- **Resultado (2026-10-13):** escena 3D 9,7 → 13,7 FPS (misma ventana, `HOLD=40`); capturas idénticas a la base.
  Primera versión con una sola diagonal por celda: rombos negros en la nieve (la profundidad no coincidía con
  las pasadas teseladas de verdad). Corregido con diagonales reflejadas por cuadrante hacia el centro del
  parche, como el teselador de D3D11/Metal. Un arranque con la T3 se colgó en el título (sin draws teselados,
  GPU ociosa); no se repitió en cuatro arranques: vigilar. Falta medir reinicios de render pass con traza.
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

### T2b. Fork de integración del SDK — hecha (fork `macos` = `b017e57`), base medida 9,7 FPS
- **2026-10-13, después:** la base desde la rama da 9,7 FPS (igual que con el parche). Añadidos a `macos`:
  `show_debug_overlay` (overlay de FPS al arrancar; el juego lo activa), T3, y el PR himdo#12 de mitchell
  (`b017e57`: slots de readback de memexport por ocurrencia, ruta rápida sin lock en `RequestRanges`, esperas
  POSIX con precisión de µs, menos coste por registro/draw): T3 13,7 → 14,8 FPS. Overlay: coste 0.
  Del SDK oficial (`development`) quedan 15 commits nuevos (codegen, entrada, build): diferidos; el de
  MoltenVK 1.4.3 ya estaba. PR himdo#8 (cargas rápidas) limpio, pendiente; himdo#4 (efectos) descartado.
  `exp_adjust` (dword 3): sin cambio visible en la escena de Rose (luminancia media igual ±1); incluido en
  `macos` por ser la lectura correcta del registro.
- **Estado (2026-10-13):** `rasty94/rexglue-sdk` rama `macos` = `a728090` + 8 commits (pacer no-Win32, `/DEF:`
  solo Windows, SIG_DFL, formato del pipeline de paint, no re-pedir paint con pacer, T1 upload-first, T2
  no-drain OFF, T1 on por defecto en macOS), `0a3b1fa`. Rama aparte `fix-exp-adjust` (`228a153`, sobre
  `macos`) para la prueba visual. Submódulo → fork/`macos`; `rexglue-sdk-fork-fixes.patch` eliminado y
  `prepare_runtime_sdk.py` ya no aplica parches de fork. Equivalencia con el parche viejo comprobada por diff
  (solo cambia un comentario). **Falta:** compilar el runtime desde la rama y repetir la medida base (el
  build se perdió con la imagen; partida guardada en `~/fable2-saves-backup`, copiar a `saves/` del build).
  Interruptores de experimento: `tools/mac/exp_switches.patch`.
- **Objetivo:** dejar de acumular `thirdparty/rexglue-sdk-fork-fixes.patch`. Upstream no acepta cambios de
  emulación Xenos (#334/#456 cerrados), así que el fork `rasty94/rexglue-sdk` es el hogar de los arreglos.
- **Qué:** rama `macos` sobre `a728090` (himdo main; trae 4c21ec4, la mitad de la T2 correcta) con un commit por
  arreglo (`docs/upstream/pr-drafts.md`), más `exp_adjust`
  si la prueba visual lo confirma. `tools/build_runtime_sdk.sh` y `prepare_runtime_sdk.py` pasan a aceptar esa
  rama como fuente (el pin de `SDK_PIN`/baked-in debe reconocerla), el submódulo apunta a ella y el parche
  queda vacío o desaparece. Rehacer la medida base (la base cambia de `b097123` a `a728090`).
- **Aceptación:** `build_runtime_sdk.sh` compila desde la rama sin parche; `git diff` del checkout vacío;
  FPS y capturas iguales o mejores que con el parche.

### T2c. Resultados pendientes de la tanda de pruebas — pendiente de recoger
- HECHO: reparto 4x/2x del MSAA: 4x→2x ≈ 1 ms, 2x→1x ≈ 50 ms (coste binario por tener MSAA); trazas
  `mst_tr_base`/`mst_tr_msaa1` grabadas en el mismo plano, pendientes de comparar.
- Punteros para los pendientes (de la cola, que no llegó a ejecutarse): renderArea recortado → el SDK usa
  dynamic rendering (`vulkan/command_processor.cpp:82`), así que el sitio es `DeferredCommandBuffer::Execute`
  (`deferred_command_buffer.cpp:193`, `kVkBeginRendering`): mirar por delante hasta `kVkEndRendering` y tomar el
  mayor fondo de scissor/`kVkClearAttachments` + 16 filas. Point sprites → `point_screen_diameter_to_ndc_radius`
  (`command_processor.cpp:6194-6198`). `dump_shaders` en Vulkan solo vuelca ucode (`shader.cpp:122-153`), no SPIR-V.
  `exp_adjust` confirmado en `xenos.h:1239` (dword 3, bits 13-18). Análisis de FPS por ventanas:
  `tools/mac/phases.py` y `phase.sh`. Pendientes: renderArea recortado; pantalla
  completa; volcado de shaders; flecha abajo; point sprites a mitad; base repetida; prueba visual de `exp_adjust`.
  Interruptores en `out/mac-measure/exp_switches*.patch`. Si el MSAA 4x concentra el coste, añadir la tarea
  "MSAA máximo 2x por opción"; si no, abrir `out/mac-measure/f2_3d.gputrace` en Xcode (usuario) para el coste
  por draw y por shader.

### T4. Verificar 30 FPS estables y limpiar — pendiente (tras T1–T3)
- Medir tres escenas (Bowerstone, exterior, interior) y pantalla completa.
- Si no llega: transferencias de EDRAM (82 passes/frame, diseño fbo de Xenia) y MSAA 4x
  (60 passes/frame): estudiar antes de tocar.

### T5. Subir los arreglos al fork y upstream — preparada (`docs/upstream/`, ramas en pr-wt), pendiente de abrir
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

### T7. Launcher para macOS (ISO, mods, configuración) — estudiada (`docs/macos-launcher-study.md`), pendiente de decidir
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

### T8. Ideas del PR #2 de saulob/geometry-wars-retro-evolved-2-recompiled — pendiente (pequeñas)
Mismo port (ReXGlue 0.10.0, mac-arm64, MoltenVK) en otro juego; revisado el 2026-10-12. Vale la pena adoptar:
- `exp_adjust` del traductor SPIR-V (dword 3, no 4): probar visualmente en Fable 2 y, si cambia, entra en la
  rama `macos` del fork (T2b). Referencia: `patches/rexglue-sdk/0002-spirv-texture-exp-adjust.patch` del PR.
- Verificación SHA-256 del zip del SDK en `tools/setup_sdk.sh` y de `default.xex` en `build.sh`/`stage_content.sh`
  (mensaje claro "unsupported default.xex"), y comprobar `rexglue --version` (~1 h).
- Sello con hashes y `nm -gU` de exports del plugin frente al oficial en `stage_renderer_sdk.py` (detecta un
  runtime/plugin instalado que no es el esperado) (~1–2 h).
- `LINK_DEPENDS` sobre el plugin de GPU en CMake, para reenlazar cuando cambie el dylib (15 min).
- Auditar nuestro cierre: su cuelgue al 100% de CPU con ventana negra al reiniciar el proceso viene de
  `Runtime::Shutdown → InputSystem::Shutdown → ~MnkInputDriver → CallInUIThreadSynchronous`; ellos lo esquivan con
  `FlushLogging()` + `std::_Exit(0)` solo en el reinicio. Comprobar si nuestra salida normal pasa por ahí (1–2 h).
- Para T7: `osascript -e 'POSIX path of (choose file ...)'` como selector de ISO sin dependencias, y
  `_NSGetArgv` + `posix_spawn` como patrón de relanzado; `GetPendingRestartFlags`/`ClearPendingRestartFlags` del
  SDK si un launcher escribe cvars restart-only antes de lanzar.
Sin interés: su build del plugin solo (nuestro target Ninja ya es incremental), los presets (tenemos `build.sh`),
y su diagnóstico del frame rate (es el #507 que ya tenemos arreglado; 25–30 FPS en un M1 Air como referencia).

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

### T9. Plan tras el análisis del 2026-10-13 (`docs/analysis/`) — pendiente de luz verde para probar
Estado medido (build `full`, 67,5 ms/frame, 14,8 FPS): GPU ≈ 60 ms y el hilo de comandos de la GPU 54–57 ms
de CPU. Para 30 FPS hacen falta **las dos cosas**: GPU ≤ 33 ms y ese hilo ≤ 33 ms. Quitar solo el MSAA da ~18 FPS.
1. **MSAA = sombreado por muestra involuntario** (`msaa-code-path.md`). `spirv_translator.cpp:2976-2980`
   declara `SampleRateShading` en todo PS que escribe color 0 sin early-Z (alpha test, kill); SPIRV-Cross
   (MoltenVK) trata cualquier módulo con esa capacidad como per-sample (`[[sample_id]]`) y Metal ejecuta el
   PS una vez por muestra. Explica el coste binario (passes principales a 2x; los 4x son de solo profundidad).
   Arreglo de una condición: declararla solo con FSI o `IsSampleRate()`; mantener `gl_SampleMask`.
   Esperado: la mayor parte de los ~45–50 ms de GPU. Probar base vs `FABLE2_EXP_MSAA1` después.
2. **Hilo de comandos (CPU)**: grabación/encode de MoltenVK + descriptores ~30 ms, caché de texturas y
   bookkeeping ~14 ms, subidas y parseo ~8 ms, espera al slot de memexport 10–13 ms. Candidatos:
   bigmah/rexglue-sdk@38955c4916 (`shared_memory_upload_coalesce_kb`, `vulkan_narrow_render_area`: 218→79
   passes, 529→42 Mpx; revisar contra nuestro upload-first), deduplicación de barreras y command buffers
   previos para subidas (rama de andrew17182), variables de MoltenVK (`MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS`,
   `MVK_CONFIG_USE_MTLHEAP`) a medir.
3. **Estratégico**: andrew17182/Fable-2-Recomp `feature/macos-playtest` lleva Fable 2 a un plugin Metal nativo
   (de XeniOS; DXBC→DXIL→Metal Shader Converter); dicen 161 FPS sin límite en M5 Max (no verificado). himdo
   cerró su PR #30 por tamaño (+72k). Evaluar: licencia, dependencias, compilarlo y medirlo en el M3 Pro.
4. Cuelgue de la T3 en el título: carrera al arrancar con la caché de pipelines fría (salto de presentación
   por pipelines provisionales). Probar con caché fría y `--vulkan_async_skip_incomplete_frames=false`.
5. Correcciones a conclusiones previas: "la CPU explica el 8 %" ya no vale (el hilo de comandos es el 80–85 %
   del frame); el ahorro de la T3 (30 ms) fue en parte CPU; `exp_adjust` no mejora nada medible.
- **Resultados T9 (2026-10-13, noche; misma escena, `HOLD=40`, captura final comprobada):**
  | Prueba | FPS 3D |
  |---|---|
  | build actual (`macos` `bbadd11`) | 14,8 |
  | MSAA forzado a 1x (`FABLE2_EXP_MSAA1`) | 25,5 → el MSAA cuesta hoy ~28 ms, no 50 |
  | sin `SampleRateShading` para la máscara (punto 1) | 14,3 → hipótesis descartada, revertido |
  | bigmah 38955c4 (área de render acotada + subidas agrupadas) | 14,4 → sin ganancia, descartado |
  | `MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS=1` | ~11, peor |
  | `MVK_CONFIG_USE_MTLHEAP=1` | 14,1, igual |
  Consecuencias: el coste del MSAA no es sombreado por muestra ni guardado/recarga de adjuntos; quedan
  las rutas solo-MSAA del emulador (transferencias de propiedad por muestra, dump/resolve multisample) o el
  propio coste de MSAA del GPU de Apple vía MoltenVK. Con 1x el frame baja a 39 ms, así que el hilo de
  comandos gasta ≤ 39 ms (la estimación de 54–57 ms estaba inflada por `sample`).
- **MoltenVK está deprecado por Khronos** (README, 2026): mantenimiento comunitario; para Apple Silicon
  recomiendan **KosmicKrisp** (Mesa, Vulkan 1.4 conforme, en el Vulkan SDK desde 2026-09-29). Su tabla de
  features: `tessellationShader`, `sampleRateShading`, stencil export y dynamic rendering sí; sin geometry
  shaders ni fragment shader interlock (igual que MoltenVK, el SDK ya no los necesita). Homebrew `mesa` lo
  compila en macOS ≥ 26. Probar = instalar Mesa y apuntar `VK_DRIVER_FILES` a su ICD, sin recompilar.

## Para la próxima sesión (anotado 2026-10-13, noche)
Estado: build `macos` `bbadd11` del fork (subido), 14,8 FPS en 3D y 29 en menús; trabajo en `/Volumes/EXT/fable2/`.
1. **KosmicKrisp** — instalado (`brew install mesa`, Mesa 26.2.4, ~2 GB) y probado una vez:
   `VK_DRIVER_FILES=/opt/homebrew/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json` (el SDK respeta la
   variable; sin ella usa MoltenVK). Arranca y llega al 3D sin recompilar; **17,2 FPS** frente a 14,8 con
   MoltenVK (escena no del todo comparable: a 150 s seguía la cinemática). **Parpadeos importantes** (vistos
   por el usuario) y, al final, **device lost** → abort (`CheckSubmissionFenceAndDeviceLoss` esperando la fence
   de `IssueDraw_MemexportReadbackFastPath`; informe `fable_2-2026-10-13-231524.ips`). Pendiente: aislar el
   parpadeo (¿presentación/sincronización, pipelines provisionales, memexport?) con `vulkan_async_skip_incomplete_frames`,
   `readback_memexport*` y la validación de Mesa (`MESA_VK_*`/`KK_DEBUG`); medir con MSAA 1x; reportar a Mesa
   si es un fallo del driver. Para volver a MoltenVK basta con no definir `VK_DRIVER_FILES`.
   **Aparcado (2026-10-16, decisión del usuario):** con la medida de parpadeo (`FLICKER=30`) MoltenVK da mediana
   1,8 / máx 2,6 / 0 picos y KosmicKrisp 2,4 / 66,5 / 3 picos, más device lost también con
   `--readback_memexport=false` y con `--vulkan_shared_memory_upload_first=false` (no es la ruta de memexport).
   Nos centramos en MoltenVK. Al volver: Mesa main tiene una regresión que reinicia el render pass en cada draw
   con vértice provocador "last" (mesa#16580); Xenia lo usa.
2. **Coste del MSAA (28 ms)**: medir las rutas solo-MSAA del emulador (transferencias de propiedad por muestra,
   dump/resolve multisample; `docs/analysis/msaa-code-path.md`, puntos 3 y medidas 3–4): contador de draws de
   transferencia y dispatches por frame, y el interruptor 2x→4x para ver si el coste escala con las muestras.
3. **Hilo de comandos de la GPU**: perfil sin `sample` (que reduce los FPS a la mitad) o con ventana corta, y
   revisar deduplicación de barreras / command buffers previos de la rama de andrew17182.
4. **Plugin Metal nativo de andrew17182** (`feature/macos-playtest`): evaluar licencia, dependencias
   (Metal Shader Converter) y medirlo en el M3 Pro.
5. Cuelgue ocasional al arrancar con caché fría (T3/T9 punto 4) y PR himdo#8 (cargas rápidas).
Árbol auxiliar: `/Volumes/EXT/fable2/t3-wt` rama `exp-msaa` con los interruptores TEMPDIAG sin commitear
(incluida la ruta de escritura de registros en bloque); plugin experimental en `t3-wt/out/mac-arm64/`.

### T10. MSAA en MoltenVK: diagnóstico cerrado (2026-10-16) y modo de 1 muestra en el host
Medidas en la calle jugable (ventana corregida a 180–220 s; antes algunas medidas caían en la cinemática):
| Prueba | FPS |
|---|---|
| MSAA normal | 14,5 |
| MSAA a 1x (truco de registro) | 25,8 |
| sin transferencias a destinos MSAA | 14,8 |
| transferencias sin sombreado por muestra | 13,3 |
| MSAA normal sin point sprites | 17,8 |
| MSAA 1x sin point sprites | 29,0 (tope de 30) |
Trazas (calle, mismo plugin): fragmentos 45,7 → 17,1 ms/frame con 1x; vértices iguales; el pase principal pasa de
3,5–5,6 a 17–25 ms de fragmentos. Volcado MSL (`MVK_CONFIG_DEBUG=1`): 42 de 108 PS del juego eran por muestra
(`[[sample_id]]` vía `SampleRateShading`); con el arreglo del punto 1 de T9 quedan 0, **sin ganancia**. Descartados:
transferencias, sombreado por muestra, 2x emulado como 4x (MoltenVK anuncia 2x). Los point sprites explican ~9 ms del
coste del MSAA; el resto (~22 ms) es el coste general de la escena con MSAA en el GPU de Apple vía MoltenVK.
**Decisión:** implementar un modo macOS que renderice superficies 2x/4x del guest en RTs de 1 muestra del host
manteniendo el layout de EDRAM del guest (`docs/analysis/msaa-code-path.md`, fix 3). Coste visual: sin antialiasing.
**Hecho (fork `macos` `2864452`):** `vulkan_host_msaa_1x`, por defecto on en macOS. Solo las superficies **2x** del
guest pasan a 1 muestra en el host; las 4x siguen 4x (con 4x, sus dos muestras horizontales llevan datos distintos
cuando el juego reinterpreta la EDRAM entre 2x/4x o 32/64 bpp: la primera versión daba rayas verticales y tinte).
Resultado sin opciones: **3D 24,2 FPS** (14,5 antes), menús 29, imagen correcta, parpadeo 1,1/1,8/0 picos.
Costes: sin antialiasing de bordes en 2x, alpha-to-mask con tramado, consultas de oclusión cuentan píxeles.
Desactivar con `--vulkan_host_msaa_1x=false`. Pendiente: revisar más escenas (interiores, noche, agua, vegetación
con alpha-to-mask) y posibles filas duplicadas si el juego reinterpreta 1x↔2x.
