#!/usr/bin/env bash
# Build distribution-specific RPMs. All output stays in the source workspace.
set -euo pipefail

projecteur_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
projecteur_target=$1
shift
case "$projecteur_target" in
    fedora)
        projecteur_spec="$projecteur_source/packaging/fedora/projecteur-gnome.spec"
        projecteur_topdir="$projecteur_source/build-rpm"
        projecteur_output="$projecteur_source/dist"
        ;;
    opensuse)
        projecteur_spec="$projecteur_source/packaging/opensuse/projecteur.spec"
        projecteur_topdir="$projecteur_source/build-rpm/opensuse"
        projecteur_output="$projecteur_source/dist/opensuse"
        ;;
    *) printf 'Unknown RPM distribution: %s\n' "$projecteur_target" >&2; exit 1 ;;
esac
projecteur_command="./packaging/$projecteur_target/build-rpm.sh"
projecteur_version=$(<"$projecteur_source/VERSION")
projecteur_release=${PROJECTEUR_RPM_RELEASE:-1}
if [[ "$projecteur_target" == opensuse ]]; then
    projecteur_release="$projecteur_release.tw"
fi
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
            printf 'Usage: %s [--desktop gnome|kde|both] [--install-deps]\n' "$projecteur_command"
            printf 'Builds binary and source RPMs. The default builds both editions.\n'
            printf 'Optional --install-deps installs the selected build dependencies.\n'
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
    if [[ "$projecteur_target" == fedora ]]; then
        sudo dnf install -y dnf5-plugins rpm-build cmake gcc-c++ ninja-build
        for desktop in "${projecteur_desktops[@]}"; do
            sudo dnf builddep -y --define "projecteur_desktop $desktop" "$projecteur_spec"
        done
    else
        elevate=()
        if ((EUID != 0)); then elevate=(sudo); fi
        "${elevate[@]}" zypper --non-interactive install --no-recommends rpm-build rpm-config-SUSE
        for desktop in "${projecteur_desktops[@]}"; do
            requirements=$(rpmspec -q --buildrequires \
                --define "projecteur_desktop $desktop" \
                --define "projecteur_version $projecteur_version" "$projecteur_spec")
            mapfile -t packages < <(sed -E 's/[[:space:]]+([<>=]+)[[:space:]]+/\1/g' <<< "$requirements")
            "${elevate[@]}" zypper --non-interactive install --no-recommends "${packages[@]}"
        done
    fi
fi
for projecteur_tool in rpmbuild tar gzip sha256sum; do
    if ! command -v "$projecteur_tool" >/dev/null; then
        printf 'Missing %s. Run %s --install-deps on the target distribution.\n' "$projecteur_tool" "$projecteur_command" >&2
        exit 1
    fi
done

projecteur_dist=$(rpm --eval '%{?dist}')
mkdir -p "$projecteur_topdir"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS,TMP} "$projecteur_output"

# shellcheck source=packaging/source-archive.sh
source "$projecteur_source/packaging/source-archive.sh" "$projecteur_source"

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
