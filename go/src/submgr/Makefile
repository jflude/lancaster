include ../../../VERSION.mk
include ../../GOOP.mk

SUBSCRIBER_PATH = ../../../subscriber
BIN_DIR = ../../../bin

all: submgr

submgr: build

release: SUBSCRIBER_PATH = ./subscriber
release: build dist

build: LD_FLAGS = \
		-X main.wireProtocolVersion $(WIRE_VERSION) \
		-X main.sourceVersion $(SOURCE_VERSION) \
		-X main.subscriberPath $(SUBSCRIBER_PATH)

build:
	go build -ldflags '$(LD_FLAGS)' .

fetch: .getGoop
	cd $(GOOP_DIR) && goop install

dist:
	cp -p submgr $(CACHESTER_DIR)/subscriber $(BIN_DIR)

clean:
	rm -f submgr
	rm -f $(BIN_DIR)/submgr
	rm -f $(BIN_DIR)/subscriber

.PHONY: all release build fetch dist clean
