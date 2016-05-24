package tradingdefs

import (
	"database/sql"
	"fmt"
	"time"
)

type MarketHours struct {
	Symbol       string
	SecurityType string
	Open         string
	Close        string
	PreOpen      *string
	PreClose     *string
	PostOpen     *string
	PostClose    *string
}

func (mh *MarketHours) String() string {
	n := func(s *string) string {
		if s == nil {
			return "nil"
		} else {
			return *s
		}
	}

	return fmt.Sprintf("Symbol: %s, Type: %s, Open: %s, Close: %s, PreOpen: %s, PreClose: %s, PostOpen: %s, PostClose: %s",
		mh.Symbol, mh.SecurityType, mh.Open, mh.Close, n(mh.PreOpen), n(mh.PreClose), n(mh.PostOpen), n(mh.PostClose))
}

func GetMarketHours(day int, month time.Month, year int) (*MarketHours, error) {
	mh := &MarketHours{}

	db, err := getTDDB()
	if err != nil {
		return mh, err
	}

	defer db.Close()

	row := db.QueryRow("select symbol,security_type,open,close,pre_open,pre_close,post_open,post_close from get_market_hours_for_date($1) where symbol='*' and security_type='*'", fmt.Sprintf("%d-%d-%d", year, month, day))

	err = row.Scan(&mh.Symbol, &mh.SecurityType, &mh.Open, &mh.Close, &mh.PreOpen, &mh.PreClose, &mh.PostOpen, &mh.PostClose)
	if err == sql.ErrNoRows {
		err = nil
	}

	return mh, err
}

func GetMarketHoursToday() (*MarketHours, error) {
	now := time.Now()
	return GetMarketHours(now.Day(), now.Month(), now.Year())
}
