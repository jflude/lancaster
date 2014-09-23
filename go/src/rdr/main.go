package main

// #include "../../../status.h"
// #include "../../..//storage.h"
// #include "../../../error.h"
// #include "../../../receiver.h"
// #include <fcntl.h>
// #include <unistd.h>
// #cgo LDFLAGS: ../../../libcachester.a -lrt -lm -w
import "C"
import (
	"database/sql"
	"errors"
	_ "github.com/lib/pq"
	"log"
        "fmt"
)

// conStr := "user=pslchi6dpgsql1 port=5432 dbname=trading_defs user=td password=td"
var conStr = "postgres://td:td@pslchi6dpgsql1:5432/trading_defs?sslmode=disable"
var store C.storage_handle
var qCapacity C.size_t
//O_RDWR  C.int = 2

func main() {
        var cs *C.char
	for securityKey, _ := range getValidSecurityKeys() {
		log.Println("Security Key: ", securityKey)
	}
	if err := err(C.storage_open(&store, cs, C.O_RDWR)); err != nil {
		log.Fatal("Failed to open store", err)
	}
	qCapacity = C.storage_get_queue_capacity(store)
	a := C.storage_get_record_size(store)
	b := C.storage_get_value_size(store)
	c := C.storage_get_value_offset(store)
	log.Println("rs", a, "vs", b, "vo", c)
	log.Println("Queue size:", qCapacity)
}

func getValidSecurityKeys() map[string]bool {
	db := getSql()
	defer db.Close()
	rows, err := db.Query("select security_key from security_definitions ")
	if err != nil {
		log.Fatal("Failed to query: ", err)
	}
	defer rows.Close()

	allKeys := parseSecurityKeys(rows)

	log.Println(len(allKeys))
	// iterate through all inactive storage files
	return allKeys
}

func parseSecurityKeys(rows *sql.Rows) map[string]bool {
	allKeys := make(map[string]bool, 1000000)
	for rows.Next() {
		var key string
		err := rows.Scan(&key)
		if err != nil {
			log.Fatal(err)
		}
		allKeys[key] = true
	}
	return allKeys
}

func getSql() *sql.DB {
	db, err := sql.Open("postgres", conStr)
	if err != nil {
		log.Fatal("Failed to connect", err)
	}
	return db
}

func err(status C.status) error {
	if status == 0 {
		return nil
	}
	str := C.GoString(C.error_last_msg())
	return fmt.Errorf("%d: %s", status, errors.New(str))
}
