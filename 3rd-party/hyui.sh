#!/bin/bash

cd hydrui
npm i
npm run generate:pack
go build -o hydrui-server ./cmd/hydrui-server
./hydrui-server
