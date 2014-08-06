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
	err = con.RegisterLocalService("cachester.local", func(c *mmd.MMDConn, ch *mmd.MMDChan, cc *mmd.ChannelCreate) {
		err := ch.Close(State.Receivers)
		if err != nil {
			log.Println("Failed to service request:", err, "request was:", cc)
		}
	})
	if err != nil {
		log.Panic(err)
	}
	log.Println("Registered as mmd service")
}
