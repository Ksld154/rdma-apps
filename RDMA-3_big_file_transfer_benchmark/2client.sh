#! /usr/bin/zsh

./client 192.168.56.111 test.img &
./client 192.168.56.111 test2.img &
wait
