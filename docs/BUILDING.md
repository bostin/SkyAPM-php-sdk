# SkyWalking PHP Agent - Building from source

This document has detailed instructions on how to build SkyWalking PHP Agent from source.

## Pre-requisites

### Linux
```shell
$ [sudo] apt-get install build-essential autoconf automake libtool g++
$ [sudo] apt-get install libboost-all-dev
```

### MacOS

On a Mac, you will first need to install Xcode or Command Line Tools for Xcode and then run the following command from a terminal:

```shell
$ [sudo] xcode-select --install
```

### Alpine
```shell
$ apk add --no-cache git ca-certificates autoconf automake libtool g++ make file linux-headers boost-dev
```

## Build from source (PHP Extension)

```shell
$ curl -Lo v4.2.0.tar.gz https://github.com/SkyAPM/SkyAPM-php-sdk/archive/v4.2.0.tar.gz
$ tar zxvf v4.2.0.tar.gz
$ cd SkyAPM-php-sdk-4.2.0
$ phpize
$ ./configure
$ make -j$(nproc)
$ sudo make install
```

**Build time**: ~1-2 minutes (no external dependencies required)
**Disk usage**: ~5MB (source code only)
