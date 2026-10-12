# tools/mac: macOS measurement tools

Salida (capturas, logs, `sample`) en `$OUT`, por defecto `out/mac-measure/` (ignorado por git).

## Protocolo de medida
- Un solo juego a la vez: las medidas se falsean si hay dos instancias.
- FPS: se leen del log del juego (`out/build/mac-arm64-release/logs/fable_2_NNN.log`,
  una linea por segundo `guest vblank pacer: guest frame (gate) ~N Hz`). No usar F3: fuerza
  un repintado por frame.
- Validar siempre con la captura `*_3d.png`: debe verse la escena 3D (personaje, muralla con
  brasero, nieve), no el menu. Con cache de shaders fria los tiempos cambian: repetir.
- Permisos (Ajustes del Sistema > Privacidad y seguridad) para la terminal que lanza los
  scripts: Accesibilidad (`osascript` envia teclas) y Grabacion de pantalla (`screencapture`).

## Scripts
- `prof3d.sh <nombre> [args]`: lanza el juego, pulsa E/S/E para cargar la partida, captura,
  hace `sample` 10 s y imprime la cadencia (`fps: ...`). Linea base: ~7.7 fps.
- `scene.sh <nombre> "t:accion ..." [args]`: ejecuta un guion temporizado (e, d, s, f=F3,
  c=captura de la ventana).
- `winlist.js`: lista las ventanas de `fable` por JXA (`osascript -l JavaScript`); da el id para `screencapture -l`.
- `oncpu.py <sample.txt>`: tiempo propio por hilo a partir de un `sample`, sin esperas.
- `gdfx_extract.py <iso> <dir>`: extrae una ISO GDFX (port de `tools/x360extract/lib/Gdfx.cs`).
- `apply_diag.py [dir]` + `tempdiag_block.inc`: inserta instrumentacion temporal
  (`TEMPDIAG`) en `command_processor.cpp` del SDK (`REXGLUE_SDK_SOURCE`, por defecto
  `/Volumes/fable2build/rexglue-sdk`). No se commitea: es solo para medir.

## Analizar una traza de Metal
```
xcrun xctrace record --template 'Metal System Trace' --attach <pid> --time-limit 5s --output X.trace
xcrun xctrace export --input X.trace --xpath '<tabla>' > mst/<tabla>.xml   # tablas de metal-*, display-*, gpu-*
```
Los scripts de `trace/` leen los XML exportados (rutas `mst/...` relativas al directorio
actual; `a4.py`/`a5.py` reciben el directorio como argumento):
- `xt.py <xml> [n]`: carga una tabla (resuelve `id`/`ref`), muestra columnas y n filas.
- `a1.py`: intervalos GPU por proceso/canal; `a2.py`: submissions, encoders, surfaces/s, vsync;
  `a3.py`: shaders y encoders por duracion; `a4.py <dir>`: asignaciones de recursos;
  `a5.py <dir>`: encoders por command buffer.
