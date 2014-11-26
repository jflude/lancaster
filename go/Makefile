include GOOP.mk

COMPONENTS = pubmgr submgr rdr

all: 
	@for component in $(COMPONENTS); do \
        $(MAKE) -C src/$$component all; \
    done

components: $(COMPONENTS)

release:
	@for component in $(COMPONENTS); do \
        $(MAKE) -C src/$$component release; \
    done

fetch:
	@for component in $(COMPONENTS); do \
        $(MAKE) -C src/$$component fetch; \
    done

depend:

clean:
	@for component in $(COMPONENTS); do \
        $(MAKE) -C src/$$component clean; \
    done

.PHONY: all components release fetch depend clean $(COMPONENTS)
