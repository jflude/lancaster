include ../../../VERSION.mk
include ../../GOOP.mk

SRCS = main.go addrs.go

BIN_DIR = ../../../bin
BIN = $(BIN_DIR)/pubmgr

all: pubmgr

pubmgr: build

release: build

build: LD_FLAGS = \
	-X main.wireProtocolVersion \
		"$(CACHESTER_WIRE_MAJOR_VERSION).$(CACHESTER_WIRE_MINOR_VERSION)" \
	-X main.sourceVersion $(CACHESTER_SOURCE_VERSION)

build: $(BIN)

$(BIN): $(SRCS)
	go build -o $(BIN) -ldflags '$(LD_FLAGS)' .

fetch: .installGoop

clean:
	rm -f $(BIN)

.PHONY: all release build fetch clean
