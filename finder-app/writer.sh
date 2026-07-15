#!/usr/bin/env bash
# File:        writer.sh
# Author:      Tristan Andrus
# Description: Writes specified contents to a specified file
################################################################################

set -o errexit   # Abort on nonzero exitstatus
set -o nounset   # Abort on unbound variable
set -o pipefail  # Don't hide errors within pipes

trap "echo 'An error occurred! Quitting mid-script!'" ERR

# Uncomment to debug
# set -x

################################################################################

function print_usage() {
    echo "Usage: writer.sh <target_file> <content_string>"
}

if [ $# -ne 2 ]; then
    print_usage
    exit 1
fi

target_file="${1:-}"
content_string="${2:-}"

parent_dir=$(dirname "$target_file")
echo "-> parent_dir = $parent_dir"
if ! mkdir -p "$parent_dir"; then
    echo "ERROR: Could not create directory $parent_dir"
    exit 1
fi

if printf "%s" "$content_string" > "$target_file"; then
    echo "SUCCESS: Wrote contents to $target_file"
else
    echo "ERROR: Could not write to file $target_file"
    exit 1
fi
