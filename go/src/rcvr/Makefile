include ../../../VERSION.mk
include ../../GOOP.mk

all: rcvr

rcvr: build

release: build dist

build:
	go build 

depend: getGoop
	(cd $(GOOP_DIR) && goop install)

dist:
	mkdir release
	cp rcvr release

clean:
	rm -f rcvr
	rm -Rf release

