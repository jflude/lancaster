// +build linux

/*
   Copyright (C)2014-2016 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

package commander

import (
	"os/exec"
	"syscall"
)

func (c *Command) initSysProcAttr(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Pdeathsig: c.DeathSignal,
	}
}
