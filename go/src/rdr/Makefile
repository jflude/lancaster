include ../../../VERSION.mk
include ../../GOOP.mk

BIN_DIR=../../../bin
BIN=$(BIN_DIR)/rdr

all: rdr

rdr: build

release: build 

build:
	go build -o $(BIN)

fetch: .installGoop

clean:
	rm -f $(BIN)

 .PHONY: all release build fetch clean
