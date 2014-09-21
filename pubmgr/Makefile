include ../VERSION.mk

PUBLISHER_PATH="../publisher"

build: LD_FLAGS= \
		-X main.wireProtocolVersion $(WIRE_VERSION) \
		-X main.sourceVersion $(SOURCE_VERSION) \
		-X main.publisherPath $(PUBLISHER_PATH)

all: pubmgr

pubmgr: deps build

release: SUBSCRIBER_PATH="./subscriber"
release: build dist

build: 
	go build -ldflags '$(LD_FLAGS)' .

deps: 
	../scripts/go-update-deps.sh

dist:
	mkdir release
	cp submgr release
	cp $(PUBLISHER_PATH) release

clean:
	rm -f pubmgr
	rm -Rf release

