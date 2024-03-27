// +build !linux

/*
   Copyright (c)2014-2017 Peak6 Investments, LP.
   Copyright (c)2018-2024 Justin Flude.
   Use of this source code is governed by the COPYING file.
*/

package commander

import (
	"os/exec"
)

func (c *Command) initSysProcAttr(cmd *exec.Cmd) {
}
