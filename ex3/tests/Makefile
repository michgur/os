CC=g++
CXX=g++
LD=g++

EXESRC=test4.cpp
EXEOBJ=$(EXESRC:.cpp=.o)

INCS=-I.
CFLAGS = -Wall -std=c++11 -pthread -g $(INCS)
CXXFLAGS = -Wall -std=c++11 -pthread -g $(INCS)
LDFLAGS = -L. -lMapReduceFramework

EXE = test4
TARGETS = $(EXE)

all: $(TARGETS)

$(TARGETS): $(EXEOBJ)
	$(LD) $(CXXFLAGS) $(EXEOBJ) libMapReduceFramework.a -o $(EXE)

clean:
	$(RM) $(TARGETS) $(EXE) $(OBJ) $(EXEOBJ) *~ *core

depend:
	makedepend -- $(CFLAGS) -- $(SRC) $(LIBSRC)

build:
	rm ./libMapReduceFramework.a ; cd .. ; make ; cp ./libMapReduceFramework.a ./tests ; cd ./tests ; make clean ; make

