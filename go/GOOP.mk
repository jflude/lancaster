SELF_DIR := $(shell dirname $(CURDIR)/$(word $(words $(MAKEFILE_LIST)), $(MAKEFILE_LIST)))
CACHESTER_DIR := $(SELF_DIR)/..
SCRIPTS_DIR := $(CACHESTER_DIR)/scripts

CACHESTER_VENDOR_DIR := $(SELF_DIR)/.vendor
CACHESTER_GOOP_DIR := $(CACHESTER_VENDOR_DIR)/..

export GOPATH := $(CACHESTER_VENDOR_DIR):$(GOPATH)
export PATH := $(CACHESTER_VENDOR_DIR)/bin:$(PATH)

.ensurePrivateGithubSSH:
	git config --global url.ssh://git@github.peak6.net/.insteadOf https://github.peak6.net

.getGoop: .ensurePrivateGithubSSH
	go get github.com/nitrous-io/goop
	mkdir -p $(CACHESTER_VENDOR_DIR)

.installGoop: .getGoop
	cd $(CACHESTER_GOOP_DIR) && goop install

.updateGoop: .getGoop
	cd $(CACHESTER_GOOP_DIR) && goop update

.PHONY: .ensurePrivateGithubSSH .getGoop .installGoop .updateGoop
