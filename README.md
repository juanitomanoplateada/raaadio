<div align="center">

<img src="Assets/RAAADIO%20ICON.png" alt="RAAADIO" width="110"/>

# RAAADIO

**Sistema de automatización radial para emisoras comunitarias, universitarias e independientes.**

Doble deck, AutoDJ, programación musical por reglas, pauta publicitaria automática, botonera,
transmisión por internet y registro de lo emitido, en una sola aplicación de escritorio nativa.

<br/>

[![Compilación y pruebas](https://github.com/juanitomanoplateada/raaadio/actions/workflows/build.yml/badge.svg)](https://github.com/juanitomanoplateada/raaadio/actions/workflows/build.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![JUCE](https://img.shields.io/badge/JUCE-8.0.3-8DC63F?style=flat-square)](https://juce.com/)
[![SQLite](https://img.shields.io/badge/SQLite-3.45-003B57?style=flat-square&logo=sqlite&logoColor=white)](https://sqlite.org/)
![Versión](https://img.shields.io/badge/versión-1.0.0-blue?style=flat-square)
![Plataforma](https://img.shields.io/badge/plataforma-Windows%20x64-0078D6?style=flat-square&logo=windows&logoColor=white)
![Idiomas](https://img.shields.io/badge/idiomas-ES%20%7C%20EN-orange?style=flat-square)
[![Licencia MIT](https://img.shields.io/badge/licencia-MIT-green?style=flat-square)](LICENSE)

<br/>

[Capturas](#capturas) &nbsp;·&nbsp;
[Características](#características) &nbsp;·&nbsp;
[Instalación](#instalación) &nbsp;·&nbsp;
[Primeros pasos](#primeros-pasos) &nbsp;·&nbsp;
[Datos y respaldos](#datos-y-respaldos) &nbsp;·&nbsp;
[Arquitectura](#arquitectura) &nbsp;·&nbsp;
[Contribuir](#contribuir)

</div>

---

## Sobre el proyecto

Las emisoras comunitarias operan 24/7 con presupuestos que no alcanzan para licencias de
automatización comercial. La alternativa habitual (software descontinuado, reproductores
genéricos o cadenas de scripts) falla justo cuando no debe: al aire.

**RAAADIO** nace para cerrar esa brecha. Es una aplicación de escritorio nativa escrita en
C++17 sobre [JUCE 8](https://juce.com/), pensada para correr sin parar en equipos modestos,
con una interfaz en español y la terminología de una cabina de radio.

No requiere servidor, ni conexión a internet, ni servicios externos: toda la emisora vive
en una carpeta que se puede llevar en un disco externo.

<div align="center">
<img src="docs/screenshots/main-oscuro.png" alt="Pantalla principal de RAAADIO en tema oscuro" width="900"/>
<br/>
<sub><i>Decks, mezclador, cola de programación y biblioteca en una sola pantalla.</i></sub>
</div>

---

## Capturas

<table>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/main-claro.png" alt="Pantalla principal en tema claro"/>
      <br/><sub><b>Tema claro</b> · la categoría fija de la emisora lleva candado</sub>
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/publicidad.png" alt="Pauta de cuñas"/>
      <br/><sub><b>Publicidad</b> · pauta de cuñas por franjas, identificaciones y pauta automática</sub>
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/botonera.png" alt="Botonera"/>
      <br/><sub><b>Botonera</b> · 12 cartuchos con F1–F12, directo al aire</sub>
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/historial.png" alt="Historial de emisión"/>
      <br/><sub><b>Historial</b> · lo que salió al aire, con hora y deck</sub>
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/compacto-1366.png" alt="Pantalla de 1366 por 768"/>
      <br/><sub><b>1366 × 768</b> · la interfaz se adapta a los monitores de cabina más comunes</sub>
    </td>
    <td width="50%" align="center">
      <img src="docs/screenshots/generador.png" alt="Generador de listas"/>
      <br/><sub><b>Generador de listas</b> · reglas de separación, separadores y plantillas</sub>
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src="docs/screenshots/ajustes-emision.png" alt="Ajustes de emisión" width="420"/>
      <br/><sub><b>Emisión</b> · protección contra silencio, registro de lo emitido y streaming</sub>
    </td>
    <td width="50%"></td>
  </tr>
</table>

---

## Características

### Motor de audio

| Componente | Descripción |
|---|---|
| **Doble deck** | Transporte independiente, velocidad con *resampling*, bucle y forma de onda. Cada deck lee el archivo en otro hilo con unos 3 s de ventaja: un disco lento o un USB que se demora no corta el aire. |
| **Salida Main / Cue** | Dos dispositivos de audio independientes: **Main** va al aire y **Cue** a los audífonos. La preescucha es antes del fader (PFL). |
| **Mezclador** | Crossfader suavizado, volumen por canal, medidores RMS y de pico, y **locución** con atenuación automática de la música. |
| **Limitador maestro** | Limitador de pico en la salida para evitar recortes al aire. |
| **Normalización por sonoridad** | Cada pista se mide una vez al importarla (EBU R 128, con [libebur128](https://github.com/jiixyj/libebur128)) y suena a −16 LUFS. Una pista suave solo se sube hasta que su pico real llega a −1 dBTP. |
| **Puntos de entrada y salida** | Se calculan al importar: la pista arranca donde empieza el sonido, deja el aire donde termina y la siguiente entra 3,5 s antes. Las pausas intencionales no la cortan; una "pista oculta" tras un silencio largo sí. |

### Automatización

- **AutoDJ.** Supervisa los decks, lanza la siguiente pista en su punto de mezcla y
  descarga la que termina. Cambia entre automático y manual sin interrumpir el aire.
- **Reproducir ahora.** La pista entra en el deck libre y lo que suena baja con un fundido
  de 2 s; si hay algo al aire, antes pregunta. Nunca corta en seco lo que oyen los oyentes.
- **Protección contra silencio.** Si la salida queda muda más de 20 s (configurable) en
  automático, salta una pista muda, reanuda la cola o la rellena con música de la categoría
  `EMERGENCIA` (si existe) o de toda la biblioteca. En manual o con la locución abierta, solo avisa.
- **Botonera.** 12 cartuchos para cortinillas y efectos con F1 a F12, directo al aire por
  encima de los decks, con la ganancia y los puntos de entrada y salida de su análisis.
- **Generador de listas por reglas.** Separación de artista, título, categoría y última
  reproducción; separadores cada N canciones; duración objetivo; plantillas reutilizables.

<details>
<summary><b>Parámetros del generador (valores por defecto)</b></summary>

<br/>

| Parámetro | Por defecto | Qué controla |
|---|:---:|---|
| `artistSeparation` | `5` | Pistas mínimas antes de repetir un artista |
| `titleSeparation` | `10` | Pistas mínimas antes de repetir un título |
| `categorySeparation` | `3` | Pistas mínimas antes de repetir una categoría |
| `lastPlayedSeparation` | `60` | Minutos mínimos desde la última reproducción |
| `targetDurationVal` | `60` | Duración objetivo del bloque (minutos u horas) |
| `separatorInterval` | `0` | Insertar un separador cada N pistas (`0` = desactivado) |
| `insertionMode` | `AddToEnd` | Añadir al final, al inicio o reemplazar la cola |

Sin categorías incluidas vale toda la música, también la que no tiene categoría; las
identificaciones, cuñas y separadores solo entran si se incluyen. Si una regla de
separación pide más variedad de la que hay, se limita a lo posible en lugar de dejar la
lista a medias.

</details>

### Categorías de la emisora

La biblioteca arranca sin géneros predefinidos: cada emisora crea los suyos. Tres categorías
son fijas, van siempre primero y no se renombran, mueven ni eliminan:

| Categoría | Qué va ahí |
|---|---|
| **IDENTIFICACIONES** | El nombre, la frecuencia y la ciudad de la emisora. Cada tanda abre con una al azar. |
| **CUÑAS** | La publicidad pagada, con número y horario en la pauta. |
| **SEPARADORES** | Cortinillas, jingles y promos entre canciones. |

La pauta, el generador y la protección contra silencio las reconocen por su rol, no por el
nombre: renombrar o traducir la interfaz no rompe la publicidad.

### Pauta publicitaria

- **Pauta de 24 horas** en franjas de 30 minutos: cada número de cuña se asigna a las franjas
  contratadas.
- **Pauta automática.** La tanda de cada franja (una identificación y sus cuñas) se inserta
  sola 20 minutos antes, en el punto de la cola donde empieza más cerca de su hora, sin cortar
  canciones. No se repite al reiniciar, y si la cola se vacía o se reemplaza antes de que la
  tanda suene, se vuelve a insertar.
- **Cuñas nuevas sin confusiones.** La cuña del mes se importa aunque se llame y dure igual
  que la anterior: en las categorías de la emisora solo cuenta como duplicado el mismo archivo.

### Emisión

- **Transmisión por internet** a Icecast, Zeno.fm y compatibles, sin BUTT ni otro programa: **MP3**
  (64 a 320 kbps, con LAME; lo abre cualquier reproductor, iPhone incluido) u **Ogg Vorbis**.
  Reconecta sola; la clave se guarda cifrada en Windows (DPAPI).
- **Título para los oyentes** limpio (`Artista - Título`) y solo de lo que está al aire, no de
  lo que se preescucha. Durante cuñas, identificaciones y separadores muestra el nombre de la
  emisora. En MP3 va por `/admin/metadata` y se puede retrasar unos segundos para que cambie
  junto con el audio que oye el oyente.
- **Techo de -1 dBFS** en lo que se transmite, para que los picos que agrega la compresión MP3 u
  Ogg no saturen en el reproductor del oyente.
- **Datos de la emisora** para directorios y reproductores: nombre, género, descripción,
  sitio web y si se lista en directorios públicos.
- **Registro de lo emitido** (opcional): la salida al aire en archivos de una hora (Ogg Vorbis
  mono, unos 20 MB por hora) con días de retención; sirve como constancia ante el regulador.
- **Registro de emisión** en la base de datos (`play_log`): cada pista que sale al aire con
  hora, deck, título y artista. La preescucha no cuenta.
- **Grabadora** del bus maestro en un hilo propio, sin cortes por picos de carga.
- **Lo que suena en JSON** (anterior, actual y siguientes, con horas): en la API local y, si se
  configura, enviado a la página web de la emisora en cada cambio. Ver [API local](#api-local).

### Biblioteca

- **Importación** de archivos o carpetas eligiendo la categoría. Los archivos se copian tal
  cual (MP3, FLAC, OGG, WAV, AIFF; un WMA se pasa a FLAC, sin pérdida). Artista y título salen
  de las etiquetas (ID3, Vorbis, INFO de WAV) o del nombre `Artista - Título`.
- **Sin duplicados.** El mismo archivo, o la misma canción en otro formato, no se copia dos
  veces: importarla en otra categoría solo la suma a esa categoría.
- **Análisis en segundo plano** de lo importado con versiones anteriores, adelantando lo que
  está por sonar.
- **Cola persistente**: sobrevive a un cierre inesperado.

### Interfaz

- Temas **claro** y **oscuro**, iconos vectoriales y una distribución que funciona desde
  1366 × 768.
- **Bilingüe** (español e inglés), conmutable en caliente. Los textos van dentro del
  ejecutable: basta copiar `RAAADIO.exe` (y `libmp3lame.dll` para transmitir en MP3).

---

## Requisitos

| | Mínimo | Recomendado |
|---|---|---|
| **Sistema** | Windows 10 x64 | Windows 11 x64 |
| **Procesador** | Intel Core i3 / AMD Ryzen 3 | Core i5 / Ryzen 5 |
| **Memoria** | 4 GB | 8 GB |
| **Disco** | HDD | SSD |
| **Audio** | Una salida estéreo | Dos salidas (Main al aire y Cue a audífonos) |
| **Pantalla** | 1366 × 768 | 1920 × 1080 |

Para compilar: CMake ≥ 3.15, Git y un compilador C++17. En Windows, Visual Studio 2022 o sus
Build Tools con la carga de trabajo "Desarrollo de escritorio con C++".

> Las compilaciones de referencia, la integración continua y las pruebas se hacen en Windows
> x64. El código usa JUCE y debería compilar en Linux y macOS, pero allí no se prueba.

---

## Instalación

RAAADIO usa `FetchContent`: CMake descarga JUCE 8.0.3, SQLite, libebur128 y LAME 3.100 por su cuenta.

```bash
git clone https://github.com/juanitomanoplateada/raaadio.git
cd raaadio
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

El ejecutable queda en `build/RAAADIO_artefacts/Release/RAAADIO.exe` y es autónomo: para
instalarlo en la máquina de aire basta copiar ese archivo. Al lado quedan `libmp3lame.dll` y su
licencia (`libmp3lame-COPYING.txt`): cópialos junto al `.exe` para transmitir en MP3. Sin ellos
RAAADIO funciona igual y transmite en Ogg Vorbis.

<details>
<summary><b>La primera compilación tarda</b></summary>

<br/>

`FetchContent` descarga JUCE completo la primera vez (varios minutos según la conexión).
Las siguientes compilaciones reutilizan `build/_deps`. No borres `build/` salvo que necesites
una configuración limpia.

</details>

<details>
<summary><b>Linux y macOS (sin soporte oficial)</b></summary>

<br/>

En Linux, JUCE necesita las cabeceras de X11, ALSA y FreeType:

```bash
sudo apt install build-essential cmake git \
  libasound2-dev libjack-jackd2-dev \
  libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libxcomposite-dev \
  libfreetype6-dev libfontconfig1-dev libcurl4-openssl-dev
cmake -S . -B build && cmake --build build --config Release
```

En macOS, con las Xcode Command Line Tools: `cmake -S . -B build -G Xcode`.

Fuera de Windows la clave del streaming se guarda sin cifrar, y para transmitir en MP3 hace falta
la biblioteca de LAME del sistema (`libmp3lame`; en Debian y Ubuntu, `sudo apt install libmp3lame0`).

</details>

---

## Primeros pasos

1. **Elige la carpeta de la emisora.** En *Configuración → Biblioteca y datos*. Un disco
   externo o USB hace la instalación portable.
2. **Configura el audio.** En *Configuración → Audio*, la salida **Main** (al aire) y, si
   tienes una segunda interfaz, la salida **Cue** (audífonos).
3. **Importa la música** con *Importar*, eligiendo la categoría (crea tus géneros con el
   botón **+** de la barra de categorías). Las cuñas van con *Importar cuñas*.
4. **Carga las identificaciones y separadores** en sus categorías fijas.
5. **Programa la pauta** en la pestaña *Publicidad*: escribe el número de la cuña y marca
   sus franjas. Activa *Pauta automática* para que las tandas entren solas.
6. **Genera una lista** con el *Generador de Playlist* y guárdala como plantilla.
7. **Activa AutoDJ** y deja la emisora al aire.

---

## Datos y respaldos

Toda la emisora vive en una carpeta reubicable (por defecto `Documentos/raaadio`; al elegir
otra ubicación se usa `raaadio_lab` dentro de ella):

```
raaadio_lab/
├── Database/      # raaadio.db: toda la emisora (pistas, categorías, pauta, cola, botonera,
│                  # ajustes, plantillas y registro de emisión)
├── Library/       # la música y las cuñas, copiadas tal cual al importarlas
├── Recordings/    # grabaciones; AirLog/ guarda el registro de lo emitido
├── Backups/       # respaldos verificados: AAAA/MM/DD/HH_MM_SS/{raaadio.db, manifest.json}
├── Logs/          # raaadio.log (se recorta a 5 MB al iniciar)
└── raaadio.lock   # mientras está abierta: en qué equipo
```

**Una sola fuente de verdad.** Todo el estado de la emisora está en SQLite, no en archivos
sueltos: cada cambio es una transacción que sobrevive a un corte de luz (diario clásico y
`synchronous=FULL`, que también funciona en un USB o un disco de red), la pauta, la cola y la
botonera no pueden apuntar a una pista borrada (claves foráneas) y un respaldo es un solo
archivo coherente. Las versiones anteriores guardaban la pauta, la cola, la botonera, los
ajustes y las plantillas en `Config/` y `Templates/`: al abrir la emisora por primera vez con
esta versión se importan a la base una sola vez y quedan como `*.migrated`. JSON queda para
exportar e intercambiar datos, nunca como fuente de verdad.

Las rutas de la biblioteca se guardan relativas a esta carpeta: sigue funcionando si el disco
cambia de letra o se lleva a otro equipo.

**Respaldos** (*Configuración → Biblioteca y datos*):

- **Diario a una hora fija** (03:00 por defecto) mientras la emisora está al aire, y al abrir
  si esa hora pasó sin respaldo. Corre en otro hilo con prioridad baja: no compite con los
  decks.
- **Verificados.** Cada respaldo es una copia consistente de la base (aunque esté al aire), que
  pasa `PRAGMA quick_check` antes de contarse, con un `manifest.json` (versión, fecha y
  SHA-256). Al restaurar se comprueba de nuevo: un respaldo alterado no se usa.
- **Destino externo**: otro disco, un USB o una carpeta de OneDrive o Google Drive. Cada
  respaldo se copia a `RAAADIO-<id>/Backups/` (aparece completo o no aparece) y la música a
  `RAAADIO-<id>/Library/`: solo lo nuevo, verificando cada archivo con el SHA-256 que la base
  guarda de la pista, y sin borrar nunca nada allá. Si el destino no está conectado, se
  reintenta cada 30 minutos.
- **Retención escalonada**, igual aquí y en el destino externo: siempre los 10 más recientes,
  el último de cada día (14 días), de cada semana (8 semanas) y de cada mes (12 meses).
- **Recuperación automática.** Al abrir, si la base está dañada, se aparta
  (`raaadio.db.damaged-FECHA`) y se pone la del último respaldo verificado, con un aviso.
- **Alertas** en la barra superior: respaldo de más de 36 horas, respaldo o copia externa
  fallidos y disco de la emisora casi lleno (menos de 5 GB o del 5 %; crítico con menos de 1 GB).
- **Restaurar o importar** se prepara al momento y se aplica al volver a abrir RAAADIO, antes
  de abrir la base de datos y después de respaldar lo que había. La cola y las tandas ya
  insertadas siguen siendo las del momento. Los respaldos de versiones anteriores también
  sirven.
- **Un solo equipo a la vez.** Si la emisora está abierta en otro equipo (un USB o un disco de
  red), RAAADIO avisa antes de abrirla; un candado de un equipo que se cerró mal se libera solo.
- **Mover la emisora.** *Mover o cambiar la carpeta* copia la emisora entera a otro disco
  (con avance, comprobación de espacio y opción de cancelar) o apunta a una que ya existe.
  Rige desde el próximo inicio; la carpeta anterior no se borra.
- **Borrar la biblioteca** hace un respaldo antes, y si no puede, no borra nada.

---

## Transmitir por internet

En *Configuración → Emisión → Transmisión por internet*. Los datos son los mismos que pide
BUTT; por ejemplo, para **Zeno.fm** (su panel muestra los datos para el codificador):

| Zeno.fm / BUTT | RAAADIO | Ejemplo |
|---|---|---|
| Dirección | Servidor | `link.zeno.fm` |
| Puerto | Puerto | `80` |
| Punto de montaje | Punto de montaje | el que da Zeno (con o sin `/`) |
| Usuario | Usuario | `source` |
| Contraseña | Contraseña | la que da Zeno |
| Formato | Formato y Calidad | **MP3, 128 kbps** (el mismo que elegiste en el panel de Zeno) |

Marca **Transmitir** y el estado pasa a *Transmitiendo*. RAAADIO usa el método `SOURCE` de
Icecast (el "protocolo anticuado" de BUTT), que acepta Icecast 2 y los servicios compatibles.
No hay SSL/TLS: la conexión va sin cifrar, igual que en BUTT con el puerto 80.

- **MP3 u Ogg.** MP3 lo abre cualquier reproductor; Ogg Vorbis suena mejor con los mismos
  kbps, pero Zeno.fm no lo acepta y algunos reproductores (sobre todo en iPhone) no lo abren.
- **Título.** En MP3 el título se envía aparte, a `/admin/metadata` del mismo servidor. Si en
  la app de Zeno el título cambia unos segundos antes que la canción, sube *Retrasar el título*.
- **Anterior y siguiente no viajan en el audio.** El protocolo tiene un solo campo de texto
  (`StreamTitle`), así que el título es siempre `Artista - Título`, el formato que las apps
  usan para buscar la carátula. Lo anterior y lo siguiente van en el JSON de abajo.

---

## API local

RAAADIO expone un servidor HTTP en `127.0.0.1:8080` con lo que está al aire, para alimentar
codificadores RDS, pantallas de cabina, OBS o scripts propios.

| Método | Endpoint | Respuesta | Content-Type |
|---|---|---|---|
| `GET` | `/nowplaying` | `Artista - Título` | `text/plain; charset=utf-8` |
| `GET` | `/nowplaying.json` | anterior, actual y siguientes | `application/json; charset=utf-8` |

```bash
curl http://127.0.0.1:8080/nowplaying
# GRUPO NICHE - UNA AVENTURA
```

```jsonc
// curl http://127.0.0.1:8080/nowplaying.json
{
  "station": "RAAADIO FM",
  "state": "music",                      // "music", "break" (cuña, identificación, separador) u "off"
  "updated": "2026-09-24T16:59:21.000-05:00",
  "now": {                               // null durante una tanda o si no suena nada
    "artist": "GRUPO NICHE", "title": "UNA AVENTURA", "text": "GRUPO NICHE - UNA AVENTURA",
    "duration": 283, "started": "2026-09-24T16:57:10.000-05:00", "ends": "2026-09-24T17:01:49.000-05:00"
  },
  "previous": [                          // la más reciente primero, hasta 5
    { "artist": "REY RUIZ", "title": "NO ME ACOSTUMBRO", "text": "REY RUIZ - NO ME ACOSTUMBRO",
      "duration": 291, "played": "2026-09-24T16:52:22.000-05:00" }
  ],
  "next": [                              // hasta 3; "eta" es null si no se sabe
    { "artist": "RICHIE RAY Y BOBBY CRUZ", "title": "SONIDO BESTIAL", "text": "…",
      "duration": 411, "eta": "2026-09-24T17:01:49.000-05:00" }
  ]
}
```

Solo cuenta la música que está al aire: una pista en preescucha no cambia nada, y las cuñas,
identificaciones y separadores no aparecen entre las anteriores ni las siguientes. Si suenan
dos decks (una mezcla), gana el que arrancó último. Las respuestas incluyen
`Access-Control-Allow-Origin: *`, así que se pueden leer desde una página local:

```js
const res = await fetch("http://127.0.0.1:8080/nowplaying.json");
const np = await res.json();
document.querySelector("#now-playing").textContent = np.now ? np.now.text : np.station;
```

> El servidor escucha solo en `127.0.0.1`. Exponerlo a la red requiere un proxy inverso, a
> propósito: la máquina de aire no queda abierta por defecto.

**Para la página web de la emisora**, escribe en *Configuración → Emisión → Enviar a la página
web* la dirección de un script tuyo: RAAADIO le envía el mismo JSON por `POST` en cada cambio y
una vez por minuto, y la pestaña muestra si el último envío llegó. RAAADIO no manda
credenciales; pon una clave larga en la dirección y compruébala en el servidor. Un receptor
mínimo en PHP que deja el JSON en `ahora.json` para que la página lo lea:

```php
<?php // ahora.php — en RAAADIO: https://tusitio.com/ahora.php?clave=UNA-CLAVE-LARGA
if (!hash_equals('UNA-CLAVE-LARGA', $_GET['clave'] ?? '')) { http_response_code(403); exit; }
$json = file_get_contents('php://input');
if (json_decode($json) === null) { http_response_code(400); exit; }
file_put_contents(__DIR__ . '/ahora.json', $json, LOCK_EX);
```

---

## Arquitectura

```mermaid
flowchart TD
    subgraph UI["Interfaz · JUCE"]
        DK["DeckComponent ×2"]
        MX[MixerComponent]
        PL[PlaylistComponent]
        LB["LibraryComponent<br/>biblioteca · historial · publicidad · botonera"]
    end

    subgraph AUTO["Automatización"]
        DJ[AutoDJ]
        OA["OnAir<br/>reproducir ahora · qué está al aire"]
        SCH["AdScheduler<br/>pauta automática"]
        DAG["DeadAirGuard<br/>protección contra silencio"]
        GEN[PlaylistGenerator]
    end

    subgraph AUDIO["Cadena de audio"]
        D1["Deck A"] --> MIX[Mixer]
        D2["Deck B"] --> MIX
        CART["CartPlayer · F1–F12"] --> MIX
        MIX --> LIM[MasterLimiter]
        LIM --> MAIN(("Main · al aire"))
        MIX --> CUE(("Cue · audífonos"))
        LIM --> TAPS["RecordingManager · AirLogRecorder · StreamEncoder"]
    end

    subgraph DATA["Datos"]
        ML[MusicLibrary] --> DB[(SQLite)]
        Q["Playlist + PlaylistStore"]
        PM["PersistenceManager<br/>carpeta · respaldos"]
    end

    DK --> D1
    DK --> D2
    PL --> Q
    LB --> ML
    DJ --> D1
    DJ --> D2
    DJ --> Q
    OA --> D1
    OA --> D2
    SCH --> Q
    DAG --> GEN
    GEN --> Q
    GEN --> ML
    PM --> DB
```

<details>
<summary><b>Organización del código fuente</b></summary>

<br/>

```
Source/
├── Main.cpp                  # Punto de entrada; una sola instancia; modo --ui-snapshot
├── MainComponent.*           # Composición y ciclo de vida de la aplicación
├── audio/
│   ├── AudioEngine.*         # Dispositivos Main y Cue, decks y mezclador
│   ├── Deck.*                # Transporte con lectura por adelantado, puntos de entrada/salida
│   ├── Mixer.*               # Crossfader, locución, medidores, FIFO de Cue, salidas del aire
│   ├── MasterLimiter.h       # Limitador de pico
│   ├── TrackAnalyzer.*       # Sonoridad (LUFS), pico real y puntos de entrada/salida
│   ├── CartPlayer.*          # Botonera F1–F12
│   ├── AirLogRecorder.*      # Registro de lo emitido, un archivo por hora
│   └── RecordingManager.*    # Grabación en un hilo propio
├── autodj/
│   ├── AutoDJ.*              # Mezcla automática entre decks
│   ├── OnAir.*               # Reproducir ahora y qué está al aire
│   ├── QueueTiming.*         # Hora de inicio de cada pista de la cola
│   ├── AdScheduler.*         # Tandas y pauta automática
│   └── DeadAirGuard.*        # Protección contra silencio
├── generator/                # Generador de listas por reglas y plantillas
├── library/                  # Biblioteca, etiquetas, cola, historial y pauta
├── database/
│   ├── Database.*            # SQLite: esquema, migraciones y consultas preparadas
│   └── StationState.cpp      # Pauta, cola, botonera, ajustes y plantillas en la base
├── network/
│   ├── ApiServer.*           # "Now playing" en 127.0.0.1:8080 (texto y JSON)
│   ├── StreamEncoder.*       # Transmisión a Icecast (MP3 u Ogg Vorbis) y títulos
│   ├── Mp3Encoder.*          # MP3 con LAME (libmp3lame.dll, cargada al transmitir)
│   ├── NowPlaying.*          # Anterior, actual y siguientes: armado y JSON
│   └── NowPlayingPublisher.* # Título del stream, API y envío a la página web
├── helpers/
│   ├── PersistenceManager.*  # Carpeta de la emisora, respaldos verificados, restauración
│   ├── BackupService.*       # Respaldo diario, destino externo y copia de la música
│   ├── StationHealth.*       # Alertas: disco y respaldos
│   ├── StationLock.*         # Un solo equipo a la vez
│   └── ...                   # Idioma, temas, estilo, ajustes, claves
└── ui/                       # Componentes y diálogos
Tests/RaaadioTests.cpp        # Pruebas automáticas (ctest)
Assets/                       # Icono, textos es/en y arte (se incrustan en el .exe)
docs/screenshots/             # Capturas del README (generadas con --ui-snapshot)
```

</details>

**Decisiones de diseño**

- **El hilo de audio nunca espera.** Los decks y la botonera leen el disco en otro hilo; la
  grabación, el registro, el streaming y la preescucha reciben el audio por colas sin bloqueo.
- **Lo que suena al aire manda.** "Reproducir ahora", el título del stream y la API solo
  consideran lo que va a la salida Main; nada corta el aire sin preguntar.
- **La publicidad no depende de nombres.** Las categorías de la emisora se reconocen por su rol.
- **La emisora es una carpeta.** El ejecutable no guarda estado: mover la carpeta mueve la
  emisora, y los respaldos y restauraciones nunca escriben sobre una base abierta.
- **Toda la interfaz es traducible.** Ninguna cadena visible va en el código; todas pasan por
  `TRANS()` y viven en `Assets/lang_*.txt`.

---

## Pruebas

```bash
ctest --test-dir build -C Release --output-on-failure
```

Más de 600 comprobaciones cubren la base de datos y sus migraciones, el análisis de audio,
las etiquetas, la importación, los decks, la cola, la pauta, la protección contra silencio,
la botonera, el streaming (Ogg, MP3 y títulos), lo que suena, los respaldos (verificación,
retención, destino externo, recuperación de una base dañada), el candado y "reproducir ahora".
Corren en laboratorios temporales, sin tocar la emisora real. La integración continua compila
y prueba cada cambio en Windows.

El ejecutable también puede renderizar la interfaz a un PNG con datos de demostración, sin
abrir dispositivos de audio (así se generan las capturas de este README):

```bash
RAAADIO.exe --ui-snapshot captura.png --size 1366x768 --theme dark --lang es --view ads
```

`--view` acepta `main`, `empty`, `history`, `ads`, `cartwall`, `settings`, `settings-library`,
`settings-broadcast`, `settings-broadcast-2` (la parte de abajo de Emisión), `settings-language`,
`settings-about`, `generator`, `track` e `import`.

---

## Hoja de ruta

- [x] Normalización por LUFS (EBU R 128) y puntos de entrada y salida
- [x] Pauta de 24 horas con tandas automáticas
- [x] Streaming integrado (Icecast y compatibles; MP3 u Ogg Vorbis)
- [x] Lo que suena en JSON (anterior, actual y siguientes), local y enviado a la página web
- [x] Respaldos verificados, programados y en un destino externo (con la música); emisora portable
- [x] Todo el estado de la emisora en la base de datos (sin JSON sueltos)
- [ ] Exportar el registro de emisión para reportes al regulador
- [ ] AAC y conexión cifrada (TLS) al servidor de streaming
- [ ] RadioText (RDS) para el transmisor FM
- [ ] Instalador y compilaciones firmadas

---

## Contribuir

Las contribuciones son bienvenidas, sobre todo de quienes operan emisoras. Lee
[CONTRIBUTING.md](CONTRIBUTING.md) para compilar, probar y proponer cambios; los problemas de
seguridad van por [SECURITY.md](SECURITY.md). Los cambios de cada versión están en
[CHANGELOG.md](CHANGELOG.md).

---

## Licencia

Distribuido bajo licencia MIT. Consulta [LICENSE](LICENSE).

Para transmitir en MP3, RAAADIO carga `libmp3lame.dll`, compilada sin cambios desde
[LAME 3.100](https://lame.sourceforge.io/) (LGPL 2): va aparte del ejecutable, con su licencia
(`libmp3lame-COPYING.txt`), y se puede reemplazar por otra compilación de LAME.

---

<div align="center">

**Hecho para fortalecer la comunicación comunitaria.**

</div>
