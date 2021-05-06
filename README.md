# nrf91-m5stack-sample

## Getting start

### Install nRF Connect SDK

See https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/getting_started.html .

### Clone this repository

```
git clone git@github.com:chibiegg/nrf91-m5stack-sample.git
cd nrf91-m5stack-sample
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
