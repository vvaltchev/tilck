#!/bin/sh

# Args: <build dir> <dest img file>

bdir=$1
dest=$1/$2

dd status=none if=/dev/zero of=$dest bs=1M count=16
mformat -i $dest -T 32768 -h 16 -s 64 ::
mcopy -i $dest $bdir/init ::/

