#! /usr/bin/zsh

./tdma-client 192.168.56.111 test.img &
date
sleep 2
date
./tdma-client 192.168.56.111 test2.img &
wait
