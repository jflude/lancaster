package logger

import (
	"encoding/json"
	"fmt"
	"log"
	"os"
	"regexp"
	"strings"
	"syscall"
	"time"
)

type Logger struct {
	buf   []byte
	jsoff bool
}

var applicationName = "?"
var loggerName = "?"
var typeName = "?"
var categoryName = "?"
var aliasName = "?"
var stashLog *log.Logger
var mirrorStderr = true

func SetApplication(name string) string {
	old := applicationName
	applicationName = name
	return old
}

func SetLogger(name string) string {
	old := loggerName
	loggerName = name
	return old
}

func SetType(name string) string {
	old := typeName
	typeName = name
	return old
}

func SetCategory(name string) string {
	old := categoryName
	categoryName = name
	return old
}

func SetAlias(name string) string {
	old := aliasName
	aliasName = name
	return old
}

func SetMirrorStderr(mirror bool) bool {
	old := mirrorStderr
	mirrorStderr = mirror
	return old
}

func InitStashLog(logDir string) {
	fname := logDir + "/stash.log"
	logFile, err := os.OpenFile(fname, os.O_WRONLY | os.O_CREATE | os.O_APPEND, 0644)
	if err != nil {
		FatalError("Failed to open log file at: " + fname)
	}

	stashLog = log.New(logFile, log.Prefix(), 0)
}

func (l *Logger) DoSkipJson() {
	l.jsoff = true
}

func (l *Logger) buffer(p []byte) []byte {
	newBuf := l.buf
	newBuf = append(newBuf, p...)
	newlineIdx := cutoffIndex(newBuf)

	for newlineIdx != -1 {
		l.buf = newBuf[:newlineIdx]
		newBuf = newBuf[(newlineIdx + 1):]

		if l.jsoff {
			writeLog("", string(l.buf[:]), true)
		} else {
			// when subscribers invoke this method, it's on their stderr stream,
			// unless the we know the stream is already in JSON format, so we log it as an error
			LogError(string(l.buf[:]))
		}

		l.buf = nil
		newlineIdx = cutoffIndex(newBuf)
	}

	return newBuf
}

func cutoffIndex(p []byte) int {
	newlineIdx := -1
	for i := range p {
		if p[i] == '\n' {
			newlineIdx = i
			break
		}
	}

	return newlineIdx
}

func (l *Logger) Write(p []byte) (n int, err error) {
	l.buf = l.buffer(p)
	return len(p[:]), nil
}

func writeLog(lvl string, txt string, skipJson bool) {
	jsonTsFmt := "2006-01-02T15:04:05.999-0700"
	stdTsFmt := "2006-01-02 15:04:05.999999"

	ts := time.Now()
	jsonTs := ts.Format(jsonTsFmt)
	standardTs := ts.Format(stdTsFmt)

	// txt is already a logstash compatible string
	if skipJson {
		var data map[string]*json.RawMessage
		err := json.Unmarshal([]byte(txt), &data)
		if err != nil {
			fmt.Fprintln(os.Stderr, standardTs, "ERROR", "Failed to send JSON log statement to regular log file:", txt)
			return
		}

		var msg string
		var logLevel string
		var msgTsStr string

		json.Unmarshal(*data["@timestamp"], &msgTsStr)
		json.Unmarshal(*data["level"], &logLevel)
		json.Unmarshal(*data["message"], &msg)

		msgTs, _ := time.Parse(jsonTsFmt, msgTsStr)
		msgTsStr = msgTs.Format(stdTsFmt)

		// log the JSON as-is
		writeStashLog(txt)
		if mirrorStderr {
			fmt.Fprintln(os.Stderr, msgTsStr, logLevel, msg)
		}

		return
	}

	jsonTxt := txt
	// escape " if txt contains JSON
	if matched, _ := regexp.MatchString(".*{.*}.*", jsonTxt); matched {
		jsonTxt = strings.Replace(jsonTxt, "\"", "\\\"", -1)
	}

	if mirrorStderr {
		fmt.Fprintln(os.Stderr, standardTs, lvl, txt)
	}

	jsonStr := fmt.Sprintf("{\"@timestamp\":\"%s\", \"app\":\"%s\", \"level\":\"%s\", " +
		"\"logger_name\":\"%s\", \"type\":\"%s\", \"cat\":\"%s\", \"alias\":\"%s\", \"message\":\"%s\"}",
		jsonTs,
		applicationName,
		lvl,
		loggerName,
		typeName,
		categoryName,
		aliasName,
		jsonTxt)

	writeStashLog(jsonStr)
}

func writeStashLog(txt string) {
	if stashLog == nil {
		fmt.Fprintln(os.Stderr, "ERROR", "stash.log is not initialized! Failed to write:", txt)
		return
	}

	stashLog.Println(txt)
}

func LogDebug(args ...interface{}) {
	writeLog("DEBUG", fmt.Sprint(args...), false)
}

func LogInfo(args ...interface{}) {
	writeLog("INFO", fmt.Sprint(args...), false)
}

func LogNotice(args ...interface{}) {
	writeLog("NOTICE", fmt.Sprint(args...), false)
}

func LogWarn(args ...interface{}) {
	writeLog("WARN", fmt.Sprint(args...), false)
}

func LogError(args ...interface{}) {
	writeLog("ERROR", fmt.Sprint(args...), false)
}

func FatalError(args ...interface{}) {
	LogError(args...)
	os.Exit(1)
}

func ReportSignal(sig os.Signal) {
	LogWarn("signal: " + sig.String())

	ret := 0
	switch sig {
	case syscall.SIGHUP:
		ret = 1
	case syscall.SIGINT:
		ret = 2
	case syscall.SIGTERM:
		ret = 15
	}

	os.Exit(128 + ret)
}
