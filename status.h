/* function return codes */

#ifndef STATUS_H
#define STATUS_H

typedef int boolean;

#define FALSE 0
#define TRUE 1

typedef int status;

#define OK 0
/* #define EOF -1 */
#define BLOCKED -1000
#define NOT_FOUND -1001
#define NO_MEMORY -1002
#define NO_HEARTBEAT -1003
#define PROTOCOL_ERROR -1004
#define UNKNOWN_PROTOCOL -1005
#define SIGN_OVERFLOW -1006
#define UNEXPECTED_SOURCE -1007
#define CHANGE_QUEUE_OVERRUN -1008
#define BUFFER_TOO_SMALL -1009
#define INVALID_ADDRESS -1010
#define STORAGE_ORPHANED -1011
#define STORAGE_RECREATED -1012
#define STORAGE_CORRUPTED -1013
#define STORAGE_READ_ONLY -1014
#define STORAGE_INVALID_SLOT -1015
#define STORAGE_WRONG_VERSION -1016
#define STORAGE_NO_CHANGE_QUEUE -1017

#define FAILED(x) ((x) < OK)

#endif
