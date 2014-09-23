SELF_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
CACHESTER_DIR := $(shell readlink -e $(SELF_DIR)/"..")
SCRIPTS_DIR := "$(shell readlink -e $(CACHESTER_DIR)/scripts)"

VENDOR_DIR:=$(shell $(SCRIPTS_DIR)/find_parent_with.sh ".vendor")
GOOP_DIR=$(shell readlink -e $(VENDOR_DIR)/..)
export GOPATH:=$(VENDOR_DIR):$(GOPATH)
export PATH:=$(VENDOR_DIR)/bin:$(PATH)

