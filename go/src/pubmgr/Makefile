include ../../../VERSION.mk
include ../../GOOP.mk

PUBLISHER_PATH = $(CACHESTER_DIR)/publisher
BIN_DIR = ../../../bin

all: pubmgr

pubmgr: build

release: PUBLISHER_PATH = ./publisher
release: build dist

build: LD_FLAGS = \
	-X main.wireProtocolVersion \
		"$(CACHESTER_WIRE_MAJOR_VERSION).$(CACHESTER_WIRE_MINOR_VERSION)" \
	-X main.sourceVersion $(CACHESTER_SOURCE_VERSION) \
	-X main.publisherPath $(PUBLISHER_PATH)

build: 
	go build -ldflags '$(LD_FLAGS)' .

fetch: .installGoop

dist:
	cp -p pubmgr $(CACHESTER_DIR)/publisher $(BIN_DIR)

clean:
	rm -f pubmgr 
	rm -f $(BIN_DIR)/publisher
	rm -f $(BIN_DIR)/pubmgr

.PHONY: all build release fetch dist clean
