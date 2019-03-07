remote_host?=bbfpa0
module_target_dir=/media/system/lib/modules/4.9.0-xilinx/
git_description=$(shell git describe --tags --dirty --always --match v[0-9]\.[0-9]\.[0-9]*)
# Strip leading 'v' from git_description
version=$(git_description:v%=%)

.PHONY: all
all: at25sf041.ko

.PHONY: include/version.h
include/version.h: include/version.h.in
	sed 's/{VERSION}/$(version)/g' $< > $@

EXTRA_CFLAGS := -std=gnu89 -Wno-declaration-after-statement -I$(src)/include
obj-m += at25sf041.o
at25sf041-objs := source/at25sf041.o
linux-source = /projects/RedPitaya/tmp/linux-xlnx-sbtOS-v2017.2/
at25sf041.ko: source/at25sf041.c include/version.h
	make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(linux-source) M=$(shell pwd) modules

.PHONY: install-remote
install-remote: at25sf041.ko
	@echo -n "Installing to $(remote_host)... "
	ssh $(remote_host) "mount -o rw,remount \$$(readlink /media/system)"
	ssh $(remote_host) "mkdir -p $(module_target_dir)"
	scp at25sf041.ko $(remote_host):$(module_target_dir)
	ssh $(remote_host) "mount -o ro,remount \$$(readlink /media/system)"
	@echo "Successfully installed to $(remote_host)."

.PHONY: restart-remote
restart-remote:
	ssh $(remote_host) "/etc/init.d/M32at25sf041 reinsert"

.PHONY: run
run: install-remote restart-remote

.PHONY: clean
clean:
	make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(linux-source) M=$(shell pwd) clean
