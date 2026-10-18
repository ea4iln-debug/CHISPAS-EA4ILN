---
name: cerrar-tarea
description: Rutina de fin de tarea en Chispas - compilar, resumir lo probado en placa y lo razonado, actualizar CLAUDE.md y docs/NOTAS.md, commit y push, y preguntar si se publica versión. Usar al terminar cualquier cambio de código o documentación, o cuando el usuario diga que la tarea está acabada.
---

# Cerrar una tarea de Chispas

Pasos, en este orden, sin pedir confirmación (la única pregunta es la del paso 6). Si uno falla, parar, arreglarlo
o decirlo; no saltarlo.

1. **Compilar** (solo si se ha tocado código, `include/` o `platformio.ini`): `./compilar.sh`. Tiene que salir OK
   y sin avisos nuevos de `src/`. Anotar RAM y flash si cambian de forma notable (más de ~1 %).
2. **Revisar el diff** con `git status` y `git diff --stat`, y el diff entero de lo que no se haya escrito con Edit:
   solo lo que pedía la tarea, tildes intactas, **`FW_VERSION` sin tocar** (salvo en la skill `release`), archivos de
   código nuevos con cabecera SPDX (`// SPDX-License-Identifier: GPL-3.0-or-later` y `// Copyright (C) 2026 EA4ILN`)
   y cada `.inc` nuevo con su `#include` en `main.cpp`. Nada de `dist/`, `.pio/` ni archivos del scratchpad.
3. **Documentar:**
   - `docs/NOTAS.md`: en la sección del tema, el detalle con fecha (qué se cambió, por qué, cómo se probó, medidas).
     Fechas absolutas ("13 oct 2026"), nunca "hoy" o "ayer".
   - `CLAUDE.md`: solo el resumen. Actualizar "Estado actual" y "Pendiente" (quitar lo hecho, añadir lo nuevo).
     Mantenerlo corto: si algo es detalle, va a NOTAS; al añadir una entrada, resumir o quitar la más vieja.
   - Si cambia el manejo visible del aparato: `README.md` y el manual (skill `manual`, con su PDF).
4. **Commit** en castellano, una línea que diga qué cambia (estilo de `git log`), con la línea de coautoría que
   indique el sistema: `git commit -q -m "<resumen>" -m "Co-Authored-By: ..."` (sin `$(cat <<EOF)` ni `cd ... &&`
   delante, que obligan a pedir permiso). Un commit por tarea; no mezclar tareas distintas.
5. **Push**: `git push -q`.
6. **Publicar:** si la tarea deja firmware nuevo probado en placa y sin publicar, hacer la pregunta de la skill
   `release` (paso 0). Es la única pregunta al usuario; si contesta que sí, seguir con esa skill.
7. **Resumen al usuario**, corto, separando:
   - **Probado en placa:** lo que se ha visto funcionar (serie, pantalla, audio, LED), con qué placa.
   - **Razonado / sin probar:** lo que solo compila o se deduce del código.
   - **Decidido por Claude:** las elecciones de diseño que se han hecho sin preguntar, para que el usuario las
     corrija si no le gustan.
   - Lo que queda pendiente, si algo.
