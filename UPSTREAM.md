# Upstream

Este repo parte de [mauricio-gg/vitaki-vitarps5](https://github.com/mauricio-gg/vitaki-vitarps5) (VitaRPS5), importado con todo su historial.

- **Commit base:** `ceeb0578fca5566c8cfaa36d0730807ce37beee0` (2026-10-21), "fix(ui): vblank wait on except while the stream is shown, plus UI/SWAPS tear probe (#442) (#443)".
- **Linaje:** Chiaki → Vitaki de AAGaming → vitaki-fork de ywnico → VitaRPS5 → este repo. Los créditos y la licencia AGPLv3 de todos ellos se conservan (`COPYING`, `LICENSES/`, encabezados SPDX).
- **Rama `ywnico`:** es el punto donde GitHub creó el fork (vitaki-fork de ywnico, `f2c8922`). Se conserva como referencia; el desarrollo va en `main`.

## Traer arreglos de upstream

```sh
git remote add upstream https://github.com/mauricio-gg/vitaki-vitarps5.git   # una sola vez
git fetch upstream main
git merge upstream/main
```

Lo hace A0 al cierre de cada oleada (actividad A0.4). Al resolver conflictos:

- `CLAUDE.md`, `UPSTREAM.md`, `ios/`, `bridge/` y `docs/ipad/` son nuestros: se queda nuestra versión.
- `lib/`: se queda la de upstream y se vuelven a aplicar nuestros parches de iOS, que deben ser pequeños y estar marcados.
- Lo demás (`vita/`, `docs/design/`, `tools/`, `.github/`): se queda la de upstream.

Anota aquí el nuevo commit base después de cada merge.
