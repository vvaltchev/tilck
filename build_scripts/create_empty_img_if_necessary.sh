#!/bin/bash

if [ ! -f $1 ]; then
   dd status=none if=/dev/zero of=$1 bs=1M count=$2
fi
