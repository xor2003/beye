##############################################################################
#   MAKEFILE - unit tests for terminal-resize handling                       #
#   requires: built ../../biewlib/libbiew.a and ../../resize.o               #
##############################################################################

include ../../config.mak

TARGET = resize_test
INCS = -I. -I../..
RESIZE_OBJ = ../../resize.o
BIEWLIB = ../../biewlib/$(LIBPREFIX)biew.$(LIBEXT)

all: $(TARGET)

$(TARGET): resize_test.o $(RESIZE_OBJ) $(BIEWLIB)
	$(LD) $(CDEFSYS) -o $@ resize_test.o $(RESIZE_OBJ) -L../../biewlib -lbiew $(OS_LIBS) -lutil

%.o : %.c
	$(CC) $(CDEFOS) $(CDEFSYS) $(INCS) -c $< -o $@

$(RESIZE_OBJ):
	$(MAKE) -C ../.. resize.o

$(BIEWLIB):
	$(MAKE) -C ../.. biewlib

run: $(TARGET)
	./$(TARGET); cat /tmp/resize_test.log

clean:
	$(RM) resize_test.o $(TARGET)
