#!/bin/bash

#SCRIPT_DIR=`dirname $0 | xargs readlink -e`
SCRIPT_DIR=`pwd`
DIR=`readlink -e $SCRIPT_DIR`

if [ -e $DIR/$1 ] 
then
	echo $DIR/$1
	exit 0
fi

while [ $DIR != "/" ]
do
	DIR=`readlink -e $DIR/..`
	if [ -e $DIR/$1 ] 
	then
		echo $DIR/$1
		exit 0
	fi
done

>&2 echo "ERROR:" $1 "not found in any parent of" $SCRIPT_DIR 
exit 1
