#! /bin/sh
go list -f '{{join .Deps "\n"}}' |  xargs go list -e -f '{{if not .Standard}}{{.ImportPath}}{{end}}' | xargs -L 1 go get -v -u 
