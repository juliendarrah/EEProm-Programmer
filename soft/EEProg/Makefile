TOPDIR  := $(shell cd ..; pwd)
include $(TOPDIR)/FTDI/Rules.make

APP = eeprog

all: $(APP)

$(APP): main.c	
	$(CC) main.c -o $(APP) $(CFLAGS)	
	
clean:
	-rm -f *.o ; rm $(APP)
