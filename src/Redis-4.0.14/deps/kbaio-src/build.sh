#!/bin/bash

if [ "$1" = "uninstall" ]; then
    rm -rf /usr/lib64/libkbaio.so
    rm -rf /usr/local/include/ae_kbaio.h
    rm -rf /usr/local/include/networking_kbaio.h
    exit 0
fi

gcc -fPIC -shared -o libkbaio.so ae_kbaio.c networking_kbaio.c \
    -L./ -luring -I./ -I../redis-4.0.14/deps/lua/src -I../redis-4.0.14/src/

if [ "$1" = "install" ]; then
    cp -rf libkbaio.so /usr/lib64/
    cp -rf ae_kbaio.h /usr/local/include/
    cp -rf networking_kbaio.h /usr/local/include/
fi
