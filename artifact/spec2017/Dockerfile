FROM shadowbound

COPY ./config /shadowbound/config
COPY ./artifact/spec2017/config /root/config
COPY ./artifact/spec2017/scripts /root/scripts

RUN cd /root/scripts && gcc -o spectest spectest.c && pip3 install prettytable numpy