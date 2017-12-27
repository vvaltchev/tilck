#!/bin/bash

# Args: <project's root dir> <build dir> <dest img file>

maindir=$1
bdir=$2
dest=$2/$3

tc=$maindir/toolchain
mtoolsdir=$tc/mtools
mformat=$mtoolsdir/mformat
mlabel=$mtoolsdir/mlabel
mmd=$mtoolsdir/mmd
mcopy=$mtoolsdir/mcopy

if [ ! -f $1 ]; then
   # If the 'fatpart' file does not already exist
   dd status=none if=/dev/zero of=$dest bs=1M count=35
fi

$mformat -i $dest -t 70 -h 16 -s 63 ::
$mlabel -i $dest ::EXOS

rm -rf $bdir/sysroot
cp -r $maindir/sysroot $bdir/

cd $bdir/sysroot

# hard-link init to sysroot/sbin
ln $bdir/init sbin/

# hard-link EFI files
ln $bdir/BOOTX64.EFI EFI/BOOT/
ln $bdir/switchmode.bin EFI/BOOT/
ln $bdir/kernel.bin EFI/BOOT/

# first, create the directories
for f in $(find * -type d); do
   $mmd -i $dest $f
done

# then, copy all the files in sysroot
for f in $(find * -type f); do
   $mcopy -i $dest $f ::/$f
done

