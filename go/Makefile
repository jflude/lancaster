include ../VERSION.mk

COMPONENTS = pubmgr submgr

GOPATH := $(abspath .)
GOBIN := $(abspath ../bin)

ifneq (,$(findstring CYGWIN,$(shell uname)))
GOPATH := $(shell cygpath -w $(GOPATH))
GOBIN := $(shell cygpath -w $(GOBIN))
endif

GO = GOPATH="$(GOPATH)" GOBIN="$(GOBIN)" go

LDFLAGS = \
	-X "main.wireProtocolVersion=$(CACHESTER_WIRE_MAJOR_VERSION).$(CACHESTER_WIRE_MINOR_VERSION)" \
	-X "main.sourceVersion=$(shell echo $(CACHESTER_SOURCE_VERSION))"

all: build

build:
	@for component in $(COMPONENTS); do \
		$(GO) install -ldflags '$(LDFLAGS)' $$component || break ; \
	done

release: build

depend:

clean:
	@for component in $(COMPONENTS); do \
		$(GO) clean -i $$component || break ; \
	done
	rm -rf pkg bin

.PHONY: all components release fetch depend clean $(COMPONENTS)
