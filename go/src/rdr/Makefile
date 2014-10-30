include ../../../VERSION.mk
include ../../GOOP.mk

SRCS = main.go

BIN_DIR = ../../../bin
BIN = $(BIN_DIR)/rdr

CACHESTER_LIB = $(BIN_DIR)/libcachester.a

all: rdr

rdr: build

release: build 

build: $(BIN)

$(BIN): $(SRCS) $(CACHESTER_LIB)
	go build -o $(BIN) .

fetch: .installGoop

clean:
	rm -f $(BIN)

 .PHONY: all release build fetch clean
