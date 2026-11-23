# Cómo contribuir a RAAADIO

Gracias por querer mejorar RAAADIO. Las contribuciones más valiosas vienen de quienes
operan emisoras y conocen los problemas reales de una cabina: si algo falla al aire,
cuéntalo aunque no sepas programar.

## Reportar un problema

Abre un *issue* con la plantilla **Reporte de error** e incluye:

- la versión de RAAADIO y de Windows;
- la interfaz de audio que usas (y si tienes salida Cue aparte);
- los pasos para reproducir el fallo, qué esperabas y qué pasó;
- si puedes, las últimas líneas de `Logs/raaadio.log` dentro de la carpeta de la emisora.

Si el problema es de **seguridad** (por ejemplo, algo que exponga claves o la máquina de
aire), no abras un *issue* público: sigue [SECURITY.md](SECURITY.md).

## Preparar el entorno

**Windows (referencia).** Visual Studio 2022 o sus Build Tools (carga de trabajo
"Desarrollo de escritorio con C++"), CMake ≥ 3.15 y Git.

```bash
git clone https://github.com/juanitomanoplateada/raaadio.git
cd raaadio
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

La primera configuración descarga JUCE 8.0.3, SQLite y libebur128 con `FetchContent`
(tarda varios minutos). Linux y macOS: ver el README.

## Pruebas

Antes de abrir un *pull request*, compila en Release y corre las pruebas:

```bash
ctest --test-dir build -C Release --output-on-failure
```

Las pruebas usan laboratorios temporales: nunca tocan tu emisora ni tu configuración.
Tardan unos 15 minutos; es normal, porque muchas esperan temporizadores reales (los
fundidos, la protección contra silencio, el dibujo de cada pantalla de la interfaz) y
detener un deck sin tarjeta de sonido espera un momento dentro de JUCE.

Si tu cambio corrige un error, agrega una prueba que falle sin la corrección. Si toca la
lógica de audio, de la cola o de la pauta, pruébala sin interfaz (mira cómo lo hacen
`OnAir`, `QueueTiming` o `PlaylistGenerator::selectCandidates`).

## Cambios visuales

El ejecutable puede renderizar la interfaz a un PNG con datos de demostración, sin abrir
dispositivos de audio y en un laboratorio temporal:

```bash
build\RAAADIO_artefacts\Release\RAAADIO.exe --ui-snapshot captura.png --size 1366x768 --theme dark --lang es --view main
```

`--view` acepta `main`, `empty`, `history`, `ads`, `cartwall`, `settings`,
`settings-library`, `settings-broadcast`, `settings-broadcast-2` (la parte de abajo de Emisión),
`settings-language`, `settings-about`, `generator`, `track` e `import`. Revisa tus cambios en **tema claro y oscuro**, en **español e inglés** y a
**1366 × 768** (la pantalla más común en cabinas). Las imágenes del README están en
`docs/screenshots/`; si cambias algo que se ve ahí, vuelve a generarlas.

## Estilo del código

- C++17 y las convenciones de JUCE; 4 espacios, sin tabuladores (ver `.editorconfig`).
- Nombres en inglés; **comentarios en español**, cortos, que expliquen el porqué.
- El hilo de audio nunca espera: nada de disco, red, bloqueos largos ni memoria nueva
  dentro de `getNextAudioBlock`.
- Toda la SQL con sentencias preparadas (`Database::run`).
- Las categorías del sistema se buscan por su rol (`CategoryRole`), nunca por el nombre.

## Textos de la interfaz

Ninguna cadena visible va escrita en el código: todas pasan por `TRANS("clave")`. Cada
clave nueva va en **los dos** archivos, `Assets/lang_es.txt` y `Assets/lang_en.txt`, que
se compilan dentro del ejecutable. Usa la terminología de radio de la interfaz
(identificaciones, cuñas, separadores, tanda, pauta).

## Commits y pull requests

- Una rama por cambio: `git checkout -b arreglo/descripcion-corta` o `mejora/...`.
- Mensajes de commit en imperativo y en español: *"Corrige el título del stream al
  preescuchar"*.
- En el *pull request*, explica **qué problema resuelve** para la emisora, no solo qué
  archivos cambian, y cómo lo probaste.
- Anota los cambios visibles en [CHANGELOG.md](CHANGELOG.md), en la sección
  *Sin publicar*.
