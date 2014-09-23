include ../../../VERSION.mk
include ../../GOOP.mk

all: rdr

rdr: build

release: build dist

build:
	go build 

depend: .getGoop
	(cd ../../ && goop install)

dist:
	mkdir release
	cp rdr release

clean:
	rm -f rdr
	rm -Rf release

