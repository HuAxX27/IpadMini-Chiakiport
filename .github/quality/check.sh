#!/usr/bin/env bash
# The port's quality checks, the same on a PC before pushing and in CI
# (.github/workflows/ios-quality.yml):
#
#   secrets   gitleaks with .gitleaks.toml (its own rules plus PSN data and
#             the bridge's Wi-Fi password) on the range's commits and on the
#             tracked tree.
#   private   the owner's name and the CK_PRIVATE_WORDS words in the tree, the
#             range's commits and the PR's title and body (private_words.py);
#             what may be committed (no .deb, SDK, sdkconfig or file over
#             2 MB); Theos makefiles that keep -Werror; whitespace errors and
#             conflict markers in what the range changes.
#   analysis  cppcheck on the C of ios/ and bridge/, and clang's static
#             analyzer (scan-build) on everything the Linux tests of ios/ and
#             bridge/ compile, lib/ aside.
#   lint      shellcheck on the scripts of ios/, bridge/ and .github/,
#             actionlint on the workflows and a parse of the Python.
#
#   .github/quality/check.sh                  # every step
#   .github/quality/check.sh secrets private  # some steps
#
# The range is CK_BASE..CK_HEAD, by default what HEAD adds to origin/main.
# Without a base (a new branch pushed, or no origin/main) only the tree is
# checked. A step whose tool is missing prints SKIP, unless CK_REQUIRE_TOOLS=1,
# as in CI, where it fails. Any failure exits non-zero.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="$root/build-quality"
cd "$root"

head="${CK_HEAD:-HEAD}"
base="${CK_BASE-}"
if [ -z "${CK_BASE+set}" ]; then
	base="$(git merge-base origin/main "$head" 2>/dev/null || true)"
fi
if [[ "$base" =~ ^0+$ ]] || ! git rev-parse -q --verify "${base:-none}^{commit}" >/dev/null; then
	base=""
fi
range="${base:+$base..$head}"

failed=()
fail() {
	echo "check: FALLÓ: $*" >&2
	[[ " ${failed[*]} " == *" $1 "* ]] || failed+=("$1")
}

# need STEP TOOL: whether TOOL is installed; SKIP or a failure when it is not.
need() {
	command -v "$2" >/dev/null && return 0
	if [ "${CK_REQUIRE_TOOLS:-0}" = 1 ]; then
		fail "$1" "falta $2"
	else
		echo "SKIP: $1 necesita $2 (.github/quality/README.md dice cómo instalarlo)"
	fi
	return 1
}

step_secrets() {
	echo "==> secrets: gitleaks"
	need secrets gitleaks || return 0
	local gitleaks=(gitleaks --config "$root/.gitleaks.toml" --no-banner --redact -v --log-level warn)
	if [ -n "$range" ]; then
		echo "    commits de $range"
		"${gitleaks[@]}" git --log-opts="$range" . || fail secrets "gitleaks encontró secretos en los commits de $range"
	fi
	# The tracked tree only, not what a local build left next to it.
	rm -rf "$out/tree"
	mkdir -p "$out/tree"
	git archive HEAD | tar -x -C "$out/tree"
	echo "    árbol de HEAD"
	(cd "$out/tree" && "${gitleaks[@]}" dir .) || fail secrets "gitleaks encontró secretos en el árbol"
	rm -rf "$out/tree"
}

step_private() {
	echo "==> private: nombre del dueño y palabras protegidas"
	local args=(--tree)
	[ -n "$range" ] && args+=(--range "$range")
	[ -n "${GITHUB_EVENT_PATH:-}" ] && [ -f "$GITHUB_EVENT_PATH" ] && args+=(--event "$GITHUB_EVENT_PATH")
	python3 "$here/private_words.py" "${args[@]}" || fail private "palabras protegidas"

	echo "==> private: makefiles de Theos con -Werror"
	local easy
	easy="$(git grep -nE 'GO_EASY_ON_ME|-Wno-error([^=]|$)' -- 'ios/**/Makefile' 'ios/**/*.mk' || true)"
	if [ -n "$easy" ]; then
		echo "$easy" >&2
		fail private "un makefile de ios/ apaga -Werror: arregla la advertencia o apágala sola con -Wno-<nombre>"
	fi

	[ -n "$range" ] || return 0
	echo "==> private: qué se agrega en $range"
	local path size bad=0
	while IFS= read -r -d '' path; do
		case "$path" in
			*.deb | *.ipa | *.dylib | *.a | *.o | *.p12 | *.pfx | *.mobileprovision | *.tbd | *.sdk/* | *.sdk.tar* \
				| */sdkconfig | sdkconfig | */.theos/*)
				echo "    $path: no se sube (paquetes, binarios, el SDK de iOS y sdkconfig quedan fuera del repo)" >&2
				bad=1
				continue
				;;
		esac
		size="$(git cat-file -s "$head:$path" 2>/dev/null || echo 0)"
		if [ "$size" -gt $((2 * 1024 * 1024)) ]; then
			echo "    $path: $((size / 1024)) KB, más de 2 MB" >&2
			bad=1
		fi
	done < <(git diff -z --name-only --diff-filter=AM "$base...$head")
	[ "$bad" = 0 ] || fail private "archivos que no van en el repo; los .deb y el firmware van como artefactos del CI"

	echo "==> private: espacios al final y marcadores de conflicto en $range"
	git diff --check "$base...$head" \
		-- ios bridge docs/ipad .github CLAUDE.md .gitleaks.toml \
		|| fail private "espacios al final de línea o marcadores de conflicto (git diff --check)"
}

step_analysis() {
	echo "==> analysis: cppcheck en ios/ y bridge/"
	if need analysis cppcheck; then
		# Same checks as the inherited Vita job (lint-format.yml). ios/tests/core
		# is a copy of Chiaki's tests; the ESP32 code is checked as Bluepad32's
		# custom platform, the one it builds for.
		cppcheck --enable=warning,performance,portability --std=c11 --language=c \
			--inline-suppr --suppressions-list=.cppcheck-suppressions --error-exitcode=1 -q \
			-I lib/include -I ios -I ios/Input -I ios/Video -I bridge/protocol -I bridge/core \
			-i ios/tests/core -DCONFIG_BLUEPAD32_PLATFORM_CUSTOM=1 \
			ios bridge || fail analysis "cppcheck"
	fi

	echo "==> analysis: analizador estático de clang (scan-build)"
	local scan_build
	# The Perl scan-build: scan-build-py fails on these suites and reports nothing.
	scan_build="$(compgen -c | grep -E '^scan-build(-[0-9]+)?$' | sort -V | tail -n 1 || true)"
	if [ -z "$scan_build" ]; then
		need analysis scan-build || return 0
	fi
	rm -rf "$out/scan-build"
	local suite
	for suite in ios/tests/input_tests.sh ios/tests/ck_h264_tests.sh bridge/tests/run_tests.sh; do
		echo "    $suite"
		# The suites build with $CC, which scan-build points at its analyzer;
		# --status-bugs fails on any report. The core (lib/) is upstream.
		if ! "$scan_build" --status-bugs --use-cc="${CC:-cc}" --exclude "$root/lib" \
			-o "$out/scan-build" "./$suite" >"$out/scan-build.log" 2>&1; then
			grep -E 'warning:|error:|scan-build:' "$out/scan-build.log" >&2 || tail -n 20 "$out/scan-build.log" >&2
			fail analysis "scan-build en $suite"
		elif ! grep -q 'scan-build: No bugs found' "$out/scan-build.log"; then
			tail -n 20 "$out/scan-build.log" >&2
			fail analysis "scan-build no terminó su análisis de $suite"
		fi
	done
}

step_lint() {
	echo "==> lint: shellcheck"
	if need lint shellcheck; then
		local scripts=()
		mapfile -d '' -t scripts < <(git ls-files -z 'ios/*.sh' 'bridge/*.sh' '.github/*.sh')
		# SC2054: commas inside compiler flags (-fsanitize=address,undefined) are meant.
		shellcheck -x -P SCRIPTDIR -S warning -e SC2054 "${scripts[@]}" || fail lint "shellcheck"
	fi

	echo "==> lint: actionlint"
	if need lint actionlint; then
		actionlint || fail lint "actionlint"
	fi

	echo "==> lint: Python"
	local py=()
	mapfile -d '' -t py < <(git ls-files -z 'ios/*.py' 'bridge/*.py' '.github/*.py')
	if [ "${#py[@]}" -gt 0 ]; then
		python3 -c 'import ast, sys
for path in sys.argv[1:]:
    with open(path, "rb") as f:
        ast.parse(f.read(), path)' "${py[@]}" || fail lint "Python que no se puede leer"
	fi
}

steps=("$@")
[ "${#steps[@]}" -gt 0 ] || steps=(secrets private analysis lint)
mkdir -p "$out"
echo "check: rango ${range:-ninguno (solo el árbol)}"
for step in "${steps[@]}"; do
	case "$step" in
		secrets | private | analysis | lint) "step_$step" ;;
		*)
			echo "check: paso desconocido: $step (secrets, private, analysis, lint)" >&2
			exit 2
			;;
	esac
done

if [ "${#failed[@]}" -gt 0 ]; then
	echo "check: FALLÓ en: ${failed[*]}" >&2
	exit 1
fi
echo "check: listo"
