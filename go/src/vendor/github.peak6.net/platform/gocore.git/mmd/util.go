package mmd

import (
    "fmt"
)

func LookupEnvironment() (string, error) {
	var mmdEnv string
	err := WebCall("mmd.env",
		map[string]interface{}{},
		&mmdEnv)
	if err != nil {
		fmt.Println("Could not determine environment from mmd: ", err)
		return "", err
	}
	return mmdEnv, nil
}

