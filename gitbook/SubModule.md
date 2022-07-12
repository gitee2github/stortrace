# Submodule
项目中依赖`libbpf`和`bpftool`,使用子模块的方式配置

`git submodule add <url> <path>`

```bash
git submodule add https://github.com/libbpf/libbpf.git libbpf
git submodule add https://github.com/libbpf/bpftool.git bpftool
```