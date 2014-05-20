/* function return codes */

#ifndef STATUS_H
#define STATUS_H

typedef int boolean;

#define FALSE 0
#define TRUE 1

typedef int status;

#define OK 0
#define FAIL -1
#define CLOSED -2
#define BLOCKED -3
#define TIMEDOUT -4
#define HEARTBEAT -5
#define NO_MEMORY -6
#define BAD_PROTOCOL -7

#define FAILED(x) ((x) < OK)

#endif
