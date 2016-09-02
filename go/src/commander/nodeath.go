// +build !linux

/*
   Copyright (C)2014-2016 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

package commander

import (
	"os/exec"
)

func (c *Command) initSysProcAttr(cmd *exec.Cmd) {
}
