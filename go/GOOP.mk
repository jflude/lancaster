SELF_DIR := $(shell readlink -e $(dir $(lastword $(MAKEFILE_LIST))))
CACHESTER_DIR := $(shell readlink -e $(SELF_DIR)/"..")
SCRIPTS_DIR := "$(shell readlink -e $(CACHESTER_DIR)/scripts)"

CACHESTER_VENDOR_DIR := $(SELF_DIR)/.vendor
CACHESTER_GOOP_DIR=$(shell readlink -e $(CACHESTER_VENDOR_DIR)/..)
export GOPATH:=$(CACHESTER_VENDOR_DIR)
export PATH:=$(CACHESTER_VENDOR_DIR)/bin:$(PATH)

.ensurePrivateGithubSSH:
	git config --global url.ssh://git@github.peak6.net/.insteadOf https://github.peak6.net

.getGoop: .ensurePrivateGithubSSH
	go get github.com/nitrous-io/goop
	mkdir -p $(CACHESTER_VENDOR_DIR)

.installGoop: .getGoop
	cd $(CACHESTER_GOOP_DIR) && goop install

