
obj-m := pcd8544.o

KERNELDIR     := /Users/martin/gnublin-develop-kernel/linux-2.6.33-lpc313x
CROSS_COMPILE := /Users/martin/cross/arm-none-linux-gnueabi/bin/arm-none-linux-gnueabi-
MAKE          := gnumake
PWD           := $(shell pwd)
ARCH          := arm

all:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(PWD) clean
