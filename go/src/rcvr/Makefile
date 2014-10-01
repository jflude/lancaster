include ../../../VERSION.mk
include ../../GOOP.mk

all: rcvr

rcvr: build

release: build dist

build:
	go build 

fetch: .getGoop
	cd $(GOOP_DIR) && goop install

dist:
	mkdir -p release
	cp -p rcvr release/

clean:
	rm -rf rcvr release

.PHONY: all release build fetch dist clean
