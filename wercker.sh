#!/bin/sh -xe

west build -b m5stack_nrf9160ns

mkdir -p dist/zephyr
cp build/zephyr/*.hex dist/zephyr/
cp build/zephyr/*.elf dist/zephyr/
cp build/zephyr/*.bin dist/zephyr/
cp build/zephyr/*.dts dist/zephyr/
cp build/zephyr/*.map dist/zephyr/

tar zcvf dist/nrf91-m5stack-sample.tar.gz -C dist zephyr

