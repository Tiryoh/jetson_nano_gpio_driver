obj-m:=myled.o

LINUX_SRC_DIR:=/lib/modules/$(shell uname -r)/build
VERBOSE:=0

ccflags-y += -std=gnu99 -Wall -Wno-declaration-after-statement

myled.ko: myled.c
	make -C $(LINUX_SRC_DIR) M=$(shell pwd) V=$(VERBOSE) modules

clean:
	make -C $(LINUX_SRC_DIR) M=$(shell pwd) V=$(VERBOSE) clean

install: myled.ko
	sudo insmod myled.ko
	sudo chmod 666 /dev/myled0
	sudo chmod 666 /dev/myswitch0

uninstall:
	sudo rmmod myled
