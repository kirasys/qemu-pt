#!/bin/bash
./configure --target-list=i386-softmmu,x86_64-softmmu --enable-vnc --enable-gtk --enable-pt --enable-redqueen --disable-werror
make -j $jobs
