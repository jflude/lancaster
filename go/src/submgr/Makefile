include ../../../VERSION.mk
include ../../GOOP.mk

SUBSCRIBER_PATH="../../../subscriber"

build: LD_FLAGS= \
		-X main.wireProtocolVersion $(WIRE_VERSION) \
		-X main.sourceVersion $(SOURCE_VERSION) \
		-X main.subscriberPath $(SUBSCRIBER_PATH)

all: submgr

submgr: build

release: SUBSCRIBER_PATH="./subscriber"
release: build dist

build:
	go build -ldflags '$(LD_FLAGS)' .

depend: getGoop
	(cd $(GOOP_DIR) && goop install)

dist:
	mkdir release
	cp submgr release
	cp $(CACHESTER_DIR)/subscriber release

clean:
	rm -f submgr
	rm -Rf release

getGoop:
	go get github.com/nitrous-io/goop
