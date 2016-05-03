package tradingdefs

import (
	"database/sql"
	"fmt"
	_ "github.com/lib/pq"
	"github.peak6.net/platform/gocore.git/mmd"
)

type hostPort struct {
	Host string
	Port int
}

func getTDDB() (*sql.DB, error) {
	hp := hostPort{}
	err := mmd.WebCall("trading_defs", map[string]string{
		"action": "locate",
	}, &hp)

	if err != nil {
		return nil, err
	}

	return sql.Open("postgres", fmt.Sprintf("postgres://td:td@%s:%d/trading_defs?sslmode=disable", hp.Host, hp.Port))
}
