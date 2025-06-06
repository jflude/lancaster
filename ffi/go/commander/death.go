// +build linux

/*
   Copyright (c)2014-2017 Peak6 Investments, LP.
   Use of this source code is governed by the COPYING file.
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
