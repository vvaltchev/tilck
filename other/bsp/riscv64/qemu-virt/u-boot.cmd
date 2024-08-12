env set loadaddr @KERNEL_PADDR@
echo Load ${prefix}uEnv.txt...
fatload ${devtype} ${devnum}:${distro_bootpart} ${loadaddr} ${prefix}uEnv.txt;
env import -t ${loadaddr} ${filesize};
run boot2;
