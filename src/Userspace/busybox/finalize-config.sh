#!/bin/sh
set -eu

make_program=$1
source_dir=$2
build_dir=$3

yes "" | "${make_program}" -C "${source_dir}" O="${build_dir}" oldconfig
