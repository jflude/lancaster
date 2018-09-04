/*
  Copyright (c)2014-2018 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/error.h>
#include <lancaster/signals.h>
#include <signal.h>
#include <string.h>
#include <stddef.h>

#ifndef NSIG
#define NSIG 65
#endif

static volatile sig_atomic_t is_raised[NSIG];

static void on_signal(int sig)
{
    is_raised[sig] = 1;
}

static status set_action(int sig, void (*handler)(int))
{
    struct sigaction sa;
    sa.sa_flags = 0;
    sa.sa_handler = handler;

    if (sigemptyset(&sa.sa_mask) == -1)
	return error_errno("sigemptyset");

    if (sigaction(sig, &sa, NULL) == -1)
	return error_errno("sigaction");

    return OK;
}

status signal_add_handler(int sig)
{
    if (sig < 1 || sig >= NSIG)
	return error_invalid_arg("sig_add_handler");

    return set_action(sig, on_signal);
}

status signal_remove_handler(int sig)
{
    if (sig < 1 || sig >= NSIG)
	return error_invalid_arg("sig_remove_handler");

    return set_action(sig, SIG_DFL);
}

status signal_is_raised(int sig)
{
    if (sig < 1 || sig >= NSIG)
	return error_invalid_arg("signal_is_raised");

    return is_raised[sig]
	? error_msg(SIG_ERROR_BASE - sig, "%s", sys_siglist[sig]) : OK;
}

status signal_any_raised(void)
{
    int i;
    for (i = 1; i < NSIG; ++i)
	if (is_raised[i])
	    return error_msg(SIG_ERROR_BASE - i, "%s", sys_siglist[i]);

    return OK;
}

status signal_clear(int sig)
{
    if (sig < 1 || sig >= NSIG)
	return error_invalid_arg("signal_is_raised");

    is_raised[sig] = 0;
    return OK;
}

status signal_on_eintr(const char *func)
{
    int i;
    if (!func) {
	error_invalid_arg("signal_on_eintr");
	error_report_fatal();
    }

    for (i = 1; i < NSIG; ++i)
	if (is_raised[i])
	    return error_msg(SIG_ERROR_BASE - i, "%s: %s",
			     func, sys_siglist[i]);

    return error_errno(func);
}
