# Vascolet 2 — EXE original en el navegador

[Abrir prueba](https://frajusu.github.io/vascolet-exe-2/)

Experimento de ejecución del **vascolet2.exe original**, con sus recursos, mediante
Boxedwine + Wine 11 en WebAssembly. No es una reconstrucción JavaScript del juego.

## Estado

Compatibilidad en evaluación. La publicación de la página y el arranque de Wine
no certifican que el juego llegue al menú ni que sea jugable. Ver `TESTING.md`.
Los cuatro ejecutables importan Direct3D 9 y DirectInput 8.

Se incluyen dos modos: single-thread JIT y single-thread intérprete, que no
requieren cabeceras COOP/COEP. El botón de escritorio permite diagnosticar Wine.
Se usa almacenamiento temporal para que cada prueba empiece limpia.

## Desarrollo

```sh
python build.py
python -m http.server 8000 --directory dist
```

Abrir http://localhost:8000. Requiere Python 3.11+ e Internet para la primera
compilación. Chrome/Edge 137+ o Firefox actualizado; JIT no está soportado por
esta compilación de Boxedwine en Safari. La primera carga pesa unos
126 MB.

GitHub Actions genera `dist/` y publica Pages. Los binarios de Wine y Boxedwine
se descargan durante el build, se verifican por SHA-256 y se sirven desde Pages.
Las URLs y hashes están en `runtime.lock.json`. La URL upstream de Wine contiene
`latest`: si cambia, el build fallará por checksum y habrá que revisar y actualizar
el pin explícitamente. No se acepta silenciosamente otro binario.

## Archivos originales

`site/game.zip` conserva los nombres y contenido de los archivos del juego.
`game-manifest.json` registra su SHA-256 y los archivos excluidos: desinstaladores,
copias de respaldo y, en Vascolet 3, una carpeta `libs` con un instalador VPN y
sus archivos, ajenos al ejecutable del juego.

## Motor y licencias

Boxedwine build 121, fuente `26bf258ab0dc00aa1ec6da81d1ca263f922f8947`:
https://github.com/danoon2/Boxedwine/tree/26bf258ab0dc00aa1ec6da81d1ca263f922f8947

El soporte WebGL/Direct3D usado aquí es experimental y posterior a la versión
estable 26R1. No usar el runtime estable como reemplazo: no soporta Direct3D web.
Boxedwine: GPL-2.0-or-later, ver `vendor/Boxedwine-COPYING.txt` y encabezados.
Wine y Tiny Core conservan sus propias licencias y avisos dentro de la imagen.
No se incluye una imagen de Microsoft Windows.

Vascolet y sus recursos pertenecen a sus autores originales. La licencia del
emulador no cambia la licencia de los juegos.

## Dependencia de audio añadida

La carpeta original no contenía `OpenAL32.dll`, requerida por el EXE.
Se añadió la copia OpenAL Soft incluida en Vascolet 4, registrada por SHA-256
en `additionalDependencies` del manifiesto. El EXE y los recursos no se alteraron.
