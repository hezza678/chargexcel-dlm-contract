# Builds cxl-run, the desktop runner for ChargeXcel on-device scripts.
# Needs a C/C++ compiler and libcurl (macOS has it; on Debian/Ubuntu:
# apt install libcurl4-openssl-dev).

CC ?= cc
CXX ?= c++
BERRY = vm/berry
INC = -Ishim -Ivm -I$(BERRY)/upstream/src -I$(BERRY)/port -I$(BERRY)/generate

BERRY_SRC = $(wildcard $(BERRY)/upstream/src/*.c) $(wildcard $(BERRY)/port/*.c)
BERRY_OBJ = $(patsubst %.c,build/%.o,$(notdir $(BERRY_SRC)))

vpath %.c $(BERRY)/upstream/src $(BERRY)/port

cxl-run: main.cpp vm/DlmScriptVm.cpp vm/DlmLogic.cpp $(BERRY_OBJ)
	$(CXX) -std=c++17 -O2 -Wall -Wextra $(INC) -o $@ main.cpp vm/DlmScriptVm.cpp vm/DlmLogic.cpp $(BERRY_OBJ) -lcurl

build/%.o: %.c | build
	$(CC) -std=c99 -Os -w $(INC) -c -o $@ $<

build:
	mkdir -p build

clean:
	rm -rf build cxl-run

.PHONY: clean
