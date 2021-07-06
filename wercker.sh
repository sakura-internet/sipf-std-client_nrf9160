#!/bin/sh

if [ "$DEBUG" = "1" ]; then
  TARGETS="develop"
else
  TARGETS="production staging develop"
fi

rm -rf build/
mkdir -p dist/bin

for TARGET in ${TARGETS}
do
  echo "${TARGET}"
    ./build.sh ${TARGET}
    RESULT="$?"
    if [ "${RESULT}" -ne 0 ]; then
        echo "Failed"
        exit ${RESULT}
    fi
    cp -v "build/${TARGET}/zephyr/merged.hex" "dist/bin/${TARGET}-merged.hex"
done

tar zcvf dist/sipf-std-client_nrf9160.tar.gz -C dist bin
