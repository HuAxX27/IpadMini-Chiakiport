# Validaciones de calidad

`check.sh` revisa lo que un PR no debe meter a `main`: secretos, datos de PSN, el nombre del dueño, código que el analizador marca como roto y scripts mal escritos. Corre en cada PR y en cada push a `main` (`.github/workflows/ios-quality.yml`), junto al build del iPad y las pruebas de Linux de `ios-ci.yml`. Es el mismo script en una PC, así que conviene correrlo antes de subir una rama.

| Paso | Qué revisa | Job del CI |
|---|---|---|
| `secrets` | gitleaks con `.gitleaks.toml`: sus reglas (llaves, tokens, llaves privadas) más el Account ID de PSN, las llaves de registro (`rp_regist_key`, `rp_key`), el PIN de registro y la contraseña de Wi-Fi del puente. En los commits del PR y en el árbol completo. | `secrets and private data` |
| `private` | El nombre del dueño y las palabras del secreto `CK_PRIVATE_WORDS` en archivos, rutas, mensajes y autores de commits, y en el título y la descripción del PR. Que no entren `.deb`, `.ipa`, librerías, el SDK de iOS, `sdkconfig` ni archivos de más de 2 MB. Que ningún makefile de Theos apague `-Werror`. Espacios al final de línea y marcadores de conflicto en lo que cambia el PR. | `secrets and private data` |
| `analysis` | cppcheck en el C de `ios/` y `bridge/`, y el analizador estático de clang (`scan-build`) sobre todo lo que compilan las pruebas de Linux de `ios/` y `bridge/`. `lib/` queda fuera: es upstream. | `static analysis (ios, bridge)` |
| `lint` | shellcheck en los scripts de `ios/`, `bridge/` y `.github/`, actionlint en los workflows y que el Python se pueda leer. | `lint (shell, workflows, python)` |

Las advertencias del compilador ya rompen el build: Theos compila con `-Wall -Werror`, `ci-ipad.sh` le suma `-Werror=unguarded-availability` a cada paquete para cuidar el piso de iOS 9.0, y las pruebas de Linux usan `-Wall -Wextra -Werror` con ASan y UBSan.

## Correrlo en una PC

En Ubuntu 24.04, una vez:

```sh
sudo apt-get install -y shellcheck cppcheck clang-tools
.github/quality/install-tools.sh   # gitleaks y actionlint, con el sha256 de tools.env
```

Antes de subir una rama:

```sh
.github/quality/check.sh                  # todos los pasos, sobre lo que la rama suma a origin/main
.github/quality/check.sh secrets private  # solo algunos
```

Un paso al que le falta su herramienta dice `SKIP`. En el CI falla, porque ahí `CK_REQUIRE_TOOLS=1`. El rango es `CK_BASE..CK_HEAD`; por defecto, lo que `HEAD` suma a `origin/main`.

## Cuando algo falla

- **Un secreto o un dato de PSN.** Subir la rama ya lo publicó, aunque el CI la frene: GitHub guarda la rama y sus commits. Primero cambia lo que se filtró (una contraseña nueva, o borrar el registro del iPad en la PS5 y registrarlo otra vez si fue una llave o el PIN). Luego saca el dato del historial de la rama (`git rebase` o `git commit --amend`, y `git push --force-with-lease`). Si es un falso positivo, agrega `gitleaks:allow` en un comentario de esa línea, o su huella (`Fingerprint` en el log) a un archivo `.gitleaksignore` en la raíz, y explícalo en el PR. En las pruebas usa valores falsos: las carpetas `test/` y `tests/` están permitidas para las reglas de PSN.
- **El nombre del dueño.** Escribe `TheHuAx`. Si está en un mensaje de commit, reescríbelo y sube la rama con `--force-with-lease`; si está en el PR, edita el título o la descripción y el job vuelve a correr solo.
- **cppcheck.** Arregla el código. Si es un falso positivo, `// cppcheck-suppress <id>` en la línea anterior.
- **scan-build.** El log dice archivo, línea y tipo de error; las páginas HTML quedan en el artefacto `scan-build` del run (en una PC, en `build-quality/scan-build`). Arregla el código: si es un falso positivo, cámbialo para que el analizador vea por qué no pasa, como en `ck_h264.c`.
- **shellcheck.** `# shellcheck disable=SCxxxx` justo antes de la línea, con el motivo.

## Palabras protegidas

`private_words.py` guarda el nombre del dueño como hash SHA-256, así el repo no publica lo que protege. Para proteger otra palabra (un apellido, un nombre de usuario de PSN) sin escribirla en el repo, agrégala al secreto `CK_PRIVATE_WORDS` del repo (Settings > Secrets and variables > Actions > New repository secret), separada por comas. Para que quede fija en el script, `private_words.py --hash palabra` da el hash que va en `PROTECTED`.

Las coincidencias son por palabra completa, sin importar mayúsculas ni acentos, y separando camelCase. El log dice dónde está, nunca la palabra ni la línea.

## Actualizar gitleaks o actionlint

Cambia la versión y el sha256 en `tools.env` (el sha256 del `.tar.gz` de Linux x86_64 de la release). `install-tools.sh` falla y dice el sha256 que bajó si no coincide.
