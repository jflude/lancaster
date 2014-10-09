include ../../../VERSION.mk
include ../../GOOP.mk

SUBSCRIBER_PATH = ./subscriber
BIN_DIR = ../../../bin
BIN = $(BIN_DIR)/submgr

all: submgr

submgr: build

release: build 

build: LD_FLAGS = \
	-X main.wireProtocolVersion \
		"$(CACHESTER_WIRE_MAJOR_VERSION).$(CACHESTER_WIRE_MINOR_VERSION)" \
	-X main.sourceVersion $(CACHESTER_SOURCE_VERSION) \
	-X main.subscriberPath $(SUBSCRIBER_PATH)

build:
	go build -o $(BIN) -ldflags '$(LD_FLAGS)' .
	cp -f ../../../subscriber $(BIN_DIR)

fetch: .installGoop

clean:
	rm -f $(BIN_DIR)/submgr
	rm -f $(BIN_DIR)/subscriber

.PHONY: all release build fetch clean
