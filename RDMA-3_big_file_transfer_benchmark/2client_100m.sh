#! /usr/bin/zsh

./client 192.168.56.115 100m.img &
./client 192.168.56.115 100m2.img &
wait
