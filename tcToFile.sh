#!/bin/sh
LOGFILE="aqmResults.txt"

while true
do
	echo $(date "+%H:%M:%S" >> $LOGFILE)
	echo $(tc -s -d qdisc show dev eth2 >> $LOGFILE)
	echo $(echo --------------- >> $LOGFILE)
	sleep 1
done
