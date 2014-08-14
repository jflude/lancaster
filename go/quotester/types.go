package main

import (
	"bytes"
	"fmt"
	"time"
)

type OpraData struct {
	keyBytes   [32]byte
	exchangeTS uint64
	opraSeq    uint32
}

type Quote struct {
	keyBytes   [32]byte
	exchangeTS uint64
	opraSeq    uint32
	bidPrice   int64
	askPrice   int64
	bidSize    int32
	askSize    int32
}

func (q *Quote) key() string {
	i := bytes.IndexByte(q.keyBytes[:], 0)
	if i < 0 {
		return string(q.keyBytes[:])
	}
	return string(q.keyBytes[:i])
}
func (q *Quote) bid() float64 {
	return float64(q.bidPrice) / 10000.0
}
func (q *Quote) ask() float64 {
	return float64(q.askPrice) / 10000.0
}
func (q *Quote) String() string {
	t := time.Unix(int64(q.exchangeTS/1000000), int64((q.exchangeTS%1000000)*1000))
	return fmt.Sprintf("%-32s %-15s %9d %6d x %03.2f @ %0.2f x %-6d", q.key(), t.Format("15:04:05.999999"), q.opraSeq, q.bidSize, q.bid(), q.ask(), q.askSize)
}

type Print struct {
	keyBytes   [32]byte
	exchangeTS uint64
	opraSeq    uint32
	lastPrice  int64
	lastSize   int32
	flags      byte
}

func (p *Print) key() string {
	i := bytes.IndexByte(p.keyBytes[:], 0)
	if i < 0 {
		return string(p.keyBytes[:])
	}
	return string(p.keyBytes[:i])
}
func (p *Print) price() float64 {
	return float64(p.lastPrice) / 10000.0
}
func (p *Print) String() string {
	t := time.Unix(int64(p.exchangeTS/1000000), int64((p.exchangeTS%1000000)*1000))
	return fmt.Sprintf("%-32s %-15s %9d %6d x %03.2f", p.key(), t.Format("15:04:05.999999"), p.opraSeq, p.lastSize, p.price())
}
