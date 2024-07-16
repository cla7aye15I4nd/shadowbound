FROM ghcr.io/cla7aye15i4nd/shadowbound/shadowbound:1.0.0

COPY ./config /shadowbound/config
COPY ./artifact/spec2017/config /root/config
COPY ./artifact/spec2017/scripts /root/scripts

RUN cd /root/scripts && gcc -o spectest spectest.c && pip3 install prettytable numpy