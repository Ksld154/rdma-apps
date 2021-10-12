#! /usr/bin/zsh

./tdma-client 192.168.56.115 test.img &
date
sleep 2
date
./tdma-client 192.168.56.115 test2.img &
wait
