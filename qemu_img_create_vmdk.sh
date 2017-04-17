#!/bin/sh

(cd build && qemu-img convert -O vmdk exos.img exos.vmdk)
