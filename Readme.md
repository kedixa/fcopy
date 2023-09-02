# Fcopy

基于[Coke](https://github.com/kedixa/coke)实现的在内网中快速将大文件分发到多个机器的工具

## 构建
```bash
git clone https://github.com/kedixa/fcopy
cd fcopy
git clone https://github.com/sogou/workflow
git clone https://github.com/kedixa/coke

cmake -S workflow -B build.workflow
make -C build.workflow -j 8
cmake -S coke -B build.coke -D Workflow_DIR=../workflow/
make -C build.coke -j 8
cmake -S . -B build.fcopy -D Workflow_DIR=workflow -D Coke_DIR=build.coke
make -C build.fcopy -j 8
```

## 运行
项目开发中，运行方式有可能在未来改变

### 服务端
在需要接收文件的机器上开启服务，支持的选项如下

- `-c, --config  fcopy.conf`，指定启动服务器的配置文件，若未指定则会尝试`~/.fcopy/fcopy.conf`
- `-p, --port  listen_port`，指定启动服务的端口号，若未指定则使用配置文件中指定的端口号，若配置文件未指定则使用默认值`5200`
- `-g, --background`，指定以后台方式启动服务
- `-h, --help`，打印帮助信息到标准输出

[配置文件示例](conf/fcopy.conf)

启动服务示例

```bash
fcopy-server -c fcopy.conf
```

### 客户端
在发送文件的机器上启动客户端，支持的选项如下

- `-t, --target  ip:port`，指定一个目标地址，多次使用该选项可指定多个地址，例如`fcopy-cli -t 192.168.0.1:5200 -t 192.168.0.2:5200 ...`
- `--target-list  target.txt`，指定一个文本文件，其中的每一行都是一个目标地址
- `-p, --parallel  n`指定执行拷贝的并发数，范围`[1, 512]`，例如`fcopy-cli -p 16 ...`
- `--wait-close, --no-wait-close`，一个文件传输后是否等待服务端完全关闭文件后再执行下一项操作，默认等待
- `--direct-io, --no-direct-io`，读取文件时是否启用`direct io`，默认启用
- `--check-self, --no-check-self`，检查远程目标中是否有本机IP或者重复地址，默认开启
- `--dry-run`，仅打印当前命令将会传输哪些文件，而不执行传输操作
- `-h, --help`，打印帮助信息到标准输出

目前仅支持链式传输模式，因此要求各个传输目标之间均可互相连通

示例，将文件`a.txt`、`b.bin`, 文件夹`dir1`、`dir2`发送到两个指定目标机器上，并发数为16
```bash
fcopy-cli -p 16 -t 192.168.0.1:5200 -t 192.168.0.2:5200 a.txt dir1 b.bin dir2
```

## LICENSE
TODO
