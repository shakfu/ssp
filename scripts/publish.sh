#!/usr/bin/env bash
# Publish releases/shakfu-ssp-plugins-<VERSION>.zip as the GitHub release for tag <VERSION>, with its
# notes as the body.
# Run after `make release` (or release-pdf), a commit, and pushing the tag; this script never
# commits, tags or pushes. It refuses unless the tree is clean, the tag is HEAD and pushed, the
# zip is newer than HEAD, and no release exists for the tag.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

fail() { echo "publish: $*" >&2; exit 1; }

version="$(head -n 1 VERSION)"
tag="$version"  # the bare version, as 0.1.0's tag was
name="shakfu-ssp-plugins-$version"
zip="releases/$name.zip"
notes="releases/$name-notes.md"

[ -f "$zip" ] || fail "no $zip; run make release"
[ -f "$notes" ] || fail "no $notes; run make release-notes"
[ -z "$(git status --porcelain)" ] || fail "uncommitted changes; commit first"
# the zip's modification time; python3 rather than stat, whose flags differ between GNU and BSD
mtime="$(python3 -c 'import os, sys; print(int(os.path.getmtime(sys.argv[1])))' "$zip")"
[ "$mtime" -ge "$(git log -1 --format=%ct)" ] || fail "$zip is older than HEAD; run make release"

git rev-parse -q --verify "refs/tags/$tag" >/dev/null || fail "no tag $tag; git tag $tag && git push origin $tag"
[ "$(git rev-parse "$tag^{commit}")" = "$(git rev-parse HEAD)" ] || fail "tag $tag is not HEAD"
git ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null || fail "tag $tag is not on origin; git push origin $tag"

command -v gh >/dev/null || fail "gh not found"
! gh release view "$tag" >/dev/null 2>&1 || fail "a release for $tag exists already"

gh release create "$tag" "$zip" --title "shakfu-ssp-plugins $version" --notes-file "$notes" --verify-tag
