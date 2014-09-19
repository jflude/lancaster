include ../VERSION.mk

SUBSCRIBER_PATH="../subscriber"
LD_FLAGS := \
	-X main.wireProtocolVersion $(WIRE_VERSION) \
	-X main.clientVersion $(SOURCE_VERSION)
	-X main.subscriberPath '$(SUBSCRIBER_PATH)'

all: submgr

submgr: deps build

build: 
	go build -ldflags '$(LD_FLAGS)' .

deps: 
	../scripts/go-update-deps.sh

release: SUBSCRIBER_PATH=./subscriber
release: build dist

dist:
	mkdir release
	cp submgr release
	cp ../subscriber release

clean:
	rm -f submgr
	rm -Rf release

