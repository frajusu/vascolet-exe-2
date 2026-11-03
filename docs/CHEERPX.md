# Vascolet 2: XO original y rasterización en el navegador

CheerpX 1.2.8 ejecuta el ELF x86 original. Canvas dibuja los sprites del renderer
YUV y Web Audio reproduce los sonidos. Los archivos originales conservan sus
hashes; `native/cheerpx/sources.json` identifica el bundle.

El adaptador corrige el tamaño de los eventos SDL (20 bytes), la invalidación
de texturas y la transparencia. Los hooks del renderer, las conversiones de
color y los atajos de transformaciones están documentados con direcciones y
hashes en `native/cheerpx/runtime-metadata.json`. Las conversiones de color se
validan contra las originales antes de activar su versión entera. Las
transformaciones arbitrarias conservan su implementación original.

## Medición local, noviembre de 2026

Chromium, AMD Radeon RX 6500 XT, con audio y movimiento/salto dentro del
Bosque Encantado: **38–44 FPS** en las rutas medidas. La última prueba de
15,73 segundos dio 38,01 FPS, mediana de 20,58 ms y p95 de 66,95 ms.
La primera imagen aparece en unos 4,7 s; la carga inicial avanza hasta el menú
en unos 8,9 s. No son mediciones de todos los niveles ni de un teléfono.

**El objetivo de 60 FPS estables sigue pendiente.** El perfil muestra que el
tiempo restante se concentra en el código emulado, después de mover la
rasterización al navegador. La interfaz y las reglas del juego se conservan.

Audio comprobado sin errores. Se verificaron importación, reinicio y exportación
de un archivo de prueba; esa prueba valida la infraestructura de persistencia,
no una partida completa. El progreso sólo existe después de que el propio
juego lo escriba.

Para recompilar los adaptadores: configurar `ZIG` con Zig 0.13.0 y ejecutar
`python native/cheerpx/rebuild.py`. `python build.py` prepara el sitio con los
binarios ya incluidos. El runtime CheerpX se carga desde su CDN oficial.

## Investigación siguiente y mejoras de memoria

El puente compartido usa carga de audio bajo demanda, reutilización de buffers
por URL, expulsión de buffers inactivos por presupuesto y liberación de canales
si falla la descarga. El transporte gráfico reutiliza memoria en vez de copiar
todo el paquete pendiente al recibir cada fragmento.

Una nueva ruta de 10,54 s con movimiento dio 46,21 FPS, mediana de 20,04 ms y
p95 de 30,67 ms; no es garantía de 60 FPS ni una medición en teléfono.

Se investiga recompilación estática ELF → C → WebAssembly, por separado del
motor publicado. Una prueba con dos funciones originales del XO3 pasó 131.072
casos en Chromium sin CheerpX/Wine. **No existe todavía un juego completo AOT**:
la cobertura de traducción no resuelve las bibliotecas, llamadas indirectas,
excepciones ni la compatibilidad de partidas por sí sola.
