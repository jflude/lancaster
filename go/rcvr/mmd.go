package main

import (
	"flag"
	"github.com/peak6/go-mmd/mmd"
	"log"
)

var enableMMD bool

func init() {
	flag.BoolVar(&enableMMD, "mmd", false, "Enable MMD Service registration")
}

func initMMD() {
	con, err := mmd.LocalConnect()
	if err != nil {
		log.Panic(err)
	}
	resp, err := con.Call("serviceregistry", map[string]interface{}{
		"action": "registerLocal",
		"name":   "cachester",
	})
	if err != nil {
		log.Panic(err)
	}
	log.Println("Registration result:", resp)
}
