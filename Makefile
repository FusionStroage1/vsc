

# ifndef VER
# VER=1.0.0
# endif

KDIR=/lib/modules/`uname -r`/build

# List of programs to build
obj-m += vsc.o
vsc-y +=../src/vsc_log.o ../src/vsc_ioctl.o ../src/vsc_main.o ../src/vsc_sym.o
# hostprogs-y := vsc_client_demo

# Tell kbuild to always build the programs
# always := $(hostprogs-y)

# HOSTCFLAGS_vsc_client_demo.o += -I$(objtree)/usr/include  -DVERSION=\"$(VER)\"
# HOSTCFLAGS_vsc_client_demo.o += -lpthread

# Flags
# CFLAGS_vsc_main.o = -DDRIVER_VER=\"$(VER)\"

all: modules

modules clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $@	
