# memory.md — Port de Fable 2 Recomp a macOS (Apple Silicon)

Registro de trabajo para retomar el port. Última actualización: 2026-10-11.
Rama: `macos-port` (sin commits todavía). Máquina de pruebas: MacBook Pro M3 Pro, páginas de 16 KB.

## Estado en una línea

El juego **compila, enlaza, corre y se ve en macOS ARM64**, en ventana y a pantalla completa: llega a la
pantalla de título ("Pulsa Ⓐ para empezar") renderizada con Vulkan sobre Metal (MoltenVK), con audio.
Commits en `macos-port`: `28d2d5f` (build de macOS), `7dbf1f4` (test de paridad) y el arreglo de pantalla completa.

## Qué está verificado

- SDK oficial ReXGlue **v0.10.0 `mac-arm64`** (precompilado, sin los parches del proyecto) en
  `out/tooling/rexglue-sdk-0.10.0/mac-arm64`. Incluye `rexglue` (codegen, Mach-O arm64), `librexruntime.dylib`,
  `librexgpu-xenos.dylib` (solo Vulkan en Mac) y MoltenVK 1.4.1.
- Codegen en Mac: 35 s, 297 ficheros C++ (308 MB). Build Release completo (`-O3`) en ~25 min; el cuello de
  botella es `generated/default/fable_2_register.cpp` (una función gigante, ~15 min en un hilo).
- Binario: `out/build/mac-arm64-release/fable_2` (120 MB). El SDK coloca al lado los dylibs, MoltenVK y su ICD.
- Al arrancar: XEX verificado (SHA-256 MATCH, perfil **GOTY Spain**), memoria del guest en
  **`0x7000000000`** (no en `0x100000000`), dispositivo Vulkan "Apple M3 Pro", swapchain Retina, audio por los
  altavoces (confirmado por el usuario).
- Los 3856 errores `BaseHeap::AllocFixed attempting to reserve an already reserved range` son **normales**:
  `docs/main_menu_crash_fix.md` explica que el juego necesita que esas reservas fallen.

## Contenido del juego

- ISO del usuario: `Fable 2 GOTY [RF][SPA].iso` (XGD2, cabecera GDFX en `0xFDA0000`). Ya está en `.gitignore` (`*.iso`).
- Extraída con un lector GDFX de un solo uso (port en Python de `tools/x360extract/lib/Gdfx.cs`, en el
  scratchpad de la sesión, no en el repo): 433 ficheros, 6,5 GiB. `default.xex`, `data/`, `nxeart` (un fichero
  STFS) y `$SystemUpdate/` están en la raíz del repo, todos ignorados por git.
- Verificado contra `constants/game_versions.json`: hash de `goty-spain` y marcadores requeridos presentes.
- Se ejecuta con `--game_data_root <raíz del repo>` para no duplicar 6,5 GB (el disco estaba al 96 %).

## Cambios hechos en la rama `macos-port`

Scripts nuevos:
- `build.sh`: equivalente a `build.cmd` (presets `mac-arm64-*` / `mac-amd64-*` / `linux-*`).
- `tools/setup_sdk.sh`: descarga el SDK oficial v0.10.0 para el host.
- `tools/stage_content.sh`: copia el contenido junto al binario (fusiona `data/dir.manifest` con `awk`).
- `.gitignore`: excepciones para los `.sh`, `*.iso`, `/nxeart` (puede ser fichero). `.gitattributes`: `*.sh eol=lf`.
- README: sección "macOS (experimental, untested in game)".

Código (para que compile en Mac):
- `src/main.cpp`: `fable2_heap_scan.h`, `fable2_av_probe.h` y `fable2_stall_dump.h` dentro de `#ifdef _WIN32`
  (SEH/VEH/Toolhelp; venían apagados por defecto).
- `fable2_deadbeef.h`, `fable2_ui_input_probe.h`: ramas `VirtualAlloc`/SEH solo en Windows; en POSIX devuelven `false`.
- `fable2_font_probe.h`: `CreateThread` → `std::thread`.
- `remote_control_server.h`: faltaba `<netinet/tcp.h>` (rompía cualquier build Debug fuera de Windows).

Bugs de ejecución corregidos:
- `fable2_state_probe.h`: usaba la base fija `0x100000000` y `page_ok()` devolvía siempre `true` fuera de
  Windows. En Mac habría tumbado el build Debug al arrancar (el clasificador de estado siempre corre en Debug).
  Ahora la base sale de `rex::system::kernel_state()->memory()->virtual_membase()` y `page_ok()` usa
  `mach_vm_region` en macOS. (En Linux sigue suponiendo legible: marcado con `ponytail:`.)
- `hotfunc/hotfunc_yield.h`: los yields saltados solo ponían barrera de compilador basándose en TSO de x86;
  fuera de x86 ahora usa `atomic_thread_fence(seq_cst)`.
- `fable2_ui_input_probe.h`: `dump_hop_on()` no tenía `return` en POSIX (comportamiento indefinido).

Pendiente de compilar:
- `CMakeLists.txt`: opción `FABLE2_REMOTE_CONTROL_ANY_CONFIG` (OFF por defecto) para meter el servidor de
  control remoto en un Release local de diagnóstico. **Editada pero no compilada** (el usuario detuvo el rebuild).

No verificado: nada de lo anterior se ha compilado para Windows (aquí no hay toolchain de Windows). Las ramas
de Windows quedan intactas salvo el hilo de `font_probe` y la base de `state_probe` (en Windows vale lo mismo).

## Cómo reproducir

```
tools/setup_sdk.sh                              # una vez (lo hace build.sh solo si falta)
./build.sh -release fable_2                     # codegen + build completo
cd out/build/mac-arm64-release
./fable_2 --game_data_root /Users/antoniorodriguez/GITHUB-RASTY/Fable-2-Recomp \
          --fullscreen=false --window_width=1280 --window_height=720 \
          --vulkan_log_debug_messages=false
```

Logs del juego en `out/build/mac-arm64-release/logs/fable_2_NNN.log`.

## Resuelto (parcialmente): pantalla negra = pantalla completa

Hallazgo del 2026-10-11 (después del análisis de Fable): **en ventana se ve bien con todas las opciones por
defecto**. Prueba A/B: en ventana y con `vulkan_async_skip_incomplete_frames` por defecto → título visible
(captura a los 55 s); a pantalla completa, con cualquier combinación de opciones → negro. Por tanto no es la LUT
de gamma ni los pipelines provisionales (causas 1 y 2 de Fable): es la **presentación a pantalla completa en
macOS** (su causa 4). Fullscreen crea un swapchain de 3024×1898 en un Space propio y lo que llega a la
pantalla es negro.

**Arreglado.** Prueba aislada capturando la ventana del juego por su ID (`screencapture -l`, con un listado
de ventanas hecho en JXA con `CGWindowListCopyWindowInfo`):

- pantalla completa nativa (Space propio) → swapchain 2560×1440 recreado a 3024×1898 → **negro**;
- pantalla completa sin Spaces (`SDL_VIDEO_MAC_FULLSCREEN_SPACES=0`) → un solo swapchain de 3024×1964 → **se ve**;
- ventana grande (2582×1836) → **se ve**.

Arreglo: `src/main.cpp` hace `setenv("SDL_VIDEO_MAC_FULLSCREEN_SPACES", "0", 0)` en un inicializador estático
(solo en `__APPLE__`, antes de que el runtime abra la ventana de SDL; si el usuario ya fijó la variable, se
respeta). No sé si el negro viene del Space o de recrear el swapchain al entrar en él: sin Spaces no hay
recreación. Queda pendiente averiguarlo para reportarlo al SDK.

Trampa al probar: el terminal del agente es zsh, que no separa `$VAR` en argumentos. Las primeras pruebas T1/T2
arrancaron sin `--game_data_root`, murieron al momento y las capturas salieron del escritorio. Hay que usar
`${=VAR}` o pasar los argumentos directamente.

Lo que sigue es el historial de la investigación, conservado como contexto.

### Historial: pruebas a pantalla completa

Pruebas hechas, **todas en negro** (todas a pantalla completa): configuración por defecto; `--render_target_path_vulkan=fbo`;
`--swap_post_effect=none --present_effect=bilinear`; `--vulkan_dynamic_rendering=false`. Una combinada
quedó sin concluir: la captura salió del escritorio porque el juego a pantalla completa estaba en otro Space.

Señales del log: `geometryShader` no soportado (usa la vía alternativa); ~14k avisos de MoltenVK
*"Metal does not support disabling primitive restart"*; un solo *"Skipping Vulkan frame presentation due
to async placeholder draw usage"*; ~52 pipelines creados en ráfagas.

### Diagnóstico de Fable 5.1 (sesión del 2026-10-11; superado por el hallazgo de arriba)

Dato nuevo: los ~14k avisos de *primitive restart* salen del **hilo principal (presenter)** a 250–580/s
desde el segundo 48. El presenter solo dibuja una tira cuando existe imagen de salida del guest. Por tanto,
**el guest sí hace swaps y el presenter sí pinta**, pero lo que pinta es negro.

Causas, de más a menos probable:
1. **La etapa de gamma del swap produce negro**: la LUT de gamma se queda a cero en la GPU. Es exactamente la
   causa documentada de la pantalla negra de Vulkan en Windows (hunk de `SetupContext()` en
   `thirdparty/rexglue-sdk-local.patch`: el buffer host_visible|device_local|host_coherent no se comporta
   coherentemente; el arreglo sube los datos con `vkCmdCopyBuffer`). El runtime oficial de Mac **no tiene ese
   arreglo**, y en Apple (memoria unificada) todos los tipos de memoria son device_local+host_visible, así
   que toma justo el camino roto.
2. **Pipelines provisionales perpetuos** → frames saltados → la salida se queda en el primer frame negro.
3. **Cadena EDRAM → resolve → textura rota en MoltenVK** (común a FSI y FBO). Sospecha secundaria: páginas de
   16 KB del host frente a vigilancia de escritura de 4 KB del guest.
4. **El swapchain no es lo que se ve** (ventana en otro Space). Se descarta con F3.
5. Que el guest no renderice nada: descartado.

Aunque se arregle, el Vulkan de Windows también tiene "muchos artefactos" (commit `afebcc5`).

### Plan ordenado (cada prueba ~60 s, en ventana)

1. **F3 a los ~50 s** (overlay ImGui con FPS del guest). Si sale el overlay con FPS > 0, el presenter y la
   pantalla funcionan. Si no sale el overlay, el problema es de presentación: probar
   `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0`.
2. **`--vulkan_async_skip_incomplete_frames=false`** (y opcionalmente `--vulkan_pipeline_creation_threads=0`).
   Si aparece algo, es la causa 2.
3. **Variables de MoltenVK, una por prueba**: `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0`; luego
   `MVK_CONFIG_LOG_LEVEL=4 MVK_CONFIG_DEBUG=1` + `--vulkan_log_debug_messages=true`; luego
   `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1`. Es la vía barata para ver qué etapa falla.
4. **Recompilar con `-DFABLE2_REMOTE_CONTROL_ANY_CONFIG=ON`** (~1 min) y usar
   `python3 tools/fable2_control.py screenshot /tmp/guest.png` (devuelve `avg_luma` / `looked_black` de la
   salida del guest) y `game-state`. Si `avg_luma` es ~0, el negro se produce antes de la presentación
   (causa 1 o 3). Si es > 0 con la pantalla negra, es un bug de presentación.
5. **Si nada de lo anterior resuelve**: compilar el SDK desde fuente (Vulkan ON, `mac-arm64`) con los parches
   del proyecto. Trae el único arreglo conocido para la causa 1 y la instrumentación TEMPDIAG (readback de la
   imagen del swap, dumps de la LUT de gamma, telemetría de presentación) que separa la causa 1 de la 3 en una
   sola ejecución. Antes de compilar hay que comprobar: (a) que el hunk de `xmemory.cpp` del parche mantiene el
   `AllocFixed` original que falla; (b) que lo de `exception_handler_win.cpp` no se compila en Mac; (c) que el
   arreglo de gamma funciona sin tipos de memoria "no device-local"; (d) que el árbol fuente fija su propio
   MoltenVK (`4efa6488`), que puede no ser el 1.4.1 actual.

## Trampas conocidas

- Disco: el repo con contenido, build y SDK ocupa >20 GB; el disco llegó al 96 %.
- `screencapture` necesita permiso de Grabación de pantalla para el terminal/VS Code; a pantalla completa el
  juego va a su propio Space y la captura puede salir del escritorio. Usar ventana.
- Diagnósticos que siguen con `0x100000000` fijo (no válido en Mac): `fable2_deadbeef.h`,
  `fable2_ui_input_probe.h` (`host_of`), `fable2_list_dump.h`, el monitor `allocwatch` de `fable_2_app.h`
  (en Mac nunca lee, porque busca páginas en `/proc/self/maps`, que no existe). Todos vienen apagados por defecto.
- Teclado y ratón como mando en Mac/Linux: `KeyboardGamepadDriver` se registra como `WindowInputListener` de la
  ventana del juego y guarda el estado de cada `VirtualKey` desde los eventos (hilo de UI); el poll del guest lo
  lee. La cámara con ratón usa `Window::SetRelativeMouseMode` (se activa desde los eventos, porque SDL exige el hilo
  principal). **Verificado:** E (botón A) salta la demo y avanza al menú; el usuario empezó una partida con el
  teclado. **Sin verificar:** la cámara con ratón en partida. Windows sigue con `GetAsyncKeyState` sin cambios.
  Para probar con teclas automáticas hace falta permiso de Accesibilidad para el terminal (`osascript` + System Events).
- El launcher WPF no existe en Mac (editar `fable2_config.toml` / `fable_2.toml` a mano).
- La cámara con ratón en partida **verificada** por el usuario ("gira bien").
- En el menú principal la **flecha abajo (cruceta) no mueve el cursor**, pero **S (stick abajo) sí**. El SDK
  traduce bien `SDL_SCANCODE_DOWN` a `kDown` y ImGui no marca el evento como gestionado. Sospecha: el script
  de prueba. `key down (key code 125)` de AppleScript ejecuta `key code`, que es una pulsación instantánea,
  y el juego, que muestrea por frame, no la ve. Pendiente: que el usuario lo pruebe a mano.

## Fallos gráficos en partida (abierto)

El usuario ve "fallos gráficos graves" en partida. En las capturas de la escena de referencia (Bowerstone, muralla
con brasero, diálogo de Rose "Piensa en ese comedor…", unos 160 s tras arrancar con el script de abajo) casi todo
se dibuja bien. Lo sospechoso: el suelo nevado del camino es un plano blanco liso sin textura ni sombreado;
el cielo a veces sale como una franja plana; hay manchas azules con bordes dentados en las ventanas (quizá sea nieve
en los alféizares). Falta que el usuario concrete qué ve (parpadeos, triángulos, colores…).

Pruebas A/B en esa escena, todas **sin cambios** respecto a la referencia: `--gpu_allow_invalid_fetch_constants=true`,
`--render_target_path_vulkan=fbo`, y las tres `--vulkan_force_*` (quads, rectángulos y point sprites) juntas.

El usuario confirma (2026-10-12): lo que se ve en las capturas es lo que hay, **rendimiento pésimo** y fallos
graves por todas partes.

**Pista fuerte:** cada ejecución escribe ~30 000–180 000 veces `[mvk-warn] Metal does not support disabling
primitive restart`. MoltenVK (`MVKPipeline.mm`) solo lo emite **al crear un pipeline** con topología strip/fan,
y de las cuatro llamadas a `vkCreateGraphicsPipelines` del backend solo los pipelines Xenos
(`pipeline_cache.cpp`, ~l. 3488) usan strips/fans. La caché en disco
(`cache/shaders/shareable/4D5307F1.*.vk.xpso`) solo tiene ~12 KB, así que **se crean pipelines sin parar** en
vez de reutilizarlos. Eso basta para hundir el rendimiento (creación de pipelines en el hilo de la GPU) y
puede explicar los fallos (draws con un pipeline provisional o sin pipeline).

**Causa encontrada y arreglada:** no eran los pipelines Xenos. En `vulkan_presenter.cpp` (pintado de la salida
del guest sobre el swapchain, topología `TRIANGLE_STRIP`) nunca se asignaba
`swapchain_effect_pipeline.swapchain_format`. En **cada frame** el formato "no coincidía": esperaba a que la
GPU terminara (`AwaitSubmissionCompletion`), destruía el pipeline y lo recreaba. Medido en la pantalla de
título: ~115 creaciones/s con el runtime oficial y **1 en total** con el arreglo. En la escena de referencia
el suelo ya tiene textura, la iluminación es correcta y el juego avanza bastante más rápido (con el mismo guion
de teclas, a los 165 s ya ha pasado el diálogo de Rose). El bug está en el fork y casi seguro también en el
0.10.0 oficial (mismo síntoma), así que afecta igual a Vulkan en Windows. Merece reportarse upstream (pendiente
de que el usuario lo autorice).

Lo que queda visible: finas líneas claras en las aristas de algunas mallas (piedras, espada) y un halo morado
alrededor de algún personaje (puede ser del juego). Aún sin medir FPS (F3).

`thirdparty/rexglue-sdk-fork-fixes.patch` (lo aplica `prepare_runtime_sdk.py` en Windows y en Mac):
1. el arreglo del presentador;
2. `presenter.h`: el fork dejó los contadores del pacer dentro de `#if REX_PLATFORM_WIN32`, lo que rompía la
   compilación fuera de Windows;
3. `src/system/CMakeLists.txt`: `-Xlinker /DEF:` solo en WIN32 (en Mach-O se exporta con visibilidad
   por defecto).
`build.sh` usa `out/tooling/runtime-sdk/<plataforma>` si existe y pasa `FABLE2_CODEGEN_TOOL` al `rexglue`
oficial, porque el `rexglue` de la copia carga el runtime nuevo y revienta con *Bus error*.

Runtime desde fuente: `tools/build_runtime_sdk.sh` (equivalente de `build_runtime_sdk.cmd`; Vulkan ON,
Tracy OFF) más `stage_renderer_sdk.py --platform mac-arm64`, que lo instala sobre una copia del SDK oficial
en `out/tooling/runtime-sdk/mac-arm64`. El fuente y la compilación van en un sparsebundle APFS case-sensitive
del disco externo (`/Volumes/EXT/fable2/fable2build.sparsebundle`, montado en `/Volumes/fable2build`;
se monta con `hdiutil attach`), porque EXT es exFAT: sin enlaces simbólicos y lleno de `._*`.
`rexglue-sdk-local.patch` solo trae arreglos para NVIDIA Optimus y diagnóstico de Windows, nada de Mac.

Herramienta de prueba (scratchpad de la sesión, no está en el repo): `scene.sh <nombre> "<t:acción ...>" [args]`
arranca el juego, envía teclas (`e`=E/A, `s`=S/stick abajo, `d`=flecha abajo) y captura la ventana (`c`) con
`screencapture -l` + JXA `CGWindowListCopyWindowInfo`. Secuencia hasta la escena: `25:e 37:s 40:e 160:c`
(título → menú → Continuar → carga). Necesita `skip_intro_videos = true` en el `fable2_config.toml` del build
(ya cambiado) y permisos de Grabación de pantalla y Accesibilidad. Copia de las partidas guardadas en el scratchpad.

## Otros trabajos de esta sesión

- Graphify 0.9.85 instalado (uv), grafo en `graphify-out/`, hooks de git (`post-commit`, `post-checkout`),
  sección en `CLAUDE.md`. Faltan por extraer semánticamente `FUNCTION_NAMES.md` y `docs/`: el clasificador
  bloqueó esos subagentes; `/graphify . --update` los recoge.
- `tests/test_config_parity.py`: comprueba que las claves de `fable2_config.toml` coinciden en C++
  (plantilla + `Read<>`), el TOML que se copia junto al exe y C# (`Fable2ConfigFile`).

## Rendimiento: de ~6 a 30 FPS (2026-10-12)

Tras arreglar el presentador, el juego iba a ~6 FPS. Diagnóstico (perfil con `sample`, análisis de Fable 5.1 y
`MVK_CONFIG_PERFORMANCE_TRACKING=1`):
- En macOS nada limita los repintados del hilo de UI (el tick de vblank del presentador solo existe en Win32), así
  que se presentaba a 120 Hz (ProMotion). Cada present esperaba ~8,2 ms en `[CAMetalLayer nextDrawable]`, porque
  MoltenVK adquiere el drawable de forma perezosa **dentro de** `vkQueueSubmit`, y lo hacía **con el
  `recursive_mutex` de la cola compartida cogido** (~98% del tiempo). El hilo "GPU Commands" no podía enviar y el
  hilo "3D Engine" del juego giraba en `GpuProgressCheck`.
- No influyeron: `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=0` (5,7 FPS), `--render_target_path_vulkan=fbo` (7,4) ni
  `--readback_memexport=false` (7,0). Ojo: el overlay F3 fuerza un repintado por frame y contamina las medidas.
- **Arreglo:** `pace_to_guest_vblank=true` (cvar del fork: como mucho un present por frame del guest), activado por
  defecto en macOS desde `fable_2_app.h` (`OnPostInitLogging`, solo si el cvar sigue en su valor por defecto).
  Resultado: 29–30 FPS estables en título, menú y carga, que es la tasa nativa de Fable 2 en 360.
  Para medir: el log `logs/fable_2_NNN.log` trae una línea `guest vblank pacer: guest frame (gate) ~N Hz` por segundo.
- Seguimiento:
  - Bucle del hilo de UI cuando el limitador suprime (~175 kHz sin frames nuevos): `vulkan_presenter.cpp` solo
    vuelve a pedir repintado si quien suprime es la puerta de vsync; un frame nuevo del guest ya lo pide desde
    `RefreshGuestOutput`. Está en `rexglue-sdk-fork-fixes.patch`.
  - Hook de `functrace`: nueva opción CMake `FABLE2_FUNC_TRACE`, OFF en Apple y ON en el resto (allí existe
    `fable2-functrace.cmd`).
  - `stateprobe` y servidor remoto: solo existían porque el build de pruebas tenía
    `FABLE2_REMOTE_CONTROL_ANY_CONFIG=ON`; ya está en OFF. El guion de pruebas no los necesita.
  - `hotfunc_yield_every` no lo toco: con el juego al tope de 30 no hay motivo.
  - No retener el mutex de la cola mientras MoltenVK espera el drawable: con el limitador la espera pasa a
    0,02 ms, así que no hace falta por ahora.
- PR del arreglo del presentador al fork: himdo/rexglue-sdk#11. Issue upstream: rexglue/rexglue-sdk#507.
  El `main` de himdo avanzó a `a728090` (espera de memexport, readback de resolve, sesgo de profundidad en
  D3D12); el submódulo sigue fijado en `b097123`.

## Cuelgue en blanco (resuelto) y rendimiento en 3D (abierto) — 2026-10-12 tarde

**Cuelgue en pantalla blanca** al entrar en la escena 3D (2 de 3 veces): no era un bloqueo, era un *crash* que
daba vueltas. `getVkHandle()` de MoltenVK reescribe `ICD_LOADER_MAGIC` sobre el puntero de despacho del loader
cada vez que notifica un mensaje a un *debug messenger*; el siguiente trampolín del loader sobre ese `VkDevice`
(`vkAllocateCommandBuffers`, cuando todos los command buffers están en vuelo) leía `0x01CDC0DE` y fallaba.
El manejador POSIX del SDK volvía de un fallo que nadie atendía y la instrucción se repetía sin fin.
Arreglo (commit `abe45e5`): `vulkan_log_debug_messages=false` por defecto en macOS (sin messenger no se llama) y,
en el parche del fork, señal no atendida → `SIG_DFL` (crash con informe en vez de bucle). Para investigar un
crash: `~/Library/Logs/DiagnosticReports/fable_2-*.ips` (JSON tras la primera línea; `faultingThread`).

**Rendimiento en 3D: ~7,7 FPS** (menús y carga a 30). Perfil del hilo "GPU Commands" en 3D: 521/1268 muestras en
`IssueDraw_MemexportReadbackFastPath → FullPath`, de ellas 386 en `vkWaitForFences` (drenado completo de la GPU por
cada draw con memexport) y ~130 en `vkQueueSubmit`; otras ~400 codificando envíos en MoltenVK. El hilo "3D Engine"
del juego espera a ese hilo (`GpuProgressCheck`). MoltenVK: "Execute a MTLCommandBuffer" media 43 ms (cola, no
solo GPU). `--render_target_path_vulkan=fbo` en 3D: igual (5,5–7,8). En curso: A/B con `--readback_memexport=false`
y `--vulkan_submit_on_primary_buffer_end=false`; traza "Metal System Trace" de 5 s en `scratchpad/mst.trace`.

Herramienta: `scratchpad/prof3d.sh <nombre> [args]` arranca, navega al 3D (E, S, E), captura, hace `sample` 10 s y
saca la cadencia del log. `oncpu.py` resume el `sample` por hilo (tiempo propio sin esperas).

Forma de trabajo pedida por el usuario: Fable 5.1 lleva el análisis; agentes de Opus para el trabajo bruto.
El hook de `functrace` seguía en el binario tras cambiar la opción CMake porque el PCH viejo no se regeneró:
borrar `CMakeFiles/fable_2_recomp.dir/cmake_pch.hxx.pch` y los `.o` fuerza la recompilación (~40 min a -O3).

### Diagnóstico 3D con traza de Metal (2026-10-12 noche)

Traza "Metal System Trace" de 5,4 s en la escena 3D (~7,9 FPS): GPU ocupada 73% a frecuencia máxima, ~105 ms
de GPU por frame; la CPU solo explica el 8%. **No son shaders pesados, es fragmentación:** 21 111 draws en 5 s
(~490/frame) van por pipelines con **teselación Xenos** (terreno: kQuadPatch, `primitive_processor.cpp:349`
cita "4D5307F1 - ground - continuous"; la caché `4D5307F1.fbo.vk.xpso` tiene 14 pipelines PatchList, 10
continuos + 4 adaptativos). MoltenVK ejecuta VS+TCS en un compute encoder y **reinicia el render pass de Metal
en cada draw** (`MVKCmdDraw.mm:222-291`, sin condición ni opción): 21 111 reinicios = 32% del tiempo de GPU,
más ~2300 encoders/frame y 17 005 `vkCmdCopyBuffer` (shared memory, memexport, resolve).
A/B en 3D, todas sin mejora: `--readback_memexport=false` (7,5), `--vulkan_submit_on_primary_buffer_end=false`
(4,7), ambas (2,8–9,7).
**`render_target_path_vulkan=fbo` es el valor por defecto** (la caché se llama `.fbo.`): mi "A/B con fbo" no
probó nada. **`=fsi` descartado:** 5 FPS e imagen corrupta (bandas de basura) en 3D; los 29–30 FPS que medí
primero con fsi eran del menú (el guion no entró en la partida: caché de shaders fría cambia los tiempos;
comprobar siempre la captura `*_3d.png`).
Vía que queda: emular en el vertex shader la teselación continua de QuadPatch (sin TCS/TES, lista de
triángulos; ~250 líneas en 5 ficheros del SDK: `shader.h` HostVertexShaderType nuevo, `primitive_processor.cpp`
6 índices por patch, `spirv_translator.cpp` registros de dominio desde gl_VertexIndex como ya hace la ruta
compute de memexport en :2323-2400, `pipeline_cache.cpp` topología TriangleList, constantes de sistema), dejando
el adaptativo con teselación real. Antes: medir `VGT_HOS_MAX_TESS_LEVEL` y el reparto continuo/adaptativo
(instrumentación temporal en curso). Memexport: la vía rápida cae a la completa (drenado) porque la clave se
repite en la misma submission (`command_processor.cpp:4353-4362`); 4c21ec4 upstream no lo arregla.
Scripts de análisis de trazas (resuelven id/ref de xctrace): `scratchpad/py/xt.py, a1-a3.py`; tablas en
`scratchpad/mst/` y `mst_fbo/`.

### Tras T1: dónde va el tiempo de GPU (traza mst_t1, 2026-10-12 noche)

T1 (subidas de shared memory antes del render pass, `vulkan_shared_memory_upload_first`, on por defecto en
macOS) cumplió lo suyo: encoders −38%, `CopyBuffer` −92%, passes fbo 409→~145/frame, GPU del 73% al 82%,
7,9→9,7 FPS en la posición del agente (7,5–8 en la mía: la posición de la escena importa; con el E extra a los
60 s se entra un poco antes). Pero la GPU por frame solo baja de 105 a 99 ms, porque **el coste fijo de passes
y reinicios era ~10 ms/frame, no ~50** (el p10 de un pass son 8 µs, no 100; mi hipótesis del volcado de
attachments por pass era falsa). Reparto real (ms/frame, mst_t1): sombreado de fragment ≈82 (63%), vertex 30
(de ellos 13,5 de teselación en compute y 12,6 de vértices teselados), coste fijo de passes 1,3, de reinicios
8,8, transfer/compute 7. **El 80% del tiempo está en 3 passes a mitad de frame** (el pass principal de escena,
unos 184 reinicios de teselación cada uno); los otros ~172 passes/frame son despreciables (p50 11 µs).
Consecuencia: T3 (quitar la teselación de Metal) ahorra como mucho ~25–30 ms/frame; lo grande es el fragment
del pass principal. Multiplicadores candidatos: MSAA 4x (60 passes/frame) y 2x, render targets gamma como
RGBA16 (`gamma_render_target_as_unorm16=true`, 8 B/px), overdraw, coste por píxel de los shaders traducidos.
Descartado: profundidad escrita desde el pixel shader (`depth_float24_convert_in_pixel_shader=false` en fbo;
solo la ruta FSI lo fuerza). Siguiente: A/B con interruptores temporales (MSAA forzado a 1x, factor de
teselación 1, `--gamma_render_target_as_unorm16=false`) y, si no discriminan, GPU Frame Capture
(`MTL_CAPTURE_ENABLED=1 MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE=3 MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE=x.gputrace`,
disparo por la tubería `/tmp/MoltenVKCapturePipe-*`) para abrirla en Xcode y ver el tiempo por draw y por
shader (el shader profiler de xctrace no guarda muestras al adjuntarse a un proceso).
Guion: `tools/mac/prof3d.sh` reintenta el E a los 60 s y captura a 110/130/150 s (hay un fundido a negro tras
la carga); `TRACE=1` graba la Metal System Trace. Tablas de la traza en `out/mac-measure/mst_t1/`.

### A/B del sombreado (2026-10-12, interruptores temporales; parche en `out/mac-measure/exp_switches.patch`)

Ventana estable de partida, ms/frame: base 104 (9,6 FPS) · **MSAA forzado a 1x (bits 16-17 de
`RB_SURFACE_INFO` a 0): 56 (−48, 17,8 FPS)** · sin draws teselados: 76 (−28, suelo negro) · sin point
sprites: 90 (−14, sin nieve) · factor de teselación 1: 98 (−6) · RT gamma como sRGB 8 bits: 103 (0) ·
MSAA 1x + gamma8: igual que MSAA 1x. Forzar 1x deja bandas de basura en la franja inferior (letterbox).
**El MSAA es el 45% del frame.** No es sombreado por muestra (`sample_rate_shading` solo con float24 en el PS,
`pipeline_cache.cpp:1394/3299`) ni emulación 2x-como-4x (2x nativo). Hipótesis en estudio: tráfico de
carga/volcado de tiles por pass sobre attachments dimensionados por la EDRAM (1040×2528, 320×8192…) × muestras.
T2 (`readback_memexport_no_drain`): +0,9 FPS pero el guest pierde casi todos los datos de memexport; OFF.

## Cierre de la sesión del 2026-10-12 (noche): tanda de pruebas y diseños

Forma de trabajo acordada: Fable 5.1 lleva el análisis y reparte; Opus para cambios en el SDK y diseños, Sonnet para
herramientas y documentación, Haiku para lo mecánico. Un solo juego en ejecución a la vez. Los agentes no hacen
commits en las tandas; los hago yo al final.

Estado de las tareas (detalle en TODO.md):
- T0 hecha (`tools/mac/`: prof3d.sh con reintento del E a 60 s, 3 capturas, `TRACE=1`; gpucapture.sh;
  menu_arrows.sh; scene.sh; oncpu.py; trace/*.py; apply_diag.py + tempdiag_block.inc).
- T1 hecha y activada por defecto en macOS (`vulkan_shared_memory_upload_first`): 7,9 → 9,7 FPS.
- T2: opción (a) implementada pero OFF (`readback_memexport_no_drain`): +0,9 FPS a costa de que el guest reciba
  solo el 1–10% de las lecturas. **La variante correcta ya existe**: el `main` del fork de himdo (`a728090`) trae
  4c21ec4 (espera solo a la fence de la submission del slot) y la rama `t2-memexport-no-drain` del worktree
  `/Volumes/fable2build/pr-wt` combina ambas. El checkout de macOS está en `b097123`, sin 4c21ec4.
- T3 diseñada (`docs/macos-gpu-plan.md`): lista de triángulos instanciada (una instancia por parche, rejilla fija
  17×17 en index buffer de 16 bits, VS de dominio como VS normal, N en `Modification`, `kVersion` 12→13),
  ~115 líneas en 8 ficheros, −12 a −18 ms/frame estimados. Ni T2 ni T3 llegan a 30 FPS: el MSAA es el 45%.
- T5 preparada (`docs/upstream/`): borrador de issue para MoltenVK (getVkHandle pisa el puntero de despacho del
  loader; el mensaje culpable no está identificado: el aviso de primitive restart pasa el pipeline, que no es
  dispatchable) y 6 PRs redactados; ramas en `/Volumes/fable2build/pr-wt`: `fix-vulkan-paint-pipeline-churn`
  (PR #11 abierto), `t1-upload-first`, `t2-memexport-no-drain` (corregir el mensaje: debe quedar OFF),
  `fix-pacer-members-non-win32`, `fix-def-only-windows`, `fix-posix-unhandled-signal`,
  `fix-ui-paint-spin-when-paced`. Sin push.
- T7 estudiada (`docs/macos-launcher-study.md`): asistente de terminal ahora (2–3 días), Avalonia con la lógica
  C# común si hace falta GUI (7–10), SwiftUI solo con cuenta de desarrollador (10–14); 9 decisiones del usuario.

Hallazgos de la tanda:
- GPU Frame Capture de la escena 3D: `out/mac-measure/f2_3d.gputrace` (1,6 GB), para abrir en Xcode
  (Performance → encoders por duración → attachments/sample count, draws y shader profiler). Pendiente de que el
  usuario la abra.
- `exp_adjust` del traductor SPIR-V leído de la palabra 4 en vez de la 3 (`spirv_translator_fetch.cpp:2032`):
  presente en nuestro checkout y en el `main` oficial; upstream cerró #334/#456 con "no aceptamos cambios en la
  emulación Xenos". Parche de referencia: PR #2 de saulob/geometry-wars-retro-evolved-2-recompiled
  (`patches/rexglue-sdk/0002-spirv-texture-exp-adjust.patch`; Xenia Canary 32889f51b). Prueba visual A/B en
  Fable 2 encargada a la cola de experimentos (resultado pendiente, ver TODO.md).
- Experimentos pendientes de resultado al parar: reparto 4x/2x del MSAA (`FABLE2_EXP_MSAA_MAX2`) con dos trazas
  (`mst_tr_base`, `mst_tr_msaa1`), renderArea recortado, pantalla completa, volcado de shaders, flecha abajo en el
  menú, point sprites a mitad de tamaño, repetición de la base. Interruptores en
  `out/mac-measure/exp_switches*.patch` (ignorados por git).
- Decisión tomada: el SDK se modifica en el fork `rasty94/rexglue-sdk` con una rama de integración `macos` sobre
  `a728090` (himdo main), un commit por arreglo; el proyecto apuntará a esa rama en lugar de al parche.
- **Reparto del MSAA (medido, mismo build):** base 111,7 ms/frame; 4x→2x (`max2`) 110,5 (≈1 ms, ruido ±3–4);
  todo a 1x 60,9. **Los ~50 ms son de los passes a 2x, y el salto es binario** (tener MSAA o no), no
  proporcional a las muestras. Trazas en el mismo plano (brasero, +28 s de la entrada al 3D):
  `out/mac-measure/mst_tr_base.trace` y `mst_tr_msaa1.trace`, capturas `tr_base_trace.png` / `tr_msaa1_trace.png`.
  Guiones: `tools/mac/prof3d_hold.sh` (mantiene el juego hasta `HOLD` s y lanza la traza a entrada+`TRACE_AT`)
  y `tools/mac/phase.sh` (FPS por ventanas alineadas a la entrada en el 3D). El interruptor `FABLE2_EXP_MSAA_MAX2`
  está en `out/mac-measure/exp_switches.patch`. Siguiente: comparar las dos trazas (3 passes grandes) y la
  GPU Frame Capture en Xcode; candidatos: HSR/early-Z con MSAA, sample count 2 en Metal, resolves de EDRAM.
- PR #2 de saulob/geometry-wars-retro-evolved-2-recompiled revisado: ideas en TODO.md T8 (hashes del SDK/XEX,
  sello y exports del plugin, LINK_DEPENDS, auditoría del cierre `~MnkInputDriver → CallInUIThreadSynchronous`,
  selector de ISO con osascript, relanzado con `_NSGetArgv`+`posix_spawn`). Nada contradice nuestras decisiones.

## 2026-10-13 — rama `macos` del fork y pérdida de la imagen de EXT

- T2b: rama `macos` en `rasty94/rexglue-sdk` (a728090 + 8 commits, `0a3b1fa`) y `fix-exp-adjust` (`228a153`),
  subidas. Submódulo apuntado al fork; parche `rexglue-sdk-fork-fixes.patch` eliminado.
- El disco interno se llenó durante el export de trazas. Al mover `out/` a la imagen APFS sobre exFAT,
  el sparsebundle perdió `Info.plist`/`token` y se volvió ilegible: se perdieron el checkout del SDK, el
  build del runtime y del juego, y todas las trazas y capturas. Los commits del SDK estaban subidos.
- Rescatado: partida en `~/fable2-saves-backup`, interruptores de experimento en `tools/mac/exp_switches.patch`.
- Siguiente: partición APFS en EXT (la hace el usuario), clonar el fork en `macos`, compilar runtime y juego
  allí, repetir la medida base y las trazas con/sin MSAA.

## 2026-10-13 (tarde) — T3, PR himdo#12, overlay y vídeos

- EXT es ahora APFS (1 TB): área de trabajo `/Volumes/EXT/fable2/`, `out/` del repo enlazado allí.
- Base desde la rama `macos` del fork: 9,7 FPS en 3D, igual que con el parche.
- T3 (teselación continua como rejilla instanciada en el VS): 13,7 FPS. Primera versión con rombos negros en
  la nieve por la diagonal de las celdas; corregido reflejando las diagonales por cuadrante (como Metal).
- PR himdo#12 (mitchell) integrado: 14,8 FPS. Overlay de FPS abierto: sin coste.
- `skip_intro_videos` y el overlay de FPS (cvar nueva `show_debug_overlay` en el fork) activados por defecto.
- Caché de Instruments: `xctrace export` llenó 72 GB del disco interno; borrar `path_manager` tras cada análisis.
- Pendiente: trazas con/sin MSAA (los ~50 ms siguen siendo el mayor coste), prueba visual de `exp_adjust`.

- Noche 2026-10-13: MSAA hoy = 28 ms (1x: 25,5 FPS). Descartados: sin SampleRateShading, commit de bigmah,
  prefill y MTLHeap de MoltenVK. MoltenVK deprecado; KosmicKrisp (Mesa) es la alternativa recomendada en Apple Silicon.
- KosmicKrisp (Homebrew mesa 26.2.4): 17,2 FPS en 3D (vs 14,8 MoltenVK) pero parpadeos y device lost en la
  espera de readback de memexport. Pendiente de aislar.
- 2026-10-16: KosmicKrisp aparcado (parpadeo y device lost no ligados a memexport). Foco: MoltenVK.
