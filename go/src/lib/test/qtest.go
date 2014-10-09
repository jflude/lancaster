package main

import (
	".."
	"flag"
	"fmt"
	"log"
	"os"
	"strconv"
	"strings"
	"sync"
	"unsafe"
)

var tail bool

type filters []string

var filterSlots map[int]bool
var filterSlotsOpt string
var find filters
var verbose bool
var diffMode bool
var validate bool

func (f *filters) String() string {
	return fmt.Sprint([]string(*f))
}
func (f *filters) Set(val string) error {
	*f = append(*f, val)
	return nil
}

type QuoteSource struct {
	cs *quotester.CachesterSource
}

/*// Load q with a clean copy of the record
func (qs *QuoteSource) getQuote(record int, q *Quote) error {
	cs := qs.cs
	raddr := cs.GetPointer(record)
	for {
		seq := *((*C.uint)(raddr))
		if seq == 0 {
			return fmt.Errorf("No such record: %d", record)
		} else if seq < 0 {
			continue
		}
		vaddr := (unsafe.Pointer)(uintptr(raddr) + cs.vOffset)
		d := (*Quote)(vaddr)
		*q = *d
		nseq := *((*C.uint)(raddr))
		if seq == nseq {
			return nil
		}
		if verbose {
			log.Println("Version stomp:", nseq, "!=", seq, "for record:", record)
		}
	}
}
*/

func ptrToString(unsafe.Pointer) string
func (qs *QuoteSource) getKeys(start int) (map[string]int, int) {
	cs := qs.cs
	ret := make(map[string]int)
	for x := 0; x < cs.MaxRecords; x++ {
		cs.GetData(x)
	}
	err := cs.WalkRecords(func(slot int, version uint, raddr unsafe.Pointer) error {
		vaddr := (unsafe.Pointer)(uintptr(raddr) + cs.vOffset)
		if version > 0 {
			d := (*Quote)(vaddr)
			ret[d.key()] = x
		}
	})
	x := start
	for ; x < cs.MaxRecords; x++ {
		raddr := cs.getPointer(x)
		seq := *((*C.uint)(raddr))
		if seq < 1 {
			return ret, x
		}
	}
	return ret, x
}

func init() {
	flag.BoolVar(&tail, "t", false, "Tail feed")
	flag.BoolVar(&verbose, "v", false, "Verbose")
	flag.BoolVar(&diffMode, "dm", diffMode, "diff mode, supress things that print differently every run")
	flag.StringVar(&filterSlotsOpt, "fs", "", "Only dump records in comma seperated set of slots")
	flag.Var(&find, "f", "Filter keys")
	flag.BoolVar(&validate, "validate", validate, "Validate quote file")
}
func main() {
	flag.Parse()
	if filterSlotsOpt != "" {
		filterSlots = make(map[int]bool)
		ss := strings.Split(filterSlotsOpt, ",")
		for _, s := range ss {
			n, err := strconv.Atoi(s)
			if err != nil {
				log.Fatalln(err)
			}
			filterSlots[n] = true
		}
	}
	args := flag.Args()
	if len(args) == 0 {
		flag.Usage()
		fmt.Fprintln(os.Stderr, "Must specify a file")
		os.Exit(1)
	}
	if len(find) > 0 {
		if verbose {
			log.Println("Filtering for:", find)
		}
	}
	if tail {
		if diffMode {
			log.Fatal("Cannot specify tail and diff mode at same time")
		}
	}
	var wg sync.WaitGroup
	for _, fn := range args {
		if verbose {
			log.Println("Loading:", fn)
		}
		cs, err := NewCachesterSource(fn)
		if err != nil {
			log.Fatal(err)
		}
		wg.Add(1)
		defer cs.Destroy()
		if tail {
			go func() {
				cs.tailQuotes([]string(find))
				wg.Done()
			}()
		} else if validate {
			go func() {
				cs.validateQuotes()
				wg.Done()
			}()
		} else {
			go func() {
				cs.findQuotes([]string(find))
				wg.Done()
			}()
		}
	}
	wg.Wait()
}

func (cs *CachesterSource) validateQuotes() {
	badRecords := make(map[int]string)
	lastGapStart := -1
	for x := 0; x < cs.maxRecords; x++ {
		raddr := cs.getPointer(x)
		rev := *((*C.uint)(raddr))
		if rev < 1 {
			if lastGapStart == -1 {
				lastGapStart = x
			}
			continue
		}
		if lastGapStart != -1 {
			for g := lastGapStart; g < x; g++ {
				fmt.Println(g, "empty slot")
			}
			lastGapStart = -1
		}
		vaddr := (unsafe.Pointer)(uintptr(raddr) + cs.vOffset)
		recKey := cs.getkey(vaddr)

		atmost := func(name string, count int, atmost int) {
			if _, ok := badRecords[x]; !ok && count > atmost {
				badRecords[x] = fmt.Sprintf("Can have at most %d %s, had %d", atmost, name, count)
			}
		}
		atleast := func(name string, count int, atleast int) {
			if _, ok := badRecords[x]; !ok && count < atleast {
				badRecords[x] = fmt.Sprintf("Need at least %d %s, had %d", atleast, name, count)
			}
		}
		oneof := func(name string, count int, allowed ...int) {
			if _, ok := badRecords[x]; !ok {
				for _, a := range allowed {
					if count == a {
						return
					}
				}
				badRecords[x] = fmt.Sprintf("Bad %s count: %d", name, count)
			}
		}

		if validate {
			charCount := 0
			colonCount := 0
			dotCount := 0
			pCount := 0
			rCount := 0
			slashCount := 0
			for _, ch := range recKey {
				switch {
				case ch >= 'A' && ch <= 'Z':
					charCount++
				case ch >= '0' && ch <= '9':
				case ch == ':':
					colonCount++
				case ch == '.':
					dotCount++
				case ch == 'p':
					pCount++
				case ch == '/':
					slashCount++
				case ch == 'r':
					rCount++
				default:
					badRecords[x] = fmt.Sprintf("Bad character: %c", ch)
				}
			}
			atleast("characters", charCount, 2)
			oneof("colon", colonCount, 5, 2, 4, 1)
			atmost("dot", dotCount, 1)
			atmost("'r' characters", rCount, 1)
			atmost("period", pCount, 1)
			atmost("slash", slashCount, 2)

		}
	}
	for x, msg := range badRecords {
		raddr := cs.getPointer(x)
		vaddr := (unsafe.Pointer)(uintptr(raddr) + cs.vOffset)
		rev := cs.getRevision(x)
		fmt.Printf("Record: %d (rev: %d), %s\n %s\n\n", x, rev, msg, cs.getstring(vaddr))
	}
}
