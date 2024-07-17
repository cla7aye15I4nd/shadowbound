#!/bin/bash

TARGET=native docker compose up nginx-eval
TARGET=shadowbound docker compose up nginx-eval

echo "[*] Check artifact/nginx/results/native.txt and artifact/nginx/results/shadowbound.txt for the results"