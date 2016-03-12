# Lorenz Leutgeb <e1127842@student.tuwien.ac.at>, 2016-01-20

obj-m := secvault.o
secvault-objs = main.o ctl.o data.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

.PHONY: all clean

default:
	$(MAKE) ARCH=um -C $(KDIR) M=$(PWD) modules

clean:
	rm -rf Module.symvers .*.cmd *.ko .*.o *.o *.mod.c .tmp_versions *.order svctl

all: default svctl
