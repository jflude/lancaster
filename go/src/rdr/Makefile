include ../../../VERSION.mk
include ../../GOOP.mk

all: rdr

rdr: build

release: build dist

build:
	go build 

goop: .getGoop
	cd ../../ && goop install

dist:
	mkdir -p release
	cp -p rdr release/

clean:
	rm -rf rdr release

.PHONY: all release build goop dist clean
