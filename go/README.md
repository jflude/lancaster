## Cachester Go Stuff


### Dependencies:
Using [gvt](https://github.com/FiloSottile/gvt) for dependancies.

Building does not require gvt, but updating deps does.


### Editors:
#### [Atom.io](https://atom.io/)
If using Atom with go-plus plugin, simply set GOPATH before launching the editor.  For example, if you want to edit the entire cachester project:

    cd /path/to/cachester
    GOPATH=`pwd`/go/ atom .

This will make atom find the correct dependencies and ignore other random stuff installed in your system GOPATH.
