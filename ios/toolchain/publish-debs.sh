#!/usr/bin/env bash
# Publishes the iPad .deb packages that CI built as a public pre-release, so a
# PC downloads them without signing in to GitHub, as Actions artifacts need
# (fetch-debs.ps1 downloads them and installs them with deploy.ps1). Run by
# the "publish" job of ios-ci.yml.
#
#   ios/toolchain/publish-debs.sh <main | PR number> <commit> <folder with .debs>
#
# One rolling pre-release per source: ipad-main for main and ipad-pr-<N> for
# PR N, with a tag of the same name. Each run moves the tag to the commit it
# built, uploads its .debs and removes those it no longer built, so the release
# holds exactly the latest build. A run whose commit is no longer the head of
# main or of the open PR publishes nothing: a newer run publishes after it.
# ios-debs-cleanup.yml deletes a PR's release and tag when the PR closes.
#
# Environment: GH_TOKEN (contents: write), GITHUB_REPOSITORY (owner/repo), and
# optionally GITHUB_SERVER_URL and GITHUB_RUN_ID for the link to the run.
set -euo pipefail

fail() {
	echo "publish-debs: FALLÓ: $*" >&2
	exit 1
}

[ $# -eq 3 ] || fail "uso: $0 <main | número de PR> <commit> <carpeta con .deb>"
source="$1"
commit="$2"
dir="$3"
repo="${GITHUB_REPOSITORY:?falta GITHUB_REPOSITORY}"
[ -n "${GH_TOKEN:-}" ] || fail "falta GH_TOKEN"
[[ "$commit" =~ ^[0-9a-f]{40}$ ]] || fail "$commit no es un commit completo"

if [ "$source" = main ]; then
	tag=ipad-main
	what=main
	title="iPad: main (${commit:0:7})"
	built="de \`main\` en el commit $commit"
	head="$(gh api "repos/$repo/commits/main" --jq .sha)"
	fetch="fetch-debs.ps1"
elif [[ "$source" =~ ^[1-9][0-9]*$ ]]; then
	tag="ipad-pr-$source"
	what="el PR #$source"
	title="iPad: PR #$source (${commit:0:7})"
	built="del PR #$source en el commit $commit, fusionado con \`main\` como en cada PR"
	state="$(gh api "repos/$repo/pulls/$source" --jq '.state + " " + .head.sha')"
	head="${state#open }"
	[ "$head" != "$state" ] || head="cerrado"
	fetch="fetch-debs.ps1 -Pr $source"
else
	fail "$source no es main ni un número de PR"
fi

if [ "$head" != "$commit" ]; then
	echo "publish-debs: $commit ya no es lo último de $what ($head): no se publica"
	exit 0
fi

debs=()
for deb in "$dir"/*.deb; do
	[ -f "$deb" ] && debs+=("$deb")
done
[ ${#debs[@]} -gt 0 ] || fail "no hay ningún .deb en $dir"

notes="$(mktemp)"
trap 'rm -f "$notes"' EXIT
{
	echo "Paquetes para el iPad mini 1 (iOS 9.3.5, armv7) que armó el CI $built."
	if [ -n "${GITHUB_RUN_ID:-}" ]; then
		echo "Corrida: ${GITHUB_SERVER_URL:-https://github.com}/$repo/actions/runs/$GITHUB_RUN_ID"
	fi
	echo
	echo "Es un pre-release automático, no una versión: cada corrida lo reemplaza con su build. Se baja sin iniciar sesión."
	echo
	echo "Para bajarlos e instalarlos en el iPad desde Windows, en la copia del repo (\`-Package <nombre>\` instala uno solo):"
	echo
	echo '```powershell'
	printf '%s\n' "powershell -ExecutionPolicy Bypass -File ios\\toolchain\\$fetch"
	echo '```'
	echo
	echo "| Paquete | sha256 |"
	echo "|---|---|"
	for deb in "${debs[@]}"; do
		echo "| \`${deb##*/}\` | \`$(sha256sum "$deb" | cut -d' ' -f1)\` |"
	done
} > "$notes"

# The tag first, so a release created now points at this commit.
if gh api "repos/$repo/git/ref/tags/$tag" >/dev/null 2>&1; then
	gh api -X PATCH "repos/$repo/git/refs/tags/$tag" -f sha="$commit" -F force=true >/dev/null
else
	gh api -X POST "repos/$repo/git/refs" -f ref="refs/tags/$tag" -f sha="$commit" >/dev/null
fi

if gh release view "$tag" -R "$repo" >/dev/null 2>&1; then
	gh release edit "$tag" -R "$repo" --prerelease --title "$title" --notes-file "$notes" >/dev/null
else
	gh release create "$tag" -R "$repo" --prerelease --latest=false --title "$title" --notes-file "$notes" >/dev/null
fi

echo "==> Subiendo ${#debs[@]} paquete(s) a $tag"
gh release upload "$tag" -R "$repo" --clobber "${debs[@]}"

# What an earlier build left, such as the .deb of a version this one bumped.
keep=" "
for deb in "${debs[@]}"; do
	keep+="${deb##*/} "
done
while IFS= read -r name; do
	if [ -n "$name" ] && [[ "$keep" != *" $name "* ]]; then
		echo "==> Quitando $name, que esta corrida ya no armó"
		gh release delete-asset "$tag" "$name" -R "$repo" --yes
	fi
done < <(gh release view "$tag" -R "$repo" --json assets --jq '.assets[].name')

echo "==> publish-debs: listo, ${GITHUB_SERVER_URL:-https://github.com}/$repo/releases/tag/$tag"
