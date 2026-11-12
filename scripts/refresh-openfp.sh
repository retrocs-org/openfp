#!/bin/sh
# SPDX-License-Identifier: MIT
# Refresh the public site from main without replacing its local configuration.
set -eu

SITE=/srv/www/vhosts/openfp.retrocs.org
REPOSITORY=https://github.com/retrocs-org/openfp.git
BRANCH=main
OWNER=wwwrun:wwwrun
DRY_RUN=

case "${1-}" in
    '') ;;
    --dry-run) DRY_RUN=--dry-run ;;
    *) echo "Usage: $0 [--dry-run]" >&2; exit 1 ;;
esac
[ "$#" -le 1 ] || { echo "Usage: $0 [--dry-run]" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || { echo "Run as root." >&2; exit 1; }
for tool in git rsync php; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "Required command is missing: $tool" >&2
        exit 1
    }
done
[ ! -L "$SITE" ] || {
    echo "Refusing a symlink document root." >&2
    exit 1
}
[ -d "$SITE" ] && [ -f "$SITE/config.inc.php" ] || {
    echo "Existing site and config.inc.php required; site left unchanged." >&2
    exit 1
}

TEMP=$(mktemp -d)
trap 'rm -rf "$TEMP"' 0
trap 'exit 1' HUP INT TERM
git clone --quiet --depth 1 --single-branch --branch "$BRANCH" \
    "$REPOSITORY" "$TEMP/repository"
SOURCE=$TEMP/repository/public-website
for file in index.php inc/site.php inc/migrate.php .htaccess; do
    [ -f "$SOURCE/$file" ] || {
        echo "public-website/$file is missing; site left unchanged." >&2
        exit 1
    }
done

# Validate the new PHP before touching the live files. Do not load the live config.
find "$SOURCE" -type f -name '*.php' -exec sh -c '
    for file do
        php -l "$file" >/dev/null || exit 1
    done
' sh {} +

# Apply ownership and permissions to the incoming files only. Excluded live
# files, including config.inc.php, keep their contents, owner and permissions.
chown -R "$OWNER" "$SOURCE"
find "$SOURCE" -type d -exec chmod 750 {} +
find "$SOURCE" -type f -exec chmod 640 {} +

rsync -a --checksum --safe-links --delete-delay --delay-updates \
    --exclude='/config.inc.php' \
    --exclude='/data/' \
    --exclude='/uploads/' \
    --exclude='/assets/releases/' \
    ${DRY_RUN:+"$DRY_RUN"} --itemize-changes "$SOURCE/" "$SITE/"

if [ -n "$DRY_RUN" ]; then
    echo "Preview only; $SITE was not changed."
else
    echo "Updated $SITE; kept config.inc.php and local data/downloads."
    echo "Next: cd $SITE && php inc/migrate.php up"
fi
