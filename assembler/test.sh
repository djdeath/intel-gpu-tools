#!/bin/sh

GI_TYPELIB_PATH=/usr/lib/x86_64-linux-gnu/girepository-1.0/:.
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:.libs

export GI_TYPELIB_PATH LD_LIBRARY_PATH

gjs $@
