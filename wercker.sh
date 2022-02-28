#!/bin/sh

BOARDS="scm-ltem1nrf_nrf9160ns nrf9160dk_nrf9160_ns thingy91_nrf9160_ns"

if [ "$DEBUG" = "1" ]; then
  TARGETS="develop"
else
  TARGETS="production" 
  #TARGETS="production staging develop"
fi

rm -rf build/

for TARGET in ${TARGETS}
do
    for BOARD in ${BOARDS}
    do
        echo "${TARGET}"
        ./build.sh ${TARGET} ${BOARD}
        RESULT="$?"
        if [ "${RESULT}" -ne 0 ]; then
            echo "Failed"
            exit ${RESULT}
        fi

        DIST_DIR="dist/${BOARD}/${TARGET}/"
        mkdir -p "${DIST_DIR}/hex"
        cp -v "build/${BOARD}/${TARGET}/zephyr/merged.hex" "${DIST_DIR}/hex/${BOARD}_${TARGET}-merged.hex"
        mkdir -p "${DIST_DIR}/update"
        cp -v "build/${BOARD}/${TARGET}/zephyr/app_update.bin" "${DIST_DIR}/update/"
    done
done

tar zcvf dist/sipf-std-client_nrf9160.tar.gz -C dist ${BOARDS}
