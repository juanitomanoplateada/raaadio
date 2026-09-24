## Qué problema resuelve

<!-- Para la emisora: qué fallaba o qué no se podía hacer. Enlaza el issue si existe. -->

## Qué cambia

<!-- Un resumen corto de la solución. -->

## Cómo se probó

- [ ] Compila en Release
- [ ] `ctest --test-dir build -C Release --output-on-failure` pasa
- [ ] Si corrige un error, hay una prueba que fallaba sin el cambio
- [ ] Si cambia la interfaz: revisado con `--ui-snapshot` en tema claro y oscuro, en español e inglés y a 1366 × 768

## Checklist

- [ ] Los textos nuevos están en `Assets/lang_es.txt` **y** `Assets/lang_en.txt`
- [ ] El hilo de audio no espera por disco, red ni bloqueos
- [ ] `CHANGELOG.md` actualizado (sección *Sin publicar*)
- [ ] README o capturas de `docs/screenshots/` actualizados si hace falta
