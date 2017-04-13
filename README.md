# PintOS Project of CS330 in KAIST

[![Build Status](https://travis-ci.com/hangpark/pintos.svg?token=gQa4om5Q1o59ZGsZT1Tf&branch=develop)](https://travis-ci.com/hangpark/pintos)

Repository for PintOS implementation project of CS330 in KAIST.

To contribute, read [CONTRIBUTING.md](CONTRIBUTING.md).

## Requirements

This OS is run in the specific environment below:

- Ubuntu 8.04 (Hardy Heron)
- GCC 3.4
- Bochs 2.2.6
- QEMU 0.15.0

You can use Bochs or QEMU for emulate PintOS.

## Build

Docker image for an environment satisfies above requirements is provided at [hangpark/pintos-dev-env-kaist](https://hub.docker.com/r/hangpark/pintos-dev-env-kaist/). Use below commands to build (or check, grade) the PintOS.
```bash
$ git clone https://github.com/hangpark/pintos.git
$ sudo docker pull hangpark/pintos-dev-env-kaist
$ sudo docker run -t -d --name pintos -v pintos:/pintos hangpark/pintos-dev-env-kaist
$ sudo docker exec -i -t pintos bash -c "cd src/<dir> && make [check|grade]"
```
