#!/usr/bin/env bash
# Build Fedora RPMs. All output stays in the source workspace.
set -euo pipefail

projecteur_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
projecteur_spec="$projecteur_source/packaging/fedora/projecteur-gnome.spec"
projecteur_version=$(<"$projecteur_source/VERSION")
projecteur_release=${PROJECTEUR_RPM_RELEASE:-1}
projecteur_jobs=${PROJECTEUR_BUILD_JOBS:-4}
if [[ ! "$projecteur_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'VERSION must contain a semantic version such as 1.0.0.\n' >&2
    exit 1
fi
if [[ ! "$projecteur_release" =~ ^[0-9][a-zA-Z0-9.]*$ ]]; then
    printf 'PROJECTEUR_RPM_RELEASE must start with a digit and contain only letters, digits and dots.\n' >&2
    exit 1
fi
if [[ ! "$projecteur_jobs" =~ ^[1-9][0-9]*$ ]]; then
    printf 'PROJECTEUR_BUILD_JOBS must be a positive integer.\n' >&2
    exit 1
fi

projecteur_desktop=both
projecteur_install_deps=false
while (($#)); do
    case "$1" in
        --desktop)
            if (($# < 2)); then
                printf 'Choose gnome, kde or both after --desktop.\n' >&2
                exit 1
            fi
            projecteur_desktop=$2
            shift
            ;;
        --install-deps)
            projecteur_install_deps=true
            ;;
        --help|-h)
            printf 'Usage: %s [--desktop gnome|kde|both] [--install-deps]\n' "$0"
            printf 'Builds binary and source RPMs into dist/. The default builds both editions.\n'
            printf 'Optional --install-deps uses sudo to install the selected build dependencies.\n'
            printf 'PROJECTEUR_BUILD_JOBS controls the number of build jobs (default: 4).\n'
            printf 'PROJECTEUR_RPM_RELEASE sets the RPM release (default: 1).\n'
            exit 0
            ;;
        *)
            printf 'Unknown argument: %s\n' "$1" >&2
            exit 1
            ;;
    esac
    shift
done

case "$projecteur_desktop" in
    both) projecteur_desktops=(gnome kde) ;;
    gnome|kde) projecteur_desktops=("$projecteur_desktop") ;;
    *) printf 'Choose gnome, kde or both after --desktop.\n' >&2; exit 1 ;;
esac

if "$projecteur_install_deps"; then
    sudo dnf install -y dnf5-plugins rpm-build cmake gcc-c++ ninja-build
    for desktop in "${projecteur_desktops[@]}"; do
        sudo dnf builddep -y --define "projecteur_desktop $desktop" "$projecteur_spec"
    done
fi
for projecteur_tool in rpmbuild tar gzip sha256sum; do
    if ! command -v "$projecteur_tool" >/dev/null; then
        printf 'Missing %s. Run %s --install-deps on Fedora.\n' "$projecteur_tool" "$0" >&2
        exit 1
    fi
done

projecteur_topdir="$projecteur_source/build-rpm"
projecteur_output="$projecteur_source/dist"
projecteur_dist=$(rpm --eval '%{?dist}')
mkdir -p "$projecteur_topdir"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS,TMP} "$projecteur_output"

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

for desktop in "${projecteur_desktops[@]}"; do
    name="projecteur-$desktop"
    archive="$name-$projecteur_version"
    spec="$projecteur_topdir/SPECS/$name.spec"
    # Embed the selected edition and version so the source RPM rebuilds the
    # same package with plain rpmbuild --rebuild.
    {
        printf '%%global projecteur_desktop %s\n' "$desktop"
        printf '%%global projecteur_version %s\n' "$projecteur_version"
        printf '%%global projecteur_release %s\n' "$projecteur_release"
        cat "$projecteur_spec"
    } > "$spec"
    archive_projecteur "$archive" | gzip -n > "$projecteur_topdir/SOURCES/$archive.tar.gz"

    rpmbuild -ba "$spec" \
        --define "_topdir $projecteur_topdir" \
        --define "_tmppath $projecteur_topdir/TMP" \
        --define "_smp_mflags -j$projecteur_jobs" \
        --define "_smp_build_ncpus $projecteur_jobs"

    find "$projecteur_topdir/RPMS" "$projecteur_topdir/SRPMS" \
        -name "$name*-$projecteur_version-$projecteur_release$projecteur_dist.*.rpm" \
        -exec cp -t "$projecteur_output" -- {} +
done
(
    cd "$projecteur_output"
    sha256sum -- *.rpm > SHA256SUMS
)
printf '\nRPMs and checksums: %s\n' "$projecteur_output"
