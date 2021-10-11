#! /usr/bin/zsh

./client 192.168.56.111 100m.img &
./client 192.168.56.111 100m2.img &
wait
