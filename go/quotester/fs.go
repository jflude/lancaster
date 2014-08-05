package main

import (
	"errors"
	"flag"
	"log"
	"os"
	"sync"
	"time"

	"bazil.org/fuse"
	"bazil.org/fuse/fs"
	_ "bazil.org/fuse/fs/fstestutil"
)

var mountPoint string

func init() {
	flag.StringVar(&mountPoint, "mount", "", "Path to mount as quotefs")
}

func startFS() error {
	if mountPoint == "" {
		log.Println("QuoteFS disabled")
		return nil
	}
	log.Println("Creating QuoteFS @", mountPoint)

	err := fuse.Unmount(mountPoint)
	if err != nil {
		log.Println("(probably ok) Failed to unmount:", mountPoint, err)
	}
	c, err := fuse.Mount(mountPoint)
	if err != nil {
		return err
	}
	go func() {
		defer c.Close()
		qfs := newFS()
		go updateINodes(&qfs)
		err := fs.Serve(c, qfs)
		if err != nil {
			log.Println("Error returned from serve:", err)
		}
	}()
	// check if the mount process has an error to report
	<-c.Ready
	if err := c.MountError; err != nil {
		log.Println("Failed to mount QuoteFS:", err)
		return err
	}
	log.Println("Mounted QuoteFS @", mountPoint)
	return nil
}

func updateINodes(qfs *QFS) {
	last := 0
	var data map[string]int
	for {
		var newlast int
		data, newlast = getKeys(last)
		if newlast != last {
			log.Println("New Last", last, "data len", len(data))
		}
		last = newlast
		for k, v := range data {
			qfs.newKey(k, v)
		}
		time.Sleep(time.Second)
	}
}

func newFS() QFS {
	return QFS{
		nameToNode: make(map[string]QuoteFile),
	}
}

type QFS struct {
	nameToNode   map[string]QuoteFile
	inodeCounter uint64
	lock         sync.RWMutex
}

func (qfs *QFS) newKey(key string, index int) error {
	qfs.lock.Lock()
	defer qfs.lock.Unlock()
	_, ok := qfs.nameToNode[key]
	if ok {
		return errors.New("Already exists")
	}
	qfs.inodeCounter++
	qfs.nameToNode[key] = QuoteFile{inode: qfs.inodeCounter, index: index}
	return nil
}

func (qfs QFS) Root() (fs.Node, fuse.Error) {
	return Dir{inode: 1, qfs: qfs}, nil
}

// Dir implements both Node and Handle for the root directory.
type Dir struct {
	inode uint64
	qfs   QFS
}

func (d Dir) Attr() fuse.Attr {
	log.Println("Attr:", d.inode)
	return fuse.Attr{Inode: d.inode, Mode: os.ModeDir | 0555}
}

func (d Dir) ReadDir(intr fs.Intr) ([]fuse.Dirent, fuse.Error) {
	log.Println("READ DIR")

	ret := make([]fuse.Dirent, 0, len(d.qfs.nameToNode))
	for k, v := range d.qfs.nameToNode {
		ret = append(ret, fuse.Dirent{Inode: v.inode, Name: k, Type: fuse.DT_File})
	}
	return ret, nil
}

func (d Dir) Lookup(name string, intr fs.Intr) (fs.Node, fuse.Error) {
	log.Println("Lookup", name)
	f, ok := d.qfs.nameToNode[name]
	// inode, ok := d.fs.nameToNode[name]
	if !ok {
		return nil, fuse.ENOENT
	}
	return f, nil //File{inode: uint64(inode), dir: d}, nil
	// return nil, fuse.ENOENT
}

// File implements both Node and Handle for the hello file.
type QuoteFile struct {
	inode uint64
	index int
}

func (f QuoteFile) Attr() fuse.Attr {
	log.Println("ATTR", f.inode)
	return fuse.Attr{Inode: f.inode, Mode: 0555, Size: uint64(100)}
}

func (f QuoteFile) ReadAll(intr fs.Intr) ([]byte, fuse.Error) {
	log.Println("ReadAll")
	var q Quote
	err := getQuote(f.index, &q)
	if err != nil {
		log.Println("Error reading QuoteFS", err)
		return nil, fuse.ENOENT
	}
	return []byte(q.String() + "\n"), nil
}
