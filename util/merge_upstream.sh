#!/bin/sh

cd ..
git remote add upstream https://github.com/freeminer/freeminer.git
git fetch --all
git pull
( git merge --no-edit upstream/master | grep -i "conflict" ) && exit
git push
git submodule update --init --recursive
git status


cd src/network/usrsctp
git co master
git pull
cd ../../..
git commit src/network/usrsctp -m "update submodules"
git push
