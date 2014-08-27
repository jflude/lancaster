#include "signals.h"
#include "error.h"
#include <stddef.h>

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

	if (sigemptyset(&sa.sa_mask) == -1) {
		error_errno("sigemptyset");
		return FAIL;
	}

	if (sigaction(sig, &sa, NULL) == -1) {
		error_errno("sigaction");
		return FAIL;
	}

	return OK;
}

status signal_add_handler(int sig)
{
	if (sig < 1 || sig >= NSIG) {
		error_invalid_arg("sig_add_handler");
		return FAIL;
	}

	return set_action(sig, on_signal);
}

status signal_remove_handler(int sig)
{
	if (sig < 1 || sig >= NSIG) {
		error_invalid_arg("sig_remove_handler");
		return FAIL;
	}

	return set_action(sig, SIG_DFL);
}

status signal_is_raised(int sig)
{
	if (sig < 1 || sig >= NSIG) {
		error_invalid_arg("signal_is_raised");
		return FAIL;
	}

	return is_raised[sig];
}

status signal_clear(int sig)
{
	if (sig < 1 || sig >= NSIG) {
		error_invalid_arg("signal_is_raised");
		return FAIL;
	}

	is_raised[sig] = 0;
	return OK;
}
