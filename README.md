# linux-3.12.10-ti2013.12.01-am3352_som

1. Export path to used toolchain , in my case gcc-linaro-arm-linux-gnueabihf-4.7-2013.01-20130125_linux
2. Make defconfig  
	* make ARCH=arm am3352_som_defconfig
3. Make kernel image and DTS
	* make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j4 zImage dtbs
4.Make Modules && Make modules install
 	* make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j4 INSTALL_MOD_PATH=out modules
 	* make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j4 INSTALL_MOD_PATH=out modules_install
 
 
Modules and firmware are located in out/ directory
Kernel and device tree :
##arch/arm/boot/zImage
##arch/arm/boot/dts/am3352-som.dtb



