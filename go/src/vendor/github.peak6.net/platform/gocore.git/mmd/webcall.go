package mmd

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/url"
	"strings"
)

var MMDAddress = "localhost:9998"

func fixToken(str string) string {
	if strings.ContainsAny(str, "-") {
		return str
	}
	return fmt.Sprintf("%s-%s-%s-%s", str[0:8], str[8:12], str[12:16], str[16:])
}

func WebCall(service string, body interface{}, response interface{}) error {
	return AuthenticatedWebCall(service, "", body, response)
}

func AuthenticatedWebCall(service string, authToken string, body interface{}, response interface{}) error {
	u := "http://" + MMDAddress + "/call/" + service
	if body != nil {
		b, err := json.Marshal(body)
		if err != nil {
			return fmt.Errorf("Failed to encode body: %s", err)
		}
		u += "?body=" + url.QueryEscape(string(b))
		if authToken != "" {
			u += "&token=" + fixToken(authToken)
		}
	}
	resp, err := http.Get(u)
	if err != nil {
		return fmt.Errorf("Error retrieving response: %s", err)
	}
	b, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("Error reading response body: %s", err)
	}
	if resp.StatusCode != 200 {
		return fmt.Errorf("Error: %s", string(b))
	}
	err = json.Unmarshal(b, response)
	if err != nil {
		return fmt.Errorf("Failed to decode response: %s -- %s", err, string(b))
	}
	return nil
}
