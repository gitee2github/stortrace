# filter
`filter`是附加在`trace`实例上的一个可选部件

一般的 若每个`io_event`事件的`trace`结果都作为一条单独的record落盘 即便使用异步io的方式写入 也会造成较大的额外开销

当不需要获取所有log 而只是希望分析其中的一部分结果 可以考虑附加一个filter 减少需要落盘的log record数量

filter的定义模式如下
```json
"filter":{
        "cold_start_iter":1000,
        "quantile":"95",
        "filter_accuracy":25,
        "stage":"sum"
    }
```
首先说明filter的工作模式

## stage
stage对应filter要筛选的阶段

描述一个io事件的延迟会使用$[T_0,T_1,...,T_n]$的格式
例如对direct io的追踪 在simple模式下会被分为三个阶段  

$$[T_{kernel-crossing},T_{file-system},T_{block-io}]$$ 

对应stage为
`[kernel_crossing,file_system,block_io]`以及一个对应全部总和的`[sum]`

## cold_start_iter
为了实现异常数据的过滤 需要先了解数据的分布情况 因此filter存在一个冷启动的过程
cold_start_iter 会在对应设置的数量内对io时间的对应stage数据分布进行汇总 然后构造初始的数据分布 用于之后的数据识别

数据分布的构造基于`t-digest`算法，具体参考论文

随机生成规模为500-1000-2000-4000的数据(模拟cold start)
### benchmark
这里accury设置为25
```shell
Run on (16 X 4701.37 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x8)
  L1 Instruction 32 KiB (x8)
  L2 Unified 256 KiB (x8)
  L3 Unified 16384 KiB (x1)
Load Average: 0.00, 0.00, 0.00
-------------------------------------------------------------------------
Benchmark                               Time             CPU   Iterations
-------------------------------------------------------------------------
BM_digest_cold_start_perf/10         2203 ns         2202 ns       314206
BM_digest_cold_start_perf/64        15747 ns        15738 ns        43209
BM_digest_cold_start_perf/512      132458 ns       132381 ns         4992
BM_digest_cold_start_perf/1000     266596 ns       266444 ns         2478
```

### accuracy
冷启动的数据精度非常依赖于数据分布，而不是依赖于数据的数量
例如比较复杂的分布 更多的迭代次数不一定更好

一般可以设置为100


## filter_accuracy
数据分布的构造基于`t-digest`算法，具体参考论文
``
`filter_accuracy`必须设置为一个正整数 数值越大则filter对于数据百分位的估算会越准确 但也会带来更大的性能开销 一般的 可以考虑设置为20-50 推荐20-25即可

由于每次io事件 无论log是否需要写入 都需要插入到filter当中

```shell
--------------------------------------------------------------------
Benchmark                          Time             CPU   Iterations
--------------------------------------------------------------------
BM_digest_insert_perf/10         259 ns          259 ns      2814359
BM_digest_insert_perf/16         235 ns          235 ns      2916704
BM_digest_insert_perf/32         209 ns          209 ns      3328341
BM_digest_insert_perf/64         186 ns          186 ns      3467174
BM_digest_insert_perf/100        202 ns          202 ns      3373339
```







