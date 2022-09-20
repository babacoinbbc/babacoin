#!/usr/bin/env bash

export LC_ALL=C

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/.. || exit

DOCKER_IMAGE=${DOCKER_IMAGE:-babacoin/babacoind-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

BUILD_DIR=${BUILD_DIR:-.}

rm docker/bin/*
mkdir docker/bin
cp $BUILD_DIR/src/babacoind docker/bin/
cp $BUILD_DIR/src/babacoin-cli docker/bin/
cp $BUILD_DIR/src/babacoin-tx docker/bin/
strip docker/bin/babacoind
strip docker/bin/babacoin-cli
strip docker/bin/babacoin-tx

docker build --pull -t $DOCKER_IMAGE:$DOCKER_TAG -f docker/Dockerfile docker
