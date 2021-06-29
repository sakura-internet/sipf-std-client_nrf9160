# sipf-std-client_nrf9160

## Getting start

### About

This software is `Standard client firmware' for Sakura's MONOPLATFORM.  
Target divice are SCO-M5SNRF9160 and SCM-LTEM1NRF.

### Install nRF Connect SDK

See [nRF Connect SDK Getting started](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/getting_started.html).  
If you want to install the development environment quickly, see [Installing the nRF Connect SDK through nRF Connect for Desktop](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/gs_assistant.html#gs-assistant).

Using nRF Connect SDK v1.5.1 .

### Clone this repository

```
git clone https://github.com/sakura-internet/sipf-std-client_nrf9160.git
cd sipf-std-client_nrf9160
```

### Clean

```
rm -rf build
```

### Build

```
west build -b m5stack_nrf9160ns
```

### Flash

`nrfjprog` is required.

```
west flash
```

OR

Write the HEX image file 'build/zephyr/merged.hex' using nRF Connect `Programmer' application.
