#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
"""Fails when a protected word shows up in what goes to GitHub.

The repo never names its owner (CLAUDE.md: "Sin nombres reales del dueño").
This looks for the owner's first name, and for any word listed in the
CK_PRIVATE_WORDS environment variable (the repo secret of the same name in
CI), in:

  --tree         every tracked file and every tracked path
  --range A..B   the messages and author names of the commits in A..B
  --event FILE   the title and body of the pull request in GitHub's event JSON

The built-in words are kept as SHA-256 hashes, so this file does not publish
what it protects. A word matches whole, ignoring case and accents, and
camelCase is split: "Word", "WORD's" and "WordOther" match, while base64 or hex
that happens to hold the same letters does not. A finding names where it is,
never the word or the line.

  .github/quality/private_words.py --tree --range origin/main..HEAD
  .github/quality/private_words.py --hash palabra   # a hash for PROTECTED
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import unicodedata

# sha256 of the word in lowercase, without accents: description.
PROTECTED = {
    "4183b9f5ed14b64d012ce1e728cfa1e7afc399cb82b6729b222784db6b1a50a7": "el nombre del dueño",
}

WORD = re.compile(r"[^\W\d_]+")
CAMEL = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+")
MAX_FILE_SIZE = 16 * 1024 * 1024


def fold(text):
    """Drops accents, so "Ñandú" reads as "Nandu"."""
    decomposed = unicodedata.normalize("NFKD", text)
    return "".join(c for c in decomposed if not unicodedata.combining(c))


def digest(word):
    return hashlib.sha256(fold(word).lower().encode("utf-8")).hexdigest()


def load_words():
    words = dict(PROTECTED)
    for word in re.split(r"[,\n]", os.environ.get("CK_PRIVATE_WORDS", "")):
        if word.strip():
            words[digest(word.strip())] = "una palabra de CK_PRIVATE_WORDS"
    return words


class Finder:
    def __init__(self, words):
        self.words = words
        self.findings = []

    def match(self, text):
        """Descriptions of the protected words in text."""
        found = []
        for run in WORD.findall(fold(text)):
            candidates = [run]
            parts = CAMEL.findall(run)
            if len(parts) > 1:
                candidates += parts
            for word in candidates:
                what = self.words.get(hashlib.sha256(word.lower().encode("utf-8")).hexdigest())
                if what:
                    found.append(what)
        return found

    def check(self, text, where):
        """where names the line with {line}, or gets ":line" appended."""
        if "{line}" not in where:
            where += ":{line}"
        for lineno, line in enumerate(text.splitlines(), 1):
            for what in self.match(line):
                self.findings.append(f"{where.format(line=lineno)}: {what}")

    def check_one(self, text, where):
        for what in self.match(text):
            self.findings.append(f"{where}: {what}")


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True).stdout


def check_tree(finder):
    paths = [p for p in git("ls-files", "-z").decode("utf-8", "replace").split("\0") if p]
    for path in paths:
        finder.check_one(path, f"la ruta {path}")
        if not os.path.isfile(path) or os.path.islink(path):
            continue
        if os.path.getsize(path) > MAX_FILE_SIZE:
            continue
        with open(path, "rb") as f:
            data = f.read()
        if b"\0" in data[:8192]:
            continue
        finder.check(data.decode("utf-8", "replace"), path)
    return len(paths)


def check_range(finder, rev_range):
    fields = "%H%x00%an%x00%cn%x00%B%x1e"
    out = git("log", f"--format={fields}", rev_range).decode("utf-8", "replace")
    count = 0
    for entry in out.split("\x1e"):
        entry = entry.strip("\n")
        if not entry:
            continue
        sha, author, committer, message = entry.split("\0", 3)
        short = sha[:10]
        finder.check_one(author, f"el autor del commit {short}")
        finder.check_one(committer, f"quien hizo el commit {short}")
        finder.check(message, f"el mensaje del commit {short} (línea {{line}})")
        count += 1
    return count


def check_event(finder, path):
    with open(path, encoding="utf-8") as f:
        event = json.load(f)
    pr = event.get("pull_request")
    if not pr:
        return False
    finder.check_one(pr.get("title") or "", f"el título del PR #{pr.get('number')}")
    finder.check(pr.get("body") or "", f"la descripción del PR #{pr.get('number')} (línea {{line}})")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tree", action="store_true", help="every tracked file and path")
    parser.add_argument("--range", help="commits A..B: messages and author names")
    parser.add_argument("--event", help="GitHub event JSON: the PR's title and body")
    parser.add_argument("--hash", help="print the hash of a word for PROTECTED and exit")
    args = parser.parse_args()

    if args.hash:
        print(digest(args.hash))
        return 0

    finder = Finder(load_words())
    if args.tree:
        print(f"private_words: {check_tree(finder)} archivos y rutas")
    if args.range:
        print(f"private_words: {check_range(finder, args.range)} commits en {args.range}")
    if args.event and check_event(finder, args.event):
        print("private_words: título y descripción del PR")

    if not finder.findings:
        print("private_words: sin palabras protegidas")
        return 0
    print("private_words: FALLÓ, hay palabras protegidas en:", file=sys.stderr)
    for finding in finder.findings:
        print(f"  {finding}", file=sys.stderr)
    print(
        "Si es el nombre del dueño, escribe TheHuAx. En un archivo, corrígelo en un commit nuevo;\n"
        "en un commit, reescribe el mensaje (git commit --amend o git rebase) y sube la rama\n"
        "con git push --force-with-lease; en el PR, edita el título o la descripción.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
