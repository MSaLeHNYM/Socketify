#!/usr/bin/env bash
# Bump Socketify SemVer in the root VERSION file.
#
# Usage:
#   ./scripts/bump-version.sh patch   # 0.2.0 -> 0.2.1
#   ./scripts/bump-version.sh minor   # 0.2.1 -> 0.3.0
#   ./scripts/bump-version.sh major   # 0.3.0 -> 1.0.0 (requires typing "yes")
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION_FILE="${ROOT}/VERSION"

usage() {
    echo "Usage: $0 {patch|minor|major}" >&2
    exit 2
}

[[ $# -eq 1 ]] || usage
KIND="$1"

if [[ ! -f "${VERSION_FILE}" ]]; then
    echo "error: ${VERSION_FILE} not found" >&2
    exit 1
fi

CUR="$(tr -d '[:space:]' < "${VERSION_FILE}")"
if [[ ! "${CUR}" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    echo "error: VERSION must be MAJOR.MINOR.PATCH (got '${CUR}')" >&2
    exit 1
fi

MAJOR="${BASH_REMATCH[1]}"
MINOR="${BASH_REMATCH[2]}"
PATCH="${BASH_REMATCH[3]}"

case "${KIND}" in
    patch)
        PATCH=$((PATCH + 1))
        ;;
    minor)
        MINOR=$((MINOR + 1))
        PATCH=0
        ;;
    major)
        echo "WARNING: major bump is a breaking / 'revolution' change."
        echo "Current version: ${CUR}"
        echo "New version will be: $((MAJOR + 1)).0.0"
        printf "Type 'yes' to confirm: "
        read -r CONFIRM
        if [[ "${CONFIRM}" != "yes" ]]; then
            echo "aborted."
            exit 1
        fi
        MAJOR=$((MAJOR + 1))
        MINOR=0
        PATCH=0
        ;;
    *)
        usage
        ;;
esac

NEW="${MAJOR}.${MINOR}.${PATCH}"
printf '%s\n' "${NEW}" > "${VERSION_FILE}"
echo "Bumped ${CUR} -> ${NEW} (${KIND})"
echo "Reminder: update the README version badge if it is hardcoded."
