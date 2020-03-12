obj-m	:=softuart.o
KDIR 	:=/home/paul/RP3_PROJECT/linux4_19_97
MYCC	=/home/paul/opt/gcc-raspberry-pi/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf-

# The KDIR must have to assign a ./direction
modules:
	$(MAKE) ARCH=arm CROSS_COMPILE=$(MYCC) -C $(KDIR) SUBDIRS=$(shell pwd) modules
clean:
	rm -f modules.order Module.symvers  *.o *.ko *.mod.c
