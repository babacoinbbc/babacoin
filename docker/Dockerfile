FROM debian:stretch
LABEL maintainer="Babacoin Developers <dev@babacoin.network>"
LABEL description="Dockerised BabaCoin, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
