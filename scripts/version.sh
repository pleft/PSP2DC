#!/bin/sh
# The name of what is being built (the PSP's Settings > Details shows it):
#   $PSP2DC_RELEASE when set (the release workflow: the tag without its v)
#   a release's (1.0.0) on a clean checkout of a tag v1.0.0
#   else "dev" and the commit, with a + when there are changes not committed
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
[ -n "$PSP2DC_RELEASE" ] && { echo "${PSP2DC_RELEASE#v}"; exit 0; }
DIRTY=
[ -z "$(git status --porcelain 2>/dev/null)" ] || DIRTY=+
TAG="$(git describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null || true)"
if [ -n "$TAG" ] && [ -z "$DIRTY" ]; then echo "${TAG#v}"; else echo "dev $(git rev-parse --short HEAD 2>/dev/null || echo unknown)$DIRTY"; fi
