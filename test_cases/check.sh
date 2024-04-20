#!/bin/bash

submission=$1
# if test_dir already exixts, delete it
if [ -d "./test_dir" ]; then
	rm -rf ./test_dir
fi
mkdir ./test_dir

# echo $submission

# Accept both .tar.gz and .gz or any other gz ;)
fileNameRegex="project_[0-9]{4}[A-Z]{2}.[0-9]{4}.*.gz" # [TODO] fix this for PROJECT GROUP ID

if ! [[ $submission =~ $fileNameRegex ]]; then
	# not considering zip for now
	echo "File doesn't match the naming convention"
	exit
fi

# Extract the Entry number
entryNoRegex="[0-9]{4}[A-Z]{2}.[0-9]{4}"
if [[ $submission =~ $entryNoRegex ]]; then
	entryNum=${BASH_REMATCH[0]}
	echo "Entry Number: $entryNum"
fi

echo "Setting the test directory"

tar -xzf "$submission" -C ./test_dir
cp *.c out* *.sh ./test_dir
cd ./test_dir

# Change the gcc binary
sed -i 's/gcc-10\|gcc-9/gcc/g' Makefile

echo "Executing the test cases"

pkill qemu-system-x86
pkill qemu-system-i386
make clean > /dev/null

make xv6.img > /dev/null
#make

SECRET="COW"
num_test=3

for ((t=0;t<$num_test;++t))
do
    start_time=$(date +%s)  # Get start time in seconds since epoch
    make clean > /dev/null

    make xv6.img > /dev/null

    echo -e "\e[91mRunning test case $t\e[0m"  # Red color
    (
        while true; do
            current_time=$(date +%s)  # Get current time in seconds since epoch
            elapsed=$((current_time - start_time))  # Calculate elapsed time
            printf "\rElapsed time: %02d:%02d:%02d" $((elapsed/3600)) $(( (elapsed%3600)/60)) $((elapsed%60))  # Print elapsed time
            sleep 1  # Wait for 1 second
        done
    ) &
    timer_pid=$!  # Store the PID of the timer process
	# echo $PWD
    timeout 20s ../test_project.sh testcow$((t+1)) | grep $SECRET | tr -d '\r' > res_project_$t
    kill $timer_pid  # Stop the timer process
    echo  # Move to the next line after the timer
done
make clean > /dev/null

check_test=$num_test
total_test=0

echo "" > .output
marks="$entryNum"
for ((t=0;t<$check_test;++t))
do
	echo -n "Test #${t}: "

	# NOTE: we are doing case insensitive matching.  If this is not what you want,
	# just remove the "-i" flag
	if diff -iZwB <(cat out_project) <(cat res_project_$t) > /dev/null
	then
		echo -e "\e[0;32mPASS\e[0m"
		marks="$marks,1" # 1 mark for pass
		((total_test++))
	else
		echo -e "\e[0;31mFAIL\e[0m"
		marks="$marks,0" # 0 mark for fail
		echo "Output for test case $t:" >> .output
		cat res_lab3_$t >> .output
	fi
done
echo "$total_test test cases passed for $entryNum"
echo $marks >> ../marks.csv
