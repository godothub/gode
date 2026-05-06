# Godot JavaScript/TypeScript

在third目录里设置子模块：
* https://github.com/godotengine/godot-cpp
* https://github.com/nodejs/node-addon-api
* https://github.com/tree-sitter/tree-sitter
* https://github.com/tree-sitter/tree-sitter-javascript
* https://github.com/tree-sitter/tree-sitter-typescript

补全`CMakeLists.txt`，实现GDE插件。
引擎在`C:\Users\moluo\Documents\godothub\engine\4.6.2-stable`目录，可供你测试example使用

构建中间产物放在build目录下的不同平台和架构的子目录
最终得到的GDE动态库直接放到`example/addons/gode/bin`目录里

libnode目录来自于`https://github.com/moluopro/libnode/releases/download/24.15.0/libnode.zip`
