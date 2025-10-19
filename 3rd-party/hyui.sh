#!/bin/bash

cd hydrui
git pull origin master
npm i
npm run generate:pack
go build -o hydrui-server ./cmd/hydrui-server
./hydrui-server
