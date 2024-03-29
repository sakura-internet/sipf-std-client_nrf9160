#!/bin/bash -xe

TARGET_BOARD=scm-ltem1nrf_nrf9160_ns
TARGET_ENV=production

if [ -n "$1" ]; then
    TARGET_ENV="$1"
fi

if [ -n "$2" ]; then
    TARGET_BOARD="$2"
fi


PRJ_BASE_FILE="prj.conf.base"
PRJ_FILE="prj.conf.$TARGET_ENV"
BUILD_DIR=build/$TARGET_BOARD/$TARGET_ENV/

if [ ! -e "$PRJ_FILE" ]; then
  echo "Invalid environment $TARGET_ENV"
  exit 1
fi

mkdir -p $BUILD_DIR
cat $PRJ_BASE_FILE $PRJ_FILE > prj.conf

west build -b $TARGET_BOARD -d $BUILD_DIR -- -DSIPF_ENVIRONMENT=$TARGET_ENV
