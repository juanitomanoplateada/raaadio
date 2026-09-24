# Política de seguridad

RAAADIO corre en la máquina de aire de una emisora, así que un fallo de seguridad puede
sacarla del aire o exponer sus datos. Gracias por avisar con cuidado.

## Cómo reportar una vulnerabilidad

**No abras un *issue* público.** Usa el reporte privado de GitHub:
pestaña *Security* del repositorio → *Report a vulnerability*. Incluye la versión, los
pasos para reproducirlo y el impacto que ves. Responderemos lo antes posible y te
avisaremos cuando haya una corrección publicada.

## Versiones con soporte

Se corrigen los problemas de seguridad en la última versión publicada y en la rama
principal.

## Qué cuida RAAADIO por diseño

- La **API local** (`/nowplaying` y `/nowplaying.json`) escucha solo en `127.0.0.1`: no abre
  la máquina de aire a la red. Para exponerla hace falta un proxy inverso, a propósito. Hacia
  la página web, RAAADIO solo *envía* (POST a la dirección configurada); no recibe nada.
- La **clave del servidor de streaming** se guarda cifrada con DPAPI para el usuario de
  Windows; no queda en claro ni en `Config/station.json` ni en los respaldos.
  Al transmitir viaja como en BUTT o cualquier codificador Icecast: en la cabecera
  `Authorization`, sin TLS. Usa una clave exclusiva para el streaming.
- `libmp3lame.dll` se carga solo desde la carpeta del ejecutable, nunca desde la ruta de
  búsqueda del sistema.
- La base de datos usa **sentencias preparadas** en todas las consultas.
- Restaurar un respaldo verifica que sea una base de datos válida y **respalda lo actual**
  antes de reemplazarlo.
