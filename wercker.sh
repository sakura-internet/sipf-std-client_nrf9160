#!/bin/sh

rm -rf build/
mkdir -p dist/bin

west build -b m5stack_nrf9160ns
RESULT="$?"
if [ "${RESULT}" -ne 0 ]; then
echo "Failed"
exit ${RESULT}
fi

cp build/zephyr/merged.hex dist/bin/
tar zcvf dist/sipf-std-client_nrf9160.tar.gz -C dist bin
