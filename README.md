# Pintos Project of CS330 in KAIST

[![Build Status](https://travis-ci.org/hangpark/pintos.svg?branch=develop)](https://travis-ci.org/hangpark/pintos) [![Test Coverage](http://showbadge.hangpark.com/hangpark/pintos/?branch=develop&key=grade)](https://github.com/hangpark/pintos)

Repository for Pintos implementation project of CS330 in KAIST.

To contribute, read [CONTRIBUTING.md](CONTRIBUTING.md).

## Requirements

This OS is run in the specific environment below:

- Ubuntu 8.04 (Hardy Heron)
- GCC 3.4
- Bochs 2.2.6
- QEMU 0.15.0

You can use Bochs or QEMU to emulate Pintos.

## Build

Docker image for an environment satisfies above requirements is provided at [hangpark/pintos-dev-env-kaist](https://hub.docker.com/r/hangpark/pintos-dev-env-kaist/). Use below commands to build (or check, grade) the Pintos.

```bash
$ git clone https://github.com/hangpark/pintos.git
$ sudo docker pull hangpark/pintos-dev-env-kaist
$ sudo docker run -t -d --name pintos -v <your-pintos-dir>:/pintos hangpark/pintos-dev-env-kaist
$ sudo docker exec -i -t pintos bash -c "cd src/<dir> && make [check|grade]"
```

Make sure that `<your-pintos-dir>` to be an absolute path.
