#!/usr/bin/env bash
# File:        finder.sh
# Author:      Tristan Andrus
# Description: Finds the number of files in a given directory, and the number of
#              lines in those files matching a search string
################################################################################

set -o errexit   # Abort on nonzero exitstatus
set -o nounset   # Abort on unbound variable
set -o pipefail  # Don't hide errors within pipes

trap "echo 'An error occurred! Quitting mid-script!'" ERR

# Uncomment to debug

################################################################################

function print_usage() {
    echo "Usage: finder.sh <filesdir> <searchstr>
    filesdir: The directory to search in
    searchstr: The string to find matches on
"
}

if [ $# -ne 2 ]; then
    print_usage
    exit 1
fi

filesdir="${1:-}"
if [ -z filesdir ]; then
    print_usage
    exit 1
fi

if [ ! -d "$filesdir" ]; then
    print_usage
    exit 1
fi

searchstr="${2:-}"
if [[ ! -v searchstr ]]; then
    print_usage
    exit 1
fi


num_files=0
num_line_matches=0

for f in "$filesdir"/** ; do
    [ -e "$f" ] && num_files=$(( num_files + 1 ))
done

num_line_matches=$(grep -R "$searchstr" "$filesdir" | wc --lines - )

echo "The number of files are $num_files and the number of matching lines are $num_line_matches"
