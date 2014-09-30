include ../../../VERSION.mk
include ../../GOOP.mk

PUBLISHER_PATH = "$(CACHESTER_DIR)/publisher"

all: pubmgr

pubmgr: build

release: PUBLISHER_PATH = "./publisher"
release: build dist

build: LD_FLAGS = \
		-X main.wireProtocolVersion $(WIRE_VERSION) \
		-X main.sourceVersion $(SOURCE_VERSION) \
		-X main.publisherPath $(PUBLISHER_PATH)

build: 
	go build -ldflags '$(LD_FLAGS)' .

fetch: .getGoop
	cd $(GOOP_DIR) && goop install

dist:
	mkdir -p release
	cp -p pubmgr $(CACHESTER_DIR)/publisher release/

clean:
	rm -rf pubmgr release

.PHONY: all build release goop dist clean
