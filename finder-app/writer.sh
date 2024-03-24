#!/bin/bash

writefile=$1
writestr=$2


if [ -z "$writefile" ]; then
	echo "Error: writefile is empty!"
	exit 1
elif [ -z "$writestr" ]; then
	echo "Error: writestr is empty!"
	exit 1
elif [ ! -f "$writefile" ]; then
	mkdir -p "$(dirname "$writefile")" && touch $writefile
	if [ ! $? -eq 0 ]; then
		echo "Error: $writefile cannot be created!"
		exit 1
	fi
fi

echo "$writestr" > $writefile
exit 0
