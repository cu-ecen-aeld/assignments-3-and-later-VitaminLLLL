#!/bin/bash

filesdir=$1
searchstr=$2


if [ -z "$filesdir" ]; then
	echo "Error: filesdir is empty!"
	exit 1
elif [ ! -d "$filesdir" ]; then
	echo "Error: filesdir is not a directory or not exist!"
	exit 1
elif [ -z "$searchstr" ]; then
	echo "Error: searchstr is empty!"
	exit 1
fi

echo "parameter is correct."

X=`find $filesdir -type f | wc -l`
Y=`grep $searchstr -r $filesdir | wc -l`

echo "The number of files are $X and the number of matching lines are $Y"

exit 0
