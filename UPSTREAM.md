# Upstream

Este repo parte de [mauricio-gg/vitaki-vitarps5](https://github.com/mauricio-gg/vitaki-vitarps5) (VitaRPS5), importado con todo su historial.

- **Commit base:** `ceeb0578fca5566c8cfaa36d0730807ce37beee0` (2026-10-21), "fix(ui): vblank wait on except while the stream is shown, plus UI/SWAPS tear probe (#442) (#443)".
- **Linaje:** Chiaki → Vitaki de AAGaming → vitaki-fork de ywnico → VitaRPS5 → este repo. Los créditos y la licencia AGPLv3 de todos ellos se conservan (`COPYING`, `LICENSES/`, encabezados SPDX).
- **Única fuente de upstream:** VitaRPS5. No se trae nada directamente de vitaki-fork.

## Traer arreglos de upstream

```sh
git remote add upstream https://github.com/mauricio-gg/vitaki-vitarps5.git   # una sola vez
git fetch upstream main
git merge upstream/main
```

Lo hace A0 al cierre de cada oleada (actividad A0.4). Al resolver conflictos:

- `README.md`, `CLAUDE.md`, `UPSTREAM.md`, `.github/pull_request_template.md`, `ios/`, `bridge/` y `docs/ipad/` son nuestros: se queda nuestra versión.
- `.github/workflows/create_release.yml` lo quitamos: publicaba versiones `.vpk` de VitaRPS5 desde este repo. Si upstream lo cambia, sigue borrado. Las versiones del iPad tendrán su propio flujo.
- `lib/`: se queda la de upstream y se vuelven a aplicar nuestros parches de iOS, que deben ser pequeños y estar marcados.
- Lo demás (`vita/`, `docs/design/`, `tools/`, `.github/`): se queda la de upstream, salvo `.github/FUNDING.yml` y `.github/ISSUE_TEMPLATE/bug_report.md`, que quitamos porque son del proyecto de Vita (el botón de donación de su autor y una plantilla de errores con rutas de la Vita). Si upstream los cambia, siguen borrados. En `.github/workflows/claude-code-review.yml` se conserva nuestro salto cuando no hay `CLAUDE_CODE_OAUTH_TOKEN`.

Anota aquí el nuevo commit base después de cada merge.
