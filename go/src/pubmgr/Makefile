include ../../../VERSION.mk
include ../../GOOP.mk

PUBLISHER_PATH="$(CACHESTER_DIR)/publisher"

build: LD_FLAGS= \
		-X main.wireProtocolVersion $(WIRE_VERSION) \
		-X main.sourceVersion $(SOURCE_VERSION) \
		-X main.publisherPath $(PUBLISHER_PATH)

all: pubmgr

pubmgr: build

release: PUBLISHER_PATH="./publisher"
release: build dist

build: 
	go build -ldflags '$(LD_FLAGS)' .

depend: getGoop
	(cd $(GOOP_DIR) && goop install)

dist:
	mkdir release
	cp pubmgr release
	cp $(CACHESTER_DIR)/publisher release

clean:
	rm -f pubmgr
	rm -Rf release

