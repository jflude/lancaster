SELF_DIR := $(shell readlink -e $(dir $(lastword $(MAKEFILE_LIST))))
CACHESTER_DIR := $(shell readlink -e $(SELF_DIR)/"..")
SCRIPTS_DIR := "$(shell readlink -e $(CACHESTER_DIR)/scripts)"

VENDOR_DIR := $(SELF_DIR)/.vendor
GOOP_DIR=$(shell readlink -e $(VENDOR_DIR)/..)
export GOPATH:=$(VENDOR_DIR):$(GOPATH)
export PATH:=$(VENDOR_DIR)/bin:$(PATH)

.getGoop:
	go get github.com/nitrous-io/goop
	mkdir -p $(VENDOR_DIR)
