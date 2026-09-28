# Registro de cambios

Todos los cambios importantes de RAAADIO se anotan aquí. El formato sigue
[Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el proyecto usa
[versionado semántico](https://semver.org/lang/es/).

## [Sin publicar]

Revisión completa de la aplicación sobre la versión 1.0.0: estabilidad al aire,
calidad de audio, pauta publicitaria, datos y organización del repositorio.

### Añadido

- **Análisis de audio al importar** (`TrackAnalyzer`): sonoridad integrada EBU R 128
  con libebur128, pico real con sobremuestreo 4×, normalización a −16 LUFS y puntos
  de entrada y salida guardados en la base de datos. Las pistas de versiones anteriores
  se analizan en segundo plano.
- **Etiquetas** ID3v1/v2, comentarios Vorbis e INFO de WAV (`TagReader`); detección de
  duplicados por contenido (SHA-256) y por artista, título y duración.
- **Protección contra silencio** (`DeadAirGuard`): salta una pista muda, reanuda la cola
  o la rellena con música; en manual solo avisa.
- **Pauta automática** (`AdScheduler`): inserta la tanda de cada franja 20 minutos antes,
  donde empieza más cerca de su hora, sin repetirla al reiniciar.
- **Botonera** de 12 cartuchos con F1–F12, directo al aire (`CartPlayer`).
- **Transmisión por internet** a Icecast y compatibles (Zeno.fm), sin BUTT: **MP3** con LAME
  (64–320 kbps, `libmp3lame.dll` aparte) u **Ogg Vorbis**, con reconexión (`StreamEncoder`).
  Datos de la emisora para directorios (género, descripción, sitio web, listado público).
- **Título para los oyentes**: `Artista - Título` de lo que está al aire, y el nombre de la
  emisora durante cuñas, identificaciones y separadores. En MP3 va por `/admin/metadata`,
  en otro hilo y con retardo opcional para que coincida con el audio.
- **Lo que suena en JSON** (`NowPlaying`, `NowPlayingPublisher`): anterior, actual y
  siguientes, solo música y con horas; en `/nowplaying.json` y enviado por POST a la página
  web de la emisora en cada cambio y cada minuto.
- **Registro de lo emitido**: la salida al aire en archivos de una hora, con retención.
- **Registro de emisión** (`play_log`) y **cola persistente**.
- **Categorías fijas de la emisora**: IDENTIFICACIONES, CUÑAS y SEPARADORES, reconocidas
  por su rol y no por el nombre; no se renombran, mueven ni eliminan.
- **Reproducir ahora seguro** (`OnAir`): la pista entra en el deck libre y lo que suena
  baja con un fundido de 2 s; si hay algo al aire, pide confirmación.
- **Respaldos verificados y programados** (`BackupService`): uno diario a la hora elegida
  mientras la emisora está al aire (y al abrir si esa hora pasó), en otro hilo; cada uno pasa
  `PRAGMA quick_check` y lleva un `manifest.json` con SHA-256, que se comprueba al restaurar.
- **Destino externo de respaldo** (otro disco, un USB, OneDrive o Google Drive): la copia de
  cada respaldo y de la música, que solo suma lo nuevo, verifica cada archivo con su SHA-256
  y nunca borra nada allá. Se reintenta cada 30 minutos si el destino no está.
- **Retención escalonada**: los 10 más recientes y el último de cada día (14), semana (8) y
  mes (12), aquí y en el destino externo.
- **Recuperación de una base dañada** al abrir: se aparta y se pone la del último respaldo
  verificado, con aviso.
- **Alertas en la barra superior**: respaldo atrasado o fallido, copia externa fallida y
  disco de la emisora casi lleno.
- **Candado de la emisora** (`raaadio.lock`): avisa si ya está abierta en otro equipo (USB o
  disco de red); un candado de un cierre inesperado se libera solo.
- **Mover la emisora** a otro disco desde Configuración, con avance, comprobación de
  espacio y cancelación.
- **Modo de captura de la interfaz** (`--ui-snapshot`) para revisar cambios visuales y
  generar las imágenes del README sin abrir dispositivos de audio.
- Pruebas automáticas (`RAAADIO_Tests`, `ctest`): más de 600 comprobaciones.
- Organización del repositorio: licencia, guía de contribución, política de seguridad,
  plantillas de GitHub, integración continua en Windows, `.gitattributes` y `.editorconfig`.

### Cambiado

- **Todo el estado de la emisora en la base de datos**: la pauta, la cola, la botonera, las
  tandas ya insertadas, los ajustes de emisión y las plantillas dejan `Config/*.json` y
  `Templates/*.json`, con transacciones y claves foráneas (borrar una pista la quita de la
  pauta, la cola y la botonera). La primera apertura los importa una sola vez y deja los
  archivos como `*.migrated`. SQLite en modo `synchronous=FULL` y diario clásico, que resiste
  cortes de luz y funciona en USB y discos de red.
- Un respaldo es solo `raaadio.db` más su manifiesto; restaurar trae la base del respaldo pero
  conserva la cola y las tandas ya insertadas del momento. Los respaldos anteriores siguen
  sirviendo.
- `Recordings/Registro` pasa a `Recordings/AirLog`, y la política de respaldo pasa de la
  configuración de Windows a la emisora.
- Las plantillas del generador aceptan cualquier nombre (ya no son nombres de archivo).
- **Interfaz rediseñada**: `LookAndFeel` propio, iconos vectoriales, temas claro y oscuro
  revisados, barra de estado nueva y diálogos reorganizados.
- Los decks y la botonera **leen el disco en otro hilo**, con ~3 s de ventaja: un disco
  lento o un USB que se demora ya no provoca cortes al aire.
- Las rutas de la biblioteca se guardan **relativas al laboratorio** (portable entre
  equipos y letras de unidad); las bases anteriores se migran solas.
- **Restaurar o importar** datos se prepara y se aplica al volver a abrir RAAADIO,
  después de respaldar lo que había.
- La **carpeta de la emisora** se decide una vez por ejecución; cambiarla rige desde el
  próximo inicio.
- **Borrar la biblioteca** hace un respaldo antes y vacía la cola y la botonera.
- El generador, sin categorías elegidas, usa **toda la música, también la que no tiene
  categoría**, y nunca mezcla cuñas ni identificaciones.
- Terminología de publicidad: "N.º de cuña" en lugar de "Número de Cue"; la pauta se
  adapta a pantallas de 1366 px.
- Los textos de la interfaz y las imágenes van **dentro del ejecutable**: basta copiar
  `RAAADIO.exe` (y `libmp3lame.dll` para transmitir en MP3).
- Toda la SQL usa sentencias preparadas; el generador trabaja sobre datos cargados de
  una vez y su lógica es pura y probada.
- Lo que se transmite pasa por un **limitador con techo de −1 dBFS**, para que la
  compresión MP3/Ogg no sature en el reproductor del oyente. La salida al aire no cambia.
- Las pestañas *Emisión* y *Biblioteca y datos* se desplazan; *Emisión* agrupa la
  transmisión y lo que suena, y *Biblioteca y datos* los respaldos.

### Corregido

- "Reproducir ahora" cortaba el aire: detenía el deck B y reemplazaba el A sin preguntar.
- El título que ven los oyentes cambiaba al preescuchar una pista en CUE.
- La versión nueva de una cuña con el mismo nombre y duración se descartaba como duplicada.
- Una tanda ya insertada se perdía al vaciar o reemplazar la cola; ahora se reinserta.
- Restaurar un respaldo no recuperaba la pauta (se sobrescribía al cerrar la app).
- Los botones de subir y bajar de la cola podían mover otra canción si AutoDJ avanzaba.
- El selector de importación ofrecía `.m4a` (ilegible) y ocultaba `.ogg`, `.aiff` y `.wma`.
- La última reproducción se leía como UTC y corría la hora.
- El generador se quedaba atascado cuando una regla de separación era imposible de cumplir.

### Eliminado

- Las 52 categorías sembradas por defecto (géneros y EMERGENCIA); una migración única
  borra las que sigan vacías y conserva las que tienen pistas.
- `LoudnessNormalizer` y `SilenceDetector`, reemplazados por `TrackAnalyzer`.
- La búsqueda de la carpeta `Assets` junto al ejecutable y una ruta fija de desarrollo.

### Seguridad

- La clave del servidor de streaming se guarda **cifrada con DPAPI** para el usuario de
  Windows; ya no queda en claro en los ajustes ni en los respaldos.
- Los datos de la emisora que van en las cabeceras del servidor se limpian de saltos de
  línea (no se pueden inyectar cabeceras). `libmp3lame.dll` solo se carga desde la carpeta
  del ejecutable.

## [1.0.0]

Primera versión: doble deck con salida Main/Cue, mezclador con locución, AutoDJ,
generador de listas por reglas, planificador de pauta de 24 horas, grabación del aire,
biblioteca SQLite, respaldos, interfaz bilingüe y API local de "now playing".
