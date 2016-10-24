cd ../../devices/ramdrive/
make
cd ../../frontend/kernel
make clean
make
cp ../../devices/ramdrive/risa_dev_ramdrive.ko ./
