#!/bin/bash
BASE_DIR=$(cd $(dirname $0);pwd)
cd ${BASE_DIR}

function format() {
  filename="$1"
  clang-format -i -style=file "${filename}"
}

TARGETS="./src ./include ./lib"
BLACK_LIST=("debug.h" "usbh_conf.h")

for directory in ${TARGETS}; do
  for filename in `find ${directory} \( -name \*.c -o -name \*.h \)`; do
    basename=`basename ${filename}`
    if ! `echo ${BLACK_LIST[@]} | grep -q "${basename}"` ; then
      echo ${filename}
      format ${filename}
      #git add ${filename}
    fi
  done
done
