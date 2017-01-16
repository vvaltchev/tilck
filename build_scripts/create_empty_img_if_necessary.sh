#!/bin/bash

if [ ! -f $1 ]; then
   dd status=none if=/dev/zero of=$1 obs=1024 ibs=1024 count=$2
fi
