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
