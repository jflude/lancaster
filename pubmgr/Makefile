include ../VERSION.mk

PUBLISHER_PATH="../publisher"

build: LD_FLAGS= \
		-X main.wireProtocolVersion $(WIRE_VERSION) \
		-X main.sourceVersion $(SOURCE_VERSION) \
		-X main.publisherPath $(PUBLISHER_PATH)

all: pubmgr

pubmgr: deps build

release: PUBLISHER_PATH="./publisher"
release: build dist

build: 
	go build -ldflags '$(LD_FLAGS)' .

deps: 
	../scripts/go-update-deps.sh

dist:
	mkdir release
	cp pubmgr release
	cp ../publisher release

clean:
	rm -f pubmgr
	rm -Rf release

