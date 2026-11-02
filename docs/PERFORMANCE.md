# Rendimiento e investigación

## Implementación publicada

JIT multihilo, aislamiento COOP/COEP mediante service worker, caché de archivos,
descargas paralelas, lanzamiento directo del EXE, entorno persistente,
resolución inicial 800×600 y vsync desactivado en el emulador. La resolución
efectiva la puede elegir el juego. La UI muestra únicamente el juego y no las
ventanas transitorias de arranque. Ocultarlas no evita el arranque interno Wine.

## Mediciones locales reales

Chromium headless, SwiftShader (renderizado por software), mismo equipo.
Los cuatro EXE mostraron gráficos y respondieron a teclado/ratón en multihilo.
Arranque hasta los primeros gráficos del juego: aproximadamente 47–51 segundos.
Vascolet 1 en JIT de un hilo: aproximadamente 37 segundos.
Las escenas estáticas pueden presentar cero cuadros nuevos sin estar bloqueadas.
El diagnóstico multihilo cuenta presentaciones reales; no confundirlo con el
refresco del monitor, ni con una garantía de FPS en todos los niveles.

En el menú animado de Vascolet 1 se observaron 21–28 presentaciones/segundo
con SwiftShader. Esto no es una comparación A/B de una partida completa ni
permite asegurar cuánto mejorará el equipo de otro usuario. No se certificó
fluidez de todos los niveles. Las capturas muestran el alcance de cada prueba.

Se verificaron escritura, sincronización, exportación y lectura de un archivo
de prueba después de recargar, en el mismo sistema de archivos que usa el juego.
No es una certificación de todos los checkpoints o logros originales.

## Métodos evaluados

- **Boxedwine multihilo JIT:** ejecuta estos cuatro juegos y conserva la vía
  Direct3D 9 → WineD3D → WebGL. Es la alternativa seleccionada para probar.
- **Caché AOT de bloques JIT:** se grabó Vascolet 1 y se optimizaron 21.968
  módulos con Binaryen; el ZIP bajó de 43,8 a 22,9 MB. La reproducción no mejoró
  de forma clara el arranque y aparecieron errores de registro Wine después
  de recargar. Se descartó y NO forma parte de los sitios publicados.
- **Cambios de servicios Wine:** no dieron una mejora convincente; se descartaron.
- **v86:** emula el equipo completo y no ofrece una GPU Direct3D 9. No es una
  mejora apropiada para estos EXE, aunque sus gráficos sean 2D.
- **RetroTick:** su lista de APIs documenta DirectDraw/OpenGL, no soporte listo
  para los requisitos Direct3D 9 de estos juegos.
- **Theseus:** traducción estática prometedora, pero experimental y sin una
  implementación Direct3D 9 lista en el árbol revisado. Requiere trabajo de
  compatibilidad específico; no es un reemplazo directo listo para publicar.

## Fuentes

- [Instrucciones y límites de Boxedwine web](https://github.com/danoon2/Boxedwine/blob/26bf258ab0dc00aa1ec6da81d1ca263f922f8947/project/emscripten/buildInstructions.md)
- [Compilación 121 y demos](https://boxedwine.org/builds/#master)
- [Hardware emulado por v86](https://github.com/copy/v86)
- [RetroTick y APIs implementadas](https://github.com/lqs/retrotick)
- [Theseus](https://github.com/evmar/theseus)
- [Traducción estática: explicación del autor](https://neugierig.org/software/blog/2026/04/theseus.html)

La opción automática usa multihilo si está disponible. En algunos equipos
el modo de un hilo puede rendir mejor; se conserva el selector para comparar.
