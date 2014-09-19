include ../VERSION.mk

SUBSCRIBER_PATH="../subscriber"

build: LD_FLAGS = \
        -X main.wireProtocolVersion $(WIRE_VERSION) \
        -X main.clientVersion $(SOURCE_VERSION) \
        -X main.subscriberPath '$(SUBSCRIBER_PATH)'

all: submgr

submgr: deps build

release: SUBSCRIBER_PATH="./subscriber"
release: build dist

build: 
	go build -ldflags '$(LD_FLAGS)' .

deps: 
	../scripts/go-update-deps.sh

dist:
	mkdir release
	cp submgr release
	cp ../subscriber release

clean:
	rm -f submgr
	rm -Rf release

