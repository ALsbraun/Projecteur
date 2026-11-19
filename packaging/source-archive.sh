#!/usr/bin/env bash
# Source this file with the source directory as its argument.
projecteur_source=${1:?Missing source directory}

projecteur_git=false
if command -v git >/dev/null && git -C "$projecteur_source" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    projecteur_git=true
    projecteur_epoch=$(git -C "$projecteur_source" log -1 --format=%ct 2>/dev/null || true)
fi
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-${projecteur_epoch:-$(stat -c %Y "$projecteur_source/VERSION")}}

archive_projecteur() {
    local archive=$1
    # Include tracked and non-ignored untracked sources so local changes build
    # before the first commit. Source RPM rebuilds can also run without .git.
    if "$projecteur_git"; then
        git -C "$projecteur_source" ls-files -z --cached --others --exclude-standard | sort -z |
            tar -C "$projecteur_source" --no-recursion --null --files-from=- --sort=name --mtime="@$SOURCE_DATE_EPOCH" \
                --owner=0 --group=0 --numeric-owner --transform="s,^,$archive/," -cf -
    else
        tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 --numeric-owner \
            --exclude='./.git' --exclude='./.codex' --exclude='./.agents' --exclude='./.aws' \
            --exclude='./build*' --exclude='./dist' --exclude='./.cache' --exclude='./.idea' \
            --exclude='./.vscode' --exclude='./.env*' --exclude='__pycache__' --exclude='*.pyc' \
            --exclude='*.key' --exclude='*.pem' --exclude='*.p12' --exclude='*.pfx' --exclude='*.kdbx' \
            --exclude='*.gpg' --exclude='*.asc' --exclude='./secrets' --exclude='./credentials' \
            --transform="s,^\.,$archive," -C "$projecteur_source" -cf - .
    fi
}
