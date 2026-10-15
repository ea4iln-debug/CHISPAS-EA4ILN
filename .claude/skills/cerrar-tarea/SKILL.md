---
name: cerrar-tarea
description: Rutina de fin de tarea en Chispas - compilar, resumir lo probado en placa y lo razonado, actualizar CLAUDE.md y docs/NOTAS.md, commit y push. Usar al terminar cualquier cambio de código o documentación, o cuando el usuario diga que la tarea está acabada.
---

# Cerrar una tarea de Chispas

Pasos, en este orden. Si uno falla, parar y decirlo; no saltarlo.

1. **Compilar** (solo si se ha tocado código, `include/` o `platformio.ini`): `./compilar.sh`. Tiene que salir OK
   y sin avisos nuevos de `src/`. Anotar RAM y flash si cambian de forma notable (más de ~1 %).
2. **Revisar el diff** con `git diff` (y `git status` para archivos nuevos): solo lo que pedía la tarea, sin cambios
   de formato sueltos, tildes intactas, archivos de código nuevos con cabecera SPDX
   (`// SPDX-License-Identifier: GPL-3.0-or-later` y `// Copyright (C) 2026 EA4ILN`), y cada `.inc` nuevo con su
   `#include` en `main.cpp`.
3. **Documentar:**
   - `docs/NOTAS.md`: en la sección del tema, el detalle con fecha (qué se cambió, por qué, cómo se probó, medidas).
     Fechas absolutas ("13 oct 2026"), nunca "hoy" o "ayer".
   - `CLAUDE.md`: solo el resumen. Actualizar "Estado actual" (fecha y versión) y "Pendiente" (quitar lo hecho,
     añadir lo nuevo). Mantenerlo corto: si algo es detalle, va a NOTAS.
   - Si cambia el manejo visible del aparato, también `README.md`.
4. **Commit** en castellano, una línea que diga qué cambia (estilo de `git log`), con la línea de coautoría que
   indique el sistema. Un commit por tarea; no mezclar tareas distintas.
5. **Push**: `git push`, sin preguntar al usuario. No subir `FW_VERSION` ni publicar release sin preguntar (ver la skill
   `release`).
6. **Resumen al usuario**, separando:
   - **Probado en placa:** lo que se ha visto funcionar (serie, pantalla, audio, LED), con qué placa.
   - **Razonado / sin probar:** lo que solo compila o se deduce del código.
   - Lo que queda pendiente, si algo.
