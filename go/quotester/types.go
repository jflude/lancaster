package main

import (
	"bytes"
	"fmt"
	"strconv"
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
func usToTime(us uint64) time.Time {
	sec := us / 1000000
	nanos := (us - (sec * 1000000)) * 1000
	return time.Unix(int64(sec), int64(nanos))
}
func (q *Quote) String() string {
	t := usToTime(q.exchangeTS)
	latency := time.Now().Sub(t)
	return fmt.Sprintf("%-32s %-15s %9d %6d x %6s @ %-6s x %-6d %s", q.key(), t.Format("15:04:05.999999"), q.opraSeq, q.bidSize, fwfloat(q.bid()), fwfloat(q.ask()), q.askSize, latency)
}

type Print struct {
	keyBytes        [32]byte
	exchangeTS      uint64
	opraSeq         uint32
	lastPrice       int64
	lastSize        int32
	flags           byte
	totalExchVolume uint32
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
func fwfloat(f float64) string {
	return strconv.FormatFloat(f, 'f', -2, 64)
}
func (p *Print) String() string {
	t := usToTime(p.exchangeTS)
	latency := time.Now().Sub(t)
	return fmt.Sprintf("%-32s %-15s %9d %6d x %-6s %-9d %s", p.key(), t.Format("15:04:05.999999"), p.opraSeq, p.lastSize, fwfloat(p.price()), p.totalExchVolume, latency)
}

type Summary struct {
	keyBytes    [32]byte
	exchangeTS  uint64
	opraSeq     uint32
	openPrice   int64
	lowPrice    int64
	highPrice   int64
	closePrice  int64
	flags       byte
	totalVolume uint32
}

func (s *Summary) key() string {
	i := bytes.IndexByte(s.keyBytes[:], 0)
	if i < 0 {
		return string(s.keyBytes[:])
	}
	return string(s.keyBytes[:i])
}
func (s *Summary) open() float64 {
	return float64(s.openPrice) / 10000.0
}
func (s *Summary) high() float64 {
	return float64(s.highPrice) / 10000.0
}
func (s *Summary) low() float64 {
	return float64(s.lowPrice) / 10000.0
}
func (s *Summary) close() float64 {
	return float64(s.closePrice) / 10000.0
}
func (s *Summary) String() string {
	t := time.Unix(int64(s.exchangeTS/1000000), int64((s.exchangeTS%1000000)*1000))
	return fmt.Sprintf("%-32s %-15s %9d  o:%-6.2f  h:%-6.2f  l:%-6.2f  c:%-6.2f  %9d",
		s.key(), t.Format("15:04:05.999999"), s.opraSeq, s.open(), s.high(), s.low(), s.close(), s.totalVolume)
}
