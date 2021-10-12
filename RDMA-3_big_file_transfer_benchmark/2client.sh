#! /usr/bin/zsh

./client 192.168.56.115 test.img &
./client 192.168.56.115 test2.img &
wait
