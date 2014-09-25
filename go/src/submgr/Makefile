include ../../../VERSION.mk
include ../../GOOP.mk

SUBSCRIBER_PATH = "../../../subscriber"

all: submgr

submgr: build

release: SUBSCRIBER_PATH = "./subscriber"
release: build dist

build: LD_FLAGS = \
		-X main.wireProtocolVersion $(WIRE_VERSION) \
		-X main.sourceVersion $(SOURCE_VERSION) \
		-X main.subscriberPath $(SUBSCRIBER_PATH)

build:
	go build -ldflags '$(LD_FLAGS)' .

goop: .getGoop
	cd $(GOOP_DIR) && goop install

dist:
	mkdir -p release
	cp -p submgr $(CACHESTER_DIR)/subscriber release/

clean:
	rm -rf submgr release

.PHONY: all release build goop dist clean
