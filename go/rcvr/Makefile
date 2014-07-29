all: bindata.go
	go build .

bindata.go:
	go-bindata -debug -prefix=assets assets/
