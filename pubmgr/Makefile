WIRE_VERSION=1.0
BUILD_VERSION:=$(shell git log --pretty=format:'%ad %h %d' --abbrev-commit --date=short -1)

LD_FLAGS= \
	-X main.wireProtocolVersion $(WIRE_VERSION) \
	-X main.myVersion '$(BUILD_VERSION)'

all: pubmgr

pubmgr: deps build

build: 
	go build -ldflags "$(LD_FLAGS)" .

deps: 
	../scripts/go-update-deps.sh

clean:
	rm pubmgr

