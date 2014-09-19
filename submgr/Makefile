include ../VERSION.mk

LD_FLAGS := \
	-X main.wireProtocolVersion $(WIRE_VERSION) \
	-X main.clientVersion $(SOURCE_VERSION)

all: submgr

submgr: deps build

build: 
	go build -ldflags '$(LD_FLAGS)' .

deps: 
	../scripts/go-update-deps.sh

clean:
	rm -f submgr

