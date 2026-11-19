#!/usr/bin/env bash
# Stable tags must exactly match VERSION. Other builds are ordered prereleases.
set -euo pipefail
version=$(<VERSION)
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || exit 1
[[ ${GITHUB_SHA:-} =~ ^[0-9a-f]{40}$ ]] || exit 1
[[ ${GITHUB_RUN_ID:-} =~ ^[0-9]+$ ]] || exit 1
[[ ${GITHUB_RUN_ATTEMPT:-} =~ ^[0-9]+$ ]] || exit 1
epoch=$(git log -1 --format=%ct)
if [[ ${GITHUB_REF_TYPE:-} == tag ]]; then
    [[ ${GITHUB_REF_NAME:-} == "v$version" ]] || {
        printf 'Release tag must exactly match v%s from VERSION.\n' "$version" >&2
        exit 1
    }
    git merge-base --is-ancestor "$GITHUB_SHA" origin/main
    release=1
    tag="v$version"
    prerelease=false
else
    stamp=$(date -u +%Y%m%d%H%M%S)
    release="0.$stamp.$GITHUB_RUN_ID.$GITHUB_RUN_ATTEMPT.git${GITHUB_SHA:0:12}"
    tag="snapshot-$version-$stamp-$GITHUB_RUN_ID-$GITHUB_RUN_ATTEMPT"
    prerelease=true
fi
{
    printf 'PROJECTEUR_RPM_RELEASE=%s\n' "$release"
    printf 'SOURCE_DATE_EPOCH=%s\n' "$epoch"
} >> "$GITHUB_ENV"
{
    printf 'version=%s\n' "$version"
    printf 'package_release=%s\n' "$release"
    printf 'tag=%s\n' "$tag"
    printf 'prerelease=%s\n' "$prerelease"
} >> "$GITHUB_OUTPUT"
