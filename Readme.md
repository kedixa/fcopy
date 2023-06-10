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

在需要接收文件的机器上开启服务
```bash
fcopy-server -p 8000
```

在发送文件的机器上启动客户端，通过`-t`指定多个服务端，`-p`指定并发数
```bash
fcopy-cli -p 16 -t 192.168.0.1:8000 [-t 192.168.0.2:8000 ...] 100G.txt 200G.txt ...
```
