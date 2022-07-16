# Prerequisites
## Visualization
可视化部分使用了基于`python-flask`的`web`框架，同时使用了`echarts`作为可视化工具
安装方法可以参考
[Install flask](https://flask.palletsprojects.com/en/2.1.x/installation/#python-version)

## Libbpf
`BPF CO-RE (Compile Once – Run Everywhere)` 是一种编写可移植`BPF`应用程序的现代方法，程序可以在多个内核版本和配置上运行，无需在目标机器上进行修改和运行时源代码编译，项目采用了`libbpf`实现了`ebpf`机制，采用静态链接的方式

如果只需使用`stortrace`功能，则无需安装`libbpf`

从源码安装则需要依赖于高版本的`clang`，由于`yum`源对应版本过低，会固定从源码安装`openEuler22.03LTS`兼容的`libbpf-0.8.1`版本


## Else
### cmake
# Log record
IO事件的具体记录暂定可能使用`redis`作为存储，也支持直接写入文件
 




