# Registro de cambios

Todos los cambios importantes de RAAADIO se anotan aquí. El formato sigue
[Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el proyecto usa
[versionado semántico](https://semver.org/lang/es/).

## [Sin publicar]

Revisión completa de la aplicación sobre la versión 1.0.0: estabilidad al aire,
calidad de audio, pauta publicitaria, datos y organización del repositorio.

### Añadido

- **Revisión de la música importada** (`ImportReview`, ventana *Revisar canciones*): la
  música entra *por revisar* en `Library/_Entrada` y AutoDJ y el generador no la usan hasta
  que alguien confirma artista, título y categoría; las cuñas, identificaciones y separadores
  entran directo. El formato de cada carpeta (`Artista - Título` o `Título - Artista`) se
  detecta porque los artistas se repiten con títulos distintos (`TrackNaming`). Se separan
  invitados, compositores, año, versión y el género pegado al nombre, y se quitan número de
  pista, `(Video Oficial)`, direcciones web y sufijos de copias. Hay dudas con respuesta de un
  clic (*¿Es…?*, *¿invitado?*, formato sin confirmar), duplicados, acciones por lote,
  preescucha por Cue y borradores que sobreviven al cierre. La misma ventana sanea la
  *Biblioteca sin revisar* sin sacarla del aire.
- **Catálogo** (esquema 3, `Catalog.cpp`): artistas únicos con clave sin mayúsculas ni
  tildes, alias aprendidos de las correcciones, créditos por rol (principal, invitado,
  compositor), versión, año, lotes de importación y el origen de cada pista.
- **Archivos ordenados desde la base**: al confirmar, `Library/Musica/<Artista>/<Artista> -
  <Título>`; editar una pista la mueve y avisa a la cola. Lo quitado va a
  `Library/_Descartadas` (nunca se borra). Tras restaurar un respaldo viejo, los archivos se
  vuelven a encontrar por su huella.
- **Artistas** (pestaña de la ventana de revisión, `ArtistsView`): renombrar o fusionar
  cambia todas sus canciones y archivos y deja el nombre anterior como alias; sugiere los
  parecidos. **Deshacer una importación** (lo que sigue por revisar va a `_Descartadas`).
- **Huella acústica propia** (`AcousticFingerprint`): 10 s de cada canción en 33 bandas; la
  revisión marca la misma grabación con otro nombre o formato. Se calcula al importar y, para
  lo que ya estaba, en segundo plano.
- **Identificación en línea** (`OnlineIdentifier`, opcional): huella de Chromaprint
  (`chromaprint.dll`, LGPL, aparte como LAME) → AcoustID → MusicBrainz, en segundo plano y
  dentro de sus límites de pedidos. La revisión lo muestra como sugerencia ("Usar datos en
  línea", en lote para lo que tiene más del 90 %); al confirmar se guardan el identificador
  de MusicBrainz y el ISRC. Se activa con una clave de AcoustID en Ajustes.
- **Buscar canciones en internet** (`SongSearch`, `SongSearcher`, ventana *Buscar canciones
  en internet*): desde el globo junto al buscador de la biblioteca, o con *Buscar «…» en
  internet* cuando la biblioteca no la tiene. Los datos vienen de la API pública de Deezer
  (sin clave: artista, título, álbum, año, duración, ISRC, carátula) y se marca lo que ya
  está en la biblioteca aunque se escriba distinto. Para la canción elegida se busca la misma
  grabación en YouTube Music (y en YouTube si allá no coincide) y se ordenan los videos por
  título, artista, duración y tipo (audio oficial primero; en vivo, karaoke y covers abajo),
  con *Coincide*, *Parecida* o *Dudosa*. El enlace se copia o se abre; también Deezer y la
  búsqueda en Spotify (su API pide Premium). La búsqueda hace un pedido a YouTube a la vez,
  con pausa, solo para la canción que se mira; lo encontrado no se repite y si YouTube
  responde 429 no se le pide nada durante 15 minutos.
- **Descargar a la biblioteca** (`SongDownloader`, `SongDownload`, `DownloadTools`,
  `TagWriter`, `ProcessRunner`): yt-dlp baja el audio del video elegido, FFmpeg lo pasa a MP3
  VBR, se escriben las etiquetas ID3 con los datos de Deezer (invitados, álbum, año, ISRC,
  enlace de origen) y la canción entra por revisar en un lote propio (se puede deshacer); el
  ISRC queda en la pista. yt-dlp, FFmpeg y Deno se instalan a pedido desde sus publicaciones
  oficiales, con el SHA-256 verificado, en `%LOCALAPPDATA%\RAAADIO\Herramientas` (o se usan los
  del `PATH`); yt-dlp se actualiza una vez al día. Ritmo para no ser bloqueado: de a una, al
  menos 20 s entre descargas, tope de 20 por hora, pocos reintentos; si YouTube pide esperar
  o confirmar que no es un robot, la fila se pausa 1 h o 6 h (la pausa se guarda) y la
  canción espera en vez de fallar. Cancelar cierra todo el árbol de procesos (job object).
  Cada intento queda en la tabla `downloads`. Aviso de derechos y términos de uso la primera
  vez. Las descargas siguen aunque se cierre la ventana de búsqueda.
- **Importar audios de WhatsApp, M4A y AAC** (`AudioConverter`): las notas de voz (Opus, en
  `.opus` o `.ogg`), `.m4a`, `.aac`, `.amr` y `.mpeg` se convierten a FLAC de 16 bits con el
  FFmpeg de la descarga de canciones, con sus etiquetas y siempre al mismo archivo (así se
  reconocen si se importan otra vez). Un `.mpeg` que es MP3 (los de WhatsApp Web) se guarda
  como `.mp3`, tal cual y sin FFmpeg. Un archivo de un formato conocido que JUCE no lee (un
  WAV con MP2 adentro) se intenta con FFmpeg. Si falta FFmpeg, al terminar la importación se
  ofrece instalar solo FFmpeg y esos archivos se importan solos después.
- **Audios temporales** (pestaña *Temporales*, `TemporariesView`, `TemporaryAudio`,
  `LinkDownload`): saludos, mensajes, notas de voz y audios de redes que se pasan una o varias
  veces. *Importar temporal* (junto a *Importar cuñas* e *Importar*) o soltar archivos sobre la
  lista; entran sin revisión a `Library/_Temporales/<fecha>`, analizados y normalizados como todo.
  Una categoría fija nueva (`CategoryRole::Temporaries`, guardada como `_Temporales`) los separa:
  no aparecen en la biblioteca, entre las categorías ni en la revisión, y AutoDJ, el generador y la
  protección contra silencio no los usan; solo se comparan entre sí al buscar duplicados (una cuña
  igual es otra pista). Desde la lista se preescuchan por Cue, salen al aire, se cargan en un
  deck, van a la cola o a la botonera (también arrastrándolos) y se renombran; cada fila muestra
  las veces al aire y si está en un deck, en la cola o en la botonera. *Quitar* y *Vaciar* borran
  de verdad archivo y registro, con confirmación y nunca lo que está en un deck, sonando o en la
  cola. **Enlaces de redes**: se pega el enlace de Facebook, Instagram, X, TikTok, YouTube u otro
  sitio que entienda yt-dlp y se baja solo el audio, que FFmpeg (`AudioConverter`, ahora también a
  MP3) convierte a MP3; va en la fila de `SongDownloader`, con su pausa entre descargas y su
  cancelación, pero el tope por hora y las pausas de YouTube ya no frenan a los demás sitios. Sin
  cookies ni sesión del navegador: lo privado o lo que pide iniciar sesión da un error claro
  (`SongDownload::Problem::LoginRequired`, `NoAudio`, `Unsupported`, `Live`).
- **Búsqueda FTS5** sin mayúsculas ni tildes, por partes de palabras, créditos y nombre
  original. **Separación de artistas por intérprete** en el generador (con invitados).
- **Mostrar en MAYÚSCULAS** (*Configuración → Biblioteca y datos*): los nombres se guardan
  bien escritos y se ven en mayúsculas si la emisora lo prefiere. Una biblioteca que ya
  existía queda así al migrar. La búsqueda no distingue tildes.
- **Análisis de audio al importar** (`TrackAnalyzer`): sonoridad integrada EBU R 128
  con libebur128, pico real con sobremuestreo 4×, normalización a −14 LUFS y puntos
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
- **Análisis de calidad con SonarQube** (`tools/sonar-scan.ps1`, `sonar-project.properties`,
  `.clang-tidy`): métricas y duplicados con el plugin sonar-cxx, los hallazgos de clang-tidy
  como issues externos y la cobertura de las pruebas con OpenCppCoverage. El token queda fuera
  del repositorio.
- **Pruebas de la interfaz**: cada pantalla se construye, se dibuja y se opera sin ventana
  (botones, menús, diálogos, tablas, arrastrar y soltar). Las preferencias del equipo, los
  diálogos de archivos y lo que se abre fuera de RAAADIO (navegador, Explorador, reproductor,
  portapapeles) pasan por `AppSettings`, `FileChoosers` y `External`, que las pruebas
  reemplazan para no tocar nada del equipo.

### Cambiado

- **Fila de pestañas del panel derecho**: con la pestaña *Temporales* y el botón *Importar
  temporal*, si no cabe todo, primero los botones y después las pestañas cerradas quedan solo con
  su icono (el nombre sigue en la ayuda emergente); la pestaña abierta siempre muestra su nombre.
- **Sonoridad objetivo de −16 a −14 LUFS**, como Spotify y YouTube: la música suena unos
  2 dB más fuerte. Al abrir, las ganancias de lo ya analizado se recalculan con la sonoridad
  y el pico guardados, sin volver a analizar la biblioteca; el objetivo aplicado queda
  anotado en la base (`library.normalizationTarget`).
- **Artista y título ya no se guardan en mayúsculas**, y un nombre sin etiquetas ya no se
  parte en cualquier guion: solo en `" - "`, con el formato de su carpeta. La música deja de
  ordenarse en la carpeta de su categoría (una canción puede estar en varias); las cuñas,
  identificaciones y separadores siguen en la suya. El diálogo de una pista edita invitados,
  versión, año y compositores, y guardar la confirma.
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
- **Generador de listas rediseñado**, con dos modos sobre los mismos ajustes: *Rápido* (cuánta
  música, qué géneros con su cantidad de canciones, dónde ponerla y separadores; los valores
  por defecto ya están puestos) y *Avanzado* (incluir y excluir categorías con botones en vez
  de clic derecho, reglas escritas como frases, separadores, plantillas en una lista
  desplegable). Un resumen dice antes de generar qué va a pasar y avisa de lo que saldría mal;
  reemplazar la cola lo dice en el botón; después de generar se informa qué se hizo y se puede
  quitar lo generado.
- **Las reglas del generador son preferencias con un orden**: no repetir canciones, separar
  artistas, cambiar de categoría. El bloque sigue de lo que ya está en la cola (no lo repite y
  separa los artistas desde la última canción), siempre llega a la duración pedida y dice
  cuánto tuvo que ceder.
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

- Al arrastrar una pista de la biblioteca (o de las cuñas o los temporales) a la cola, la rueda
  del mouse movía la biblioteca y no la cola: JUCE se la manda a la lista donde empezó el
  arrastre. Ahora se desplaza la lista que está bajo el puntero (`WheelRouter`), y la cola además
  se desplaza sola al arrastrar cerca de su borde de arriba (o de la cabecera) o de abajo, con la
  marca de inserción siguiendo el desplazamiento.
- La prueba de interfaz *Mover la emisora… copia completa* fallaba a veces: contestaba la
  primera ventana modal que aparecía, y si la copia seguía en curso esa era la ventana de
  avance, que al cerrarse cancela la copia. Ahora espera el aviso del final. Para repetir una
  prueba sin correr toda la suite, `RAAADIO_TESTS=<parte del nombre>` corre solo esas clases.
- Mover filas de la cola con las flechas podía cerrar la aplicación: al deseleccionar, la
  tabla borraba el control de la fila mientras su botón todavía se estaba pulsando.
- "Reproducir ahora" cortaba el aire: detenía el deck B y reemplazaba el A sin preguntar.
- El título que ven los oyentes cambiaba al preescuchar una pista en CUE.
- La versión nueva de una cuña con el mismo nombre y duración se descartaba como duplicada.
- Una tanda ya insertada se perdía al vaciar o reemplazar la cola; ahora se reinserta.
- Restaurar un respaldo no recuperaba la pauta (se sobrescribía al cerrar la app).
- Los botones de subir y bajar de la cola podían mover otra canción si AutoDJ avanzaba.
- El selector de importación ofrecía `.m4a` (ilegible) y ocultaba `.ogg`, `.aiff` y `.wma`.
- Un WMA importado guardaba la huella SHA-256 del original y no la del FLAC que quedaba en la
  biblioteca, así que su copia en el destino externo de respaldo fallaba la verificación.
- La última reproducción se leía como UTC y corría la hora.
- El generador se quedaba atascado cuando una regla de separación era imposible de cumplir.
- El generador, con una categoría chica junto a una grande, repetía las canciones de la chica
  para "cambiar de categoría" (con 300 de salsa y 10 boleros, la mitad de una lista de 4 horas
  eran boleros y cada uno sonaba hasta 4 veces).
- Generar dos bloques seguidos repetía en el segundo canciones que seguían en la cola, y podía
  empezar con el artista de la última canción de la cola.
- Una lista que quedaba a medias (reglas imposibles) se daba por buena sin avisar; una pista
  sin duración dejaba al generador agregando miles de filas; se programaban canciones cuyo
  archivo ya no estaba; una plantilla a la que le faltaba un campo cargaba ceros en vez de los
  valores por defecto; y un mismo separador podía salir dos veces seguidas.
- La transmisión por internet se caía al sonar música detrás de un firewall que revisa el
  tráfico web (un FortiGate tomaba el MP3 por peticiones HTTP y cortaba por "URL inválida"):
  ahora hay **conexión cifrada (TLS)**, que además protege la clave. También se caía en redes
  lentas: en Windows, leer la respuesta del servidor dejaba el socket en modo no bloqueante, y
  un envío que encontraba la red atascada un instante se tomaba por una conexión perdida.
  Además se presenta con `PUT` (Icecast 2.4 en adelante) y, si el servidor no lo acepta, con
  `SOURCE`; nota enseguida cuando el servidor corta (antes seguía enviando unos segundos a una
  conexión muerta); si el corte llega a los pocos segundos, espera cada vez más antes de
  insistir; y el registro dice con hora quién cortó, cuánto duró, cuánto audio salió y qué
  respondió el servidor (la página entera, si mandó una).

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
