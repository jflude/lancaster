include ../../../VERSION.mk
include ../../GOOP.mk

PUBLISHER_PATH = ./publisher
BIN_DIR := ../../../bin
BIN := $(BIN_DIR)/pubmgr

all: pubmgr

pubmgr: build

release: PUBLISHER_PATH = ./publisher
release: build

build: LD_FLAGS = \
	-X main.wireProtocolVersion \
		"$(CACHESTER_WIRE_MAJOR_VERSION).$(CACHESTER_WIRE_MINOR_VERSION)" \
	-X main.sourceVersion $(CACHESTER_SOURCE_VERSION) \
	-X main.publisherPath $(PUBLISHER_PATH)

build: 
	go build -o $(BIN) -ldflags '$(LD_FLAGS)' .
	cp ../../../publisher $(BIN_DIR)

fetch: .installGoop

clean:
	rm -f $(BIN)
	rm -f $(BIN_DIR)/publisher

.PHONY: all build release fetch clean
