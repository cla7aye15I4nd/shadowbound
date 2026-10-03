ARG BASE_IMAGE=ghcr.io/cla7aye15i4nd/shadowbound:latest
FROM ${BASE_IMAGE}

COPY ./config /shadowbound/config
COPY ./artifact/spec2017/config /root/config
COPY ./artifact/spec2017/scripts /root/scripts

RUN cd /root/scripts && gcc -o spectest spectest.c && pip3 install prettytable numpy