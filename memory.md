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
- Sin teclado ni ratón como mando en Mac (`GetAsyncKeyState` solo existe en Windows): pendiente de SDL3. El
  launcher WPF no existe en Mac (editar `fable2_config.toml` / `fable_2.toml` a mano).

## Otros trabajos de esta sesión

- Graphify 0.9.85 instalado (uv), grafo en `graphify-out/`, hooks de git (`post-commit`, `post-checkout`),
  sección en `CLAUDE.md`. Faltan por extraer semánticamente `FUNCTION_NAMES.md` y `docs/`: el clasificador
  bloqueó esos subagentes; `/graphify . --update` los recoge.
- `tests/test_config_parity.py`: comprueba que las claves de `fable2_config.toml` coinciden en C++
  (plantilla + `Read<>`), el TOML que se copia junto al exe y C# (`Fable2ConfigFile`).
