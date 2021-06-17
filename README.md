# sipf-std-client_nrf9160

## Getting start

### Install nRF Connect SDK

See https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/getting_started.html .

Using nRF Connect SDK v1.5.1 .

### Clone this repository

```
git clone git@github.com:sakurainc/sipf-std-client_nrf9160.git
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
