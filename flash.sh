#!/bin/bash -xe

TARGET_BOARD=m5stack_nrf9160ns
TARGET_ENV=develop

if [ -n "$1" ]; then
    TARGET_ENV="$1"
fi


PRJ_BASE_FILE="prj.conf.base"
PRJ_FILE="prj.conf.$TARGET_ENV"
BUILD_DIR=build/$TARGET_ENV/

if [ ! -e "$PRJ_FILE" ]; then
  echo "Invalid environment $TARGET_ENV"
  exit 1
fi

mkdir -p $BUILD_DIR
cat $PRJ_BASE_FILE $PRJ_FILE > prj.conf

west flash -d $BUILD_DIR
