include ../../.makefile-header

LFLAGSLOCAL =  
CCFLAGSLOCAL = $(CFLAGSCFD) -I../../lib/include -I../../.

####### Files

HEADERS =   chem_share.h fluid_share.h model_share.h
SOURCES =   chem_share.c fluid_share.c model_share.c
OBJECTS =   chem_share.o fluid_share.o model_share.o
TARGETS  =  model_share.a


####### Build rules

all: src 

src: $(OBJECTS) 

arc: 
	( rm -f $(TARGETS) ; ar cqs $(TARGETS) *.o )

cleanall: clean

# DO NOT DELETE THIS LINE -- make depend depends on it.
