package appinfo

import ( 
	"github.com/peak6/go-mmd/mmd"
	"github.peak6.net/platform/gocore.git/logger"
	"time"
	"os"
	"io/ioutil"
)

type CheckServicesStartedFunc func() bool

func Setup(appName string, releaseLogPath string, areSvcsStartedFn CheckServicesStartedFunc) error {
	
    startTimeSecs := time.Now().UnixNano() / 1000000000
	
	hostname, err := os.Hostname()
        if err != nil {
		logger.LogError("Error getting hostname: ", err, "Can't register service")
		return err
	}

	connection, err := mmd.LocalConnect()
	if err != nil {
		logger.LogError("Error connecting to local mmd: ", err, "Can't register servce")
		return err
	} else {
		logger.LogInfo("Created mmd connection: ", connection)
	}

	env, err := connection.Call("mmd.env", nil)
	if err != nil {
		logger.LogError("Failed to call 'mmd.env' service:", err, "Can't register service.")
		return err
	}
	
	fileContents, err := ioutil.ReadFile(releaseLogPath + "/RELEASE_LOG")
	if err != nil {
		logger.LogError("Error reading RELEASE_LOG: ", err, "Can't register service")
		return err
	}
	
	serviceName := "app.info." + appName + "." + hostname
	logger.LogInfo("Registering App Info Service with name: ", serviceName)
	return connection.RegisterService(serviceName, 
		func(mmdConn *mmd.Conn, 
			mmdChan *mmd.Chan, 
			mmdChanCreate *mmd.ChannelCreate) {
				
				response := map[string]interface{}{
					"startTime" : startTimeSecs,
					"env" : env,
					"releaseLogContents" : string(fileContents),
					"allServicesHaveStarted" : areSvcsStartedFn(),
				}
				
				mmdChan.Close(response)
			})

}
