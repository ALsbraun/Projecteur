#!/usr/bin/env bash
# Build the GNOME and Plasma binary packages on Ubuntu 26.10.
set -euo pipefail

projecteur_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
version=$(<"$projecteur_source/VERSION")
revision=${PROJECTEUR_DEB_REVISION:-1}
jobs=${PROJECTEUR_BUILD_JOBS:-4}
desktop=both
install_deps=false

while (($#)); do
    case "$1" in
        --desktop)
            if (($# < 2)); then printf 'Choose gnome, kde or both.\n' >&2; exit 1; fi
            desktop=$2
            shift
            ;;
        --install-deps) install_deps=true ;;
        --help|-h)
            printf 'Usage: %s [--desktop gnome|kde|both] [--install-deps]\n' "$0"
            exit 0
            ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 1 ;;
    esac
    shift
done

case "$desktop" in
    gnome) profile=pkg.projecteur.gnome-only ;;
    kde) profile=pkg.projecteur.kde-only ;;
    both) profile= ;;
    *) printf 'Choose gnome, kde or both.\n' >&2; exit 1 ;;
esac
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ||
      ! "$revision" =~ ^[0-9][a-zA-Z0-9.+~]*$ ||
      ! "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    printf 'Check VERSION, PROJECTEUR_DEB_REVISION and PROJECTEUR_BUILD_JOBS.\n' >&2
    exit 1
fi

# shellcheck source=packaging/source-archive.sh
source "$projecteur_source/packaging/source-archive.sh" "$projecteur_source"
build_root="$projecteur_source/build-ubuntu"
dist="$projecteur_source/dist/ubuntu26.10"
mkdir -p "$build_root" "$dist"
work=$(mktemp -d "$build_root/packages.XXXXXX")
source_name="projecteur-$version"
archive_projecteur "$source_name" | tar -xf - -C "$work"
cd "$work/$source_name"
cp -a packaging/ubuntu/debian ./debian
package_version="$version-$revision~ubuntu26.10"
printf 'projecteur (%s) stonking; urgency=medium\n\n  * Package Projecteur for Ubuntu 26.10.\n\n -- Projecteur contributors <stefan.braun@ergon.ch>  %s\n' \
    "$package_version" "$(LC_ALL=C date -R -d "@$SOURCE_DATE_EPOCH")" > debian/changelog

profile_args=()
if [[ -n "$profile" ]]; then profile_args=("--build-profiles=$profile"); fi
if "$install_deps"; then
    elevate=()
    if ((EUID != 0)); then elevate=(sudo); fi
    "${elevate[@]}" apt-get update
    "${elevate[@]}" apt-get -y --no-install-recommends build-dep "${profile_args[@]}" .
fi
dpkg-buildpackage -b -us -uc -j"$jobs" "${profile_args[@]}"
find "$work" -maxdepth 1 -name '*.deb' -exec cp -t "$dist" -- {} +
(
    cd "$dist"
    sha256sum -- *.deb > SHA256SUMS
)
printf '\nUbuntu packages: %s\n' "$dist"
