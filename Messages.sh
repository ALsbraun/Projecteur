#! /usr/bin/env bash
# SPDX-License-Identifier: MIT

$XGETTEXT src/*.{cc,h} -o "$podir/projecteur.pot"
xgettext --join-existing --from-code=UTF-8 --language=JavaScript --keyword=_ \
    gnome/preferences/*.js gnome/projecteur@projecteur.github.io/*.js -o "$podir/projecteur.pot"
