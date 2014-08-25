/* function return codes */

#ifndef STATUS_H
#define STATUS_H

typedef int boolean;

#define FALSE 0
#define TRUE 1

typedef int status;

#define OK 0
/* #define EOF -1 */
#define FAIL -2
#define BLOCKED -3
#define TIMED_OUT -4
#define NO_MEMORY -5
#define NO_HEARTBEAT -6
#define PROTOCOL_ERROR -7
#define UNKNOWN_PROTOCOL -8
#define SIGN_OVERFLOW -9
#define UNEXPECTED_SOURCE -10
#define CHANGE_QUEUE_OVERRUN -11
#define BUFFER_TOO_SMALL -12
#define INVALID_DEVICE_FORMAT -13
#define STORAGE_ORPHANED -14
#define STORAGE_RECREATED -15
#define STORAGE_CORRUPTED -16
#define STORAGE_READ_ONLY -17
#define STORAGE_INVALID_SLOT -18
#define STORAGE_NO_CHANGE_QUEUE -19

#define FAILED(x) ((x) < OK)

#endif
