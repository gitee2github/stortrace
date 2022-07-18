# BPF maps
`map`是`bpf`程序缓存结果的最主要数据结构，主要分为3类
### Array
```c
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 128);
    __type(key, u32);
    __type(value, struct my_value);
} my_array_map SEC(".maps");
```

### Hashmap
```c
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, u32);
    __type(value, struct my_value);
} my_hash_map SEC(".maps")
```

### PER-CPU array
```c
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, u32);
    __type(value, struct my_value);
} heap SEC(".maps");
```

对于`PERF_EVENT_ARRAY`,`STACK_TRACE`和一些特殊类型的maps，例如`DEVMAP`,`CPUMAP...`，`key/value`需要定义大小

```c
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u32));
} events SEC(".maps");
```

# 访问BPF map中的元素

```c
struct event *data = bpf_map_lookup_elem(&heap, &zero);
bpf_map_update_elem(&my_hash_map, &id, &my_val, 0 /* flags */);
bpf_perf_event_output(args, &events, BPF_F_CURRENT_CPU, data, data_len);
``