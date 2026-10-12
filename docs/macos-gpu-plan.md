# Plan GPU macOS: T3 (teselación continua en el VS) y T2 (memexport sin drenar)

## Resumen

1. **T3, hacerlo ya.** Los 485 draws kQuadPatch continuos (factor 16) pasan a VS normal + lista de triángulos
   instanciada (una instancia por parche, rejilla 17×17 de un index buffer builtin). Los 95 adaptativos siguen con teselación real.
2. Ahorro esperado de T3: compute de VS+TCS (13,5 ms) y reinicios (8,8 ms) × 485/580 ≈ **−18 ms**, menos 0–6 ms de
   VS extra por peor reutilización de vértices: **−12 a −18 ms/frame (104 → 86–92 ms, ~11–11,6 FPS)**.
3. Coste: ~120 líneas en 8 ficheros del SDK, sin cambios en `pipeline_cache.cpp` salvo la versión de la caché.
4. **T2, la variante correcta ya existe:** `pr-wt` rama `t2-memexport-no-drain` (6862bd7) = 4c21ec4 + `no_drain`.
   El build de macOS (`b097123` + `rexglue-sdk-fork-fixes.patch`) **no** trae 4c21ec4: lo medido fue la (a) pura.
5. Recomendación T2: llevar 4c21ec4 al parche de macOS (o fijar el submódulo en `a728090`), `no_drain` on, y
   medir con contadores listo/esperado/saltado. Esperado: **0 a −9 ms/frame** (cota: la (a) pura dio −9 ms).
6. La (b) (copia diferida en el reclaim) entrega todos los datos pero con edad variable (justo lo que causó #68):
   descartada salvo que la (a') dé glitches.
7. Ni T2 ni T3 llegan a 30 FPS: el MSAA es el 45% del frame (−48 ms con 1x). Va después, como T4/T5.

Líneas citadas: checkout `/Volumes/fable2build/rexglue-sdk` (b097123 + parche del fork + bloques TEMPDIAG de la
sesión, que desplazan unas 10 líneas en `vulkan/command_processor.cpp`). `shader.h` está en
`include/rex/graphics/pipeline/shader/shader.h`, no en `include/rex/graphics/shader.h`.

## Parte A — T3: teselación continua emulada en el vertex shader

### A.0 Cómo funciona hoy (lo que hay que reproducir)
- `primitive_processor.cpp:349-353`: kQuadPatch → `kQuadDomainPatchIndexed`, 1 punto de control por parche.
- `vulkan/pipeline_cache.cpp:133-147` (VS GLSL "indexed"): valor = `gl_VertexIndex` → swap de endian →
  `+ xe_vertex_base_index` → `& 0xFFFFFF` → clamp `[vertex_index_min, vertex_index_max]` → float.
  `:236-266` (TCS continuo): `patch_control_point_indices.x = valor`; todos los niveles = `xe_tessellation_factor_range_max`.
- `vulkan/command_processor.cpp:6154-6155`: `range_max = VGT_HOS_MAX_TESS_LEVEL + 1` (registro **float**): 15 → 16.
- `spirv_translator.cpp:1209-1213`: TES con `SpacingFractionalEven` y `VertexOrderCw`. Con factor par entero
  el espaciado fraccional par es el uniforme: vértices en u = i/16 exactos.
- `spirv_translator.cpp:1913-1920`: QuadDomainPatchIndexed → r0.x = índice del parche, r0.yz = gl_TessCoord.xy, r1.x = 0.
- No hay `VkPipelineTessellationDomainOriginStateCreateInfo`: origen del dominio UPPER_LEFT por defecto.

### A.1 Tipo nuevo y `Modification`
- `shader.h:640-663`: añadir **al final** del enum (tras `kRectangleListAsTriangleStrip`, valor 11; caben 16 en
  los 4 bits de `kHostVertexShaderTypeBitCount`, :666) `kQuadDomainPatchIndexedAsTriangleList`. Queda fuera de
  `[kDomainStart, kDomainEnd)`: `IsSpirvVertexShader()` es true, `IsSpirvTessEvalShader()` false
  (`spirv_translator.h:520-527`), y `ArePipelineRequirementsMet` (`pipeline_cache.cpp:1576-1582`) pide etapa VERTEX.
- `spirv_translator.h:70-72` (palabra 1 de `vertex`, usa 6 bits): añadir `uint32_t emulated_tessellation_factor : 7;`
  (N, 2..64). N va en la modificación y no en una constante de sistema: el SPIR-V lo trata como constante
  (división y módulo por N+1 constantes), y Fable 2 solo usa N=16 (una traducción más por shader de terreno).
  Así **no hace falta exponer** `tessellation_factor_range_max` al SPIR-V (hoy solo lo ven los GLSL auxiliares;
  no está en la lista de `spirv_translator.cpp:260-300` ni en el enum `kSystemConstant*`).
- `spirv_translator.h:36`: `Modification::kVersion` 12 → 13. La caché de pipelines usa
  `max(PipelineDescription::kVersion, Modification::kVersion)` (`pipeline_cache.cpp:439-440`): se invalida la
  `.xpso` (una sesión con caché fría). Los `.xsh` (`ShaderStoredHeader::kVersion`) guardan ucode, no hace falta tocarlos.
- `pipeline_cache.cpp:925-929` (`GetCurrentVertexShaderModification`): para el tipo nuevo,
  `modification.vertex.emulated_tessellation_factor = uint32_t(regs.Get<float>(XE_GPU_REG_VGT_HOS_MAX_TESS_LEVEL) + 1.0f);`
  (el primitive processor ya validó que es par y entero; mismo registro, mismo draw).

### A.2 Selección e índices (`primitive_processor.cpp`)
- Cvar en `src/graphics/vulkan/primitive_processor.cpp` (junto a las `vulkan_force_*`, :24), con el patrón de
  `kSharedMemoryUploadFirstDefault` (`vulkan/shared_memory.cpp:33-38`): `vulkan_emulate_continuous_tessellation`,
  true con `REX_PLATFORM_MAC`, false en el resto, `kRequiresRestart` (decide el index buffer builtin).
  Se pasa como parámetro nuevo de `InitializeCommon` (`primitive_processor.cpp:123`, llamada en
  `vulkan/primitive_processor.cpp:65`; D3D12 pasa false) y queda en `emulate_continuous_quad_patch_tessellation_`.
- **Cuándo** (dentro de `case kQuadPatch`, :349-353): cvar on **y** `tessellation_mode == kContinuous` **y**
  `f = VGT_HOS_MAX_TESS_LEVEL + 1` cumple `f == float(N)`, N par, 2 ≤ N ≤ 64. Entonces
  `host_vertex_shader_type = kQuadDomainPatchIndexedAsTriangleList` y `host_primitive_type = kTriangleList`.
  Si no (adaptativo, factor impar o fraccional, o el TEMPDIAG `FABLE2_EXP_TESS1`, que solo toca la constante): ruta actual.
- **Index buffer:** no se genera por draw. El builtin de `InitializeCommon` (:150-235) añade una rejilla por
  N par (2..64), 16 bits, vértice `(i, j)` → `j·(N+1)+i` (máx. 65² = 4225), celda `(i, j)` →
  `(i,j) (i+1,j) (i,j+1)` y `(i+1,j) (i+1,j+1) (i,j+1)`, en orden de filas para la caché de vértices.
  Total de los 32 tamaños: 274 560 índices = 537 KB, una vez. Offsets en `builtin_ib_offset_tess_grid_[33]`
  (`primitive_processor.h:638-640`).
- **Draw instanciado:** `index_count = 6·N²` (1536 para N=16), `instance_count = P` = `guest_draw_vertex_count`
  (un "vértice" del guest por parche). Hoy `ProcessingResult` (`primitive_processor.h:123-152`) no tiene
  instancias: añadir `uint32_t host_draw_instance_count` (1 por defecto, asignado en `:928`).
  `gl_VertexIndex` = vértice local de la rejilla, `gl_InstanceIndex` = ordinal del parche.
- **Índice del guest:** se reutiliza **tal cual** la rama de puntos/rectángulos (:431-489): `kHostBuiltinForAuto`
  si es auto-indexado (ordinal = índice) o `kHostBuiltinForDMA` (el VS lee el índice del parche de la shared
  memory con `kSysFlag_ComputeOrPrimitiveVertexIndexLoad`, `vulkan/command_processor.cpp:5970-5978`, y la
  `RequestRange` de :910-920). Solo cambia el bloque de `host_draw_vertex_count/handle/format`:
  `host_index_format = kInt16`, `host_primitive_reset_enabled = false`.
- Alternativa descartada (la de la fórmula `parche = v / (N+1)²`): index buffer convertido por draw con
  `RequestHostConvertedIndexBufferForCurrentFrame` y `P·6N²` índices de 32 bits = 6 KB por parche y draw
  escritos por la CPU (con P=256, 1,5 MB por draw). Con instancias el coste de CPU es cero.

### A.3 Traductor (`spirv_translator.cpp`)
- `StartVertexOrTessEvalShaderBeforeMain` (:1666-1670): para el tipo nuevo, crear además `gl_InstanceIndex`
  (`BuiltInInstanceIndex`, int) en un miembro nuevo `input_instance_index_` (`spirv_translator.h`), y añadirlo a `main_interface_`.
- `StartVertexOrTessEvalShaderInMain`, rama de :1984-1987 (hoy puntos/rectángulos): incluir el tipo nuevo. Dentro:
  ```
  local = vertex_index                    // gl_VertexIndex, 0..(N+1)²-1
  i = local % (N+1);  j = local / (N+1)   // N+1 constante
  u = float(i) * (1/N);  v = float(j) * (1/N)   // exacto para N potencia de 2
  vertex_index = bitcast<uint>(gl_InstanceIndex) // ordinal del parche
  ```
  Lo siguiente (:2016-2083, carga opcional desde la DMA con endian) ya sirve sin tocar: convierte el ordinal
  en el índice del guest. Después `remap_vertex_index_to_float` (:2259-2268: base, máscara 24 bits, clamp),
  que equivale al VS GLSL de la teselación.
- Final (:2431-2433, `store_register_component(0, 0, vertex_index_float)` del VS): para el tipo nuevo, además
  `r0.y = u`, `r0.z = v` y, si `register_count() >= 2`, `r1.x = 0`, como en el TES (:1913-1919).
- La emulación de dominio de :2323-2430 es de la ruta compute (`kMemExportCompute`), que el backend Vulkan
  **no usa** (no aparece en `src/graphics/vulkan`): sirve de modelo, no se reutiliza.
- Memexport: sin restricción. El TES actual tampoco limita `main_memexport_allowed_` por punto del dominio,
  así que se mantiene la paridad (y en Vulkan exige `vertexPipelineStoresAndAtomics` en ambos casos, :3649-3656).

### A.4 Pipeline y backend
- `pipeline_cache.cpp:1404-1462`: el tipo nuevo no es dominio → `IsTessellated()` false → rama `else`;
  no es punto/rectángulo (:1420) → `switch (host_primitive_type)` → `kTriangleList`. Sin TCS/TES
  (:2920 `tessellated` false), sin `patchControlPoints` (:3426), `primitive_restart` false. **Nada que cambiar**
  en `:187-266` ni en `:3210`. La clave del pipeline cambia sola: `vertex_shader_modification` lleva el tipo y N.
- `vulkan/command_processor.cpp:3748-3751`: aceptar el tipo nuevo (si no, "Unsupported Vulkan host vertex shader type").
- `:4148` y `:4179`: `CmdVkDraw/CmdVkDrawIndexed(count, result.host_draw_instance_count, ...)` en vez de `1`.
- `:5986-5990` (reset de índices del guest): nada, `host_primitive_reset_enabled` es false.
- `:6154`: nada (N va en la modificación).

### A.5 Riesgos y verificación
- **Winding/culling:** el TES declara `VertexOrderCw` con origen UPPER_LEFT (v hacia abajo); los triángulos de
  A.2 son horarios en ese sistema. Si el terreno desaparece con `PA_SU_SC_MODE_CNTL` culling activo, el orden es
  el contrario: intercambiar los dos últimos índices de cada triángulo en el builtin (sin otra lógica).
- **Diagonal de cada celda:** el teselador de Metal puede elegir otra diagonal (el de D3D11 las refleja por
  cuadrante). Con relieve desplazado cambia algo el sombreado interior, no los bordes: los vértices de borde
  (u o v ∈ {0,1}) y sus segmentos son los mismos. Solo replicar el patrón si el A/B lo muestra.
- **Grietas con parches adaptativos vecinos:** los bordes coinciden si el juego pone factor 16 en la arista
  compartida (lo exige el hardware para ser estanco). Las posiciones u = i/16 son exactas en ambos lados; lo
  que puede diferir es la evaluación del VS del guest: el TES (función post-teselación de Metal) y el VS se
  compilan por separado y pueden contraer FMAs distinto → píxeles sueltos en la costura. Revisar con zoom.
- `primitive_restart`: no aplica (lista, false); no reaparece el aviso de MoltenVK.
- **Coste de vértices:** Metal evalúa una vez cada punto del dominio (289 por parche); el VS indexado depende
  de la caché post-transformación (orden por filas). Peor caso, hasta +6 ms del VS teselado. Medir.
- Memexport en el VS del terreno: improbable; si existe, sigue igual que en el TES (ver A.3).
- **Verificar:** `tools/mac/prof3d.sh` A/B con `--vulkan_emulate_continuous_tessellation=false/true`
  (misma escena de Rose, cámara fija): capturas `*_3d.png` y diferencia de píxeles de la mitad inferior,
  `compare -metric AE -fuzz 3% a.png b.png diff.png` (ImageMagick), más inspección del diff. `TRACE=1` para la
  Metal System Trace: reinicios por frame < 120 (solo adaptativos), encoders compute de teselación −84%.
  Contador temporal (como `tools/mac/apply_diag.py`) de draws emulados/reales por frame: 485/95.

### A.6 Líneas y orden
| Fichero | Líneas |
|---|---|
| `include/rex/graphics/pipeline/shader/shader.h` | +2 |
| `include/rex/graphics/pipeline/shader/spirv_translator.h` | +3 (campo, `input_instance_index_`, kVersion) |
| `src/graphics/pipeline/shader/spirv_translator.cpp` | +40 |
| `include/rex/graphics/primitive_processor.h` | +4 |
| `src/graphics/primitive_processor.cpp` | +45 (selección, rejillas builtin, rama de índices) |
| `src/graphics/vulkan/primitive_processor.cpp` (+ D3D12, 1 línea) | +12 |
| `src/graphics/vulkan/pipeline_cache.cpp` | +3 |
| `src/graphics/vulkan/command_processor.cpp` | +4 |
| **Total** | **~115** |

1. **Traductor + modificación** (shader.h, spirv_translator.*, pipeline_cache.cpp:925). Inalcanzable aún.
   Verifica: compila, carga la caché nueva sin errores, partida idéntica (el tipo no se selecciona).
2. **Primitive processor + draw instanciado**, cvar **false** por defecto. Verifica: con la cvar a true, A/B
   de capturas y diff de píxeles, culling correcto, contador 485/95.
3. **Cvar true en macOS**, traza de Metal y FPS (`prof3d.sh`, tres capturas). Aceptación de T3 (TODO.md):
   reinicios < 120/frame, terreno igual, ms/frame medidos en el commit. PR al fork.

## Parte B — T2 correcto: lectura de memexport sin drenar

Estado: la vía rápida (`IssueDraw_MemexportReadbackFastPath`, `vulkan/command_processor.cpp:4290-4410`)
copia cada draw a un slot doble por clave `(dirección, tamaño)` y lee el slot anterior. Si no está completado,
`FullPath` (:4213-4288) graba otra copia y llama a `AwaitAllQueueOperationsCompletion` (:4251): cierra la
submission abierta y espera toda la cola. Medido: ~97% de lecturas por la vía rápida y ~1 drenado por frame,
pero ese drenado serializa CPU y GPU. La (a) pura (saltar) dio +0,9 FPS, pero sin el drenado la GPU va un frame
por detrás y solo el 1–10% de las lecturas encuentran el slot listo: el guest casi no recibe datos.

### B.1 Opción (a'): esperar la fence del slot si ya está enviado; saltar solo si es de la submission abierta
Ya está escrita: `pr-wt` (`t2-memexport-no-drain`, 6862bd7, `command_processor.cpp:4370-4397`) = 4c21ec4 + no_drain.
```
 CheckSubmissionFenceAndDeviceLoss(0);
-bool previous_slot_ready = written && submission_written[read] <= submission_completed_;
+bool written = ... && submission_written[read] != 0;
+if (written && readback_memexport_fast_wait &&
+    submission_written[read] > submission_completed_ && submission_written[read] < GetCurrentSubmission())
+  CheckSubmissionFenceAndDeviceLoss(submission_written[read]);   // solo esa fence, sin cerrar la abierta
+bool previous_slot_ready = written && submission_written[read] <= submission_completed_;
 if (!previous_slot_ready) {                     // aquí solo llega el slot de la submission abierta
-  IssueDraw_MemexportReadbackFullPath(total_size);
+  if (!readback_memexport_no_drain) IssueDraw_MemexportReadbackFullPath(total_size);
   readback.current_index = read_index; return true; }
```
Más la cvar `readback_memexport_fast_wait` (`src/graphics/command_processor.cpp`, 8 líneas de 4c21ec4).
- **Qué ve el guest:** los datos de la copia anterior con la misma clave, siempre un draw de memexport atrás
  (determinista a cualquier FPS, que es lo que arregló #68). Solo cuando las dos últimas copias de esa clave caen
  en la submission abierta se salta la lectura (el guest conserva lo que tenía): ~1 vez por frame según la medida.
- **Coste:** ~20 líneas, ya escritas; falta llevarlas al parche de macOS (cherry-pick de 4c21ec4 sobre b097123 o
  subir el submódulo a `a728090`) y medir.
- **Riesgo:** bajo-medio. La espera es a la última submission enviada: si la primera lectura del frame llega
  pronto, la GPU queda ociosa desde que termina S hasta que se envía S+1 (burbuja = CPU de grabación restante).
  Mando si la traza lo muestra: 3 slots por clave (`ReadbackBuffer`, `vulkan/command_processor.h:451-460`,
  arrays de 2 → 3, rotación `(i+1)%3`, ~10 líneas): se espera a S−1, sin burbuja, y los datos tienen dos draws.
  Contador temporal obligatorio: lecturas listas / esperadas / saltadas por frame.

### B.2 Opción (b): copia diferida en el reclaim de fences, con búfer por submission
```
+struct PendingMemexport { uint64_t submission; VkBuffer buf; VkDeviceMemory mem; void* map;
+                          std::vector<std::pair<uint32_t,uint32_t>> ranges; };   // (dirección, tamaño)
+std::deque<PendingMemexport> pending_memexport_;   // en orden de submission, pool de búferes libres
 IssueDraw:  en vez de FastPath -> CmdVkCopyBuffer(shared, pending_actual.buf, offset) + barrera host,
             ranges.push_back(...)   // trozos de 256 KB: un búfer no puede crecer con copias grabadas
 CheckSubmissionFenceAndDeviceLoss (:4980), donde avanza submission_completed_:
+  while (!pending.empty() && pending.front().submission <= submission_completed_) {
+    vkInvalidateMappedMemoryRanges; memcpy de cada rango a memory_->TranslatePhysical(dir); al pool; pop; }
 Shutdown / pérdida de dispositivo (:1941-1955): liberar el pool.
```
- **Qué ve el guest:** los datos de **todos** los draws, sin esperas, pero cuando la CPU observa la fence:
  1–2 frames tarde y con una edad que varía con el retraso de la GPU, que es el comportamiento que 4c21ec4
  quitó (perro y montañas negras de #68). Además, el memcpy tardío puede pisar escrituras de la CPU del guest
  en ese rango hechas después del draw (la vía rápida tiene el mismo problema, con una ventana menor).
- **Coste:** ~130 líneas nuevas (estado, pool, reclaim, limpieza), sin reutilizar `memexport_readback_buffers_`.
- **Riesgo:** medio-alto (temporización no determinista y ciclo de vida de memoria nuevo).

### B.3 Recomendación
**(a')**: ya está escrita y probada en la rama del PR, cuesta ~20 líneas y mantiene la semántica que el upstream
eligió para #68. Pasos: (1) añadir 4c21ec4 al parche de macOS con `no_drain` true; (2) A/B con
`--readback_memexport_no_drain=false` y con `--readback_memexport_fast_wait=false` en `prof3d.sh`, con
contadores listo/esperado/saltado y `vkWaitForFences` en el `sample` del hilo "GPU Commands"; (3) comprobar
perro/héroe y montañas (#68). Si la espera crea burbuja: 3 slots. Solo si (a') da glitches: (b).
Esperado: entre 0 y −9 ms/frame (la (a) pura, sin ninguna espera, es la cota: 9,7 → 10,6 FPS).
