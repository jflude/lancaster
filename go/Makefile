include GOOP.mk

COMPONENTS = pubmgr submgr

all: 
	@for component in $(COMPONENTS); do \
        $(MAKE) -C src/$$component all || break ; \
    done

components: $(COMPONENTS)

release:
	@for component in $(COMPONENTS); do \
        $(MAKE) -C src/$$component release || break ; \
    done

fetch:
	@for component in $(COMPONENTS); do \
        $(MAKE) -C src/$$component fetch || break ; \
    done

depend:

clean:
	@for component in $(COMPONENTS); do \
        $(MAKE) -C src/$$component clean || break ; \
    done

.PHONY: all components release fetch depend clean $(COMPONENTS)
