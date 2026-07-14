#!/bin/sh
set -eu

make_program=$1
source_dir=$2
build_dir=$3

answers="${build_dir}/.oldconfig-answers"
: > "${answers}"
i=0
while [ "${i}" -lt 50000 ]; do
    printf '\n' >> "${answers}"
    i=$((i + 1))
done

"${make_program}" -C "${source_dir}" O="${build_dir}" oldconfig < "${answers}" || true
"${make_program}" -C "${source_dir}" O="${build_dir}" silentoldconfig
