# Libbpf
`BPF CO-RE (Compile Once – Run Everywhere)` 是一种编写可移植`BPF`应用程序的现代方法，程序可以在多个内核版本和配置上运行，无需在目标机器上进行修改和运行时源代码编译

项目采用了`libbpf`，如果只需要运行程序，无需从源码编译，只需要使用对应的二进制版本，从源码安装则需要依赖于高版本的`clang`和开启内核特定配置

# Visualization
可视化部分使用了基于`flask`的`web`框架，同时使用了`echarts`作为可视化工具

# Log record
IO事件的具体记录暂定可能使用`redis`作为存储，也支持直接写入文件
 




